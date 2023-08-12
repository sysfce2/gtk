#include "config.h"

#include "gskvulkanpushconstantsopprivate.h"

#include "gskroundedrectprivate.h"
#include "gskvulkanprivate.h"

typedef struct _GskVkOldPushConstantsOp GskVkOldPushConstantsOp;
typedef struct _GskVkOldPushConstantsInstance GskVkOldPushConstantsInstance;

struct _GskVkOldPushConstantsInstance
{
  float mvp[16];
  float clip[12];
  float scale[2];
};

struct _GskVkOldPushConstantsOp
{
  GskVkOldOp op;

  GskVkOldPushConstantsInstance instance;
};

uint32_t
gsk_vk_old_push_constants_get_range_count (void)
{
  return 1;
}

const VkPushConstantRange *
gsk_vk_old_push_constants_get_ranges (void)
{
  static const VkPushConstantRange ranges[1] = {
      {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
          .offset = 0,
          .size = sizeof (GskVkOldPushConstantsInstance)
      }
  };

  return ranges;
}

static void
gsk_vk_old_push_constants_op_finish (GskVkOldOp *op)
{
}

static void
gsk_vk_old_push_constants_op_print (GskVkOldOp *op,
                                    GString     *string,
                                    guint        indent)
{
  print_indent (string, indent);
  g_string_append_printf (string, "push-constants ");
  print_newline (string);
}

static gsize
gsk_vk_old_push_constants_op_count_vertex_data (GskVkOldOp *op,
                                                gsize        n_bytes)
{
  return n_bytes;
}

static void
gsk_vk_old_push_constants_op_collect_vertex_data (GskVkOldOp *op,
                                                  guchar      *data)
{
}

static void
gsk_vk_old_push_constants_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                                      GskVkOldRender *render)
{
}

static GskVkOldOp *
gsk_vk_old_push_constants_op_command (GskVkOldOp      *op,
                                      GskVkOldRender  *render,
                                      VkRenderPass      render_pass,
                                      VkCommandBuffer   command_buffer)
{
  GskVkOldPushConstantsOp *self = (GskVkOldPushConstantsOp *) op;

  vkCmdPushConstants (command_buffer,
                      gsk_vk_old_render_get_pipeline_layout (render),
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0,
                      sizeof (self->instance),
                      &self->instance);

  return op->next;
}

static const GskVkOldOpClass GSK_VK_OLD_PUSH_CONSTANTS_OP_CLASS = {
  GSK_VK_OLD_OP_SIZE (GskVkOldPushConstantsOp),
  GSK_VK_OLD_STAGE_COMMAND,
  gsk_vk_old_push_constants_op_finish,
  gsk_vk_old_push_constants_op_print,
  gsk_vk_old_push_constants_op_count_vertex_data,
  gsk_vk_old_push_constants_op_collect_vertex_data,
  gsk_vk_old_push_constants_op_reserve_descriptor_sets,
  gsk_vk_old_push_constants_op_command
};

void
gsk_vk_old_push_constants_op (GskVkOldRender         *render,
                              const graphene_vec2_t   *scale,
                              const graphene_matrix_t *mvp,
                              const GskRoundedRect    *clip)
{
  GskVkOldPushConstantsOp *self;

  self = (GskVkOldPushConstantsOp *) gsk_vk_old_op_alloc (render, &GSK_VK_OLD_PUSH_CONSTANTS_OP_CLASS);

  graphene_matrix_to_float (mvp, self->instance.mvp);
  gsk_rounded_rect_to_float (clip, graphene_point_zero (), self->instance.clip);
  graphene_vec2_to_float (scale, self->instance.scale);
}
