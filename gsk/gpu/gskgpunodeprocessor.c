#include "config.h"

#include "gskgpunodeprocessorprivate.h"

#include "gskgpuborderopprivate.h"
#include "gskgpuclipprivate.h"
#include "gskgpucolorizeopprivate.h"
#include "gskgpudeviceprivate.h"
#include "gskgpuframeprivate.h"
#include "gskgpuglobalsopprivate.h"
#include "gskgpuimageprivate.h"
#include "gskgpurenderpassopprivate.h"
#include "gskgpuscissoropprivate.h"
#include "gskgputextureopprivate.h"
#include "gskgpuuberopprivate.h"
#include "gskgpuuploadopprivate.h"

#include "gskdebugprivate.h"
#include "gskrectprivate.h"
#include "gskrendernodeprivate.h"
#include "gskroundedrectprivate.h"
#include "gsktransformprivate.h"

/* A note about coordinate systems
 *
 * The rendering code keeps track of multiple coordinate systems to optimize rendering as
 * much as possible and in the coordinate system it makes most sense in.
 * Sometimes there are cases where GL requires a certain coordinate system, too.
 *
 * 1. the node coordinate system
 * This is the coordinate system of the rendernode. It is basically not used outside of
 * looking at the node and basically never hits the GPU (it does for paths). We immediately
 * convert it to:
 *
 * 2. the basic coordinate system
 * convert on CPU: NodeProcessor.offset
 * convert on GPU: ---
 * This is the coordinate system we emit vertex state in, the clip is tracked here.
 * The main benefit is that most transform nodes only change the offset, so we can avoid
 * updating any state in this coordinate system when that happens.
 *
 * 3. the scaled coordinate system
 * converts on CPU: NodeProcessor.scale
 * converts on GPU: push.scale
 * This includes the current scale of the transform. It is usually equal to the scale factor
 * of the window we are rendering to (which is bad because devs without hidpi screens can
 * forget this and then everyone else will see bugs). We make decisions about pixel sizes in
 * this coordinate system, like picking glyphs from the glyph cache or the sizes of offscreens
 * for offscreen rendering.
 *
 * 4. the device coordinate system
 * converts on CPU: NodeProcessor.modelview
 * converts on GPU: ---
 * The scissor rect is tracked in this coordinate system. It represents the actual device pixels.
 * A bunch of optimizations (like glScissor() and glClear()) can be done here, so in the case
 * that modelview == NULL and we end up with integer coordinates (because pixels), we try to go
 * here.
 * This coordinate system does not exist on shaders as they rarely reason about pixels, and if
 * they need to, they can ask the fragment shader via gl_FragCoord.
 *
 * 5. the GL coordinate system
 * converts on CPU: NodeProcessor.projection
 * converts on GPU: push.mvp (from scaled coordinate system)
 * This coordinate system is what GL (or Vulkan) expect coordinates to appear in, and is usually
 * (-1, -1) => (1, 1), but may be flipped etc depending on the render target. The CPU essentially
 * never uses it, other than to allow the vertex shaders to emit its vertices.
 */

static void
gsk_gpu_shader_image_clear (gpointer data)
{
  GskGpuShaderImage *image = data;
  g_object_unref (image->image);
}

#define GDK_ARRAY_NAME gsk_gpu_pattern_images
#define GDK_ARRAY_TYPE_NAME GskGpuPatternImages
#define GDK_ARRAY_ELEMENT_TYPE GskGpuShaderImage
#define GDK_ARRAY_FREE_FUNC gsk_gpu_shader_image_clear
#define GDK_ARRAY_PREALLOC 8
#define GDK_ARRAY_BY_VALUE 1
#include "gdk/gdkarrayimpl.c"

typedef struct _GskGpuNodeProcessor GskGpuNodeProcessor;
typedef struct _GskGpuPatternWriter GskGpuPatternWriter;

typedef enum {
  GSK_GPU_GLOBAL_MATRIX  = (1 << 0),
  GSK_GPU_GLOBAL_SCALE   = (1 << 1),
  GSK_GPU_GLOBAL_CLIP    = (1 << 2),
  GSK_GPU_GLOBAL_SCISSOR = (1 << 3),
} GskGpuGlobals;

struct _GskGpuNodeProcessor
{
  GskGpuFrame           *frame;
  cairo_rectangle_int_t  scissor;
  graphene_point_t       offset;
  graphene_matrix_t      projection;
  graphene_vec2_t        scale;
  GskTransform          *modelview;
  GskGpuClip             clip;

  GskGpuGlobals          pending_globals;
};

struct _GskGpuPatternWriter
{
  GskGpuFrame           *frame;

  graphene_rect_t        bounds;
  graphene_point_t       offset;
  graphene_vec2_t        scale;
  guint                  stack;

  GskGpuBufferWriter     writer;
  GskGpuPatternImages    images;
};

static void             gsk_gpu_node_processor_add_node                 (GskGpuNodeProcessor            *self,
                                                                         GskRenderNode                  *node);
static gboolean         gsk_gpu_node_processor_create_node_pattern      (GskGpuPatternWriter            *self,
                                                                         GskRenderNode                  *node);

static void
gsk_gpu_node_processor_finish (GskGpuNodeProcessor *self)
{
  g_clear_pointer (&self->modelview, gsk_transform_unref);
}

static void
gsk_gpu_node_processor_init (GskGpuNodeProcessor         *self,
                             GskGpuFrame                 *frame,
                             GskGpuImage                 *target,
                             const cairo_rectangle_int_t *clip,
                             const graphene_rect_t       *viewport)
{
  self->frame = frame;

  self->scissor = *clip;
  gsk_gpu_clip_init_empty (&self->clip, &GRAPHENE_RECT_INIT (0, 0, viewport->size.width, viewport->size.height));

  self->modelview = NULL;
  gsk_gpu_image_get_projection_matrix (target, &self->projection);
  graphene_vec2_init (&self->scale, gsk_gpu_image_get_width (target) / viewport->size.width,
                                    gsk_gpu_image_get_height (target) / viewport->size.height);
  self->offset = GRAPHENE_POINT_INIT (-viewport->origin.x,
                                      -viewport->origin.y);
  self->pending_globals = GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP | GSK_GPU_GLOBAL_SCISSOR;
}

static void
gsk_gpu_node_processor_emit_globals_op (GskGpuNodeProcessor *self)
{
  graphene_matrix_t mvp;

  if (self->modelview)
    {
      gsk_transform_to_matrix (self->modelview, &mvp);
      graphene_matrix_multiply (&mvp, &self->projection, &mvp);
    }
  else
    graphene_matrix_init_from_matrix (&mvp, &self->projection);

  gsk_gpu_globals_op (self->frame,
                      &self->scale,
                      &mvp,
                      &self->clip.rect);

  self->pending_globals &= ~(GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP);
}

