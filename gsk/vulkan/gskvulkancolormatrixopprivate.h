#pragma once

#include "gskvulkanopprivate.h"

G_BEGIN_DECLS

void                    gsk_vk_old_color_matrix_op                      (GskVkOldRender                *render,
                                                                         GskVkOldShaderClip             clip,
                                                                         GskVkOldImage                 *image,
                                                                         const graphene_rect_t          *rect,
                                                                         const graphene_point_t         *offset,
                                                                         const graphene_rect_t          *tex_rect,
                                                                         const graphene_matrix_t        *color_matrix,
                                                                         const graphene_vec4_t          *color_offset);

void                    gsk_vk_old_color_matrix_op_opacity              (GskVkOldRender                *render,
                                                                         GskVkOldShaderClip             clip,
                                                                         GskVkOldImage                 *image,
                                                                         const graphene_rect_t          *rect,
                                                                         const graphene_point_t         *offset,
                                                                         const graphene_rect_t          *tex_rect,
                                                                         float                           opacity);


G_END_DECLS

