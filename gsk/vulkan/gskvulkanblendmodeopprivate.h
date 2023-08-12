#pragma once

#include "gskvulkanopprivate.h"

G_BEGIN_DECLS

void                    gsk_vk_old_blend_mode_op                        (GskVkOldRender                *render,
                                                                         GskVkOldShaderClip             clip,
                                                                         const graphene_rect_t          *bounds,
                                                                         const graphene_point_t         *offset,
                                                                         GskBlendMode                    blend_mode,
                                                                         GskVkOldImage                 *top_image,
                                                                         const graphene_rect_t          *top_rect,
                                                                         const graphene_rect_t          *top_tex_rect,
                                                                         GskVkOldImage                 *bottom_image,
                                                                         const graphene_rect_t          *bottom_rect,
                                                                         const graphene_rect_t          *bottom_tex_rect);


G_END_DECLS

