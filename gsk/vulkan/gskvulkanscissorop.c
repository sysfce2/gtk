#include "config.h"

#include "gskvulkanscissoropprivate.h"

#include "gskvulkanprivate.h"

typedef struct _GskVkOldScissorOp GskVkOldScissorOp;

struct _GskVkOldScissorOp
{
  GskVkOldOp op;

  cairo_rectangle_int_t rect;
};

static void
gsk_vk_old_scissor_op_finish (GskVkOldOp *op)
{
}

static void
gsk_vk_old_scissor_op_print (GskVkOldOp *op,
                             GString     *string,
                             guint        indent)
{
  GskVkOldScissorOp *self = (GskVkOldScissorOp *) op;

  print_indent (string, indent);
  print_int_rect (string, &self->rect);
  g_string_append_printf (string, "scissor ");
  print_newline (string);
}

static gsize
gsk_vk_old_scissor_op_count_vertex_data (GskVkOldOp *op,
                                         gsize        n_bytes)
{
  return n_bytes;
}

static void
gsk_vk_old_scissor_op_collect_vertex_data (GskVkOldOp *op,
                                           guchar      *data)
{
}

static void
gsk_vk_old_scissor_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                               GskVkOldRender *render)
{
}

static GskVkOldOp *
gsk_vk_old_scissor_op_command (GskVkOldOp      *op,
                               GskVkOldRender  *render,
                               VkRenderPass      render_pass,
                               VkCommandBuffer   command_buffer)
{
  GskVkOldScissorOp *self = (GskVkOldScissorOp *) op;

  vkCmdSetScissor (command_buffer,
                   0,
                   1,
                   &(VkRect2D) {
                     { self->rect.x, self->rect.y },
                     { self->rect.width, self->rect.height },
                   });

  return op->next;
}

static const GskVkOldOpClass GSK_VK_OLD_SCISSOR_OP_CLASS = {
  GSK_VK_OLD_OP_SIZE (GskVkOldScissorOp),
  GSK_VK_OLD_STAGE_COMMAND,
  gsk_vk_old_scissor_op_finish,
  gsk_vk_old_scissor_op_print,
  gsk_vk_old_scissor_op_count_vertex_data,
  gsk_vk_old_scissor_op_collect_vertex_data,
  gsk_vk_old_scissor_op_reserve_descriptor_sets,
  gsk_vk_old_scissor_op_command
};

void
gsk_vk_old_scissor_op (GskVkOldRender             *render,
                       const cairo_rectangle_int_t *rect)
{
  GskVkOldScissorOp *self;

  self = (GskVkOldScissorOp *) gsk_vk_old_op_alloc (render, &GSK_VK_OLD_SCISSOR_OP_CLASS);

  self->rect = *rect;
}
