#pragma once

#include <gdk/gdk.h>

G_BEGIN_DECLS

typedef struct _GskVkOldCommandPool GskVkOldCommandPool;

GskVkOldCommandPool *  gsk_vk_old_command_pool_new                     (GdkVulkanContext       *context);
void                    gsk_vk_old_command_pool_free                    (GskVkOldCommandPool   *self);

void                    gsk_vk_old_command_pool_reset                   (GskVkOldCommandPool   *self);

VkCommandBuffer         gsk_vk_old_command_pool_get_buffer              (GskVkOldCommandPool   *self);
void                    gsk_vk_old_command_pool_submit_buffer           (GskVkOldCommandPool   *self,
                                                                         VkCommandBuffer         buffer,
                                                                         gsize                   wait_semaphore_count,
                                                                         VkSemaphore            *wait_semaphores,
                                                                         gsize                   signal_semaphores_count,
                                                                         VkSemaphore            *signal_semaphores,
                                                                         VkFence                 fence);

G_END_DECLS

