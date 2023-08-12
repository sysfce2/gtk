#pragma once

#include "gskvulkanrenderer.h"
#include "gskvulkanglyphcacheprivate.h"
#include "gskvulkanimageprivate.h"

G_BEGIN_DECLS

GskVkOldImage *        gsk_vk_old_renderer_get_texture_image           (GskVkOldRenderer      *self,
                                                                         GdkTexture             *texture);
void                    gsk_vk_old_renderer_add_texture_image           (GskVkOldRenderer      *self,
                                                                         GdkTexture             *texture,
                                                                         GskVkOldImage         *image);

GskVkOldGlyphCache *   gsk_vk_old_renderer_get_glyph_cache             (GskVkOldRenderer      *self);


G_END_DECLS

