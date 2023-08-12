#pragma once

#include "gskvulkanopprivate.h"

G_BEGIN_DECLS

void                    gsk_vk_old_mask_op                              (GskVkOldRender                *render,
                                                                         GskVkOldShaderClip             clip,
                                                                         const graphene_point_t         *offset,
                                                                         GskVkOldImage                 *source,
                                                                         const graphene_rect_t          *source_rect,
                                                                         const graphene_rect_t          *source_tex_rect,
                                                                         GskVkOldImage                 *mask,
                                                                         const graphene_rect_t          *mask_rect,
                                                                         const graphene_rect_t          *mask_tex_rect,
                                                                         GskMaskMode                     mask_mode);


G_END_DECLS

