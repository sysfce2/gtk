#include "config.h"

#include "gsk/vulkan/gskvulkanrenderer.h"

#include "gskgpurendererprivate.h"
#include "gskvulkandeviceprivate.h"
#include "gskvulkanframeprivate.h"
#include "gskvulkanimageprivate.h"

#include "gdk/gdkdisplayprivate.h"

struct _GskVulkanRenderer
{
  GskGpuRenderer parent_instance;

  guint n_targets;
  GskGpuImage **targets;
};

struct _GskVulkanRendererClass
{
  GskGpuRendererClass parent_class;
};

G_DEFINE_TYPE (GskVulkanRenderer, gsk_vulkan_renderer, GSK_TYPE_GPU_RENDERER)

static void
gsk_vulkan_renderer_free_targets (GskVulkanRenderer *self)
{
  guint i;

  for (i = 0; i < self->n_targets; i++)
    {
      g_object_unref (self->targets[i]);
    }

  g_clear_pointer (&self->targets, g_free);
  self->n_targets = 0;
}

static void
gsk_vulkan_renderer_update_images_cb (GdkVulkanContext  *context,
                                      GskVulkanRenderer *self)
{
  GskVulkanDevice *device;
  GdkSurface *surface;
  double scale;
  gsize width, height;
  guint i;

  surface = gsk_renderer_get_surface (GSK_RENDERER (self));
  if (surface == NULL)
    return;

  device = GSK_VULKAN_DEVICE (gsk_gpu_renderer_get_device (GSK_GPU_RENDERER (self)));

  gsk_vulkan_renderer_free_targets (self);

  self->n_targets = gdk_vulkan_context_get_n_images (context);
  self->targets = g_new (GskGpuImage *, self->n_targets);

  scale = gdk_surface_get_scale (surface);
  width = (gsize) ceil (gdk_surface_get_width (surface) * scale);
  height = (gsize) ceil (gdk_surface_get_height (surface) * scale);

  for (i = 0; i < self->n_targets; i++)
    {
      self->targets[i] = gsk_vulkan_image_new_for_swapchain (device,
                                                             gdk_vulkan_context_get_image (context, i),
                                                             gdk_vulkan_context_get_image_format (context),
                                                             width, height);
    }
}

static GdkDrawContext *
gsk_vulkan_renderer_create_context (GskGpuRenderer       *renderer,
                                    GdkDisplay           *display,
                                    GdkSurface           *surface,
                                    GskGpuOptimizations  *supported,
                                    GError              **error)
{
  GskVulkanRenderer *self = GSK_VULKAN_RENDERER (renderer);
  GdkVulkanContext *context;

  if (surface)
    context = gdk_surface_create_vulkan_context (surface, error);
  else
    context = gdk_display_create_vulkan_context (display, error);

  g_signal_connect (context,
                    "images-updated",
                    G_CALLBACK (gsk_vulkan_renderer_update_images_cb),
                    self);
  gsk_vulkan_renderer_update_images_cb (context, self);

  *supported = -1;

  return GDK_DRAW_CONTEXT (context);
}

static GskGpuImage *
gsk_vulkan_renderer_get_backbuffer (GskGpuRenderer *renderer)
{
  GskVulkanRenderer *self = GSK_VULKAN_RENDERER (renderer);
  GdkVulkanContext *context;
  
  context = GDK_VULKAN_CONTEXT (gsk_gpu_renderer_get_context (renderer));

  return self->targets[gdk_vulkan_context_get_draw_index (context)];
}

static void
gsk_vulkan_renderer_wait (GskGpuRenderer  *renderer,
                          GskGpuFrame    **frames,
                          gsize            n_frames)
{
  VkFence *fences;
  VkDevice vk_device;
  gsize i;

  vk_device = gsk_vulkan_device_get_vk_device (GSK_VULKAN_DEVICE (gsk_gpu_renderer_get_device (renderer)));

  fences = g_alloca (sizeof (VkFence) * n_frames);

  for (i = 0; i < n_frames; i++)
    {
      fences[i] = gsk_vulkan_frame_get_vk_fence (GSK_VULKAN_FRAME (frames[i]));
    }

  GSK_VK_CHECK (vkWaitForFences, vk_device,
                                 n_frames,
                                 fences,
                                 VK_FALSE,
                                 INT64_MAX);
}

static void
gsk_vulkan_renderer_unrealize (GskRenderer *renderer)
{
  GskVulkanRenderer *self = GSK_VULKAN_RENDERER (renderer);

  gsk_vulkan_renderer_free_targets (self);
  g_signal_handlers_disconnect_by_func (gsk_gpu_renderer_get_context (GSK_GPU_RENDERER (self)),
                                        gsk_vulkan_renderer_update_images_cb,
                                        self);

  GSK_RENDERER_CLASS (gsk_vulkan_renderer_parent_class)->unrealize (renderer);
}

static void
gsk_vulkan_renderer_class_init (GskVulkanRendererClass *klass)
{
  GskGpuRendererClass *gpu_renderer_class = GSK_GPU_RENDERER_CLASS (klass);
  GskRendererClass *renderer_class = GSK_RENDERER_CLASS (klass);

  gpu_renderer_class->frame_type = GSK_TYPE_VULKAN_FRAME;

  gpu_renderer_class->get_device = gsk_vulkan_device_get_for_display;
  gpu_renderer_class->create_context = gsk_vulkan_renderer_create_context;
  gpu_renderer_class->get_backbuffer = gsk_vulkan_renderer_get_backbuffer;
  gpu_renderer_class->wait = gsk_vulkan_renderer_wait;

  renderer_class->unrealize = gsk_vulkan_renderer_unrealize;
}

static void
gsk_vulkan_renderer_init (GskVulkanRenderer *self)
{
}

/**
 * gsk_vulkan_renderer_new:
 *
 * Creates a new Vulkan renderer.
 *
 * The Vulkan renderer is a renderer that uses the Vulkan library for
 * rendering.
 *
 * This function is only available when GTK was compiled with Vulkan
 * support.
 *
 * Returns: a new Vulkan renderer
 **/
GskRenderer *
gsk_vulkan_renderer_new (void)
{
  return g_object_new (GSK_TYPE_VULKAN_RENDERER, NULL);
}