static void
gsk_gpu_node_processor_emit_scissor_op (GskGpuNodeProcessor *self)
{
  gsk_gpu_scissor_op (self->frame,
                      &self->scissor);
  self->pending_globals &= ~GSK_GPU_GLOBAL_SCISSOR;
}

static void
gsk_gpu_node_processor_sync_globals (GskGpuNodeProcessor *self,
                                     GskGpuGlobals        ignored)
{
  GskGpuGlobals required;

  required = self->pending_globals & ~ignored;

  if (required & (GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP))
    gsk_gpu_node_processor_emit_globals_op (self);
  if (required & GSK_GPU_GLOBAL_SCISSOR)
    gsk_gpu_node_processor_emit_scissor_op (self);
}

void
gsk_gpu_node_processor_process (GskGpuFrame                 *frame,
                                GskGpuImage                 *target,
                                const cairo_rectangle_int_t *clip,
                                GskRenderNode               *node,
                                const graphene_rect_t       *viewport)
{
  GskGpuNodeProcessor self;

  gsk_gpu_node_processor_init (&self,
                               frame,
                               target,
                               clip,
                               viewport);

  gsk_gpu_node_processor_add_node (&self, node);

  gsk_gpu_node_processor_finish (&self);
}

static void
gsk_gpu_pattern_writer_init (GskGpuPatternWriter    *self,
                             GskGpuFrame            *frame,
                             const graphene_vec2_t  *scale,
                             const graphene_point_t *offset,
                             const graphene_rect_t  *bounds)
{
  self->frame = frame;
  self->bounds = GRAPHENE_RECT_INIT (bounds->origin.x + offset->x,
                                     bounds->origin.y + offset->y,
                                     bounds->size.width,
                                     bounds->size.height);
  self->offset = *offset;
  self->scale = *scale;
  self->stack = 0;

  gsk_gpu_frame_write_buffer_memory (frame, &self->writer);
  gsk_gpu_pattern_images_init (&self->images);
}

static gboolean
gsk_gpu_pattern_writer_push_stack (GskGpuPatternWriter *self)
{
  if (self->stack >= GSK_GPU_PATTERN_STACK_SIZE)
    return FALSE;

  self->stack++;
  return TRUE;
}

static void
gsk_gpu_pattern_writer_pop_stack (GskGpuPatternWriter *self)
{
  g_assert (self->stack > 0);
  self->stack--;
}

static void
gsk_gpu_pattern_writer_finish (GskGpuPatternWriter *self)
{
  g_assert (self->stack == 0);
  gsk_gpu_pattern_images_clear (&self->images);
}

static void
gsk_gpu_pattern_writer_abort (GskGpuPatternWriter *self)
{
  gsk_gpu_buffer_writer_abort (&self->writer);

  gsk_gpu_pattern_writer_finish (self);
}

static void
gsk_gpu_pattern_writer_commit_op (GskGpuPatternWriter   *self,
                                  GskGpuShaderClip       clip)
{
  guint32 pattern_id;
  GskGpuShaderImage *images;
  gsize n_images;

  pattern_id = gsk_gpu_buffer_writer_commit (&self->writer) / sizeof (float);
  n_images = gsk_gpu_pattern_images_get_size (&self->images);
  images = gsk_gpu_pattern_images_steal (&self->images);

  {
    for (gsize i = 0; i < n_images; i++)
      g_assert (images[i].image);
  }

  gsk_gpu_uber_op (self->frame,
                   clip,
                   &self->bounds,
                   &self->offset,
                   images,
                   n_images,
                   pattern_id);

  gsk_gpu_pattern_writer_finish (self);
}

static gboolean
gsk_gpu_pattern_writer_add_image (GskGpuPatternWriter *self,
                                  GskGpuImage         *image,
                                  GskGpuSampler        sampler,
                                  guint32             *out_descriptor)
{
  if (gsk_gpu_pattern_images_get_size (&self->images) >= 16)
    return FALSE;

  *out_descriptor = gsk_gpu_frame_get_image_descriptor (self->frame, image, sampler);
  gsk_gpu_pattern_images_append (&self->images,
                                 &(GskGpuShaderImage) {
                                     image,
                                     sampler,
                                     *out_descriptor
                                 });

  return TRUE;
}

static void
extract_scale_from_transform (GskTransform *transform,
                              float        *out_scale_x,
                              float        *out_scale_y)
{
  switch (gsk_transform_get_category (transform))
    {
    default:
      g_assert_not_reached ();
      G_GNUC_FALLTHROUGH;
    case GSK_TRANSFORM_CATEGORY_IDENTITY:
    case GSK_TRANSFORM_CATEGORY_2D_TRANSLATE:
      *out_scale_x = 1.0f;
      *out_scale_y = 1.0f;
      return;

    case GSK_TRANSFORM_CATEGORY_2D_AFFINE:
      {
        float scale_x, scale_y, dx, dy;
        gsk_transform_to_affine (transform, &scale_x, &scale_y, &dx, &dy);
        *out_scale_x = fabs (scale_x);
        *out_scale_y = fabs (scale_y);
      }
      return;

    case GSK_TRANSFORM_CATEGORY_2D:
      {
        float skew_x, skew_y, scale_x, scale_y, angle, dx, dy;
        gsk_transform_to_2d_components (transform,
                                        &skew_x, &skew_y,
                                        &scale_x, &scale_y,
                                        &angle,
                                        &dx, &dy);
        *out_scale_x = fabs (scale_x);
        *out_scale_y = fabs (scale_y);
      }
      return;

    case GSK_TRANSFORM_CATEGORY_UNKNOWN:
    case GSK_TRANSFORM_CATEGORY_ANY:
    case GSK_TRANSFORM_CATEGORY_3D:
      {
        graphene_quaternion_t rotation;
        graphene_matrix_t matrix;
        graphene_vec4_t perspective;
        graphene_vec3_t translation;
        graphene_vec3_t matrix_scale;
        graphene_vec3_t shear;

        gsk_transform_to_matrix (transform, &matrix);
        graphene_matrix_decompose (&matrix,
                                   &translation,
                                   &matrix_scale,
                                   &rotation,
                                   &shear,
                                   &perspective);

        *out_scale_x = fabs (graphene_vec3_get_x (&matrix_scale));
        *out_scale_y = fabs (graphene_vec3_get_y (&matrix_scale));
      }
      return;
    }
}

