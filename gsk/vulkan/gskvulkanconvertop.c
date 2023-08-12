#include "config.h"

#include "gskvulkanconvertopprivate.h"

#include "gskvulkanprivate.h"
#include "gskvulkanshaderopprivate.h"

#include "vulkan/resources/convert.vert.h"

typedef struct _GskVkOldConvertOp GskVkOldConvertOp;

struct _GskVkOldConvertOp
{
  GskVkOldShaderOp op;

  graphene_rect_t rect;
  graphene_rect_t tex_rect;

  guint32 image_descriptor;
};

static void
gsk_vk_old_convert_op_print (GskVkOldOp *op,
                             GString     *string,
                             guint        indent)
{
  GskVkOldConvertOp *self = (GskVkOldConvertOp *) op;
  GskVkOldShaderOp *shader = (GskVkOldShaderOp *) op;

  print_indent (string, indent);
  print_rect (string, &self->rect);
  g_string_append (string, "convert ");
  print_image (string, shader->images[0]);
  print_newline (string);
}

static void
gsk_vk_old_convert_op_collect_vertex_data (GskVkOldOp *op,
                                           guchar      *data)
{
  GskVkOldConvertOp *self = (GskVkOldConvertOp *) op;
  GskVkOldConvertInstance *instance = (GskVkOldConvertInstance *) (data + ((GskVkOldShaderOp *) op)->vertex_offset);
  GskVkOldShaderOp *shader = (GskVkOldShaderOp *) op;

  instance->rect[0] = self->rect.origin.x;
  instance->rect[1] = self->rect.origin.y;
  instance->rect[2] = self->rect.size.width;
  instance->rect[3] = self->rect.size.height;
  instance->tex_rect[0] = self->tex_rect.origin.x;
  instance->tex_rect[1] = self->tex_rect.origin.y;
  instance->tex_rect[2] = self->tex_rect.size.width;
  instance->tex_rect[3] = self->tex_rect.size.height;
  instance->tex_id = self->image_descriptor;
  instance->postprocess = gsk_vk_old_image_get_postprocess (shader->images[0]);
}

static void
gsk_vk_old_convert_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                               GskVkOldRender *render)
{
  GskVkOldConvertOp *self = (GskVkOldConvertOp *) op;
  GskVkOldShaderOp *shader = (GskVkOldShaderOp *) op;

  self->image_descriptor = gsk_vk_old_render_get_image_descriptor (render, shader->images[0], GSK_VK_OLD_SAMPLER_NEAREST);
}

static const GskVkOldShaderOpClass GSK_VK_OLD_CONVERT_OP_CLASS = {
  {
    GSK_VK_OLD_OP_SIZE (GskVkOldConvertOp),
    GSK_VK_OLD_STAGE_SHADER,
    gsk_vk_old_shader_op_finish,
    gsk_vk_old_convert_op_print,
    gsk_vk_old_shader_op_count_vertex_data,
    gsk_vk_old_convert_op_collect_vertex_data,
    gsk_vk_old_convert_op_reserve_descriptor_sets,
    gsk_vk_old_shader_op_command
  },
  "convert",
  1,
  &gsk_vk_old_convert_info,
};

void
gsk_vk_old_convert_op (GskVkOldRender        *render,
                       GskVkOldShaderClip     clip,
                       GskVkOldImage         *image,
                       const graphene_rect_t  *rect,
                       const graphene_point_t *offset,
                       const graphene_rect_t  *tex_rect)
{
  GskVkOldConvertOp *self;

  self = (GskVkOldConvertOp *) gsk_vk_old_shader_op_alloc (render,
                                                            &GSK_VK_OLD_CONVERT_OP_CLASS,
                                                            clip,
                                                            &image);

  graphene_rect_offset_r (rect, offset->x, offset->y, &self->rect);
  gsk_vk_old_normalize_tex_coords (&self->tex_rect, rect, tex_rect);
}
