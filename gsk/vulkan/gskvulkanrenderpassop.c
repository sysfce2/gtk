#include "config.h"

#include "gskvulkanrenderpassopprivate.h"

#include "gskrendernodeprivate.h"
#include "gskvulkanprivate.h"
#include "gskvulkanshaderopprivate.h"

#include "gdk/gdkvulkancontextprivate.h"

typedef struct _GskVkOldRenderPassOp GskVkOldRenderPassOp;

struct _GskVkOldRenderPassOp
{
  GskVkOldOp op;

  GskVkOldImage *image;
  cairo_rectangle_int_t area;

  VkImageLayout initial_layout;
  VkImageLayout final_layout;
};

static void
gsk_vk_old_render_pass_op_finish (GskVkOldOp *op)
{
  GskVkOldRenderPassOp *self = (GskVkOldRenderPassOp *) op;

  g_object_unref (self->image);
}

static void
gsk_vk_old_render_pass_op_print (GskVkOldOp *op,
                                 GString     *string,
                                 guint        indent)
{
  GskVkOldRenderPassOp *self = (GskVkOldRenderPassOp *) op;

  print_indent (string, indent);
  g_string_append_printf (string, "begin-render-pass %zux%zu ",
                          gsk_vk_old_image_get_width (self->image),
                          gsk_vk_old_image_get_height (self->image));
  print_newline (string);
}

static gsize
gsk_vk_old_render_pass_op_count_vertex_data (GskVkOldOp *op,
                                             gsize        n_bytes)
{
  return n_bytes;
}

static void
gsk_vk_old_render_pass_op_collect_vertex_data (GskVkOldOp *op,
                                               guchar      *data)
{
}

static void
gsk_vk_old_render_pass_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                                   GskVkOldRender *render)
{
}

static void
gsk_vk_old_render_pass_op_do_barriers (GskVkOldRenderPassOp *self,
                                       VkCommandBuffer        command_buffer)
{
  GskVkOldShaderOp *shader;
  GskVkOldOp *op;
  gsize i;

  for (op = ((GskVkOldOp *) self)->next;
       op->op_class->stage != GSK_VK_OLD_STAGE_END_PASS;
       op = op->next)
    {
      if (op->op_class->stage != GSK_VK_OLD_STAGE_SHADER)
        continue;

      shader = (GskVkOldShaderOp *) op;

      for (i = 0; i < ((GskVkOldShaderOpClass *) op->op_class)->n_images; i++)
        {
          gsk_vk_old_image_transition (shader->images[i],
                                       command_buffer,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                       VK_ACCESS_SHADER_READ_BIT);
        }
    }
}

static GskVkOldOp *
gsk_vk_old_render_pass_op_command (GskVkOldOp      *op,
                                   GskVkOldRender  *render,
                                   VkRenderPass      render_pass,
                                   VkCommandBuffer   command_buffer)
{
  GskVkOldRenderPassOp *self = (GskVkOldRenderPassOp *) op;
  VkRenderPass vk_render_pass;

  /* nesting render passes not allowed */
  g_assert (render_pass == VK_NULL_HANDLE);

  gsk_vk_old_render_pass_op_do_barriers (self, command_buffer);

  vk_render_pass = gsk_vk_old_render_get_render_pass (render,
                                                      gsk_vk_old_image_get_vk_format (self->image),
                                                      self->initial_layout,
                                                      self->final_layout);


  vkCmdSetViewport (command_buffer,
                    0,
                    1,
                    &(VkViewport) {
                        .x = 0,
                        .y = 0,
                        .width = gsk_vk_old_image_get_width (self->image),
                        .height = gsk_vk_old_image_get_height (self->image),
                        .minDepth = 0,
                        .maxDepth = 1
                    });

  vkCmdBeginRenderPass (command_buffer,
                        &(VkRenderPassBeginInfo) {
                            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                            .renderPass = vk_render_pass,
                            .framebuffer = gsk_vk_old_image_get_framebuffer (self->image,
                                                                             vk_render_pass),
                            .renderArea = { 
                                { self->area.x, self->area.y },
                                { self->area.width, self->area.height }
                            },
                            .clearValueCount = 1,
                            .pClearValues = (VkClearValue [1]) {
                                { .color = { .float32 = { 0.f, 0.f, 0.f, 0.f } } }
                            }
                        },
                        VK_SUBPASS_CONTENTS_INLINE);

  op = op->next;
  while (op->op_class->stage != GSK_VK_OLD_STAGE_END_PASS)
    {
      op = gsk_vk_old_op_command (op, render, vk_render_pass, command_buffer);
    }

  op = gsk_vk_old_op_command (op, render, vk_render_pass, command_buffer);

  return op;
}

static const GskVkOldOpClass GSK_VK_OLD_RENDER_PASS_OP_CLASS = {
  GSK_VK_OLD_OP_SIZE (GskVkOldRenderPassOp),
  GSK_VK_OLD_STAGE_BEGIN_PASS,
  gsk_vk_old_render_pass_op_finish,
  gsk_vk_old_render_pass_op_print,
  gsk_vk_old_render_pass_op_count_vertex_data,
  gsk_vk_old_render_pass_op_collect_vertex_data,
  gsk_vk_old_render_pass_op_reserve_descriptor_sets,
  gsk_vk_old_render_pass_op_command
};