static gboolean
gsk_gpu_node_processor_rect_is_integer (GskGpuNodeProcessor   *self,
                                        const graphene_rect_t *rect,
                                        cairo_rectangle_int_t *int_rect)
{
  graphene_rect_t transformed_rect;
  float scale_x = graphene_vec2_get_x (&self->scale);
  float scale_y = graphene_vec2_get_y (&self->scale);

  switch (gsk_transform_get_category (self->modelview))
    {
    case GSK_TRANSFORM_CATEGORY_UNKNOWN:
    case GSK_TRANSFORM_CATEGORY_ANY:
    case GSK_TRANSFORM_CATEGORY_3D:
    case GSK_TRANSFORM_CATEGORY_2D:
      /* FIXME: We could try to handle 90° rotation here,
       * but I don't think there's a use case */
      return FALSE;

    case GSK_TRANSFORM_CATEGORY_2D_AFFINE:
    case GSK_TRANSFORM_CATEGORY_2D_TRANSLATE:
      gsk_transform_transform_bounds (self->modelview, rect, &transformed_rect);
      rect = &transformed_rect;
      break;

    case GSK_TRANSFORM_CATEGORY_IDENTITY:
    default:
      break;
    }

  int_rect->x = rect->origin.x * scale_x;
  int_rect->y = rect->origin.y * scale_y;
  int_rect->width = rect->size.width * scale_x;
  int_rect->height = rect->size.height * scale_y;

  return int_rect->x == rect->origin.x * scale_x
      && int_rect->y == rect->origin.y * scale_y
      && int_rect->width == rect->size.width * scale_x
      && int_rect->height == rect->size.height * scale_y;
}

static void
gsk_gpu_node_processor_get_clip_bounds (GskGpuNodeProcessor *self,
                                        graphene_rect_t     *out_bounds)
{
  graphene_rect_offset_r (&self->clip.rect.bounds,
                          - self->offset.x,
                          - self->offset.y,
                          out_bounds);
 
  /* FIXME: We could try the scissor rect here.
   * But how often is that smaller than the clip bounds?
   */
}
 
static gboolean G_GNUC_WARN_UNUSED_RESULT
gsk_gpu_node_processor_clip_node_bounds (GskGpuNodeProcessor *self,
                                         GskRenderNode       *node,
                                         graphene_rect_t     *out_bounds)
{
  graphene_rect_t tmp;

  gsk_gpu_node_processor_get_clip_bounds (self, &tmp);
  
  if (!gsk_rect_intersection (&tmp, &node->bounds, out_bounds))
    return FALSE;

  return TRUE;
}

static GskGpuImage *
gsk_gpu_get_node_as_image (GskGpuFrame            *frame,
                           const graphene_rect_t  *clip_bounds,
                           const graphene_vec2_t  *scale,
                           GskRenderNode          *node,
                           graphene_rect_t        *out_bounds)
{
  GskGpuImage *result;

  switch ((guint) gsk_render_node_get_node_type (node))
    {
    case GSK_TEXTURE_NODE:
      {
        GdkTexture *texture = gsk_texture_node_get_texture (node);
        GskGpuDevice *device = gsk_gpu_frame_get_device (frame);
        gint64 timestamp = gsk_gpu_frame_get_timestamp (frame);
        result = gsk_gpu_device_lookup_texture_image (device, texture, timestamp);
        if (result == NULL)
          {
            result = gsk_gpu_upload_texture_op (frame, texture);
            gsk_gpu_device_cache_texture_image (device, texture, timestamp, result);
          }

        *out_bounds = node->bounds;
        return result;
      }

    case GSK_CAIRO_NODE:
      {
        graphene_rect_t clipped;

        graphene_rect_intersection (clip_bounds, &node->bounds, &clipped);

        if (clipped.size.width == 0 || clipped.size.height == 0)
          return NULL;

        result = gsk_gpu_upload_cairo_op (frame,
                                          node,
                                          scale,
                                          &clipped);
        g_object_ref (result);

        *out_bounds = clipped;
        return result;
      }

    default:
      {
        graphene_rect_t clipped;

        graphene_rect_intersection (clip_bounds, &node->bounds, &clipped);

        if (clipped.size.width == 0 || clipped.size.height == 0)
          return NULL;

        GSK_DEBUG (FALLBACK, "Offscreening node '%s'", g_type_name_from_instance ((GTypeInstance *) node));
        result = gsk_gpu_render_pass_op_offscreen (frame,
                                                   scale,
                                                   &clipped,
                                                   node);

        *out_bounds = clipped;
        return result;
      }
   }
}

static void
gsk_gpu_node_processor_add_fallback_node (GskGpuNodeProcessor *self,
                                          GskRenderNode       *node)
{
  GskGpuImage *image;
  graphene_rect_t clipped_bounds;

  if (!gsk_gpu_node_processor_clip_node_bounds (self, node, &clipped_bounds))
    return;

  gsk_gpu_node_processor_sync_globals (self, 0);

  image = gsk_gpu_upload_cairo_op (self->frame,
                                   node,
                                   &self->scale,
                                   &clipped_bounds);

  gsk_gpu_texture_op (self->frame,
                      gsk_gpu_clip_get_shader_clip (&self->clip, &self->offset, &clipped_bounds),
                      image,
                      GSK_GPU_SAMPLER_DEFAULT,
                      &node->bounds,
                      &self->offset,
                      &clipped_bounds);
}

static void
gsk_gpu_node_processor_add_node_as_pattern (GskGpuNodeProcessor *self,
                                            GskRenderNode       *node)
{
  GskGpuPatternWriter writer;
 
  gsk_gpu_pattern_writer_init (&writer,
                               self->frame,
                               &self->scale,
                               &self->offset,
                               &node->bounds);
 
  if (!gsk_gpu_node_processor_create_node_pattern (&writer, node))
    {
      gsk_gpu_pattern_writer_abort (&writer);
      GSK_DEBUG (FALLBACK, "Pattern shader for node %s failed", g_type_name_from_instance ((GTypeInstance *) node));
      gsk_gpu_node_processor_add_fallback_node (self, node);
      return;
    }

  gsk_gpu_buffer_writer_append_uint (&writer.writer, GSK_GPU_PATTERN_DONE);

  gsk_gpu_pattern_writer_commit_op (&writer,
                                    gsk_gpu_clip_get_shader_clip (&self->clip, &self->offset, &node->bounds));
}

