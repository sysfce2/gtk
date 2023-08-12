/*
 * Copyright © 2016  Benjamin Otte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <gdk/gdk.h>
#include <gsk/gsk.h>

#ifdef GDK_RENDERING_VULKAN

#include <vulkan/vulkan.h>

G_BEGIN_DECLS

#define GSK_TYPE_VK_OLD_RENDERER (gsk_vk_old_renderer_get_type ())

#define GSK_VK_OLD_RENDERER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSK_TYPE_VK_OLD_RENDERER, GskVkOldRenderer))
#define GSK_IS_VK_OLD_RENDERER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSK_TYPE_VK_OLD_RENDERER))
#define GSK_VK_OLD_RENDERER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GSK_TYPE_VK_OLD_RENDERER, GskVkOldRendererClass))
#define GSK_IS_VK_OLD_RENDERER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GSK_TYPE_VK_OLD_RENDERER))
#define GSK_VK_OLD_RENDERER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GSK_TYPE_VK_OLD_RENDERER, GskVkOldRendererClass))

/**
 * GskVkOldRenderer:
 *
 * A GSK renderer that is using Vulkan.
 */
typedef struct _GskVkOldRenderer                GskVkOldRenderer;
typedef struct _GskVkOldRendererClass           GskVkOldRendererClass;

GDK_AVAILABLE_IN_4_14
GType                   gsk_vk_old_renderer_get_type            (void) G_GNUC_CONST;

GDK_AVAILABLE_IN_4_14
GskRenderer *           gsk_vk_old_renderer_new                 (void);

G_END_DECLS

#endif /* GDK_RENDERING_VULKAN */

