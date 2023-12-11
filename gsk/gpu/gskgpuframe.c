#include "config.h"

#include "gskgpuframeprivate.h"

#include "gskgpubufferprivate.h"
#include "gskgpudeviceprivate.h"
#include "gskgpudownloadopprivate.h"
#include "gskgpuimageprivate.h"
#include "gskgpunodeprocessorprivate.h"
#include "gskgpuopprivate.h"
#include "gskgpurendererprivate.h"
#include "gskgpurenderpassopprivate.h"

#include "gskdebugprivate.h"
#include "gskrendererprivate.h"

#define DEFAULT_VERTEX_BUFFER_SIZE 128 * 1024

/* GL_MAX_UNIFORM_BLOCK_SIZE is at 16384 */
#define DEFAULT_STORAGE_BUFFER_SIZE 16 * 1024 * 64

#define GDK_ARRAY_NAME gsk_gpu_ops
#define GDK_ARRAY_TYPE_NAME GskGpuOps
#define GDK_ARRAY_ELEMENT_TYPE guchar
#define GDK_ARRAY_BY_VALUE 1
#include "gdk/gdkarrayimpl.c"

typedef struct _GskGpuFramePrivate GskGpuFramePrivate;

struct _GskGpuFramePrivate
{
  GskGpuRenderer *renderer;
  GskGpuDevice *device;
  GskGpuOptimizations optimizations;
  gint64 timestamp;

  GskGpuOps ops;
  GskGpuOp *first_op;

  GskGpuBuffer *vertex_buffer;
  guchar *vertex_buffer_data;
  gsize vertex_buffer_used;
  GskGpuBuffer *storage_buffer;
  guchar *storage_buffer_data;
  gsize storage_buffer_used;
};

G_DEFINE_TYPE_WITH_PRIVATE (GskGpuFrame, gsk_gpu_frame, G_TYPE_OBJECT)

static void
gsk_gpu_frame_default_setup (GskGpuFrame *self)
{
}

static void
gsk_gpu_frame_default_cleanup (GskGpuFrame *self)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);
  GskGpuOp *op;
  gsize i;

  for (i = 0; i < gsk_gpu_ops_get_size (&priv->ops); i += op->op_class->size)
    {
      op = (GskGpuOp *) gsk_gpu_ops_index (&priv->ops, i);

      gsk_gpu_op_finish (op);
    }
  gsk_gpu_ops_set_size (&priv->ops, 0);
}

static void
gsk_gpu_frame_cleanup (GskGpuFrame *self)
{
  GSK_GPU_FRAME_GET_CLASS (self)->cleanup (self);
}

static void
gsk_gpu_frame_dispose (GObject *object)
{
  GskGpuFrame *self = GSK_GPU_FRAME (object);

  gsk_gpu_frame_cleanup (self);

  G_OBJECT_CLASS (gsk_gpu_frame_parent_class)->dispose (object);
}

static void
gsk_gpu_frame_finalize (GObject *object)
{
  GskGpuFrame *self = GSK_GPU_FRAME (object);
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  gsk_gpu_ops_clear (&priv->ops);

  g_clear_object (&priv->vertex_buffer);
  g_clear_object (&priv->storage_buffer);

  g_object_unref (priv->device);

  G_OBJECT_CLASS (gsk_gpu_frame_parent_class)->finalize (object);
}

static void
gsk_gpu_frame_class_init (GskGpuFrameClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  klass->setup = gsk_gpu_frame_default_setup;
  klass->cleanup = gsk_gpu_frame_default_cleanup;

  object_class->dispose = gsk_gpu_frame_dispose;
  object_class->finalize = gsk_gpu_frame_finalize;
}

static void
gsk_gpu_frame_init (GskGpuFrame *self)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  gsk_gpu_ops_init (&priv->ops);
}

void
gsk_gpu_frame_setup (GskGpuFrame         *self,
                     GskGpuRenderer      *renderer,
                     GskGpuDevice        *device,
                     GskGpuOptimizations  optimizations)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  /* no reference, the renderer owns us */
  priv->renderer = renderer;
  priv->device = g_object_ref (device);
  priv->optimizations = optimizations;

  GSK_GPU_FRAME_GET_CLASS (self)->setup (self);
}