static void
gsk_gpu_node_processor_add_clip_node (GskGpuNodeProcessor *self,
                                      GskRenderNode       *node)
{
  GskRenderNode *child;
  GskGpuClip old_clip;
  graphene_rect_t clip;
  cairo_rectangle_int_t scissor;

  child = gsk_clip_node_get_child (node);
  graphene_rect_offset_r (gsk_clip_node_get_clip (node),
                          self->offset.x, self->offset.y,
                          &clip);

  gsk_gpu_clip_init_copy (&old_clip, &self->clip);

  /* Check if we can use scissoring for the clip */
  if (gsk_gpu_node_processor_rect_is_integer (self, &clip, &scissor))
    {
      cairo_rectangle_int_t old_scissor;

      if (!gdk_rectangle_intersect (&scissor, &self->scissor, &scissor))
        return;

      old_scissor = self->scissor;

      if (gsk_gpu_clip_intersect_rect (&self->clip, &old_clip, &clip))
        {
          if (self->clip.type == GSK_GPU_CLIP_ALL_CLIPPED)
            {
              gsk_gpu_clip_init_copy (&self->clip, &old_clip);
              return;
            }
          else if (self->clip.type == GSK_GPU_CLIP_RECT)
            {
              self->clip.type = GSK_GPU_CLIP_NONE;
            }

          self->scissor = scissor;
          self->pending_globals |= GSK_GPU_GLOBAL_SCISSOR | GSK_GPU_GLOBAL_CLIP;

          gsk_gpu_node_processor_add_node (self, child);

          gsk_gpu_clip_init_copy (&self->clip, &old_clip);
          self->scissor = old_scissor;
          self->pending_globals |= GSK_GPU_GLOBAL_SCISSOR | GSK_GPU_GLOBAL_CLIP;
        }
      else
        {
          self->scissor = scissor;
          self->pending_globals |= GSK_GPU_GLOBAL_SCISSOR;

          gsk_gpu_clip_init_copy (&self->clip, &old_clip);

          gsk_gpu_node_processor_add_node (self, child);

          self->scissor = old_scissor;
          self->pending_globals |= GSK_GPU_GLOBAL_SCISSOR;
        }
    }
  else
    {
      if (!gsk_gpu_clip_intersect_rect (&self->clip, &old_clip, &clip))
        {
          GSK_DEBUG (FALLBACK, "Failed to find intersection between clip of type %u and rectangle", self->clip.type);
          gsk_gpu_clip_init_copy (&self->clip, &old_clip);
          gsk_gpu_node_processor_add_fallback_node (self, node);
          return;
        }

      if (self->clip.type == GSK_GPU_CLIP_ALL_CLIPPED)
        {
          gsk_gpu_clip_init_copy (&self->clip, &old_clip);
          return;
        }

      self->pending_globals |= GSK_GPU_GLOBAL_CLIP;

      gsk_gpu_node_processor_add_node (self, child);

      gsk_gpu_clip_init_copy (&self->clip, &old_clip);
      self->pending_globals |= GSK_GPU_GLOBAL_CLIP;
    }
}

static gboolean
gsk_gpu_node_processor_create_clip_pattern (GskGpuPatternWriter *self,
                                            GskRenderNode       *node)
{
  if (!gsk_gpu_node_processor_create_node_pattern (self, gsk_opacity_node_get_child (node)))
    return FALSE;

  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_CLIP);
  gsk_gpu_buffer_writer_append_rect (&self->writer,
                                     gsk_clip_node_get_clip (node),
                                     &self->offset);

  return TRUE;
}

static void
gsk_gpu_node_processor_add_rounded_clip_node (GskGpuNodeProcessor *self,
                                              GskRenderNode       *node)
{
  GskGpuClip old_clip;
  GskRoundedRect clip;

  gsk_gpu_clip_init_copy (&old_clip, &self->clip);

  clip = *gsk_rounded_clip_node_get_clip (node);
  gsk_rounded_rect_offset (&clip, self->offset.x, self->offset.y);

  if (!gsk_gpu_clip_intersect_rounded_rect (&self->clip, &old_clip, &clip))
    {
      GSK_DEBUG (FALLBACK, "Failed to find intersection between clip of type %u and rounded rectangle", self->clip.type);
      gsk_gpu_clip_init_copy (&self->clip, &old_clip);
      gsk_gpu_node_processor_add_fallback_node (self, node);
      return;
    }

  if (self->clip.type == GSK_GPU_CLIP_ALL_CLIPPED)
    {
      gsk_gpu_clip_init_copy (&self->clip, &old_clip);
      return;
    }

  self->pending_globals |= GSK_GPU_GLOBAL_CLIP;

  gsk_gpu_node_processor_add_node (self, gsk_rounded_clip_node_get_child (node));

  gsk_gpu_clip_init_copy (&self->clip, &old_clip);
  self->pending_globals |= GSK_GPU_GLOBAL_CLIP;
}

static void
gsk_gpu_node_processor_add_transform_node (GskGpuNodeProcessor *self,
                                           GskRenderNode       *node)
{
  GskRenderNode *child;
  GskTransform *transform;
  graphene_point_t old_offset;
  graphene_vec2_t old_scale;
  GskTransform *old_modelview;
  GskGpuClip old_clip;

  child = gsk_transform_node_get_child (node);
  transform = gsk_transform_node_get_transform (node);

  switch (gsk_transform_get_category (transform))
    {
    case GSK_TRANSFORM_CATEGORY_IDENTITY:
    case GSK_TRANSFORM_CATEGORY_2D_TRANSLATE:
      {
        float dx, dy;
        gsk_transform_to_translate (transform, &dx, &dy);
        old_offset = self->offset;
        self->offset.x += dx;
        self->offset.y += dy;
        gsk_gpu_node_processor_add_node (self, child);
        self->offset = old_offset;
      }
      return;

    case GSK_TRANSFORM_CATEGORY_2D_AFFINE:
      {
        float dx, dy, scale_x, scale_y;

        gsk_gpu_clip_init_copy (&old_clip, &self->clip);
        old_offset = self->offset;
        old_scale = self->scale;
        old_modelview = gsk_transform_ref (self->modelview);

        gsk_transform_to_affine (transform, &scale_x, &scale_y, &dx, &dy);
        gsk_gpu_clip_scale (&self->clip, &old_clip, scale_x, scale_y);
        self->offset.x = (self->offset.x + dx) / scale_x;
        self->offset.y = (self->offset.y + dy) / scale_y;
        graphene_vec2_init (&self->scale, fabs (scale_x), fabs (scale_y));
        graphene_vec2_multiply (&self->scale, &old_scale, &self->scale);
        self->modelview = gsk_transform_scale (self->modelview,
                                               scale_x / fabs (scale_x),
                                               scale_y / fabs (scale_y));
      }
      break;

    case GSK_TRANSFORM_CATEGORY_2D:
    case GSK_TRANSFORM_CATEGORY_UNKNOWN:
    case GSK_TRANSFORM_CATEGORY_ANY:
    case GSK_TRANSFORM_CATEGORY_3D:
      {
        GskTransform *clip_transform;
        float scale_x, scale_y, old_pixels, new_pixels;

        clip_transform = gsk_transform_transform (gsk_transform_translate (NULL, &self->offset), transform);
        gsk_gpu_clip_init_copy (&old_clip, &self->clip);

        if (gsk_gpu_clip_contains_rect (&self->clip, &self->offset, &node->bounds))
          {
            gsk_gpu_clip_init_empty (&self->clip, &child->bounds);
          }
        else if (!gsk_gpu_clip_transform (&self->clip, &old_clip, clip_transform, &child->bounds))
          {
            gsk_transform_unref (clip_transform);
            GSK_DEBUG (FALLBACK, "Transform nodes can't deal with clip type %u", self->clip.type);
            gsk_gpu_node_processor_add_fallback_node (self, node);
            return;
          }

        old_offset = self->offset;
        old_scale = self->scale;
        old_modelview = gsk_transform_ref (self->modelview);

        self->modelview = gsk_transform_scale (self->modelview,
                                               graphene_vec2_get_x (&self->scale),
                                               graphene_vec2_get_y (&self->scale));
        self->modelview = gsk_transform_transform (self->modelview, clip_transform);
        gsk_transform_unref (clip_transform);

        extract_scale_from_transform (self->modelview, &scale_x, &scale_y);

        old_pixels = graphene_vec2_get_x (&old_scale) * graphene_vec2_get_y (&old_scale) *
                     old_clip.rect.bounds.size.width * old_clip.rect.bounds.size.height;
        new_pixels = scale_x * scale_y * self->clip.rect.bounds.size.width * self->clip.rect.bounds.size.height;
        if (new_pixels > 2 * old_pixels)
          {
            float forced_downscale = 2 * old_pixels / new_pixels;
            scale_x *= forced_downscale;
            scale_y *= forced_downscale;
          }

        self->modelview = gsk_transform_scale (self->modelview, 1 / scale_x, 1 / scale_y);
        graphene_vec2_init (&self->scale, scale_x, scale_y);
        self->offset = *graphene_point_zero ();
      }
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  self->pending_globals |= GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP;

  gsk_gpu_node_processor_add_node (self, child);

  self->offset = old_offset;
  self->scale = old_scale;
  gsk_transform_unref (self->modelview);
  self->modelview = old_modelview;
  gsk_gpu_clip_init_copy (&self->clip, &old_clip);
  self->pending_globals |= GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP;
}

static gboolean
gsk_gpu_node_processor_create_color_pattern (GskGpuPatternWriter *self,
                                             GskRenderNode       *node)
{
  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_COLOR);
  gsk_gpu_buffer_writer_append_rgba (&self->writer, gsk_color_node_get_color (node));

  return TRUE;
}

