#include "config.h"

#include "gdkdmabuf-wayland-private.h"
#include "gdkwaylanddmabufformats.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>

#include "gdkdebugprivate.h"
#include "gdkdmabufformatsbuilderprivate.h"
#include "gdkdmabufformatsprivate.h"

#include "linux-dmabuf-unstable-v1-client-protocol.h"


static DmabufTranche *
dmabuf_tranche_new (void)
{
  return g_new0 (DmabufTranche, 1);
}

static void
dmabuf_tranche_free (DmabufTranche *tranche)
{
  g_free (tranche->formats);
  g_free (tranche);
}

static DmabufFormats *
dmabuf_formats_new (void)
{
  DmabufFormats *formats;

  formats = g_new0 (DmabufFormats, 1);
  formats->tranches = g_ptr_array_new_with_free_func ((GDestroyNotify) dmabuf_tranche_free);

  return formats;
}

static void
dmabuf_formats_free (DmabufFormats *formats)
{
  g_ptr_array_unref (formats->tranches);
  g_free (formats);
}

static gboolean
is_in_tranche (GdkDmabufFormats *formats,
               gsize             idx,
               guint32           fourcc,
               guint64           modifier)
{
  gsize end;
  guint32 f;
  guint64 m;

  end = gdk_dmabuf_formats_next_priority (formats, idx);
  for (gsize i = idx; i < end; i++)
    {
      gdk_dmabuf_formats_get_format (formats, i, &f, &m);
      if (f == fourcc && m == modifier)
        return TRUE;
    }

  return FALSE;
}

static void
update_dmabuf_formats (DmabufFormatsInfo *info)
{
  GdkDmabufFormatsBuilder *builder;
  GdkDmabufFormats *egl_formats = info->egl_formats;
  DmabufFormats *formats = info->dmabuf_formats;

  builder = gdk_dmabuf_formats_builder_new ();

  for (gsize i = 0; i < formats->tranches->len; i++)
    {
      DmabufTranche *tranche = g_ptr_array_index (formats->tranches, i);

      for (gsize k = 0; k < gdk_dmabuf_formats_get_n_formats (egl_formats); k = gdk_dmabuf_formats_next_priority (egl_formats, k))
        {
          for (gsize j = 0; j < tranche->n_formats; j++)
            {
              if (is_in_tranche (egl_formats, k,
                                 tranche->formats[j].fourcc,
                                 tranche->formats[j].modifier))
                gdk_dmabuf_formats_builder_add_format_for_device (builder,
                                                                  tranche->formats[j].fourcc,
                                                                  tranche->flags,
                                                                  tranche->formats[j].modifier,
                                                                  tranche->target_device);
            }
          gdk_dmabuf_formats_builder_next_priority (builder);
        }
    }

  g_clear_pointer (&info->formats, gdk_dmabuf_formats_unref);
  info->formats = gdk_dmabuf_formats_builder_free_to_formats_for_device (builder, formats->main_device);

  info->callback (info->data, info);
}

static void
linux_dmabuf_done (void *data,
                   struct zwp_linux_dmabuf_feedback_v1 *feedback)
{
  DmabufFormatsInfo *info = data;
  DmabufFormats *formats;

  g_clear_pointer (&info->dmabuf_formats, dmabuf_formats_free);

  info->dmabuf_formats = info->pending_dmabuf_formats;
  info->pending_dmabuf_formats = NULL;

  formats = info->dmabuf_formats;

  GDK_DEBUG (DMABUF, "Wayland dmabuf %s feedback:", info->name);
  GDK_DEBUG (DMABUF, "main device: %u %u", major (formats->main_device), minor (formats->main_device));
  for (int i = 0; i < formats->tranches->len; i++)
    {
      DmabufTranche *tranche = g_ptr_array_index (formats->tranches, i);
      GDK_DEBUG (DMABUF, "tranche target device: %u %u", major (tranche->target_device), minor (tranche->target_device));
      GDK_DEBUG (DMABUF,
                 "tranche flags: %s",
                 tranche->flags & ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT ? "scanout" : "");
      GDK_DEBUG (DMABUF, "tranche formats (%lu entries):", tranche->n_formats);
      for (int j = 0; j < tranche->n_formats; j++)
        {
          DmabufFormat *format = &tranche->formats[j];
          GDK_DEBUG (DMABUF, "  %.4s:%#lx", (char *) &format->fourcc, format->modifier);
        }
      GDK_DEBUG (DMABUF, "---");
    }

  update_dmabuf_formats (info);
}

