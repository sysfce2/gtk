#pragma once

#include "gskvulkanopprivate.h"

#include "gskvulkanclipprivate.h"

G_BEGIN_DECLS

typedef struct _GskVkOldShaderOp GskVkOldShaderOp;
typedef struct _GskVkOldShaderOpClass GskVkOldShaderOpClass;

struct _GskVkOldShaderOp
{
  GskVkOldOp parent_op;

  GskVkOldShaderClip clip;
  gsize vertex_offset;
  GskVkOldImage *images[2];
};

struct _GskVkOldShaderOpClass
{
  GskVkOldOpClass      parent_class;

  const char *          shader_name;
  gsize                 n_images;
  const VkPipelineVertexInputStateCreateInfo *vertex_input_state;
};

GskVkOldShaderOp *     gsk_vk_old_shader_op_alloc                      (GskVkOldRender        *render,
                                                                         const GskVkOldShaderOpClass *op_class,
                                                                         GskVkOldShaderClip     clip,
                                                                         GskVkOldImage        **images);

void                    gsk_vk_old_shader_op_finish                     (GskVkOldOp            *op);
gsize                   gsk_vk_old_shader_op_count_vertex_data          (GskVkOldOp            *op,
                                                                         gsize                   n_bytes);
GskVkOldOp *           gsk_vk_old_shader_op_command_n                  (GskVkOldOp            *op,
                                                                         GskVkOldRender        *render,
                                                                         VkRenderPass            render_pass,
                                                                         VkCommandBuffer         command_buffer,
                                                                         gsize                   instance_scale);
GskVkOldOp *           gsk_vk_old_shader_op_command                    (GskVkOldOp            *op,
                                                                         GskVkOldRender        *render,
                                                                         VkRenderPass            render_pass,
                                                                         VkCommandBuffer         command_buffer);

G_END_DECLS