static void
gsk_gpu_node_processor_add_border_node (GskGpuNodeProcessor *self,
                                        GskRenderNode       *node)
{
  gsk_gpu_border_op (self->frame,
                     gsk_gpu_clip_get_shader_clip (&self->clip, &self->offset, &node->bounds),
                     gsk_border_node_get_outline (node),
                     &self->offset,
                     gsk_border_node_get_widths (node),
                     gsk_border_node_get_colors (node));
}

static void
gsk_gpu_node_processor_add_texture_node (GskGpuNodeProcessor *self,
                                         GskRenderNode       *node)
{
  GskGpuDevice *device;
  GskGpuImage *image;
  GdkTexture *texture;
  gint64 timestamp;

  device = gsk_gpu_frame_get_device (self->frame);
  texture = gsk_texture_node_get_texture (node);
  timestamp = gsk_gpu_frame_get_timestamp (self->frame);

  image = gsk_gpu_device_lookup_texture_image (device, texture, timestamp);
  if (image == NULL)
    {
      image = gsk_gpu_upload_texture_op (self->frame, texture);
      gsk_gpu_device_cache_texture_image (device, texture, timestamp, image);
      image = g_object_ref (image);
    }

  gsk_gpu_texture_op (self->frame,
                      gsk_gpu_clip_get_shader_clip (&self->clip, &self->offset, &node->bounds),
                      image,
                      GSK_GPU_SAMPLER_DEFAULT,
                      &node->bounds,
                      &self->offset,
                      &node->bounds);

  g_object_unref (image);
}

static gboolean
gsk_gpu_node_processor_create_texture_pattern (GskGpuPatternWriter *self,
                                               GskRenderNode       *node)
{
  GskGpuDevice *device;
  GdkTexture *texture;
  gint64 timestamp;
  guint32 descriptor;
  GskGpuImage *image;

  device = gsk_gpu_frame_get_device (self->frame);
  texture = gsk_texture_node_get_texture (node);
  timestamp = gsk_gpu_frame_get_timestamp (self->frame);

  image = gsk_gpu_device_lookup_texture_image (device, texture, timestamp);
  if (image == NULL)
    {
      image = gsk_gpu_upload_texture_op (self->frame, texture);
      gsk_gpu_device_cache_texture_image (device, texture, timestamp, image);
    }

  if (!gsk_gpu_pattern_writer_add_image (self, image, GSK_GPU_SAMPLER_DEFAULT, &descriptor))
    return FALSE;

  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_TEXTURE);
  gsk_gpu_buffer_writer_append_uint (&self->writer, descriptor);
  gsk_gpu_buffer_writer_append_rect (&self->writer, &node->bounds, &self->offset);

  return TRUE;
}

static gboolean
gsk_gpu_node_processor_create_linear_gradient_pattern (GskGpuPatternWriter *self,
                                                       GskRenderNode       *node)
{
  if (gsk_render_node_get_node_type (node) == GSK_REPEATING_LINEAR_GRADIENT_NODE)
    gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_REPEATING_LINEAR_GRADIENT);
  else
    gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_LINEAR_GRADIENT);

  gsk_gpu_buffer_writer_append_point (&self->writer,
                                      gsk_linear_gradient_node_get_start (node),
                                      &self->offset);
  gsk_gpu_buffer_writer_append_point (&self->writer,
                                      gsk_linear_gradient_node_get_end (node),
                                      &self->offset);
  gsk_gpu_buffer_writer_append_color_stops (&self->writer, 
                                            gsk_linear_gradient_node_get_color_stops (node, NULL),
                                            gsk_linear_gradient_node_get_n_color_stops (node));

  return TRUE;
}

static gboolean
gsk_gpu_node_processor_create_radial_gradient_pattern (GskGpuPatternWriter *self,
                                                       GskRenderNode       *node)
{
  if (gsk_render_node_get_node_type (node) == GSK_REPEATING_RADIAL_GRADIENT_NODE)
    gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_REPEATING_RADIAL_GRADIENT);
  else
    gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_RADIAL_GRADIENT);

  gsk_gpu_buffer_writer_append_point (&self->writer,
                                      gsk_radial_gradient_node_get_center (node),
                                      &self->offset);
  gsk_gpu_buffer_writer_append_float (&self->writer, gsk_radial_gradient_node_get_hradius (node));
  gsk_gpu_buffer_writer_append_float (&self->writer, gsk_radial_gradient_node_get_vradius (node));
  gsk_gpu_buffer_writer_append_float (&self->writer, gsk_radial_gradient_node_get_start (node));
  gsk_gpu_buffer_writer_append_float (&self->writer, gsk_radial_gradient_node_get_end (node));
  gsk_gpu_buffer_writer_append_color_stops (&self->writer, 
                                            gsk_radial_gradient_node_get_color_stops (node, NULL),
                                            gsk_radial_gradient_node_get_n_color_stops (node));

  return TRUE;
}

