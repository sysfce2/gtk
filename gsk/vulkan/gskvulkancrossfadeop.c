#include "config.h"

#include "gskvulkancrossfadeopprivate.h"

#include "gskvulkanprivate.h"
#include "gskvulkanshaderopprivate.h"

#include "vulkan/resources/cross-fade.vert.h"

typedef struct _GskVkOldCrossFadeOp GskVkOldCrossFadeOp;

struct _GskVkOldCrossFadeOp
{
  GskVkOldShaderOp op;

  graphene_rect_t bounds;
  float progress;

  struct {
    graphene_rect_t rect;
    graphene_rect_t tex_rect;
    guint32 image_descriptor;
  } start, end;
};

static void
gsk_vk_old_cross_fade_op_print (GskVkOldOp *op,
                                GString     *string,
                                guint        indent)
{
  GskVkOldCrossFadeOp *self = (GskVkOldCrossFadeOp *) op;

  print_indent (string, indent);
  print_rect (string, &self->bounds);
  g_string_append_printf (string, "cross-fade %d%% ", (int) (self->progress * 100 + 0.5));
  print_newline (string);
}

static void
gsk_vk_old_cross_fade_op_collect_vertex_data (GskVkOldOp *op,
                                              guchar      *data)
{
  GskVkOldCrossFadeOp *self = (GskVkOldCrossFadeOp *) op;
  GskVkOldCrossFadeInstance *instance = (GskVkOldCrossFadeInstance *) (data + ((GskVkOldShaderOp *) op)->vertex_offset);

  gsk_rect_to_float (&self->bounds, instance->rect);
  gsk_rect_to_float (&self->start.rect, instance->start_rect);
  gsk_rect_to_float (&self->end.rect, instance->end_rect);
  gsk_rect_to_float (&self->start.tex_rect, instance->start_tex_rect);
  gsk_rect_to_float (&self->end.tex_rect, instance->end_tex_rect);

  instance->start_tex_id = self->start.image_descriptor;
  instance->end_tex_id = self->end.image_descriptor;
  instance->progress = self->progress;
}

static void
gsk_vk_old_cross_fade_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                                  GskVkOldRender *render)
{
  GskVkOldCrossFadeOp *self = (GskVkOldCrossFadeOp *) op;
  GskVkOldShaderOp *shader = (GskVkOldShaderOp *) op;

  self->start.image_descriptor = gsk_vk_old_render_get_image_descriptor (render,
                                                                         shader->images[0],
                                                                         GSK_VK_OLD_SAMPLER_DEFAULT);
  self->end.image_descriptor = gsk_vk_old_render_get_image_descriptor (render,
                                                                       shader->images[1],
                                                                       GSK_VK_OLD_SAMPLER_DEFAULT);
}

static const GskVkOldShaderOpClass GSK_VK_OLD_CROSS_FADE_OP_CLASS = {
  {
    GSK_VK_OLD_OP_SIZE (GskVkOldCrossFadeOp),
    GSK_VK_OLD_STAGE_SHADER,
    gsk_vk_old_shader_op_finish,
    gsk_vk_old_cross_fade_op_print,
    gsk_vk_old_shader_op_count_vertex_data,
    gsk_vk_old_cross_fade_op_collect_vertex_data,
    gsk_vk_old_cross_fade_op_reserve_descriptor_sets,
    gsk_vk_old_shader_op_command
  },
  "cross-fade",
  2,
  &gsk_vk_old_cross_fade_info,
};

void
gsk_vk_old_cross_fade_op (GskVkOldRender        *render,
                          GskVkOldShaderClip     clip,
                          const graphene_rect_t  *bounds,
                          const graphene_point_t *offset,
                          float                   progress,
                          GskVkOldImage         *start_image,
                          const graphene_rect_t  *start_rect,
                          const graphene_rect_t  *start_tex_rect,
                          GskVkOldImage         *end_image,
                          const graphene_rect_t  *end_rect,
                          const graphene_rect_t  *end_tex_rect)
{
  GskVkOldCrossFadeOp *self;

  self = (GskVkOldCrossFadeOp *) gsk_vk_old_shader_op_alloc (render,
                                                              &GSK_VK_OLD_CROSS_FADE_OP_CLASS,
                                                              clip,
                                                              (GskVkOldImage *[2]) {
                                                                  start_image,
                                                                  end_image
                                                              });

  graphene_rect_offset_r (bounds, offset->x, offset->y, &self->bounds);
  self->progress = progress;

  graphene_rect_offset_r (start_rect, offset->x, offset->y, &self->start.rect);
  gsk_vk_old_normalize_tex_coords (&self->start.tex_rect, bounds, start_tex_rect);

  graphene_rect_offset_r (end_rect, offset->x, offset->y, &self->end.rect);
  gsk_vk_old_normalize_tex_coords (&self->end.tex_rect, bounds, end_tex_rect);
}
