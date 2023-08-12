#pragma once

#include <gdk/gdk.h>
#include <graphene.h>
#include <gsk/gskroundedrect.h>

G_BEGIN_DECLS

typedef enum {
  GSK_VK_OLD_SHADER_CLIP_NONE,
  GSK_VK_OLD_SHADER_CLIP_RECT,
  GSK_VK_OLD_SHADER_CLIP_ROUNDED
} GskVkOldShaderClip;

typedef enum {
  /* The whole area is clipped, no drawing is necessary.
   * This can't be handled by return values because for return
   * values we return if clips could even be computed.
   */
  GSK_VK_OLD_CLIP_ALL_CLIPPED,
  /* No clipping is necessary, but the clip rect is set
   * to the actual bounds of the underlying framebuffer
   */
  GSK_VK_OLD_CLIP_NONE,
  /* The clip is a rectangular area */
  GSK_VK_OLD_CLIP_RECT,
  /* The clip is a rounded rectangle */
  GSK_VK_OLD_CLIP_ROUNDED
} GskVkOldClipComplexity;

typedef struct _GskVkOldClip GskVkOldClip;

struct _GskVkOldClip
{
  GskVkOldClipComplexity type;
  GskRoundedRect          rect;
};

void                    gsk_vk_old_clip_init_empty                      (GskVkOldClip          *clip,
                                                                         const graphene_rect_t  *rect);
void                    gsk_vk_old_clip_init_copy                       (GskVkOldClip          *self,
                                                                         const GskVkOldClip    *src);
void                    gsk_vk_old_clip_init_rect                       (GskVkOldClip          *clip,
                                                                         const graphene_rect_t  *rect);

gboolean                gsk_vk_old_clip_intersect_rect                  (GskVkOldClip          *dest,
                                                                         const GskVkOldClip    *src,
                                                                         const graphene_rect_t  *rect) G_GNUC_WARN_UNUSED_RESULT;
gboolean                gsk_vk_old_clip_intersect_rounded_rect          (GskVkOldClip          *dest,
                                                                         const GskVkOldClip    *src,
                                                                         const GskRoundedRect   *rounded) G_GNUC_WARN_UNUSED_RESULT;
void                    gsk_vk_old_clip_scale                           (GskVkOldClip          *dest,
                                                                         const GskVkOldClip    *src,
                                                                         float                   scale_x,
                                                                         float                   scale_y);
gboolean                gsk_vk_old_clip_transform                       (GskVkOldClip          *dest,
                                                                         const GskVkOldClip    *src,
                                                                         GskTransform           *transform,
                                                                         const graphene_rect_t  *viewport) G_GNUC_WARN_UNUSED_RESULT;

gboolean                gsk_vk_old_clip_contains_rect                   (const GskVkOldClip    *self,
                                                                         const graphene_point_t *offset,
                                                                         const graphene_rect_t  *rect) G_GNUC_WARN_UNUSED_RESULT;
gboolean                gsk_vk_old_clip_may_intersect_rect              (const GskVkOldClip    *self,
                                                                         const graphene_point_t *offset,
                                                                         const graphene_rect_t  *rect) G_GNUC_WARN_UNUSED_RESULT;
GskVkOldShaderClip     gsk_vk_old_clip_get_shader_clip                 (const GskVkOldClip    *self,
                                                                         const graphene_point_t *offset,
                                                                         const graphene_rect_t  *rect);

G_END_DECLS

