#include "config.h"

#include "gskvulkancolormatrixopprivate.h"

#include "gskvulkanprivate.h"
#include "gskvulkanshaderopprivate.h"

#include "vulkan/resources/color-matrix.vert.h"

typedef struct _GskVkOldColorMatrixOp GskVkOldColorMatrixOp;

struct _GskVkOldColorMatrixOp
{
  GskVkOldShaderOp op;

  graphene_matrix_t color_matrix;
  graphene_vec4_t color_offset;
  graphene_rect_t rect;
  graphene_rect_t tex_rect;

  guint32 image_descriptor;
};

static void
gsk_vk_old_color_matrix_op_print (GskVkOldOp *op,
                                  GString     *string,
                                  guint        indent)
{
  GskVkOldColorMatrixOp *self = (GskVkOldColorMatrixOp *) op;

  print_indent (string, indent);
  print_rect (string, &self->rect);
  g_string_append (string, "color-matrix ");
  print_newline (string);
}

static void
gsk_vk_old_color_matrix_op_collect_vertex_data (GskVkOldOp *op,
                                                guchar      *data)
{
  GskVkOldColorMatrixOp *self = (GskVkOldColorMatrixOp *) op;
  GskVkOldColorMatrixInstance *instance = (GskVkOldColorMatrixInstance *) (data + ((GskVkOldShaderOp *) op)->vertex_offset);

  instance->rect[0] = self->rect.origin.x;
  instance->rect[1] = self->rect.origin.y;
  instance->rect[2] = self->rect.size.width;
  instance->rect[3] = self->rect.size.height;
  instance->tex_rect[0] = self->tex_rect.origin.x;
  instance->tex_rect[1] = self->tex_rect.origin.y;
  instance->tex_rect[2] = self->tex_rect.size.width;
  instance->tex_rect[3] = self->tex_rect.size.height;
  graphene_matrix_to_float (&self->color_matrix, instance->color_matrix);
  graphene_vec4_to_float (&self->color_offset, instance->color_offset);
  instance->tex_id = self->image_descriptor;
}

static void
gsk_vk_old_color_matrix_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                                    GskVkOldRender *render)
{
  GskVkOldColorMatrixOp *self = (GskVkOldColorMatrixOp *) op;
  GskVkOldShaderOp *shader = (GskVkOldShaderOp *) op;

  self->image_descriptor = gsk_vk_old_render_get_image_descriptor (render,
                                                                   shader->images[0],
                                                                   GSK_VK_OLD_SAMPLER_DEFAULT);
}

static const GskVkOldShaderOpClass GSK_VK_OLD_COLOR_MATRIX_OP_CLASS = {
  {
    GSK_VK_OLD_OP_SIZE (GskVkOldColorMatrixOp),
    GSK_VK_OLD_STAGE_SHADER,
    gsk_vk_old_shader_op_finish,
    gsk_vk_old_color_matrix_op_print,
    gsk_vk_old_shader_op_count_vertex_data,
    gsk_vk_old_color_matrix_op_collect_vertex_data,
    gsk_vk_old_color_matrix_op_reserve_descriptor_sets,
    gsk_vk_old_shader_op_command
  },
  "color-matrix",
  1,
  &gsk_vk_old_color_matrix_info,
};

void
gsk_vk_old_color_matrix_op (GskVkOldRender         *render,
                            GskVkOldShaderClip      clip,
                            GskVkOldImage          *image,
                            const graphene_rect_t   *rect,
                            const graphene_point_t  *offset,
                            const graphene_rect_t   *tex_rect,
                            const graphene_matrix_t *color_matrix,
                            const graphene_vec4_t   *color_offset)
{
  GskVkOldColorMatrixOp *self;

  self = (GskVkOldColorMatrixOp *) gsk_vk_old_shader_op_alloc (render, &GSK_VK_OLD_COLOR_MATRIX_OP_CLASS, clip, &image);

  graphene_rect_offset_r (rect, offset->x, offset->y, &self->rect);
  gsk_vk_old_normalize_tex_coords (&self->tex_rect, rect, tex_rect);
  self->color_matrix = *color_matrix;
  self->color_offset = *color_offset;
}

void
gsk_vk_old_color_matrix_op_opacity (GskVkOldRender        *render,
                                    GskVkOldShaderClip     clip,
                                    GskVkOldImage         *image,
                                    const graphene_rect_t  *rect,
                                    const graphene_point_t *offset,
                                    const graphene_rect_t  *tex_rect,
                                    float                   opacity)
{
  graphene_matrix_t color_matrix;
  graphene_vec4_t color_offset;

  graphene_matrix_init_from_float (&color_matrix,
                                   (float[16]) {
                                       1.0, 0.0, 0.0, 0.0,
                                       0.0, 1.0, 0.0, 0.0,
                                       0.0, 0.0, 1.0, 0.0,
                                       0.0, 0.0, 0.0, opacity
                                   });
  graphene_vec4_init (&color_offset, 0.0, 0.0, 0.0, 0.0);

  gsk_vk_old_color_matrix_op (render,
                              clip,
                              image,
                              rect,
                              offset,
                              tex_rect,
                              &color_matrix,
                              &color_offset);
}

