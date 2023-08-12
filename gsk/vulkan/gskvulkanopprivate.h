#pragma once

#include <gdk/gdk.h>

#include "gskvulkanrenderpassprivate.h"

G_BEGIN_DECLS

typedef enum
{
  GSK_VK_OLD_STAGE_UPLOAD,
  GSK_VK_OLD_STAGE_COMMAND,
  GSK_VK_OLD_STAGE_SHADER,
  /* magic ones */
  GSK_VK_OLD_STAGE_BEGIN_PASS,
  GSK_VK_OLD_STAGE_END_PASS
} GskVkOldStage;

struct _GskVkOldOp
{
  const GskVkOldOpClass *op_class;

  GskVkOldOp *next;
};

struct _GskVkOldOpClass
{
  gsize                 size;
  GskVkOldStage        stage;

  void                  (* finish)                                      (GskVkOldOp            *op);

  void                  (* print)                                       (GskVkOldOp            *op,
                                                                         GString                *string,
                                                                         guint                   indent);

  gsize                 (* count_vertex_data)                           (GskVkOldOp            *op,
                                                                         gsize                   n_bytes);
  void                  (* collect_vertex_data)                         (GskVkOldOp            *op,
                                                                         guchar                 *data);
  void                  (* reserve_descriptor_sets)                     (GskVkOldOp            *op,
                                                                         GskVkOldRender        *render);
  GskVkOldOp *         (* command)                                     (GskVkOldOp            *op,
                                                                         GskVkOldRender        *render,
                                                                         VkRenderPass            render_pass,
                                                                         VkCommandBuffer         command_buffer);
};

/* ensures alignment of ops to multipes of 16 bytes - and that makes graphene happy */
#define GSK_VK_OLD_OP_SIZE(struct_name) ((sizeof(struct_name) + 15) & ~15)

GskVkOldOp *           gsk_vk_old_op_alloc                             (GskVkOldRender        *render,
                                                                         const GskVkOldOpClass *op_class);
void                    gsk_vk_old_op_finish                            (GskVkOldOp            *op);

void                    gsk_vk_old_op_print                             (GskVkOldOp            *op,
                                                                         GString                *string,
                                                                         guint                   indent);

gsize                   gsk_vk_old_op_count_vertex_data                 (GskVkOldOp            *op,
                                                                         gsize                   n_bytes);
void                    gsk_vk_old_op_collect_vertex_data               (GskVkOldOp            *op,
                                                                         guchar                 *data);
void                    gsk_vk_old_op_reserve_descriptor_sets           (GskVkOldOp            *op,
                                                                         GskVkOldRender        *render);
GskVkOldOp *           gsk_vk_old_op_command                           (GskVkOldOp            *op,
                                                                         GskVkOldRender        *render,
                                                                         VkRenderPass            render_pass,
                                                                         VkCommandBuffer         command_buffer);

G_END_DECLS

