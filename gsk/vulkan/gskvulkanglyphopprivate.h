#pragma once

#include "gskvulkanopprivate.h"

G_BEGIN_DECLS

void                    gsk_vk_old_glyph_op                             (GskVkOldRender                *render,
                                                                         GskVkOldShaderClip             clip,
                                                                         GskVkOldImage                 *image,
                                                                         const graphene_rect_t          *rect,
                                                                         const graphene_point_t         *offset,
                                                                         const graphene_rect_t          *tex_rect,
                                                                         const GdkRGBA                  *color);


G_END_DECLS