GskGpuDevice *
gsk_gpu_frame_get_device (GskGpuFrame *self)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  return priv->device;
}

GdkDrawContext *
gsk_gpu_frame_get_context (GskGpuFrame *self)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  return gsk_gpu_renderer_get_context (priv->renderer);
}

gint64
gsk_gpu_frame_get_timestamp (GskGpuFrame *self)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  return priv->timestamp;
}

gboolean
gsk_gpu_frame_should_optimize (GskGpuFrame         *self,
                               GskGpuOptimizations  optimization)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  return (priv->optimizations & optimization) == optimization;
}

static void
gsk_gpu_frame_verbose_print (GskGpuFrame *self,
                             const char  *heading)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  if (GSK_RENDERER_DEBUG_CHECK (GSK_RENDERER (priv->renderer), VERBOSE))
    {
      GskGpuOp *op;
      guint indent = 1;
      GString *string = g_string_new (heading);
      g_string_append (string, ":\n");

      for (op = priv->first_op; op; op = op->next)
        {
          if (op->op_class->stage == GSK_GPU_STAGE_END_PASS)
            indent--;
          gsk_gpu_op_print (op, self, string, indent);
          if (op->op_class->stage == GSK_GPU_STAGE_BEGIN_PASS)
            indent++;
        }

      gdk_debug_message ("%s", string->str);
      g_string_free (string, TRUE);
    }
}

static void
gsk_gpu_frame_seal_ops (GskGpuFrame *self)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);
  GskGpuOp *last, *op;
  guint i;

  priv->first_op = (GskGpuOp *) gsk_gpu_ops_index (&priv->ops, 0);

  last = priv->first_op;
  for (i = last->op_class->size; i < gsk_gpu_ops_get_size (&priv->ops); i += op->op_class->size)
    {
      op = (GskGpuOp *) gsk_gpu_ops_index (&priv->ops, i);

      last->next = op;
      last = op;
    }
}

typedef struct 
{
  struct {
    GskGpuOp *first;
    GskGpuOp *last;
  } upload, command;
} SortData;

static GskGpuOp *
gsk_gpu_frame_sort_render_pass (GskGpuFrame *self,
                                GskGpuOp    *op,
                                SortData    *sort_data)
{
  SortData subpasses = { { NULL, NULL }, { NULL, NULL } };

  while (op)
    {
      switch (op->op_class->stage)
      {
        case GSK_GPU_STAGE_UPLOAD:
          if (sort_data->upload.first == NULL)
            sort_data->upload.first = op;
          else
            sort_data->upload.last->next = op;
          sort_data->upload.last = op;
          op = op->next;
          break;

        case GSK_GPU_STAGE_COMMAND:
        case GSK_GPU_STAGE_SHADER:
          if (sort_data->command.first == NULL)
            sort_data->command.first = op;
          else
            sort_data->command.last->next = op;
          sort_data->command.last = op;
          op = op->next;
          break;

        case GSK_GPU_STAGE_PASS:
          if (subpasses.command.first == NULL)
            subpasses.command.first = op;
          else
            subpasses.command.last->next = op;
          subpasses.command.last = op;
          op = op->next;
          break;

        case GSK_GPU_STAGE_BEGIN_PASS:
          if (subpasses.command.first == NULL)
            subpasses.command.first = op;
          else
            subpasses.command.last->next = op;
          subpasses.command.last = op;

          /* append subpass to existing subpasses */
          op = gsk_gpu_frame_sort_render_pass (self, op->next, &subpasses);
          break;

        case GSK_GPU_STAGE_END_PASS:
          sort_data->command.last->next = op;
          sort_data->command.last = op;
          op = op->next;
          goto out;

        default:
          g_assert_not_reached ();
          break;
      }
    }

out:
  /* prepend subpasses to the current pass */
  if (subpasses.upload.first)
    {
      if (sort_data->upload.first != NULL)
        subpasses.upload.last->next = sort_data->upload.first;
      else
        sort_data->upload.last = subpasses.upload.last;
      sort_data->upload.first = subpasses.upload.first;
    }
  if (subpasses.command.first)
    {
      if (sort_data->command.first != NULL)
        subpasses.command.last->next = sort_data->command.first;
      else
        sort_data->command.last = subpasses.command.last;
      sort_data->command.first = subpasses.command.first;
    }

  return op;
}

