#pragma once

#include "gskgpudeviceprivate.h"

G_BEGIN_DECLS

#define GSK_TYPE_GL_DEVICE (gsk_gl_device_get_type ())

G_DECLARE_FINAL_TYPE (GskGLDevice, gsk_gl_device, GSK, GL_DEVICE, GskGpuDevice)

GskGpuDevice *          gsk_gl_device_get_for_display                   (GdkDisplay             *display,
                                                                         GError                **error);

void                    gsk_gl_device_use_program                       (GskGLDevice            *self,
                                                                         const GskGpuShaderOpClass *op_class,
                                                                         GskGpuShaderClip        clip);

GLuint                  gsk_gl_device_get_sampler_id                    (GskGLDevice            *self,
                                                                         GskGpuSampler           sampler);

G_END_DECLS
