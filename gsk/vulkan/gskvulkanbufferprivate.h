#pragma once

#include <gdk/gdk.h>

G_BEGIN_DECLS

typedef struct _GskVkOldBuffer GskVkOldBuffer;

typedef enum
{
  GSK_VK_OLD_READ = (1 << 0),
  GSK_VK_OLD_WRITE = (1 << 1),
  GSK_VK_OLD_READWRITE = GSK_VK_OLD_READ | GSK_VK_OLD_WRITE
} GskVkOldMapMode;

GskVkOldBuffer *       gsk_vk_old_buffer_new                           (GdkVulkanContext       *context,
                                                                         gsize                   size);
GskVkOldBuffer *       gsk_vk_old_buffer_new_storage                   (GdkVulkanContext       *context,
                                                                         gsize                   size);
GskVkOldBuffer *       gsk_vk_old_buffer_new_map                       (GdkVulkanContext       *context,
                                                                         gsize                   size,
                                                                         GskVkOldMapMode        mode);
void                    gsk_vk_old_buffer_free                          (GskVkOldBuffer        *buffer);

VkBuffer                gsk_vk_old_buffer_get_buffer                    (GskVkOldBuffer        *self);
gsize                   gsk_vk_old_buffer_get_size                      (GskVkOldBuffer        *self);

guchar *                gsk_vk_old_buffer_map                           (GskVkOldBuffer        *self);
void                    gsk_vk_old_buffer_unmap                         (GskVkOldBuffer        *self);

G_END_DECLS

