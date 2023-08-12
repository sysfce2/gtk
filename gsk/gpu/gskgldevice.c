#include "config.h"

#include "gskgldeviceprivate.h"
#include "gskglimageprivate.h"

#include "gdk/gdkdisplayprivate.h"
#include "gdk/gdkglcontextprivate.h"

struct _GskGLDevice
{
  GskGpuDevice parent_instance;
};

struct _GskGLDeviceClass
{
  GskGpuDeviceClass parent_class;
};

G_DEFINE_TYPE (GskGLDevice, gsk_gl_device, GSK_TYPE_GPU_DEVICE)

static GskGpuImage *
gsk_gl_device_create_offscreen_image (GskGpuDevice   *device,
                                      GdkMemoryDepth  depth,
                                      gsize           width,
                                      gsize           height)
{
  GskGLDevice *self = GSK_GL_DEVICE (device);

  return gsk_gl_image_new (self,
                           GDK_MEMORY_R8G8B8A8_PREMULTIPLIED,
                           width,
                           height);
}

static GskGpuImage *
gsk_gl_device_create_upload_image (GskGpuDevice    *device,
                                   GdkMemoryFormat  format,
                                   gsize            width,
                                   gsize            height)
{
  GskGLDevice *self = GSK_GL_DEVICE (device);

  return gsk_gl_image_new (self,
                           format,
                           width,
                           height);
}

static void
gsk_gl_device_finalize (GObject *object)
{
  GskGLDevice *self = GSK_GL_DEVICE (object);
  GskGpuDevice *device = GSK_GPU_DEVICE (self);

  g_object_steal_data (G_OBJECT (gsk_gpu_device_get_display (device)), "-gsk-gl-device");

  G_OBJECT_CLASS (gsk_gl_device_parent_class)->finalize (object);
}

static void
gsk_gl_device_class_init (GskGLDeviceClass *klass)
{
  GskGpuDeviceClass *gpu_device_class = GSK_GPU_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gpu_device_class->create_offscreen_image = gsk_gl_device_create_offscreen_image;
  gpu_device_class->create_upload_image = gsk_gl_device_create_upload_image;

  object_class->finalize = gsk_gl_device_finalize;
}

static void
gsk_gl_device_init (GskGLDevice *self)
{
}

GskGpuDevice *
gsk_gl_device_get_for_display (GdkDisplay  *display,
                               GError     **error)
{
  GskGLDevice *self;

  self = g_object_get_data (G_OBJECT (display), "-gsk-gl-device");
  if (self)
    return GSK_GPU_DEVICE (g_object_ref (self));

  if (!gdk_display_prepare_gl (display, error))
    return NULL;

  self = g_object_new (GSK_TYPE_GL_DEVICE, NULL);
  gsk_gpu_device_setup (GSK_GPU_DEVICE (self), display);

  g_object_set_data (G_OBJECT (display), "-gsk-gl-device", self);

  return GSK_GPU_DEVICE (self);
}

