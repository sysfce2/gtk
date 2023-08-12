#include "config.h"

#include "gskvulkancoloropprivate.h"

#include "gskvulkanprivate.h"
#include "gskvulkanshaderopprivate.h"

#include "vulkan/resources/color.vert.h"

typedef struct _GskVkOldColorOp GskVkOldColorOp;

struct _GskVkOldColorOp
{
  GskVkOldShaderOp op;

  graphene_rect_t rect;
  GdkRGBA color;
};

static void
gsk_vk_old_color_op_finish (GskVkOldOp *op)
{
}

static void
gsk_vk_old_color_op_print (GskVkOldOp *op,
                           GString     *string,
                           guint        indent)
{
  GskVkOldColorOp *self = (GskVkOldColorOp *) op;

  print_indent (string, indent);
  print_rect (string, &self->rect);
  g_string_append (string, "color ");
  print_rgba (string, &self->color);
  print_newline (string);
}

static void
gsk_vk_old_color_op_collect_vertex_data (GskVkOldOp *op,
                                         guchar      *data)
{
  GskVkOldColorOp *self = (GskVkOldColorOp *) op;
  GskVkOldColorInstance *instance = (GskVkOldColorInstance *) (data + ((GskVkOldShaderOp *) op)->vertex_offset);

  instance->rect[0] = self->rect.origin.x;
  instance->rect[1] = self->rect.origin.y;
  instance->rect[2] = self->rect.size.width;
  instance->rect[3] = self->rect.size.height;
  instance->color[0] = self->color.red;
  instance->color[1] = self->color.green;
  instance->color[2] = self->color.blue;
  instance->color[3] = self->color.alpha;
}

static void
gsk_vk_old_color_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                             GskVkOldRender *render)
{
}

static const GskVkOldShaderOpClass GSK_VK_OLD_COLOR_OP_CLASS = {
  {
    GSK_VK_OLD_OP_SIZE (GskVkOldColorOp),
    GSK_VK_OLD_STAGE_SHADER,
    gsk_vk_old_color_op_finish,
    gsk_vk_old_color_op_print,
    gsk_vk_old_shader_op_count_vertex_data,
    gsk_vk_old_color_op_collect_vertex_data,
    gsk_vk_old_color_op_reserve_descriptor_sets,
    gsk_vk_old_shader_op_command
  },
  "color",
  0,
  &gsk_vk_old_color_info,
};

void
gsk_vk_old_color_op (GskVkOldRender        *render,
                     GskVkOldShaderClip     clip,
                     const graphene_rect_t  *rect,
                     const graphene_point_t *offset,
                     const GdkRGBA          *color)
{
  GskVkOldColorOp *self;

  self = (GskVkOldColorOp *) gsk_vk_old_shader_op_alloc (render, &GSK_VK_OLD_COLOR_OP_CLASS, clip, NULL);

  graphene_rect_offset_r (rect, offset->x, offset->y, &self->rect);
  self->color = *color;
}