typedef struct _GskVkOldRenderPassEndOp GskVkOldRenderPassEndOp;

struct _GskVkOldRenderPassEndOp
{
  GskVkOldOp op;

  GskVkOldImage *image;
  VkImageLayout final_layout;
};

static void
gsk_vk_old_render_pass_end_op_finish (GskVkOldOp *op)
{
  GskVkOldRenderPassEndOp *self = (GskVkOldRenderPassEndOp *) op;

  g_object_unref (self->image);
}

static void
gsk_vk_old_render_pass_end_op_print (GskVkOldOp *op,
                                     GString     *string,
                                     guint        indent)
{
  GskVkOldRenderPassEndOp *self = (GskVkOldRenderPassEndOp *) op;

  print_indent (string, indent);
  g_string_append_printf (string, "end-render-pass ");
  print_image (string, self->image);
  print_newline (string);
}

static gsize
gsk_vk_old_render_pass_end_op_count_vertex_data (GskVkOldOp *op,
                                                 gsize        n_bytes)
{
  return n_bytes;
}

static void
gsk_vk_old_render_pass_end_op_collect_vertex_data (GskVkOldOp *op,
                                                   guchar      *data)
{
}

static void
gsk_vk_old_render_pass_end_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                                       GskVkOldRender *render)
{
}

static GskVkOldOp *
gsk_vk_old_render_pass_end_op_command (GskVkOldOp      *op,
                                       GskVkOldRender  *render,
                                       VkRenderPass      render_pass,
                                       VkCommandBuffer   command_buffer)
{
  GskVkOldRenderPassEndOp *self = (GskVkOldRenderPassEndOp *) op;

  vkCmdEndRenderPass (command_buffer);

  gsk_vk_old_image_set_vk_image_layout (self->image,
                                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                        self->final_layout,
                                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  return op->next;
}

static const GskVkOldOpClass GSK_VK_OLD_RENDER_PASS_END_OP_CLASS = {
  GSK_VK_OLD_OP_SIZE (GskVkOldRenderPassEndOp),
  GSK_VK_OLD_STAGE_END_PASS,
  gsk_vk_old_render_pass_end_op_finish,
  gsk_vk_old_render_pass_end_op_print,
  gsk_vk_old_render_pass_end_op_count_vertex_data,
  gsk_vk_old_render_pass_end_op_collect_vertex_data,
  gsk_vk_old_render_pass_end_op_reserve_descriptor_sets,
  gsk_vk_old_render_pass_end_op_command
};

void
gsk_vk_old_render_pass_begin_op (GskVkOldRender             *render,
                                 GskVkOldImage              *image,
                                 const cairo_rectangle_int_t *area,
                                 VkImageLayout                initial_layout,
                                 VkImageLayout                final_layout)
{
  GskVkOldRenderPassOp *self;

  self = (GskVkOldRenderPassOp *) gsk_vk_old_op_alloc (render, &GSK_VK_OLD_RENDER_PASS_OP_CLASS);

  self->image = g_object_ref (image);
  self->initial_layout = initial_layout;
  self->final_layout = final_layout;
  self->area = *area;
}

void
gsk_vk_old_render_pass_end_op (GskVkOldRender *render,
                               GskVkOldImage  *image,
                               VkImageLayout    final_layout)
{
  GskVkOldRenderPassEndOp *self;

  self = (GskVkOldRenderPassEndOp *) gsk_vk_old_op_alloc (render, &GSK_VK_OLD_RENDER_PASS_END_OP_CLASS);

  self->image = g_object_ref (image);
  self->final_layout = final_layout;
}

GskVkOldImage *
gsk_vk_old_render_pass_op_offscreen (GskVkOldRender       *render,
                                     const graphene_vec2_t *scale,
                                     const graphene_rect_t *viewport,
                                     GskRenderNode         *node)
{
  GskVkOldRenderPass *render_pass;
  GdkVulkanContext *context;
  GskVkOldImage *image;
  int width, height;

  width = ceil (graphene_vec2_get_x (scale) * viewport->size.width);
  height = ceil (graphene_vec2_get_y (scale) * viewport->size.height);

  context = gsk_vk_old_render_get_context (render);
  image = gsk_vk_old_image_new_for_offscreen (context,
                                              gdk_vulkan_context_get_offscreen_format (context,
                                                  gsk_render_node_get_preferred_depth (node)),
                                              width, height);

  gsk_vk_old_render_pass_begin_op (render,
                                   image,
                                   &(cairo_rectangle_int_t) { 0, 0, width, height },
                                   VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  render_pass = gsk_vk_old_render_pass_new ();
  gsk_vk_old_render_pass_add (render_pass,
                              render,
                              width, height,
                              &(cairo_rectangle_int_t) { 0, 0, width, height },
                              node,
                              viewport);
  gsk_vk_old_render_pass_free (render_pass);

  gsk_vk_old_render_pass_end_op (render,
                                 image,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  g_object_unref (image);

  return image;
}
