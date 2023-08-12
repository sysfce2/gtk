#pragma once

#include <pango/pango.h>
#include "gskvulkanimageprivate.h"
#include "gskvulkanprivate.h"

G_BEGIN_DECLS

#define GSK_TYPE_VK_OLD_GLYPH_CACHE (gsk_vk_old_glyph_cache_get_type ())

G_DECLARE_FINAL_TYPE(GskVkOldGlyphCache, gsk_vk_old_glyph_cache, GSK, VK_OLD_GLYPH_CACHE, GObject)

typedef struct
{
  guint texture_index;

  float tx;
  float ty;
  float tw;
  float th;

  int draw_x;
  int draw_y;
  int draw_width;
  int draw_height;

  GskVkOldImage *atlas_image;
  int atlas_x;
  int atlas_y;

  guint64 timestamp;
} GskVkOldCachedGlyph;

GskVkOldGlyphCache  *gsk_vk_old_glyph_cache_new            (GdkVulkanContext    *vulkan);

GskVkOldCachedGlyph *gsk_vk_old_glyph_cache_lookup         (GskVkOldGlyphCache *cache,
                                                             GskVkOldRender     *render,
                                                             PangoFont           *font,
                                                             PangoGlyph           glyph,
                                                             int                  x,
                                                             int                  y,

                                                             float                scale);

void                  gsk_vk_old_glyph_cache_begin_frame    (GskVkOldGlyphCache *cache);

