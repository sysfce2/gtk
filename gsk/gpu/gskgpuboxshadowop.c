#include "config.h"

#include "gskgpuboxshadowopprivate.h"

#include "gskgpuframeprivate.h"
#include "gskgpuprintprivate.h"
#include "gskgpushaderopprivate.h"
#include "gskrectprivate.h"
#include "gsk/gskroundedrectprivate.h"

#include "gpu/shaders/gskgpuboxshadowinstance.h"

typedef struct _GskGpuBoxShadowOp GskGpuBoxShadowOp;

struct _GskGpuBoxShadowOp
{
  GskGpuShaderOp op;
};

static void
gsk_gpu_box_shadow_op_print (GskGpuOp    *op,
                             GskGpuFrame *frame,
                             GString     *string,
                             guint        indent)
{
  GskGpuShaderOp *shader = (GskGpuShaderOp *) op;
  GskGpuBoxshadowInstance *instance;

  instance = (GskGpuBoxshadowInstance *) gsk_gpu_frame_get_vertex_data (frame, shader->vertex_offset);

  gsk_gpu_print_op (string, indent, instance->inset ? "inset-shadow" : "outset-shadow");
  gsk_gpu_print_rounded_rect (string, instance->outline);
  gsk_gpu_print_rgba (string, instance->color);
  g_string_append_printf (string, "%g %g %g %g ",
                          instance->shadow_offset[0], instance->shadow_offset[1],
                          instance->blur_radius, instance->shadow_spread);
  gsk_gpu_print_newline (string);
}

static const GskGpuShaderOpClass GSK_GPU_BOX_SHADOW_OP_CLASS = {
  {
    GSK_GPU_OP_SIZE (GskGpuBoxShadowOp),
    GSK_GPU_STAGE_SHADER,
    gsk_gpu_shader_op_finish,
    gsk_gpu_box_shadow_op_print,
#ifdef GDK_RENDERING_VULKAN
    gsk_gpu_shader_op_vk_command,
#endif
    gsk_gpu_shader_op_gl_command
  },
  "gskgpuboxshadow",
  sizeof (GskGpuBoxshadowInstance),
#ifdef GDK_RENDERING_VULKAN
  &gsk_gpu_boxshadow_info,
#endif
  gsk_gpu_boxshadow_setup_vao
};

void
gsk_gpu_box_shadow_op (GskGpuFrame            *frame,
                       GskGpuShaderClip        clip,
                       gboolean                inset,
                       const graphene_rect_t  *bounds,
                       const GskRoundedRect   *outline,
                       const graphene_point_t *shadow_offset,
                       float                   spread,
                       float                   blur_radius,
                       const graphene_point_t *offset,
                       const GdkRGBA          *color)
{
  GskGpuBoxshadowInstance *instance;

  /* Use border shader for no blurring */
  g_return_if_fail (blur_radius > 0.0f);

  gsk_gpu_shader_op_alloc (frame,
                           &GSK_GPU_BOX_SHADOW_OP_CLASS,
                           clip,
                           NULL,
                           &instance);

  gsk_gpu_rect_to_float (bounds, offset, instance->bounds);
  gsk_rounded_rect_to_float (outline, offset, instance->outline);
  gsk_gpu_rgba_to_float (color, instance->color);
  instance->shadow_offset[0] = shadow_offset->x;
  instance->shadow_offset[1] = shadow_offset->y;
  instance->shadow_spread = spread;
  instance->blur_radius = blur_radius;
  instance->inset = inset ? 1 : 0;
}

