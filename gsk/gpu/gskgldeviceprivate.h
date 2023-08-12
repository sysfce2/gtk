#pragma once

#include "gskgpudeviceprivate.h"

G_BEGIN_DECLS

#define GSK_TYPE_GL_DEVICE (gsk_gl_device_get_type ())

G_DECLARE_FINAL_TYPE (GskGLDevice, gsk_gl_device, GSK, GL_DEVICE, GskGpuDevice)

GskGpuDevice *          gsk_gl_device_get_for_display                   (GdkDisplay             *display,
                                                                         GError                **error);


G_END_DECLS
