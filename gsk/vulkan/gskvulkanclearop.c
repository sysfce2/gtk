#include "config.h"

#include "gskvulkanclearopprivate.h"

#include "gskvulkanprivate.h"

typedef struct _GskVkOldClearOp GskVkOldClearOp;

struct _GskVkOldClearOp
{
  GskVkOldOp op;

  cairo_rectangle_int_t rect;
  GdkRGBA color;
};

static void
gsk_vk_old_clear_op_finish (GskVkOldOp *op)
{
}

static void
gsk_vk_old_clear_op_print (GskVkOldOp *op,
                           GString     *string,
                           guint        indent)
{
  GskVkOldClearOp *self = (GskVkOldClearOp *) op;

  print_indent (string, indent);
  print_int_rect (string, &self->rect);
  g_string_append_printf (string, "clear ");
  print_rgba (string, &self->color);
  print_newline (string);
}

static gsize
gsk_vk_old_clear_op_count_vertex_data (GskVkOldOp *op,
                                       gsize        n_bytes)
{
  return n_bytes;
}

static void
gsk_vk_old_clear_op_collect_vertex_data (GskVkOldOp *op,
                                         guchar      *data)
{
}

static void
gsk_vk_old_clear_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                             GskVkOldRender *render)
{
}

static void
gsk_vk_old_init_clear_value (VkClearValue  *value,
                             const GdkRGBA *rgba)
{
  gsk_vk_old_rgba_to_float (rgba, value->color.float32);
}

static GskVkOldOp *
gsk_vk_old_clear_op_command (GskVkOldOp      *op,
                             GskVkOldRender  *render,
                             VkRenderPass      render_pass,
                             VkCommandBuffer   command_buffer)
{
  GskVkOldClearOp *self = (GskVkOldClearOp *) op;
  VkClearValue clear_value;

  gsk_vk_old_init_clear_value (&clear_value, &self->color);

  vkCmdClearAttachments (command_buffer,
                         1,
                         &(VkClearAttachment) {
                           VK_IMAGE_ASPECT_COLOR_BIT,
                           0,
                           clear_value,
                         },
                         1,
                         &(VkClearRect) {
                           {
                             { self->rect.x, self->rect.y },
                             { self->rect.width, self->rect.height },
                           },
                           0,
                           1
                         });

  return op->next;
}

static const GskVkOldOpClass GSK_VK_OLD_SCISSOR_OP_CLASS = {
  GSK_VK_OLD_OP_SIZE (GskVkOldClearOp),
  GSK_VK_OLD_STAGE_COMMAND,
  gsk_vk_old_clear_op_finish,
  gsk_vk_old_clear_op_print,
  gsk_vk_old_clear_op_count_vertex_data,
  gsk_vk_old_clear_op_collect_vertex_data,
  gsk_vk_old_clear_op_reserve_descriptor_sets,
  gsk_vk_old_clear_op_command
};

void
gsk_vk_old_clear_op (GskVkOldRender             *render,
                     const cairo_rectangle_int_t *rect,
                     const GdkRGBA               *color)
{
  GskVkOldClearOp *self;

  self = (GskVkOldClearOp *) gsk_vk_old_op_alloc (render, &GSK_VK_OLD_SCISSOR_OP_CLASS);

  self->rect = *rect;
  self->color = *color;
}
