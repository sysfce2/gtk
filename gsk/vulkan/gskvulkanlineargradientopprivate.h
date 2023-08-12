#pragma once

#include "gskvulkanopprivate.h"

G_BEGIN_DECLS

void                    gsk_vk_old_linear_gradient_op                   (GskVkOldRender                *render,
                                                                         GskVkOldShaderClip             clip,
                                                                         const graphene_rect_t          *rect,
                                                                         const graphene_point_t         *offset,
                                                                         const graphene_point_t         *start,
                                                                         const graphene_point_t         *end,
                                                                         gboolean                        repeating,
                                                                         const GskColorStop             *stops,
                                                                         gsize                           n_stops);


G_END_DECLS

