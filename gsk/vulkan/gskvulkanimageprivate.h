#pragma once

#include <gdk/gdk.h>

#include "gskvulkanbufferprivate.h"
#include "gskvulkancommandpoolprivate.h"


G_BEGIN_DECLS

/* required postprocessing steps before the image van be used */
typedef enum
{
  GSK_VK_OLD_IMAGE_PREMULTIPLY = (1 << 0),
} GskVkOldImagePostprocess;

#define GSK_TYPE_VK_OLD_IMAGE (gsk_vk_old_image_get_type ())

G_DECLARE_FINAL_TYPE (GskVkOldImage, gsk_vk_old_image, GSK, VK_OLD_IMAGE, GObject)

GskVkOldImage *        gsk_vk_old_image_new_for_swapchain              (GdkVulkanContext       *context,
                                                                         VkImage                 image,
                                                                         VkFormat                format,
                                                                         gsize                   width,
                                                                         gsize                   height);

GskVkOldImage *        gsk_vk_old_image_new_for_atlas                  (GdkVulkanContext       *context,
                                                                         gsize                   width,
                                                                         gsize                   height);
GskVkOldImage *        gsk_vk_old_image_new_for_offscreen              (GdkVulkanContext       *context,
                                                                         GdkMemoryFormat         preferred_format,
                                                                         gsize                   width,
                                                                         gsize                   height);

GskVkOldImage *        gsk_vk_old_image_new_for_upload                 (GdkVulkanContext       *context,
                                                                         GdkMemoryFormat         format,
                                                                         gsize                   width,
                                                                         gsize                   height);
guchar *                gsk_vk_old_image_try_map                        (GskVkOldImage         *self,
                                                                         gsize                  *out_stride);
void                    gsk_vk_old_image_unmap                          (GskVkOldImage         *self);

gsize                   gsk_vk_old_image_get_width                      (GskVkOldImage         *self);
gsize                   gsk_vk_old_image_get_height                     (GskVkOldImage         *self);
GskVkOldImagePostprocess
                        gsk_vk_old_image_get_postprocess                (GskVkOldImage         *self);
VkPipelineStageFlags    gsk_vk_old_image_get_vk_pipeline_stage          (GskVkOldImage         *self);
VkImageLayout           gsk_vk_old_image_get_vk_image_layout            (GskVkOldImage         *self);
VkAccessFlags           gsk_vk_old_image_get_vk_access                  (GskVkOldImage         *self);
void                    gsk_vk_old_image_set_vk_image_layout            (GskVkOldImage         *self,
                                                                         VkPipelineStageFlags    stage,
                                                                         VkImageLayout           image_layout,
                                                                         VkAccessFlags           access);
void                    gsk_vk_old_image_transition                     (GskVkOldImage         *self,
                                                                         VkCommandBuffer         command_buffer,
                                                                         VkPipelineStageFlags    stage,
                                                                         VkImageLayout           image_layout,
                                                                         VkAccessFlags           access);
#define gdk_vulkan_image_transition_shader(image) \
  gsk_vk_old_image_transition ((image), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, \
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT)

VkImage                 gsk_vk_old_image_get_vk_image                   (GskVkOldImage         *self);
VkImageView             gsk_vk_old_image_get_image_view                 (GskVkOldImage         *self);
VkFormat                gsk_vk_old_image_get_vk_format                  (GskVkOldImage         *self);
GdkMemoryFormat         gsk_vk_old_image_get_format                     (GskVkOldImage         *self);
VkFramebuffer           gsk_vk_old_image_get_framebuffer                (GskVkOldImage         *self,
                                                                         VkRenderPass            pass);

static inline void
print_image (GString        *string,
             GskVkOldImage *image)
{
  g_string_append_printf (string, "%zux%zu ",
                          gsk_vk_old_image_get_width (image),
                          gsk_vk_old_image_get_height (image));
}

G_END_DECLS

