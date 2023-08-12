#pragma once

#include <gdk/gdk.h>
#include <gsk/gskrendernode.h>

#include "gskvulkanclipprivate.h"
#include "gskvulkanimageprivate.h"
#include "gskvulkanprivate.h"
#include "gskvulkanrenderpassprivate.h"

G_BEGIN_DECLS

typedef enum {
  GSK_VK_OLD_SAMPLER_DEFAULT,
  GSK_VK_OLD_SAMPLER_REPEAT,
  GSK_VK_OLD_SAMPLER_NEAREST
} GskVkOldRenderSampler;

typedef void            (* GskVkOldDownloadFunc)                       (gpointer                user_data,
                                                                         GdkMemoryFormat         format,
                                                                         const guchar           *data,
                                                                         int                     width,
                                                                         int                     height,
                                                                         gsize                   stride);

GskVkOldRender *       gsk_vk_old_render_new                           (GskRenderer            *renderer,
                                                                         GdkVulkanContext       *context);
void                    gsk_vk_old_render_free                          (GskVkOldRender        *self);

gboolean                gsk_vk_old_render_is_busy                       (GskVkOldRender        *self);
void                    gsk_vk_old_render_render                        (GskVkOldRender        *self,
                                                                         GskVkOldImage         *target,
                                                                         const graphene_rect_t  *rect,
                                                                         const cairo_region_t   *clip,
                                                                         GskRenderNode          *node,
                                                                         GskVkOldDownloadFunc   download_func,
                                                                         gpointer                download_data);

GskRenderer *           gsk_vk_old_render_get_renderer                  (GskVkOldRender        *self);
GdkVulkanContext *      gsk_vk_old_render_get_context                   (GskVkOldRender        *self);

gpointer                gsk_vk_old_render_alloc_op                      (GskVkOldRender        *self,
                                                                         gsize                   size);

VkPipelineLayout        gsk_vk_old_render_get_pipeline_layout           (GskVkOldRender        *self);
VkPipeline              gsk_vk_old_render_get_pipeline                  (GskVkOldRender        *self,
                                                                         const GskVkOldOpClass *op_class,
                                                                         GskVkOldShaderClip     clip,
                                                                         VkRenderPass            render_pass);
VkRenderPass            gsk_vk_old_render_get_render_pass               (GskVkOldRender        *self,
                                                                         VkFormat                format,
                                                                         VkImageLayout           from_layout,
                                                                         VkImageLayout           to_layout);
gsize                   gsk_vk_old_render_get_image_descriptor          (GskVkOldRender        *self,
                                                                         GskVkOldImage         *source,
                                                                         GskVkOldRenderSampler  render_sampler);
gsize                   gsk_vk_old_render_get_buffer_descriptor         (GskVkOldRender        *self,
                                                                         GskVkOldBuffer        *buffer);
guchar *                gsk_vk_old_render_get_buffer_memory             (GskVkOldRender        *self,
                                                                         gsize                   size,
                                                                         gsize                   alignment,
                                                                         gsize                  *out_offset);

VkFence                 gsk_vk_old_render_get_fence                     (GskVkOldRender        *self);

G_END_DECLS

