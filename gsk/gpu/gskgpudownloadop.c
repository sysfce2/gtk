#include "config.h"

#include "gskgpudownloadopprivate.h"

#include "gskgpuframeprivate.h"
#include "gskglimageprivate.h"
#include "gskgpuimageprivate.h"
#include "gskgpuprintprivate.h"
#ifdef GDK_RENDERING_VULKAN
#include "gskvulkanbufferprivate.h"
#include "gskvulkanimageprivate.h"
#endif

#include "gdk/gdkglcontextprivate.h"

typedef struct _GskGpuDownloadOp GskGpuDownloadOp;

typedef void (* GdkGpuDownloadOpCreateFunc) (GskGpuDownloadOp *);

struct _GskGpuDownloadOp
{
  GskGpuOp op;

  GskGpuImage *image;
  GdkGpuDownloadOpCreateFunc create_func;
  GskGpuDownloadFunc func;
  gpointer user_data;

  GdkTexture *texture;
  GskGpuBuffer *buffer;
};

static void
gsk_gpu_download_op_finish (GskGpuOp *op)
{
  GskGpuDownloadOp *self = (GskGpuDownloadOp *) op;

  if (self->create_func)
    self->create_func (self);

  self->func (self->user_data, self->texture);

  g_object_unref (self->texture);
  g_object_unref (self->image);
  g_clear_object (&self->buffer);
}

static void
gsk_gpu_download_op_print (GskGpuOp    *op,
                           GskGpuFrame *frame,
                           GString     *string,
                           guint        indent)
{
  GskGpuDownloadOp *self = (GskGpuDownloadOp *) op;

  gsk_gpu_print_op (string, indent, "download");
  gsk_gpu_print_image (string, self->image);
  gsk_gpu_print_newline (string);
}

#ifdef GDK_RENDERING_VULKAN
static void
gsk_gpu_download_op_vk_create (GskGpuDownloadOp *self)
{
  GBytes *bytes;
  guchar *data;
  gsize width, height, stride;
  GdkMemoryFormat format;

  data = gsk_gpu_buffer_map (self->buffer);
  width = gsk_gpu_image_get_width (self->image);
  height = gsk_gpu_image_get_height (self->image);
  format = gsk_gpu_image_get_format (self->image);
  stride = width * gdk_memory_format_bytes_per_pixel (format);
  bytes = g_bytes_new (data, stride * height);
  self->texture = gdk_memory_texture_new (width,
                                          height,
                                          format,
                                          bytes,
                                          stride);
  g_bytes_unref (bytes);
  gsk_gpu_buffer_unmap (self->buffer);
}

static GskGpuOp *
gsk_gpu_download_op_vk_command_with_area (GskGpuOp                    *op,
                                          GskGpuFrame                 *frame,
                                          VkCommandBuffer              command_buffer,
                                          GskVulkanImage              *image,
                                          const cairo_rectangle_int_t *area,
                                          GskGpuBuffer               **buffer)
{
  GskGpuDownloadOp *self = (GskGpuDownloadOp *) op;
  gsize stride;

  stride = area->width * gdk_memory_format_bytes_per_pixel (gsk_gpu_image_get_format (GSK_GPU_IMAGE (image)));
  *buffer = gsk_vulkan_buffer_new_read (GSK_VULKAN_DEVICE (gsk_gpu_frame_get_device (frame)),
                                        area->height * stride);

  gsk_vulkan_image_transition (image,
                               command_buffer,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               VK_ACCESS_TRANSFER_READ_BIT);

  vkCmdCopyImageToBuffer (command_buffer,
                          gsk_vulkan_image_get_vk_image (image),
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          gsk_vulkan_buffer_get_vk_buffer (GSK_VULKAN_BUFFER (*buffer)),
                          1,
                          (VkBufferImageCopy[1]) {
                               {
                                   .bufferOffset = 0,
                                   .bufferRowLength = area->width,
                                   .bufferImageHeight = area->height,
                                   .imageSubresource = {
                                       .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                       .mipLevel = 0,
                                       .baseArrayLayer = 0,
                                       .layerCount = 1
                                   },
                                   .imageOffset = {
                                       .x = area->x,
                                       .y = area->y,
                                       .z = 0
                                   },
                                   .imageExtent = {
                                       .width = area->width,
                                       .height = area->height,
                                       .depth = 1
                                   }
                               }
                          });

  vkCmdPipelineBarrier (command_buffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT,
                        0,
                        0, NULL,
                        1, &(VkBufferMemoryBarrier) {
                            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .buffer = gsk_vulkan_buffer_get_vk_buffer (GSK_VULKAN_BUFFER (*buffer)),
                            .offset = 0,
                            .size = VK_WHOLE_SIZE,
                        },
                        0, NULL);

  self->create_func = gsk_gpu_download_op_vk_create;

  return op->next;
}

