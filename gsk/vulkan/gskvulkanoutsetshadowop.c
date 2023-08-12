#include "config.h"

#include "gskvulkanoutsetshadowopprivate.h"

#include "gskvulkanprivate.h"
#include "gskvulkanshaderopprivate.h"
#include "gsk/gskroundedrectprivate.h"

#include "vulkan/resources/outset-shadow.vert.h"

typedef struct _GskVkOldOutsetShadowOp GskVkOldOutsetShadowOp;

struct _GskVkOldOutsetShadowOp
{
  GskVkOldShaderOp op;

  GskRoundedRect outline;
  GdkRGBA color;
  graphene_point_t offset;
  float spread;
  float blur_radius;
};

static void
gsk_vk_old_outset_shadow_op_finish (GskVkOldOp *op)
{
}

static void
gsk_vk_old_outset_shadow_op_print (GskVkOldOp *op,
                                   GString     *string,
                                   guint        indent)
{
  GskVkOldOutsetShadowOp *self = (GskVkOldOutsetShadowOp *) op;

  print_indent (string, indent);
  print_rounded_rect (string, &self->outline);
  g_string_append (string, "outset-shadow ");
  if (self->blur_radius > 0)
    g_string_append_printf (string, "blur %gpx ", self->blur_radius);
  print_newline (string);
}

static void
gsk_vk_old_outset_shadow_op_collect_vertex_data (GskVkOldOp *op,
                                                 guchar      *data)
{
  GskVkOldOutsetShadowOp *self = (GskVkOldOutsetShadowOp *) op;
  GskVkOldOutsetShadowInstance *instance = (GskVkOldOutsetShadowInstance *) (data + ((GskVkOldShaderOp *) op)->vertex_offset);

  gsk_rounded_rect_to_float (&self->outline, graphene_point_zero (), instance->outline);
  gsk_vk_old_rgba_to_float (&self->color, instance->color);
  gsk_vk_old_point_to_float (&self->offset, instance->offset);
  instance->spread = self->spread;
  instance->blur_radius = self->blur_radius;
}

static void
gsk_vk_old_outset_shadow_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                                     GskVkOldRender *render)
{
}

static const GskVkOldShaderOpClass GSK_VK_OLD_OUTSET_SHADOW_OP_CLASS = {
  {
    GSK_VK_OLD_OP_SIZE (GskVkOldOutsetShadowOp),
    GSK_VK_OLD_STAGE_SHADER,
    gsk_vk_old_outset_shadow_op_finish,
    gsk_vk_old_outset_shadow_op_print,
    gsk_vk_old_shader_op_count_vertex_data,
    gsk_vk_old_outset_shadow_op_collect_vertex_data,
    gsk_vk_old_outset_shadow_op_reserve_descriptor_sets,
    gsk_vk_old_shader_op_command
  },
  "outset-shadow",
  0,
  &gsk_vk_old_outset_shadow_info,
};

void
gsk_vk_old_outset_shadow_op (GskVkOldRender         *render,
                             GskVkOldShaderClip      clip,
                             const GskRoundedRect    *outline,
                             const graphene_point_t  *offset,
                             const GdkRGBA           *color,
                             const graphene_point_t  *shadow_offset,
                             float                    spread,
                             float                    blur_radius)
{
  GskVkOldOutsetShadowOp *self;

  self = (GskVkOldOutsetShadowOp *) gsk_vk_old_shader_op_alloc (render, &GSK_VK_OLD_OUTSET_SHADOW_OP_CLASS, clip, NULL);

  self->outline = *outline;
  gsk_rounded_rect_offset (&self->outline, offset->x, offset->y);
  self->color = *color;
  self->offset = *shadow_offset;
  self->spread = spread;
  self->blur_radius = blur_radius;
}