static void
gsk_gpu_frame_sort_ops (GskGpuFrame *self)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);
  SortData sort_data = { { NULL, }, };
  
  gsk_gpu_frame_sort_render_pass (self, priv->first_op, &sort_data);

  if (sort_data.upload.first)
    {
      sort_data.upload.last->next = sort_data.command.first;
      priv->first_op = sort_data.upload.first;
    }
  else
    priv->first_op = sort_data.command.first;
  if (sort_data.command.last)
    sort_data.command.last->next = NULL;
}

gpointer
gsk_gpu_frame_alloc_op (GskGpuFrame *self,
                        gsize        size)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);
  gsize pos;

  pos = gsk_gpu_ops_get_size (&priv->ops);

  gsk_gpu_ops_splice (&priv->ops,
                      pos,
                      0, FALSE,
                      NULL,
                      size);

  return gsk_gpu_ops_index (&priv->ops, pos);
}

GskGpuDescriptors *
gsk_gpu_frame_create_descriptors (GskGpuFrame *self)
{
  return GSK_GPU_FRAME_GET_CLASS (self)->create_descriptors (self);
}

static GskGpuBuffer *
gsk_gpu_frame_create_vertex_buffer (GskGpuFrame *self,
                                    gsize        size)
{
  return GSK_GPU_FRAME_GET_CLASS (self)->create_vertex_buffer (self, size);
}

static GskGpuBuffer *
gsk_gpu_frame_create_storage_buffer (GskGpuFrame *self,
                                     gsize        size)
{
  return GSK_GPU_FRAME_GET_CLASS (self)->create_storage_buffer (self, size);
}

static inline gsize
round_up (gsize number, gsize divisor)
{
  return (number + divisor - 1) / divisor * divisor;
}

gsize
gsk_gpu_frame_reserve_vertex_data (GskGpuFrame *self,
                                   gsize        size)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);
  gsize size_needed;

  if (priv->vertex_buffer == NULL)
    priv->vertex_buffer = gsk_gpu_frame_create_vertex_buffer (self, DEFAULT_VERTEX_BUFFER_SIZE);

  size_needed = round_up (priv->vertex_buffer_used, size) + size;

  if (gsk_gpu_buffer_get_size (priv->vertex_buffer) < size_needed)
    {
      gsize old_size = gsk_gpu_buffer_get_size (priv->vertex_buffer);
      GskGpuBuffer *new_buffer = gsk_gpu_frame_create_vertex_buffer (self, old_size * 2);
      guchar *new_data = gsk_gpu_buffer_map (new_buffer);

      if (priv->vertex_buffer_data)
        {
          memcpy (new_data, priv->vertex_buffer_data, old_size);
          gsk_gpu_buffer_unmap (priv->vertex_buffer);
        }
      g_object_unref (priv->vertex_buffer);
      priv->vertex_buffer = new_buffer;
      priv->vertex_buffer_data = new_data;
    }

  priv->vertex_buffer_used = size_needed;
  
  return size_needed - size;
}

guchar *
gsk_gpu_frame_get_vertex_data (GskGpuFrame *self,
                               gsize        offset)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  if (priv->vertex_buffer_data == NULL)
    priv->vertex_buffer_data = gsk_gpu_buffer_map (priv->vertex_buffer);

  return priv->vertex_buffer_data + offset;
}

static void
gsk_gpu_frame_ensure_storage_buffer (GskGpuFrame *self)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  if (priv->storage_buffer_data != NULL)
    return;

  if (priv->storage_buffer == NULL)
    priv->storage_buffer = gsk_gpu_frame_create_storage_buffer (self, DEFAULT_STORAGE_BUFFER_SIZE);

  priv->storage_buffer_data = gsk_gpu_buffer_map (priv->storage_buffer);
}

