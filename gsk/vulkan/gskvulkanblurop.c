#include "config.h"

#include "gskvulkanbluropprivate.h"

#include "gskvulkanprivate.h"
#include "gskvulkanshaderopprivate.h"

#include "vulkan/resources/blur.vert.h"

typedef struct _GskVkOldBlurOp GskVkOldBlurOp;

struct _GskVkOldBlurOp
{
  GskVkOldShaderOp op;

  graphene_rect_t rect;
  graphene_rect_t tex_rect;
  float radius;

  guint32 image_descriptor;
};

static void
gsk_vk_old_blur_op_print (GskVkOldOp *op,
                          GString     *string,
                          guint        indent)
{
  GskVkOldBlurOp *self = (GskVkOldBlurOp *) op;

  print_indent (string, indent);
  print_rect (string, &self->rect);
  g_string_append_printf (string, "blur %g",
                          self->radius);
  print_newline (string);
}

static void
gsk_vk_old_blur_op_collect_vertex_data (GskVkOldOp *op,
                                        guchar      *data)
{
  GskVkOldBlurOp *self = (GskVkOldBlurOp *) op;
  GskVkOldBlurInstance *instance = (GskVkOldBlurInstance *) (data + ((GskVkOldShaderOp *) op)->vertex_offset);

  gsk_rect_to_float (&self->rect, instance->rect);
  gsk_rect_to_float (&self->tex_rect, instance->tex_rect);
  instance->tex_id = self->image_descriptor;
  instance->radius = self->radius;
}

static void
gsk_vk_old_blur_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                            GskVkOldRender *render)
{
  GskVkOldBlurOp *self = (GskVkOldBlurOp *) op;
  GskVkOldShaderOp *shader = (GskVkOldShaderOp *) op;

  self->image_descriptor = gsk_vk_old_render_get_image_descriptor (render,
                                                                   shader->images[0],
                                                                   GSK_VK_OLD_SAMPLER_DEFAULT);
}

static const GskVkOldShaderOpClass GSK_VK_OLD_BLUR_OP_CLASS = {
  {
    GSK_VK_OLD_OP_SIZE (GskVkOldBlurOp),
    GSK_VK_OLD_STAGE_SHADER,
    gsk_vk_old_shader_op_finish,
    gsk_vk_old_blur_op_print,
    gsk_vk_old_shader_op_count_vertex_data,
    gsk_vk_old_blur_op_collect_vertex_data,
    gsk_vk_old_blur_op_reserve_descriptor_sets,
    gsk_vk_old_shader_op_command
  },
  "blur",
  1,
  &gsk_vk_old_blur_info,
};

void
gsk_vk_old_blur_op (GskVkOldRender         *render,
                    GskVkOldShaderClip      clip,
                    GskVkOldImage          *image,
                    const graphene_rect_t   *rect,
                    const graphene_point_t  *offset,
                    const graphene_rect_t   *tex_rect,
                    float                    radius)
{
  GskVkOldBlurOp *self;

  g_assert (radius > 0);

  self = (GskVkOldBlurOp *) gsk_vk_old_shader_op_alloc (render, &GSK_VK_OLD_BLUR_OP_CLASS, clip, &image);

  graphene_rect_offset_r (rect, offset->x, offset->y, &self->rect);
  gsk_vk_old_normalize_tex_coords (&self->tex_rect, rect, tex_rect);
  self->radius = radius;
}

