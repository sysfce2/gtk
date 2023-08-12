#include "config.h"

#include "gskvulkanopprivate.h"

GskVkOldOp *
gsk_vk_old_op_alloc (GskVkOldRender        *render,
                     const GskVkOldOpClass *op_class)
{
  GskVkOldOp *op;

  op = gsk_vk_old_render_alloc_op (render, op_class->size);
  op->op_class = op_class;

  return op;
}

void
gsk_vk_old_op_finish (GskVkOldOp *op)
{
  op->op_class->finish (op);
}

void
gsk_vk_old_op_print (GskVkOldOp *op,
                     GString     *string,
                     guint        indent)
{
  op->op_class->print (op, string, indent);
}

gsize
gsk_vk_old_op_count_vertex_data (GskVkOldOp *op,
                                 gsize        n_bytes)
{
  return op->op_class->count_vertex_data (op, n_bytes);
}

void
gsk_vk_old_op_collect_vertex_data (GskVkOldOp *op,
                                   guchar      *data)
{
  op->op_class->collect_vertex_data (op, data);
}

void
gsk_vk_old_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                       GskVkOldRender *render)
{
  op->op_class->reserve_descriptor_sets (op, render);
}

GskVkOldOp *
gsk_vk_old_op_command (GskVkOldOp      *op,
                       GskVkOldRender  *render,
                       VkRenderPass      render_pass,
                       VkCommandBuffer   command_buffer)
{
  return op->op_class->command (op, render, render_pass, command_buffer);
}

