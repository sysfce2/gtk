#include "config.h"

#include "gskvulkanmaskopprivate.h"

#include "gskvulkanprivate.h"
#include "gskvulkanshaderopprivate.h"

#include "vulkan/resources/mask.vert.h"

typedef struct _GskVkOldMaskOp GskVkOldMaskOp;

struct _GskVkOldMaskOp
{
  GskVkOldShaderOp op;

  struct {
    graphene_rect_t rect;
    graphene_rect_t tex_rect;
    guint32 image_descriptor;
  } source, mask;
  GskMaskMode mask_mode;
};

static void
gsk_vk_old_mask_op_print (GskVkOldOp *op,
                          GString     *string,
                          guint        indent)
{
  GskVkOldMaskOp *self = (GskVkOldMaskOp *) op;

  print_indent (string, indent);
  print_rect (string, &self->source.rect);
  g_string_append (string, "mask ");
  print_rect (string, &self->mask.rect);
  switch (self->mask_mode)
  {
    case GSK_MASK_MODE_ALPHA:
      g_string_append (string, "alpha ");
      break;
    case GSK_MASK_MODE_INVERTED_ALPHA:
      g_string_append (string, "inverted-alpha ");
      break;
    case GSK_MASK_MODE_LUMINANCE:
      g_string_append (string, "luminance ");
      break;
    case GSK_MASK_MODE_INVERTED_LUMINANCE:
      g_string_append (string, "inverted-luminance ");
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  print_newline (string);
}

static void
gsk_vk_old_mask_op_collect_vertex_data (GskVkOldOp *op,
                                        guchar      *data)
{
  GskVkOldMaskOp *self = (GskVkOldMaskOp *) op;
  GskVkOldMaskInstance *instance = (GskVkOldMaskInstance *) (data + ((GskVkOldShaderOp *) op)->vertex_offset);

  gsk_rect_to_float (&self->source.rect, instance->source_rect);
  gsk_rect_to_float (&self->source.tex_rect, instance->source_tex_rect);
  instance->source_id = self->source.image_descriptor;
  gsk_rect_to_float (&self->mask.rect, instance->mask_rect);
  gsk_rect_to_float (&self->mask.tex_rect, instance->mask_tex_rect);
  instance->mask_id = self->mask.image_descriptor;
  instance->mask_mode = self->mask_mode;
}

static void
gsk_vk_old_mask_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                            GskVkOldRender *render)
{
  GskVkOldMaskOp *self = (GskVkOldMaskOp *) op;
  GskVkOldShaderOp *shader = (GskVkOldShaderOp *) op;

  self->source.image_descriptor = gsk_vk_old_render_get_image_descriptor (render, shader->images[0], GSK_VK_OLD_SAMPLER_DEFAULT);
  self->mask.image_descriptor = gsk_vk_old_render_get_image_descriptor (render, shader->images[1], GSK_VK_OLD_SAMPLER_DEFAULT);
}

static const GskVkOldShaderOpClass GSK_VK_OLD_COLOR_MASK_OP_CLASS = {
  {
    GSK_VK_OLD_OP_SIZE (GskVkOldMaskOp),
    GSK_VK_OLD_STAGE_SHADER,
    gsk_vk_old_shader_op_finish,
    gsk_vk_old_mask_op_print,
    gsk_vk_old_shader_op_count_vertex_data,
    gsk_vk_old_mask_op_collect_vertex_data,
    gsk_vk_old_mask_op_reserve_descriptor_sets,
    gsk_vk_old_shader_op_command
  },
  "mask",
  2,
  &gsk_vk_old_mask_info,
};

void
gsk_vk_old_mask_op (GskVkOldRender        *render,
                    GskVkOldShaderClip     clip,
                    const graphene_point_t *offset,
                    GskVkOldImage         *source,
                    const graphene_rect_t  *source_rect,
                    const graphene_rect_t  *source_tex_rect,
                    GskVkOldImage         *mask,
                    const graphene_rect_t  *mask_rect,
                    const graphene_rect_t  *mask_tex_rect,
                    GskMaskMode             mask_mode)
{
  GskVkOldMaskOp *self;

  self = (GskVkOldMaskOp *) gsk_vk_old_shader_op_alloc (render,
                                                         &GSK_VK_OLD_COLOR_MASK_OP_CLASS,
                                                         clip,
                                                         (GskVkOldImage *[2]) {
                                                             source,
                                                             mask,
                                                         });

  graphene_rect_offset_r (source_rect, offset->x, offset->y, &self->source.rect);
  gsk_vk_old_normalize_tex_coords (&self->source.tex_rect, source_rect, source_tex_rect);
  graphene_rect_offset_r (mask_rect, offset->x, offset->y, &self->mask.rect);
  gsk_vk_old_normalize_tex_coords (&self->mask.tex_rect, mask_rect, mask_tex_rect);
  self->mask_mode = mask_mode;
}
