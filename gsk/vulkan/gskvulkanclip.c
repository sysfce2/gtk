#include "config.h"

#include "gskvulkanclipprivate.h"

#include "gskrectprivate.h"
#include "gskroundedrectprivate.h"
#include "gsktransform.h"

void
gsk_vk_old_clip_init_empty (GskVkOldClip         *clip,
                            const graphene_rect_t *rect)
{
  clip->type = GSK_VK_OLD_CLIP_NONE;
  gsk_rounded_rect_init_from_rect (&clip->rect, rect, 0);
}

void
gsk_vk_old_clip_init_rect (GskVkOldClip         *clip,
                           const graphene_rect_t *rect)
{
  clip->type = GSK_VK_OLD_CLIP_RECT;
  gsk_rounded_rect_init_from_rect (&clip->rect, rect, 0);
}

void
gsk_vk_old_clip_init_copy (GskVkOldClip *self,
                           const GskVkOldClip *src)
{
  self->type = src->type;
  gsk_rounded_rect_init_copy (&self->rect, &src->rect);
}

static gboolean
gsk_vk_old_clip_init_after_intersection (GskVkOldClip              *self,
                                         GskRoundedRectIntersection  res)
{
  if (res == GSK_INTERSECTION_NOT_REPRESENTABLE)
    return FALSE;

  if (res == GSK_INTERSECTION_EMPTY)
    self->type = GSK_VK_OLD_CLIP_ALL_CLIPPED;
  else if (gsk_rounded_rect_is_rectilinear (&self->rect))
    self->type = GSK_VK_OLD_CLIP_RECT;
  else
    self->type = GSK_VK_OLD_CLIP_ROUNDED;

  return TRUE;
}

gboolean
gsk_vk_old_clip_intersect_rect (GskVkOldClip         *dest,
                                const GskVkOldClip   *src,
                                const graphene_rect_t *rect)
{
  GskRoundedRectIntersection res;

  if (graphene_rect_contains_rect (rect, &src->rect.bounds))
    {
      gsk_vk_old_clip_init_copy (dest, src);
      return TRUE;
    }
  if (!gsk_rect_intersects (rect, &src->rect.bounds))
    {
      dest->type = GSK_VK_OLD_CLIP_ALL_CLIPPED;
      return TRUE;
    }

  switch (src->type)
    {
    case GSK_VK_OLD_CLIP_ALL_CLIPPED:
      dest->type = GSK_VK_OLD_CLIP_ALL_CLIPPED;
      break;

    case GSK_VK_OLD_CLIP_NONE:
      gsk_vk_old_clip_init_copy (dest, src);
      if (graphene_rect_intersection (&dest->rect.bounds, rect, &dest->rect.bounds))
        dest->type = GSK_VK_OLD_CLIP_RECT;
      else
        dest->type = GSK_VK_OLD_CLIP_ALL_CLIPPED;
      break;

    case GSK_VK_OLD_CLIP_RECT:
      gsk_vk_old_clip_init_copy (dest, src);
      if (!graphene_rect_intersection (&dest->rect.bounds, rect, &dest->rect.bounds))
        dest->type = GSK_VK_OLD_CLIP_ALL_CLIPPED;
      break;

    case GSK_VK_OLD_CLIP_ROUNDED:
      res = gsk_rounded_rect_intersect_with_rect (&src->rect, rect, &dest->rect);
      if (!gsk_vk_old_clip_init_after_intersection (dest, res))
        return FALSE;
      break;

    default:
      g_assert_not_reached ();
      return FALSE;
    }

  return TRUE;
}

gboolean
gsk_vk_old_clip_intersect_rounded_rect (GskVkOldClip        *dest,
                                        const GskVkOldClip  *src,
                                        const GskRoundedRect *rounded)
{
  GskRoundedRectIntersection res;

  if (gsk_rounded_rect_contains_rect (rounded, &src->rect.bounds))
    {
      gsk_vk_old_clip_init_copy (dest, src);
      return TRUE;
    }
  if (!gsk_rect_intersects (&rounded->bounds, &src->rect.bounds))
    {
      dest->type = GSK_VK_OLD_CLIP_ALL_CLIPPED;
      return TRUE;
    }

  switch (src->type)
    {
    case GSK_VK_OLD_CLIP_ALL_CLIPPED:
      dest->type = GSK_VK_OLD_CLIP_ALL_CLIPPED;
      break;

    case GSK_VK_OLD_CLIP_NONE:
      dest->type = GSK_VK_OLD_CLIP_ROUNDED;
      gsk_rounded_rect_init_copy (&dest->rect, rounded);
      break;

    case GSK_VK_OLD_CLIP_RECT:
      res = gsk_rounded_rect_intersect_with_rect (rounded, &src->rect.bounds, &dest->rect);
      if (!gsk_vk_old_clip_init_after_intersection (dest, res))
        return FALSE;
      break;

    case GSK_VK_OLD_CLIP_ROUNDED:
      res = gsk_rounded_rect_intersection (&src->rect, rounded, &dest->rect);
      if (!gsk_vk_old_clip_init_after_intersection (dest, res))
        return FALSE;
      break;

    default:
      g_assert_not_reached ();
      return FALSE;
    }

  return TRUE;
}