static void
linux_dmabuf_format_table (void *data,
                           struct zwp_linux_dmabuf_feedback_v1 *feedback,
                           int32_t fd,
                           uint32_t size)
{
  DmabufFormatsInfo *info = data;

  if (info->dmabuf_formats)
    munmap (info->dmabuf_formats, sizeof (DmabufFormat) * info->n_dmabuf_formats);

  info->n_dmabuf_formats = size / 16;
  info->dmabuf_format_table = mmap (NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
}

static void
linux_dmabuf_main_device (void *data,
                          struct zwp_linux_dmabuf_feedback_v1 *feedback,
                          struct wl_array *device)
{
  DmabufFormatsInfo *info = data;
  dev_t dev = *(dev_t *)device->data;

  g_assert (info->pending_dmabuf_formats == NULL);

  info->pending_dmabuf_formats = dmabuf_formats_new ();
  info->pending_dmabuf_formats->main_device = dev;
}

static void
linux_dmabuf_tranche_done (void *data,
                           struct zwp_linux_dmabuf_feedback_v1 *feedback)
{
  DmabufFormatsInfo *info = data;

  g_ptr_array_add (info->pending_dmabuf_formats->tranches,
                   info->pending_tranche);

  info->pending_tranche = NULL;
}

static void
linux_dmabuf_tranche_target_device (void *data,
                                    struct zwp_linux_dmabuf_feedback_v1 *feedback,
                                    struct wl_array *device)
{
  DmabufFormatsInfo *info = data;
  dev_t dev = *(dev_t *)device->data;
  DmabufTranche *tranche;

  g_assert (info->pending_tranche == NULL);

  tranche = dmabuf_tranche_new ();
  tranche->target_device = dev;

  info->pending_tranche = tranche;
}

static void
linux_dmabuf_tranche_formats (void *data,
                              struct zwp_linux_dmabuf_feedback_v1 *feedback,
                              struct wl_array *indices)
{
  DmabufFormatsInfo *info = data;
  DmabufTranche *tranche;
  int i;
  guint16 *pos;

  g_assert (info->pending_tranche != NULL);
  tranche = info->pending_tranche;

  tranche->n_formats = indices->size / sizeof (guint16);
  tranche->formats = g_new (DmabufFormat, tranche->n_formats);

  i = 0;
  wl_array_for_each (pos, indices)
    {
      tranche->formats[i++] = info->dmabuf_format_table[*pos];
    }
}

static void
linux_dmabuf_tranche_flags (void *data,
                            struct zwp_linux_dmabuf_feedback_v1 *feedback,
                            uint32_t flags)
{
  DmabufFormatsInfo *info = data;
  DmabufTranche *tranche;

  g_assert (info->pending_tranche != NULL);
  tranche = info->pending_tranche;
  tranche->flags = flags;
}

static const struct zwp_linux_dmabuf_feedback_v1_listener feedback_listener = {
  linux_dmabuf_done,
  linux_dmabuf_format_table,
  linux_dmabuf_main_device,
  linux_dmabuf_tranche_done,
  linux_dmabuf_tranche_target_device,
  linux_dmabuf_tranche_formats,
  linux_dmabuf_tranche_flags,
};

DmabufFormatsInfo *
dmabuf_formats_info_new (const char                          *name,
                         GdkDmabufFormats                    *egl_formats,
                         struct zwp_linux_dmabuf_feedback_v1 *feedback,
                         DmabufFormatsUpdateCallback          callback,
                         gpointer                             data)
{
  DmabufFormatsInfo *info;

  info = g_new0 (DmabufFormatsInfo, 1);

  info->name = g_strdup (name);
  info->egl_formats = gdk_dmabuf_formats_ref (egl_formats);
  info->formats = gdk_dmabuf_formats_ref (egl_formats);
  info->feedback = feedback;

  info->callback = callback;
  info->data = data;

  if (info->feedback)
    zwp_linux_dmabuf_feedback_v1_add_listener (info->feedback,
                                               &feedback_listener, info);
  else
    info->callback (info->data, info);

  return info;
}

void
dmabuf_formats_info_free (DmabufFormatsInfo *info)
{
  g_free (info->name);
  g_clear_pointer (&info->formats, gdk_dmabuf_formats_unref);
  g_clear_pointer (&info->egl_formats, gdk_dmabuf_formats_unref);
  g_clear_pointer (&info->feedback, zwp_linux_dmabuf_feedback_v1_destroy);
  if (info->dmabuf_format_table)
    {
      munmap (info->dmabuf_format_table, info->n_dmabuf_formats * 16);
      info->dmabuf_format_table = NULL;
    }
  g_clear_pointer (&info->dmabuf_formats, dmabuf_formats_free);
  g_clear_pointer (&info->pending_dmabuf_formats, dmabuf_formats_free);
  g_clear_pointer (&info->pending_tranche, dmabuf_tranche_free);

  g_free (info);
}

/**
 * gdk_wayland_dmabuf_formats_get_main_device:
 * @formats: a `GdkDmabufFormats`
 *
 * Returns the DRM device that the compositor uses for compositing.
 *
 * If this information isn't available (e.g. because @formats wasn't
 * obtained form the compositor), then 0 is returned.
 *
 * Returns: the main DRM device that the compositor prefers
 *
 * Since: 4.14
 */
dev_t
gdk_wayland_dmabuf_formats_get_main_device (GdkDmabufFormats *formats)
{
  return formats->device;
}

/**
 * gdk_wayland_dmabuf_formats_get_target_device:
 * @formats: a `GdkDmabufFormats`
 * @idx: the index of the format to return
 *
 * Returns the target DRM device that should be used for creating buffers
 * with this format.
 *
 * If this information isn't available (e.g. because @formats wasn't
 * obtained form the compositor), then 0 is returned.
 *
 * Returns: the target DRM device for this format
 *
 * Since: 4.14
 */
dev_t
gdk_wayland_dmabuf_formats_get_target_device (GdkDmabufFormats *formats,
                                              gsize             idx)
{
  GdkDmabufFormat *format;

  g_return_val_if_fail (idx < formats->n_formats, 0);

  format = &formats->formats[idx];

  return format->device;
}

/**
 * gdk_wayland_dmabuf_formats_is_scanout:
 * @formats: a `GdkDmabufFormats`
 * @idx: the index of the format to return
 *
 * Returns whether the compositor intents to use buffers with this
 * format for scanout.
 *
 * If this information isn't available (e.g. because @formats wasn't
 * obtained form the compositor), then 0 is returned.
 *
 * Returns: whether the format will be used for scanout
 *
 * Since: 4.14
 */
gboolean
gdk_wayland_dmabuf_formats_is_scanout (GdkDmabufFormats *formats,
                                       gsize             idx)
{
  GdkDmabufFormat *format;

  g_return_val_if_fail (idx < formats->n_formats, 0);

  format = &formats->formats[idx];

  return format->flags & ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT;
}
