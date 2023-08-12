#include "config.h"

#include "gskvulkanborderopprivate.h"

#include "gskvulkanprivate.h"
#include "gskvulkanshaderopprivate.h"
#include "gsk/gskroundedrectprivate.h"

#include "vulkan/resources/border.vert.h"

typedef struct _GskVkOldBorderOp GskVkOldBorderOp;

struct _GskVkOldBorderOp
{
  GskVkOldShaderOp op;

  GskRoundedRect outline;
  float widths[4];
  GdkRGBA colors[4];
};

static void
gsk_vk_old_border_op_finish (GskVkOldOp *op)
{
}

static void
gsk_vk_old_border_op_print (GskVkOldOp *op,
                            GString     *string,
                            guint        indent)
{
  GskVkOldBorderOp *self = (GskVkOldBorderOp *) op;

  print_indent (string, indent);
  print_rounded_rect (string, &self->outline);
  g_string_append (string, "border ");
  print_rgba (string, &self->colors[0]);
  if (!gdk_rgba_equal (&self->colors[3], &self->colors[0]) ||
      !gdk_rgba_equal (&self->colors[2], &self->colors[0]) ||
      !gdk_rgba_equal (&self->colors[1], &self->colors[0]))
    {
      print_rgba (string, &self->colors[1]);
      print_rgba (string, &self->colors[2]);
      print_rgba (string, &self->colors[3]);
    }
  g_string_append_printf (string, "%g ", self->widths[0]);
  if (self->widths[0] != self->widths[1] ||
      self->widths[0] != self->widths[2] ||
      self->widths[0] != self->widths[3])
    g_string_append_printf (string, "%g %g %g ", self->widths[1], self->widths[2], self->widths[3]);

  print_newline (string);
}

static void
gsk_vk_old_border_op_collect_vertex_data (GskVkOldOp *op,
                                          guchar      *data)
{
  GskVkOldBorderOp *self = (GskVkOldBorderOp *) op;
  GskVkOldBorderInstance *instance = (GskVkOldBorderInstance *) (data + ((GskVkOldShaderOp *) op)->vertex_offset);
  guint i;

  gsk_rounded_rect_to_float (&self->outline, graphene_point_zero (), instance->rect);
  for (i = 0; i < 4; i++)
    {
      instance->border_widths[i] = self->widths[i];
      gsk_vk_old_rgba_to_float (&self->colors[i], (gpointer) &instance->border_colors[4 * i]);
    }
}

static void
gsk_vk_old_border_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                              GskVkOldRender *render)
{
}

static GskVkOldOp *
gsk_vk_old_border_op_command (GskVkOldOp     *op,
                              GskVkOldRender *render,
                              VkRenderPass     render_pass,
                              VkCommandBuffer  command_buffer)
{
  return gsk_vk_old_shader_op_command_n (op, render, render_pass, command_buffer, 8);
}

static const GskVkOldShaderOpClass GSK_VK_OLD_BORDER_OP_CLASS = {
  {
    GSK_VK_OLD_OP_SIZE (GskVkOldBorderOp),
    GSK_VK_OLD_STAGE_SHADER,
    gsk_vk_old_border_op_finish,
    gsk_vk_old_border_op_print,
    gsk_vk_old_shader_op_count_vertex_data,
    gsk_vk_old_border_op_collect_vertex_data,
    gsk_vk_old_border_op_reserve_descriptor_sets,
    gsk_vk_old_border_op_command
  },
  "border",
  0,
  &gsk_vk_old_border_info,
};

void
gsk_vk_old_border_op (GskVkOldRender         *render,
                      GskVkOldShaderClip      clip,
                      const GskRoundedRect    *outline,
                      const graphene_point_t  *offset,
                      const float              widths[4],
                      const GdkRGBA            colors[4])
{
  GskVkOldBorderOp *self;
  guint i;

  self = (GskVkOldBorderOp *) gsk_vk_old_shader_op_alloc (render, &GSK_VK_OLD_BORDER_OP_CLASS, clip, NULL);

  self->outline = *outline;
  gsk_rounded_rect_offset (&self->outline, offset->x, offset->y);
  for (i = 0; i < 4; i++)
    {
      self->widths[i] = widths[i];
      self->colors[i] = colors[i];
    }
}

