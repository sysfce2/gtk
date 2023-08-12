#include "config.h"

#include "gskvulkandownloadopprivate.h"

#include "gskvulkanprivate.h"

#include "gdk/gdkmemoryformatprivate.h"

static gsize
gsk_vk_old_download_op_count_vertex_data (GskVkOldOp *op,
                                          gsize        n_bytes)
{
  return n_bytes;
}

static void
gsk_vk_old_download_op_collect_vertex_data (GskVkOldOp *op,
                                            guchar      *data)
{
}

static void
gsk_vk_old_download_op_reserve_descriptor_sets (GskVkOldOp     *op,
                                                GskVkOldRender *render)
{
}

static GskVkOldOp *
gsk_vk_old_download_op_command_with_area (GskVkOldOp                 *op,
                                          GskVkOldRender             *render,
                                          VkCommandBuffer              command_buffer,
                                          GskVkOldImage              *image,
                                          const cairo_rectangle_int_t *area,
                                          GskVkOldBuffer            **buffer)
{
  gsize stride;

  stride = area->width * gdk_memory_format_bytes_per_pixel (gsk_vk_old_image_get_format (image));
  *buffer = gsk_vk_old_buffer_new_map (gsk_vk_old_render_get_context (render),
                                       area->height * stride,
                                       GSK_VK_OLD_READ);

  gsk_vk_old_image_transition (image,
                               command_buffer,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               VK_ACCESS_TRANSFER_READ_BIT);

  vkCmdCopyImageToBuffer (command_buffer,
                          gsk_vk_old_image_get_vk_image (image),
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          gsk_vk_old_buffer_get_buffer (*buffer),
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
                            .buffer = gsk_vk_old_buffer_get_buffer (*buffer),
                            .offset = 0,
                            .size = VK_WHOLE_SIZE,
                        },
                        0, NULL);

  return op->next;
}

typedef struct _GskVkOldDownloadOp GskVkOldDownloadOp;

struct _GskVkOldDownloadOp
{
  GskVkOldOp op;

  GskVkOldImage *image;
  GskVkOldDownloadFunc func;
  gpointer user_data;

  GskVkOldBuffer *buffer;
};

static void
gsk_vk_old_download_op_finish (GskVkOldOp *op)
{
  GskVkOldDownloadOp *self = (GskVkOldDownloadOp *) op;
  guchar *data;
  gsize stride;

  data = gsk_vk_old_buffer_map (self->buffer);
  stride = gsk_vk_old_image_get_width (self->image) *
           gdk_memory_format_bytes_per_pixel (gsk_vk_old_image_get_format (self->image));
  self->func (self->user_data, 
              gsk_vk_old_image_get_format (self->image),
              data,
              gsk_vk_old_image_get_width (self->image),
              gsk_vk_old_image_get_height (self->image),
              stride);
  gsk_vk_old_buffer_unmap (self->buffer);

  g_object_unref (self->image);
  g_clear_pointer (&self->buffer, gsk_vk_old_buffer_free);
}

static void
gsk_vk_old_download_op_print (GskVkOldOp *op,
                              GString     *string,
                              guint        indent)
{
  GskVkOldDownloadOp *self = (GskVkOldDownloadOp *) op;

  print_indent (string, indent);
  g_string_append (string, "download ");
  print_image (string, self->image);
  print_newline (string);
}

static GskVkOldOp *
gsk_vk_old_download_op_command (GskVkOldOp      *op,
                                GskVkOldRender  *render,
                                VkRenderPass      render_pass,
                                VkCommandBuffer   command_buffer)
{
  GskVkOldDownloadOp *self = (GskVkOldDownloadOp *) op;

  return gsk_vk_old_download_op_command_with_area (op,
                                                   render,
                                                   command_buffer,
                                                   self->image,
                                                   &(cairo_rectangle_int_t) {
                                                       0, 0,
                                                       gsk_vk_old_image_get_width (self->image),
                                                       gsk_vk_old_image_get_height (self->image)
                                                   },
                                                   &self->buffer);
}

static const GskVkOldOpClass GSK_VK_OLD_DOWNLOAD_OP_CLASS = {
  GSK_VK_OLD_OP_SIZE (GskVkOldDownloadOp),
  GSK_VK_OLD_STAGE_COMMAND,
  gsk_vk_old_download_op_finish,
  gsk_vk_old_download_op_print,
  gsk_vk_old_download_op_count_vertex_data,
  gsk_vk_old_download_op_collect_vertex_data,
  gsk_vk_old_download_op_reserve_descriptor_sets,
  gsk_vk_old_download_op_command
};

void
gsk_vk_old_download_op (GskVkOldRender       *render,
                        GskVkOldImage        *image,
                        GskVkOldDownloadFunc  func,
                        gpointer               user_data)
{
  GskVkOldDownloadOp *self;

  self = (GskVkOldDownloadOp *) gsk_vk_old_op_alloc (render, &GSK_VK_OLD_DOWNLOAD_OP_CLASS);

  self->image = g_object_ref (image);
  self->func = func,
  self->user_data = user_data;
}

static void
gsk_vk_old_download_save_png_cb (gpointer         filename,
                                 GdkMemoryFormat  format,
                                 const guchar    *data,
                                 int              width,
                                 int              height,
                                 gsize            stride)
{
  GdkTexture *texture;
  GBytes *bytes;

  bytes = g_bytes_new_static (data, stride * height);
  texture = gdk_memory_texture_new (width, height,
                                    format,
                                    bytes,
                                    stride);
  gdk_texture_save_to_png (texture, filename);

  g_object_unref (texture);
  g_bytes_unref (bytes);
  g_free (filename);
}

void
gsk_vk_old_download_png_op (GskVkOldRender *render,
                            GskVkOldImage  *image,
                            const char      *filename_format,
                            ...)
{
  va_list args;
  char *filename;

  va_start (args, filename_format);
  filename = g_strdup_vprintf (filename_format, args);
  va_end (args);

  gsk_vk_old_download_op (render,
                          image,
                          gsk_vk_old_download_save_png_cb,
                          filename);
}