static void
gsk_gpu_frame_buffer_memory_ensure_size (GskGpuBufferWriter *writer,
                                         gsize               size)
{
  /* FIXME: implement */
  g_assert_not_reached ();
}

static gsize
gsk_gpu_frame_buffer_memory_finish (GskGpuBufferWriter *writer,
                                    gboolean            commit)
{
  GskGpuFrame *self = GSK_GPU_FRAME (writer->user_data);
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);
  gsize offset;

  if (!commit)
    return 0;

  offset = priv->storage_buffer_used;
  priv->storage_buffer_used = writer->size;

  return offset;
}

void
gsk_gpu_frame_write_buffer_memory (GskGpuFrame        *self,
                                   GskGpuBufferWriter *writer)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  gsk_gpu_frame_ensure_storage_buffer (self);

  writer->user_data = self;
  writer->ensure_size = gsk_gpu_frame_buffer_memory_ensure_size;
  writer->finish = gsk_gpu_frame_buffer_memory_finish;

  writer->data = priv->storage_buffer_data;
  writer->initial_size = priv->storage_buffer_used;
  writer->size = priv->storage_buffer_used;
  writer->allocated = gsk_gpu_buffer_get_size (priv->storage_buffer);
}

gboolean
gsk_gpu_frame_is_busy (GskGpuFrame *self)
{
  return GSK_GPU_FRAME_GET_CLASS (self)->is_busy (self);
}

static void
copy_texture (gpointer    user_data,
              GdkTexture *texture)
{
  GdkTexture **target = (GdkTexture **) user_data;

  *target = g_object_ref (texture);
}

static void
gsk_gpu_frame_record (GskGpuFrame            *self,
                      gint64                  timestamp,
                      GskGpuImage            *target,
                      const cairo_region_t   *clip,
                      GskRenderNode          *node,
                      const graphene_rect_t  *viewport,
                      GdkTexture            **texture)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);
  cairo_rectangle_int_t extents;

  priv->timestamp = timestamp;

  if (clip)
    {
      cairo_region_get_extents (clip, &extents);
    }
  else
    {
      extents = (cairo_rectangle_int_t) {
                    0, 0,
                    gsk_gpu_image_get_width (target),
                    gsk_gpu_image_get_height (target)
                };
    }

  gsk_gpu_render_pass_begin_op (self,
                                target,
                                &extents,
                                GSK_RENDER_PASS_PRESENT);

  gsk_gpu_node_processor_process (self,
                                  target,
                                  &extents,
                                  node,
                                  viewport);

  gsk_gpu_render_pass_end_op (self,
                              target,
                              GSK_RENDER_PASS_PRESENT);

  if (texture)
    gsk_gpu_download_op (self, target, copy_texture, texture);

  gsk_gpu_frame_seal_ops (self);
  gsk_gpu_frame_verbose_print (self, "start of frame");
  gsk_gpu_frame_sort_ops (self);
  gsk_gpu_frame_verbose_print (self, "after sort");
}

static void
gsk_gpu_frame_submit (GskGpuFrame *self)
{
  GskGpuFramePrivate *priv = gsk_gpu_frame_get_instance_private (self);

  if (priv->vertex_buffer)
    {
      gsk_gpu_buffer_unmap (priv->vertex_buffer);
      priv->vertex_buffer_data = NULL;
      priv->vertex_buffer_used = 0;
    }

  if (priv->storage_buffer_data)
    {
      gsk_gpu_buffer_unmap (priv->storage_buffer);
      priv->storage_buffer_data = NULL;
      priv->storage_buffer_used = 0;
    }

  GSK_GPU_FRAME_GET_CLASS (self)->submit (self,
                                          priv->vertex_buffer,
                                          priv->storage_buffer,
                                          priv->first_op);
}

void
gsk_gpu_frame_render (GskGpuFrame            *self,
                      gint64                  timestamp,
                      GskGpuImage            *target,
                      const cairo_region_t   *region,
                      GskRenderNode          *node,
                      const graphene_rect_t  *viewport,
                      GdkTexture            **texture)
{
  gsk_gpu_frame_cleanup (self);

  gsk_gpu_frame_record (self, timestamp, target, region, node, viewport, texture);

  gsk_gpu_frame_submit (self);
}


