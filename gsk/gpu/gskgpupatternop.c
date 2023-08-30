#include "config.h"

#include "gskgpupatternopprivate.h"

#include "gskgpuframeprivate.h"
#include "gskgpuprintprivate.h"
#include "gskgpushaderopprivate.h"
#include "gskrectprivate.h"

#include "gpu/shaders/gskgpupatterninstance.h"

typedef struct _GskGpuPatternOp GskGpuPatternOp;

struct _GskGpuPatternOp
{
  GskGpuShaderOp op;
};

static void
gsk_gpu_pattern_op_print (GskGpuOp    *op,
                          GskGpuFrame *frame,
                          GString     *string,
                          guint        indent)
{
  GskGpuShaderOp *shader = (GskGpuShaderOp *) op;
  GskGpuPatternInstance *instance;

  instance = (GskGpuPatternInstance *) gsk_gpu_frame_get_vertex_data (frame, shader->vertex_offset);

  gsk_gpu_print_op (string, indent, "pattern");
  gsk_gpu_print_rect (string, instance->rect);
  gsk_gpu_print_newline (string);
}

static const GskGpuShaderOpClass GSK_GPU_PATTERN_OP_CLASS = {
  {
    GSK_GPU_OP_SIZE (GskGpuPatternOp),
    GSK_GPU_STAGE_SHADER,
    gsk_gpu_shader_op_finish,
    gsk_gpu_pattern_op_print,
#ifdef GDK_RENDERING_VULKAN
    gsk_gpu_shader_op_vk_command,
#endif
    gsk_gpu_shader_op_gl_command
  },
  "gskgpupattern",
  sizeof (GskGpuPatternInstance),
#ifdef GDK_RENDERING_VULKAN
  &gsk_gpu_pattern_info,
#endif
  gsk_gpu_pattern_setup_vao
};

void
gsk_gpu_pattern_op (GskGpuFrame            *frame,
                    GskGpuShaderClip        clip,
                    const graphene_rect_t  *rect,
                    const graphene_point_t *offset,
                    guint32                 pattern_id)
{
  GskGpuPatternInstance *instance;
  graphene_rect_t tmp;

  gsk_gpu_shader_op_alloc (frame,
                           &GSK_GPU_PATTERN_OP_CLASS,
                           clip,
                           &instance);

  graphene_rect_offset_r (rect, offset->x, offset->y, &tmp);
  gsk_rect_to_float (&tmp, instance->rect);
  instance->pattern_id = pattern_id;
}
