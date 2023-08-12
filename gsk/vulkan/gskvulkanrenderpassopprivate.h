#pragma once

#include "gskvulkanopprivate.h"

G_BEGIN_DECLS


void                    gsk_vk_old_render_pass_begin_op                 (GskVkOldRender                *render,
                                                                         GskVkOldImage                 *image,
                                                                         const cairo_rectangle_int_t    *area,
                                                                         VkImageLayout                   initial_layout,
                                                                         VkImageLayout                   final_layout);
void                    gsk_vk_old_render_pass_end_op                   (GskVkOldRender                *render,
                                                                         GskVkOldImage                 *image,
                                                                         VkImageLayout                   final_layout);

GskVkOldImage *        gsk_vk_old_render_pass_op_offscreen             (GskVkOldRender                *render,
                                                                         const graphene_vec2_t          *scale,
                                                                         const graphene_rect_t          *viewport,
                                                                         GskRenderNode                  *node);

G_END_DECLS