static GskGpuOp *
gsk_gpu_download_op_vk_command (GskGpuOp         *op,
                                GskGpuFrame      *frame,
                                VkRenderPass      render_pass,
                                VkFormat          format,
                                VkCommandBuffer   command_buffer)
{
  GskGpuDownloadOp *self = (GskGpuDownloadOp *) op;

  return gsk_gpu_download_op_vk_command_with_area (op,
                                                   frame,
                                                   command_buffer,
                                                   GSK_VULKAN_IMAGE (self->image),
                                                   &(cairo_rectangle_int_t) {
                                                       0, 0,
                                                       gsk_gpu_image_get_width (self->image),
                                                       gsk_gpu_image_get_height (self->image)
                                                   },
                                                   &self->buffer);
}
#endif

typedef struct _GskGLTextureData GskGLTextureData;

struct _GskGLTextureData
{
  GdkGLContext *context;
  GLuint texture_id;
  GLsync sync;
};

static void
gsk_gl_texture_data_free (gpointer user_data)
{
  GskGLTextureData *data = user_data;

  gdk_gl_context_make_current (data->context);

  g_clear_pointer (&data->sync, glDeleteSync);
  glDeleteTextures (1, &data->texture_id);
  g_object_unref (data->context);

  g_free (data);
}

static GskGpuOp *
gsk_gpu_download_op_gl_command (GskGpuOp    *op,
                                GskGpuFrame *frame,
                                gsize        flip_y)
{
  GskGpuDownloadOp *self = (GskGpuDownloadOp *) op;
  GdkGLTextureBuilder *builder;
  GskGLTextureData *data;
  GdkGLContext *context;

  context = GDK_GL_CONTEXT (gsk_gpu_frame_get_context (frame));

  data = g_new (GskGLTextureData, 1);
  data->context = g_object_ref (context);
  data->texture_id = gsk_gl_image_steal_texture (GSK_GL_IMAGE (self->image));

  if (gdk_gl_context_has_sync (context))
    data->sync = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

  builder = gdk_gl_texture_builder_new ();
  gdk_gl_texture_builder_set_context (builder, context);
  gdk_gl_texture_builder_set_id (builder, data->texture_id);
  gdk_gl_texture_builder_set_width (builder, gsk_gpu_image_get_width (self->image));
  gdk_gl_texture_builder_set_height (builder, gsk_gpu_image_get_height (self->image));
  gdk_gl_texture_builder_set_sync (builder, data->sync);

  self->texture = gdk_gl_texture_builder_build (builder,
                                                gsk_gl_texture_data_free,
                                                data);

  return op->next;
}

static const GskGpuOpClass GSK_GPU_DOWNLOAD_OP_CLASS = {
  GSK_GPU_OP_SIZE (GskGpuDownloadOp),
  GSK_GPU_STAGE_COMMAND,
  gsk_gpu_download_op_finish,
  gsk_gpu_download_op_print,
#ifdef GDK_RENDERING_VULKAN
  gsk_gpu_download_op_vk_command,
#endif
  gsk_gpu_download_op_gl_command
};

void
gsk_gpu_download_op (GskGpuFrame        *frame,
                     GskGpuImage        *image,
                     GskGpuDownloadFunc  func,
                     gpointer            user_data)
{
  GskGpuDownloadOp *self;

  self = (GskGpuDownloadOp *) gsk_gpu_op_alloc (frame, &GSK_GPU_DOWNLOAD_OP_CLASS);

  self->image = g_object_ref (image);
  self->func = func,
  self->user_data = user_data;
}

static void
gsk_gpu_download_save_png_cb (gpointer    filename,
                              GdkTexture *texture)
{
  gdk_texture_save_to_png (texture, filename);

  g_free (filename);
}

void
gsk_gpu_download_png_op (GskGpuFrame *frame,
                         GskGpuImage *image,
                         const char  *filename_format,
                         ...)
{
  va_list args;
  char *filename;

  va_start (args, filename_format);
  filename = g_strdup_vprintf (filename_format, args);
  va_end (args);

  gsk_gpu_download_op (frame,
                       image,
                       gsk_gpu_download_save_png_cb,
                       filename);
}
