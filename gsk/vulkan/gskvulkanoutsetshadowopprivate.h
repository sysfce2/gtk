#pragma once

#include "gskvulkanopprivate.h"

G_BEGIN_DECLS

void                    gsk_vk_old_outset_shadow_op                     (GskVkOldRender                *render,
                                                                         GskVkOldShaderClip             clip,
                                                                         const GskRoundedRect           *outline,
                                                                         const graphene_point_t         *offset,
                                                                         const GdkRGBA                  *color,
                                                                         const graphene_point_t         *shadow_offset,
                                                                         float                           spread,
                                                                         float                           blur_radius);


G_END_DECLS

