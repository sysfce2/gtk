#pragma once

#include "gskvulkanopprivate.h"

G_BEGIN_DECLS

void                    gsk_vk_old_color_op                             (GskVkOldRender                *render,
                                                                         GskVkOldShaderClip             clip,
                                                                         const graphene_rect_t          *rect,
                                                                         const graphene_point_t         *offset,
                                                                         const GdkRGBA                  *color);


G_END_DECLS

