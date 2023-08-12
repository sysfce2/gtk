#pragma once

#include <gdk/gdk.h>

G_BEGIN_DECLS

typedef struct _GskVkOldMemory GskVkOldMemory;

GskVkOldMemory *       gsk_vk_old_memory_new                           (GdkVulkanContext       *context,
                                                                         uint32_t                allowed_types,
                                                                         VkMemoryPropertyFlags   properties,
                                                                         gsize                   size);
void                    gsk_vk_old_memory_free                          (GskVkOldMemory        *memory);

VkDeviceMemory          gsk_vk_old_memory_get_device_memory             (GskVkOldMemory        *self);

gboolean                gsk_vk_old_memory_can_map                       (GskVkOldMemory        *self,
                                                                         gboolean                fast);
guchar *                gsk_vk_old_memory_map                           (GskVkOldMemory        *self);
void                    gsk_vk_old_memory_unmap                         (GskVkOldMemory        *self);

G_END_DECLS