static gboolean
gsk_gpu_node_processor_create_conic_gradient_pattern (GskGpuPatternWriter *self,
                                                      GskRenderNode       *node)
{
  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_CONIC_GRADIENT);
  gsk_gpu_buffer_writer_append_point (&self->writer,
                                      gsk_conic_gradient_node_get_center (node),
                                      &self->offset);
  gsk_gpu_buffer_writer_append_float (&self->writer, gsk_conic_gradient_node_get_angle (node));
  gsk_gpu_buffer_writer_append_color_stops (&self->writer, 
                                            gsk_conic_gradient_node_get_color_stops (node, NULL),
                                            gsk_conic_gradient_node_get_n_color_stops (node));

  return TRUE;
}

static gboolean
gsk_gpu_node_processor_create_cross_fade_pattern (GskGpuPatternWriter *self,
                                                  GskRenderNode       *node)
{
  GskRenderNode *start_child, *end_child;

  start_child = gsk_cross_fade_node_get_start_child (node);
  end_child = gsk_cross_fade_node_get_end_child (node);

  if (!gsk_gpu_node_processor_create_node_pattern (self, start_child))
    return FALSE;
  if (!gsk_rect_contains_rect (&start_child->bounds, &node->bounds))
    {
      gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_CLIP);
      gsk_gpu_buffer_writer_append_rect (&self->writer, &start_child->bounds, &self->offset);
    }

  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_PUSH_COLOR);

  if (!gsk_gpu_pattern_writer_push_stack (self))
    return FALSE;

  if (!gsk_gpu_node_processor_create_node_pattern (self, end_child))
    {
      gsk_gpu_pattern_writer_pop_stack (self);
      return FALSE;
    }
  if (!gsk_rect_contains_rect (&end_child->bounds, &node->bounds))
    {
      gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_CLIP);
      gsk_gpu_buffer_writer_append_rect (&self->writer, &end_child->bounds, &self->offset);
    }

  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_POP_CROSS_FADE);
  gsk_gpu_buffer_writer_append_float (&self->writer, gsk_cross_fade_node_get_progress (node));

  gsk_gpu_pattern_writer_pop_stack (self);

  return TRUE;
}

static void
gsk_gpu_node_processor_add_glyph_node (GskGpuNodeProcessor *self,
                                       GskRenderNode       *node)
{
  GskGpuDevice *device;
  const PangoGlyphInfo *glyphs;
  PangoFont *font;
  graphene_point_t offset;
  guint i, num_glyphs;
  float scale, inv_scale;

  device = gsk_gpu_frame_get_device (self->frame);
  num_glyphs = gsk_text_node_get_num_glyphs (node);
  glyphs = gsk_text_node_get_glyphs (node, NULL);
  font = gsk_text_node_get_font (node);
  offset = *gsk_text_node_get_offset (node);
  offset.x += self->offset.x;
  offset.y += self->offset.y;

  scale = MAX (graphene_vec2_get_x (&self->scale), graphene_vec2_get_y (&self->scale));
  inv_scale = 1.f / scale;

  for (i = 0; i < num_glyphs; i++)
    {
      GskGpuImage *image;
      graphene_rect_t glyph_bounds, glyph_tex_rect;
      graphene_point_t glyph_offset;

      image = gsk_gpu_device_lookup_glyph_image (device,
                                                 self->frame,
                                                 font,
                                                 glyphs[i].glyph,
                                                 0,
                                                 scale,
                                                 &glyph_bounds,
                                                 &glyph_offset);

      graphene_rect_scale (&glyph_bounds, inv_scale, inv_scale, &glyph_bounds);
      glyph_offset = GRAPHENE_POINT_INIT (offset.x - glyph_offset.x * inv_scale + (float) glyphs[i].geometry.x_offset / PANGO_SCALE,
                                          offset.y - glyph_offset.y * inv_scale + (float) glyphs[i].geometry.y_offset / PANGO_SCALE);
      glyph_tex_rect = GRAPHENE_RECT_INIT (
                           0, 0,
                           gsk_gpu_image_get_width (image) * inv_scale,
                           gsk_gpu_image_get_height (image) * inv_scale
                       );
      if (gsk_text_node_has_color_glyphs (node))
        gsk_gpu_texture_op (self->frame,
                            gsk_gpu_clip_get_shader_clip (&self->clip, &glyph_offset, &glyph_bounds),
                            image,
                            GSK_GPU_SAMPLER_DEFAULT,
                            &glyph_bounds,
                            &glyph_offset,
                            &glyph_tex_rect);
      else
        gsk_gpu_colorize_op (self->frame,
                             gsk_gpu_clip_get_shader_clip (&self->clip, &glyph_offset, &glyph_bounds),
                             image,
                             &glyph_bounds,
                             &glyph_offset,
                             &glyph_tex_rect,
                             gsk_text_node_get_color (node));

      offset.x += (float) glyphs[i].geometry.width / PANGO_SCALE;
    }
}

static gboolean
gsk_gpu_node_processor_create_glyph_pattern (GskGpuPatternWriter *self,
                                             GskRenderNode       *node)
{
  GskGpuDevice *device;
  const PangoGlyphInfo *glyphs;
  PangoFont *font;
  guint num_glyphs;
  gsize i;
  float scale, inv_scale;
  guint32 tex_id;
  GskGpuImage *last_image;
  graphene_point_t offset;

  if (gsk_text_node_has_color_glyphs (node))
    return FALSE;

  device = gsk_gpu_frame_get_device (self->frame);
  num_glyphs = gsk_text_node_get_num_glyphs (node);
  glyphs = gsk_text_node_get_glyphs (node, NULL);
  font = gsk_text_node_get_font (node);
  offset = *gsk_text_node_get_offset (node);
  offset.x += self->offset.x;
  offset.y += self->offset.y;

  scale = MAX (graphene_vec2_get_x (&self->scale), graphene_vec2_get_y (&self->scale));
  inv_scale = 1.f / scale;

  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_GLYPHS);
  gsk_gpu_buffer_writer_append_rgba (&self->writer, gsk_text_node_get_color (node));
  gsk_gpu_buffer_writer_append_uint (&self->writer, num_glyphs);

  last_image = NULL;
  for (i = 0; i < num_glyphs; i++)
    {
      GskGpuImage *image;
      graphene_rect_t glyph_bounds;
      graphene_point_t glyph_offset;

      image = gsk_gpu_device_lookup_glyph_image (device,
                                                 self->frame,
                                                 font,
                                                 glyphs[i].glyph,
                                                 0,
                                                 scale,
                                                 &glyph_bounds,
                                                 &glyph_offset);

      if (image != last_image)
        {
          if (!gsk_gpu_pattern_writer_add_image (self, g_object_ref (image), GSK_GPU_SAMPLER_DEFAULT, &tex_id))
            return FALSE;

          last_image = image;
        }

      graphene_rect_scale (&glyph_bounds, inv_scale, inv_scale, &glyph_bounds);
      glyph_offset = GRAPHENE_POINT_INIT (offset.x - glyph_offset.x * inv_scale + (float) glyphs[i].geometry.x_offset / PANGO_SCALE,
                                          offset.y - glyph_offset.y * inv_scale + (float) glyphs[i].geometry.y_offset / PANGO_SCALE);

      gsk_gpu_buffer_writer_append_uint (&self->writer, tex_id);
      gsk_gpu_buffer_writer_append_rect (&self->writer,
                                         &glyph_bounds,
                                         &glyph_offset);
      gsk_gpu_buffer_writer_append_rect (&self->writer,
                                         &GRAPHENE_RECT_INIT (
                                             0, 0,
                                             gsk_gpu_image_get_width (image) * inv_scale,
                                             gsk_gpu_image_get_height (image) * inv_scale
                                         ),
                                         &glyph_offset);

      offset.x += (float) glyphs[i].geometry.width / PANGO_SCALE;
    }

  return TRUE;
}