void
gsk_vk_old_clip_scale (GskVkOldClip       *dest,
                       const GskVkOldClip *src,
                       float                scale_x,
                       float                scale_y)
{
  dest->type = src->type;
  gsk_rounded_rect_scale_affine (&dest->rect, 
                                 &src->rect,
                                 1.0f / scale_x, 1.0f / scale_y,
                                 0, 0);
}

gboolean
gsk_vk_old_clip_transform (GskVkOldClip         *dest,
                           const GskVkOldClip   *src,
                           GskTransform          *transform,
                           const graphene_rect_t *viewport)
{
  switch (src->type)
    {
    default:
      g_assert_not_reached();
      return FALSE;

    case GSK_VK_OLD_CLIP_ALL_CLIPPED:
      gsk_vk_old_clip_init_copy (dest, src);
      return TRUE;

    case GSK_VK_OLD_CLIP_NONE:
      gsk_vk_old_clip_init_empty (dest, viewport);
      return TRUE;

    case GSK_VK_OLD_CLIP_RECT:
    case GSK_VK_OLD_CLIP_ROUNDED:
      switch (gsk_transform_get_category (transform))
        {
        case GSK_TRANSFORM_CATEGORY_IDENTITY:
          gsk_vk_old_clip_init_copy (dest, src);
          return TRUE;

        case GSK_TRANSFORM_CATEGORY_2D_TRANSLATE:
          {
            float dx, dy;

            gsk_transform_to_translate (transform, &dx, &dy);
            gsk_vk_old_clip_init_copy (dest, src);
            dest->rect.bounds.origin.x -= dx;
            dest->rect.bounds.origin.y -= dy;
          }
          return TRUE;

        case GSK_TRANSFORM_CATEGORY_2D_AFFINE:
          {
            float dx, dy, scale_x, scale_y;

            gsk_transform_to_affine (transform, &scale_x, &scale_y, &dx, &dy);
            scale_x = 1. / scale_x;
            scale_y = 1. / scale_y;
            gsk_vk_old_clip_init_copy (dest, src);
            dest->rect.bounds.origin.x = (dest->rect.bounds.origin.x - dx) * scale_x;
            dest->rect.bounds.origin.y = (dest->rect.bounds.origin.y - dy) * scale_y;
            dest->rect.bounds.size.width *= scale_x;
            dest->rect.bounds.size.height *= scale_y;
            if (src->type != GSK_VK_OLD_CLIP_RECT)
              {
                dest->rect.corner[0].width *= scale_x;
                dest->rect.corner[0].height *= scale_y;
                dest->rect.corner[1].width *= scale_x;
                dest->rect.corner[1].height *= scale_y;
                dest->rect.corner[2].width *= scale_x;
                dest->rect.corner[2].height *= scale_y;
                dest->rect.corner[3].width *= scale_x;
                dest->rect.corner[3].height *= scale_y;
              }
          }
          return TRUE;

        case GSK_TRANSFORM_CATEGORY_UNKNOWN:
        case GSK_TRANSFORM_CATEGORY_ANY:
        case GSK_TRANSFORM_CATEGORY_3D:
        case GSK_TRANSFORM_CATEGORY_2D:
        default:
          return FALSE;
        }
    }
}

gboolean
gsk_vk_old_clip_may_intersect_rect (const GskVkOldClip    *self,
                                    const graphene_point_t *offset,
                                    const graphene_rect_t  *rect)
{
  graphene_rect_t r = *rect;
  r.origin.x += offset->x;
  r.origin.y += offset->y;

  switch (self->type)
    {
    default:
      g_assert_not_reached();
    case GSK_VK_OLD_CLIP_ALL_CLIPPED:
      return FALSE;

    case GSK_VK_OLD_CLIP_NONE:
    case GSK_VK_OLD_CLIP_RECT:
    case GSK_VK_OLD_CLIP_ROUNDED:
      return gsk_rect_intersects (&self->rect.bounds, &r);
    }
}

gboolean
gsk_vk_old_clip_contains_rect (const GskVkOldClip    *self,
                               const graphene_point_t *offset,
                               const graphene_rect_t  *rect)
{
  graphene_rect_t r = *rect;
  r.origin.x += offset->x;
  r.origin.y += offset->y;

  switch (self->type)
    {
    default:
      g_assert_not_reached();
    case GSK_VK_OLD_CLIP_ALL_CLIPPED:
      return FALSE;

    case GSK_VK_OLD_CLIP_NONE:
      return TRUE;

    case GSK_VK_OLD_CLIP_RECT:
      return graphene_rect_contains_rect (&self->rect.bounds, &r);

    case GSK_VK_OLD_CLIP_ROUNDED:
      return gsk_rounded_rect_contains_rect (&self->rect, &r);
    }
}

GskVkOldShaderClip
gsk_vk_old_clip_get_shader_clip (const GskVkOldClip    *self,
                                 const graphene_point_t *offset,
                                 const graphene_rect_t  *rect)
{
  if (gsk_vk_old_clip_contains_rect (self, offset, rect))
    return GSK_VK_OLD_SHADER_CLIP_NONE;
  else if (self->type == GSK_VK_OLD_CLIP_RECT)
    return GSK_VK_OLD_SHADER_CLIP_RECT;
  else
    return GSK_VK_OLD_SHADER_CLIP_ROUNDED;
}

