#pragma once

#include "gskvulkanopprivate.h"

G_BEGIN_DECLS

GskVkOldImage *        gsk_vk_old_upload_texture_op                    (GskVkOldRender                *render,
                                                                         GdkTexture                     *texture);

GskVkOldImage *        gsk_vk_old_upload_cairo_op                      (GskVkOldRender                *render,
                                                                         GskRenderNode                  *node,
                                                                         const graphene_vec2_t          *scale,
                                                                         const graphene_rect_t          *viewport);

void                    gsk_vk_old_upload_glyph_op                      (GskVkOldRender                *render,
                                                                         GskVkOldImage                 *image,
                                                                         cairo_rectangle_int_t          *area,
                                                                         PangoFont                      *font,
                                                                         PangoGlyphInfo                 *glyph_info,
                                                                         float                           scale);

G_END_DECLS

