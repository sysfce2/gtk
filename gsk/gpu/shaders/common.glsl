#ifndef _COMMON_
#define _COMMON_

void            main_clip_none                  (void);
void            main_clip_rect                  (void);
void            main_clip_rounded               (void);

#include "enums.glsl"

#ifdef VULKAN
#include "common-vulkan.glsl"
#else
#include "common-gl.glsl"
#endif

#include "color.glsl"
#include "rect.glsl"
#include "roundedrect.glsl"

#define PI 3.1415926535897932384626433832795
#define SQRT1_2 1.4142135623730951

Rect
rect_clip (Rect r)
{
  if (GSK_SHADER_CLIP == GSK_GPU_SHADER_CLIP_NONE)
    return r;
  else
    return rect_intersect (r, rect_from_gsk (GSK_GLOBAL_CLIP_RECT));
}

#ifdef GSK_VERTEX_SHADER

const vec2 offsets[6] = vec2[6](vec2(0.0, 0.0),
                                vec2(1.0, 0.0),
                                vec2(0.0, 1.0),
                                vec2(0.0, 1.0),
                                vec2(1.0, 0.0),
                                vec2(1.0, 1.0));

void
gsk_set_position (vec2 pos)
{
  gl_Position = GSK_GLOBAL_MVP * vec4 (pos, 0.0, 1.0);
}

vec2
rect_get_position (Rect rect)
{
  Rect r = rect_round_larger (rect_clip (rect));

  vec2 pos = mix (r.bounds.xy, r.bounds.zw, offsets[GSK_VERTEX_INDEX]);

  return pos;
}

vec2
border_get_position (RoundedRect outside,
                     RoundedRect inside)
{
  uint slice_index = uint (GSK_VERTEX_INDEX) / 6u;
  uint vert_index = uint (GSK_VERTEX_INDEX) % 6u;

  Rect rect = rounded_rect_intersection_slice (outside, inside, slice_index);

  switch (slice_index)
    {
    case SLICE_TOP_LEFT:
      rect = rect_round_larger (rect);
      rect.bounds = rect.bounds.xwzy;
      break;
    case SLICE_TOP:
      rect = rect_round_smaller_larger (rect);
      break;
    case SLICE_TOP_RIGHT:
      rect = rect_round_larger (rect);
      break;
    case SLICE_RIGHT:
      rect = rect_round_larger_smaller (rect);
      break;
    case SLICE_BOTTOM_RIGHT:
      rect = rect_round_larger (rect);
      rect.bounds = rect.bounds.zyxw;
      break;
    case SLICE_BOTTOM:
      rect = rect_round_smaller_larger (rect);
      break;
    case SLICE_BOTTOM_LEFT:
      rect = rect_round_larger (rect);
      rect.bounds = rect.bounds.zwxy;
      break;
    case SLICE_LEFT:
      rect = rect_round_larger_smaller (rect);
      break;
    }

  vec2 pos = mix (rect.bounds.xy, rect.bounds.zw, offsets[vert_index]);

  return pos;
}

vec2
scale_tex_coord (vec2 in_pos,
                 Rect in_rect,
                 vec4 tex_rect)
{
  return tex_rect.xy + (in_pos - in_rect.bounds.xy) / rect_size (in_rect) * tex_rect.zw;
}

void            run                             (out vec2 pos);

void
main (void)
{
  vec2 pos;

  run (pos);

  gsk_set_position (pos);
}

#endif /* GSK_VERTEX_SHADER */


#ifdef GSK_FRAGMENT_SHADER

void            run                             (out vec4 color,
                                                 out vec2 pos);

void
main_clip_none (void)
{
  vec4 color;
  vec2 pos;

  run (color, pos);

  gsk_set_output_color (color);
}

void
main_clip_rect (void)
{
  vec4 color;
  vec2 pos;

  run (color, pos);

  Rect clip = rect_from_gsk (GSK_GLOBAL_CLIP_RECT);

  float coverage = rect_coverage (clip, pos);
  color *= coverage;

  gsk_set_output_color (color);
}

void
main_clip_rounded (void)
{
  vec4 color;
  vec2 pos;

  run (color, pos);

  RoundedRect clip = rounded_rect_from_gsk (GSK_GLOBAL_CLIP);

  float coverage = rounded_rect_coverage (clip, pos);
  color *= coverage;

  gsk_set_output_color (color);
}

void
main (void)
{
  if (GSK_SHADER_CLIP == GSK_GPU_SHADER_CLIP_NONE)
    main_clip_none ();
  else if (GSK_SHADER_CLIP == GSK_GPU_SHADER_CLIP_RECT)
    main_clip_rect ();
  else if (GSK_SHADER_CLIP == GSK_GPU_SHADER_CLIP_ROUNDED)
    main_clip_rounded ();
}

#endif /* GSK_FRAGMENT_SHADER */

#endif