static gboolean
gsk_gpu_node_processor_create_opacity_pattern (GskGpuPatternWriter *self,
                                               GskRenderNode       *node)
{
  if (!gsk_gpu_node_processor_create_node_pattern (self, gsk_opacity_node_get_child (node)))
    return FALSE;

  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_OPACITY);
  gsk_gpu_buffer_writer_append_float (&self->writer, gsk_opacity_node_get_opacity (node));

  return TRUE;
}

static gboolean
gsk_gpu_node_processor_create_color_matrix_pattern (GskGpuPatternWriter *self,
                                                    GskRenderNode       *node)
{
  if (!gsk_gpu_node_processor_create_node_pattern (self, gsk_color_matrix_node_get_child (node)))
    return FALSE;

  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_COLOR_MATRIX);
  gsk_gpu_buffer_writer_append_matrix (&self->writer, gsk_color_matrix_node_get_color_matrix (node));
  gsk_gpu_buffer_writer_append_vec4 (&self->writer, gsk_color_matrix_node_get_color_offset (node));

  return TRUE;
}

static gboolean
gsk_gpu_node_processor_create_repeat_pattern (GskGpuPatternWriter *self,
                                              GskRenderNode       *node)
{
  GskRenderNode *child;
  const graphene_rect_t *child_bounds;
  graphene_rect_t old_bounds;

  child = gsk_repeat_node_get_child (node);
  child_bounds = gsk_repeat_node_get_child_bounds (node);

  if (gsk_rect_is_empty (child_bounds))
    {
      gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_COLOR);
      gsk_gpu_buffer_writer_append_rgba (&self->writer, &(GdkRGBA) { 0, 0, 0, 0 });
      return TRUE;
    }

  if (!gsk_gpu_pattern_writer_push_stack (self))
    return FALSE;

  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_REPEAT_PUSH);
  gsk_gpu_buffer_writer_append_rect (&self->writer, child_bounds, &self->offset);

  old_bounds = self->bounds;
  self->bounds = GRAPHENE_RECT_INIT (child_bounds->origin.x + self->offset.x,
                                     child_bounds->origin.y + self->offset.y,
                                     child_bounds->size.width,
                                     child_bounds->size.height);

  if (!gsk_gpu_node_processor_create_node_pattern (self, child))
    {
      gsk_gpu_pattern_writer_pop_stack (self);
      return FALSE;
    }
  self->bounds = old_bounds;

  if (!gsk_rect_contains_rect (&child->bounds, child_bounds))
    {
      gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_CLIP);
      gsk_gpu_buffer_writer_append_rect (&self->writer, &child->bounds, &self->offset);
    }

  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_POSITION_POP);
  gsk_gpu_pattern_writer_pop_stack (self);

  return TRUE;
}

static gboolean
gsk_gpu_node_processor_create_subsurface_pattern (GskGpuPatternWriter *self,
                                                  GskRenderNode       *node)
{
  return gsk_gpu_node_processor_create_node_pattern (self, gsk_subsurface_node_get_child (node));
}

static void
gsk_gpu_node_processor_add_subsurface_node (GskGpuNodeProcessor *self,
                                            GskRenderNode       *node)
{
  gsk_gpu_node_processor_add_node (self, gsk_subsurface_node_get_child (node));
}

static void
gsk_gpu_node_processor_add_container_node (GskGpuNodeProcessor *self,
                                           GskRenderNode       *node)
{
  for (guint i = 0; i < gsk_container_node_get_n_children (node); i++)
    gsk_gpu_node_processor_add_node (self, gsk_container_node_get_child (node, i));
}

static gboolean
gsk_gpu_node_processor_create_debug_pattern (GskGpuPatternWriter *self,
                                             GskRenderNode       *node)
{
  return gsk_gpu_node_processor_create_node_pattern (self, gsk_debug_node_get_child (node));
}

static void
gsk_gpu_node_processor_add_debug_node (GskGpuNodeProcessor *self,
                                       GskRenderNode       *node)
{
  gsk_gpu_node_processor_add_node (self, gsk_debug_node_get_child (node));
}

