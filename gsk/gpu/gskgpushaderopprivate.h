#pragma once

#include "gskgpuopprivate.h"

#include "gskgputypesprivate.h"

G_BEGIN_DECLS

struct _GskGpuShaderOp
{
  GskGpuOp parent_op;

  GskGpuDescriptors *desc;
  GskGpuShaderClip clip;
  gsize vertex_offset;
};

struct _GskGpuShaderOpClass
{
  GskGpuOpClass         parent_class;

  const char *          shader_name;
  gsize                 vertex_size;
#ifdef GDK_RENDERING_VULKAN
  const VkPipelineVertexInputStateCreateInfo *vertex_input_state;
#endif
  void                  (* setup_vao)                                   (gsize                   offset);
};

GskGpuShaderOp *        gsk_gpu_shader_op_alloc                         (GskGpuFrame            *frame,
                                                                         const GskGpuShaderOpClass *op_class,
                                                                         GskGpuShaderClip        clip,
                                                                         GskGpuDescriptors      *desc,
                                                                         gpointer                out_vertex_data);

void                    gsk_gpu_shader_op_finish                        (GskGpuOp               *op);

#ifdef GDK_RENDERING_VULKAN
GskGpuOp *              gsk_gpu_shader_op_vk_command_n                  (GskGpuOp               *op,
                                                                         GskGpuFrame            *frame,
                                                                         VkRenderPass            render_pass,
                                                                         VkFormat                format,
                                                                         VkCommandBuffer         command_buffer,
                                                                         gsize                   instance_scale);
GskGpuOp *              gsk_gpu_shader_op_vk_command                    (GskGpuOp               *op,
                                                                         GskGpuFrame            *frame,
                                                                         VkRenderPass            render_pass,
                                                                         VkFormat                format,
                                                                         VkCommandBuffer         command_buffer);
#endif
GskGpuOp *              gsk_gpu_shader_op_gl_command_n                  (GskGpuOp               *op,
                                                                         GskGpuFrame            *frame,
                                                                         gsize                   flip_y,
                                                                         gsize                   instance_scale);
GskGpuOp *              gsk_gpu_shader_op_gl_command                    (GskGpuOp               *op,
                                                                         GskGpuFrame            *frame,
                                                                         gsize                   flip_y);

static inline void
gsk_gpu_rgba_to_float (const GdkRGBA *rgba,
                       float          values[4])
{
  values[0] = rgba->red;
  values[1] = rgba->green;
  values[2] = rgba->blue;
  values[3] = rgba->alpha;
}

G_END_DECLS