static const struct
{
  GskGpuGlobals ignored_globals;
  void                  (* process_node)                        (GskGpuNodeProcessor    *self,
                                                                 GskRenderNode          *node);
  gboolean              (* create_pattern)                      (GskGpuPatternWriter    *self,
                                                                 GskRenderNode          *node);
} nodes_vtable[] = {
  [GSK_NOT_A_RENDER_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_CONTAINER_NODE] = {
    GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP | GSK_GPU_GLOBAL_SCISSOR,
    gsk_gpu_node_processor_add_container_node,
    NULL,
  },
  [GSK_CAIRO_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_COLOR_NODE] = {
    0,
    gsk_gpu_node_processor_add_node_as_pattern,
    gsk_gpu_node_processor_create_color_pattern,
  },
  [GSK_LINEAR_GRADIENT_NODE] = {
    0,
    gsk_gpu_node_processor_add_node_as_pattern,
    gsk_gpu_node_processor_create_linear_gradient_pattern,
  },
  [GSK_REPEATING_LINEAR_GRADIENT_NODE] = {
    0,
    gsk_gpu_node_processor_add_node_as_pattern,
    gsk_gpu_node_processor_create_linear_gradient_pattern,
  },
  [GSK_RADIAL_GRADIENT_NODE] = {
    0,
    gsk_gpu_node_processor_add_node_as_pattern,
    gsk_gpu_node_processor_create_radial_gradient_pattern,
  },
  [GSK_REPEATING_RADIAL_GRADIENT_NODE] = {
    0,
    gsk_gpu_node_processor_add_node_as_pattern,
    gsk_gpu_node_processor_create_radial_gradient_pattern,
  },
  [GSK_CONIC_GRADIENT_NODE] = {
    0,
    gsk_gpu_node_processor_add_node_as_pattern,
    gsk_gpu_node_processor_create_conic_gradient_pattern,
  },
  [GSK_BORDER_NODE] = {
    0,
    gsk_gpu_node_processor_add_border_node,
    NULL,
  },
  [GSK_TEXTURE_NODE] = {
    0,
    gsk_gpu_node_processor_add_texture_node,
    gsk_gpu_node_processor_create_texture_pattern,
  },
  [GSK_INSET_SHADOW_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_OUTSET_SHADOW_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_TRANSFORM_NODE] = {
    GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP | GSK_GPU_GLOBAL_SCISSOR,
    gsk_gpu_node_processor_add_transform_node,
    NULL,
  },
  [GSK_OPACITY_NODE] = {
    0,
    gsk_gpu_node_processor_add_node_as_pattern,
    gsk_gpu_node_processor_create_opacity_pattern,
  },
  [GSK_COLOR_MATRIX_NODE] = {
    0,
    gsk_gpu_node_processor_add_node_as_pattern,
    gsk_gpu_node_processor_create_color_matrix_pattern
  },
  [GSK_REPEAT_NODE] = {
    0,
    gsk_gpu_node_processor_add_node_as_pattern,
    gsk_gpu_node_processor_create_repeat_pattern
  },
  [GSK_CLIP_NODE] = {
    GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP | GSK_GPU_GLOBAL_SCISSOR,
    gsk_gpu_node_processor_add_clip_node,
    gsk_gpu_node_processor_create_clip_pattern,
  },
  [GSK_ROUNDED_CLIP_NODE] = {
    GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP | GSK_GPU_GLOBAL_SCISSOR,
    gsk_gpu_node_processor_add_rounded_clip_node,
    NULL,
  },
  [GSK_SHADOW_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_BLEND_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_CROSS_FADE_NODE] = {
    0,
    gsk_gpu_node_processor_add_node_as_pattern,
    gsk_gpu_node_processor_create_cross_fade_pattern,
  },
  [GSK_TEXT_NODE] = {
    0,
    gsk_gpu_node_processor_add_glyph_node,
    gsk_gpu_node_processor_create_glyph_pattern,
  },
  [GSK_BLUR_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_DEBUG_NODE] = {
    GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP | GSK_GPU_GLOBAL_SCISSOR,
    gsk_gpu_node_processor_add_debug_node,
    gsk_gpu_node_processor_create_debug_pattern,
  },
  [GSK_GL_SHADER_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_TEXTURE_SCALE_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_MASK_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_FILL_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_STROKE_NODE] = {
    0,
    NULL,
    NULL,
  },
  [GSK_SUBSURFACE_NODE] = {
    GSK_GPU_GLOBAL_MATRIX | GSK_GPU_GLOBAL_SCALE | GSK_GPU_GLOBAL_CLIP | GSK_GPU_GLOBAL_SCISSOR,
    gsk_gpu_node_processor_add_subsurface_node,
    gsk_gpu_node_processor_create_subsurface_pattern,
  },
};

static void
gsk_gpu_node_processor_add_node (GskGpuNodeProcessor *self,
                                 GskRenderNode       *node)
{
  GskRenderNodeType node_type;

  /* This catches the corner cases of empty nodes, so after this check
   * there's quaranteed to be at least 1 pixel that needs to be drawn */
  if (node->bounds.size.width == 0 || node->bounds.size.height == 0)
    return;
  if (!gsk_gpu_clip_may_intersect_rect (&self->clip, &self->offset, &node->bounds))
    return;

  node_type = gsk_render_node_get_node_type (node);
  if (node_type >= G_N_ELEMENTS (nodes_vtable))
    {
      g_critical ("unkonwn node type %u for %s", node_type, g_type_name_from_instance ((GTypeInstance *) node));
      gsk_gpu_node_processor_add_fallback_node (self, node);
      return;
    }

  gsk_gpu_node_processor_sync_globals (self, nodes_vtable[node_type].ignored_globals);
  g_assert ((self->pending_globals & ~nodes_vtable[node_type].ignored_globals) == 0);

  if (nodes_vtable[node_type].process_node)
    {
      nodes_vtable[node_type].process_node (self, node);
    }
  else
    {
      GSK_DEBUG (FALLBACK, "Unsupported node '%s'",
                 g_type_name_from_instance ((GTypeInstance *) node));
      gsk_gpu_node_processor_add_fallback_node (self, node);
    }
}

static gboolean
gsk_gpu_node_processor_create_node_pattern (GskGpuPatternWriter *self,
                                            GskRenderNode       *node)
{
  GskRenderNodeType node_type;
  graphene_rect_t bounds;
  guchar *tmp_data;
  GskGpuImage *image;
  gsize tmp_size;
  guint32 tex_id;

  node_type = gsk_render_node_get_node_type (node);
  if (node_type >= G_N_ELEMENTS (nodes_vtable))
    {
      g_critical ("unkonwn node type %u for %s", node_type, g_type_name_from_instance ((GTypeInstance *) node));
      return FALSE;
    }

  if (nodes_vtable[node_type].create_pattern != NULL)
    {
      gsize size_before = gsk_gpu_buffer_writer_get_size (&self->writer);
      gsize images_before = gsk_gpu_pattern_images_get_size (&self->images);
      if (nodes_vtable[node_type].create_pattern (self, node))
        return TRUE;
      gsk_gpu_buffer_writer_rewind (&self->writer, size_before);
      g_assert (gsk_gpu_pattern_images_get_size (&self->images) >= images_before);
      gsk_gpu_pattern_images_set_size (&self->images, images_before);
    }

  tmp_data = gsk_gpu_buffer_writer_backup (&self->writer, &tmp_size);
  gsk_gpu_buffer_writer_abort (&self->writer);
  image = gsk_gpu_get_node_as_image (self->frame,
                                     &GRAPHENE_RECT_INIT (
                                         self->bounds.origin.x - self->offset.x,
                                         self->bounds.origin.y - self->offset.y,
                                         self->bounds.size.width,
                                         self->bounds.size.height
                                     ),
                                     &self->scale,
                                     node,
                                     &bounds);
  if (image == NULL)
    {
      gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_COLOR);
      gsk_gpu_buffer_writer_append_rgba (&self->writer, &(GdkRGBA) { 0, 0, 0, 0 });
      return TRUE;
    }

  gsk_gpu_frame_write_buffer_memory (self->frame, &self->writer);
  if (tmp_size)
    {
      gsk_gpu_buffer_writer_append (&self->writer, sizeof (float), tmp_data, tmp_size);
      g_free (tmp_data);
    }

  if (!gsk_gpu_pattern_writer_add_image (self, image, GSK_GPU_SAMPLER_DEFAULT, &tex_id))
    return FALSE;

  gsk_gpu_buffer_writer_append_uint (&self->writer, GSK_GPU_PATTERN_TEXTURE);
  gsk_gpu_buffer_writer_append_uint (&self->writer, tex_id);
  gsk_gpu_buffer_writer_append_rect (&self->writer, &bounds, &self->offset);

  return TRUE;
}
