/* gdkdmabufformats.c
 *
 * Copyright 2023 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <glib-object.h>
#include "gdkdmabufformatsprivate.h"


/**
 * GdkDmabufFormats:
 *
 * The `GdkDmabufFormats struct provides information about
 * supported DMA buffer formats.
 *
 * You can query whether a given format is supported with
 * [method@Gdk.DmabufFormats.contains] and you can iterate
 * over the list of all supported formats with
 * [method@Gdk.DmabufFormats.get_n_formats] and
 * [method@Gdk.DmabufFormats.get_format].
 *
 * The list of supported formats is sorted by preference,
 * with the best formats coming first.
 *
 * The list may contains (format, modfier) pairs where the modifier
 * is `DMA_FORMAT_MOD_INVALID`, indicating that **_implicit modifiers_**
 * may be used with this format.
 *
 * See [class@Gdk.DmabufTextureBuilder] for more information
 * about DMA buffers.
 *
 * Note that DMA buffers only exist on Linux.
 *
 * Since: 4.14
 */

struct _GdkDmabufFormats
{
  int ref_count;

  gsize n_formats;
  GdkDmabufFormat *formats;
};

G_DEFINE_BOXED_TYPE (GdkDmabufFormats, gdk_dmabuf_formats, gdk_dmabuf_formats_ref, gdk_dmabuf_formats_unref)

/**
 * gdk_dmabuf_formats_ref:
 * @formats: a `GdkDmabufFormats`
 *
 * Increases the reference count of @formats.
 *
 * Returns: the passed-in object
 *
 * Since: 4.14
 */
GdkDmabufFormats *
gdk_dmabuf_formats_ref (GdkDmabufFormats *formats)
{
  formats->ref_count++;

  return formats;
}

/**
 * gdk_dmabuf_formats_unref:
 * @formats: a `GdkDmabufFormats`
 *
 * Decreases the reference count of @formats.
 *
 * When the reference count reaches zero,
 * the object is freed.
 *
 * Since: 4.14
 */
void
gdk_dmabuf_formats_unref (GdkDmabufFormats *formats)
{
  formats->ref_count--;

  if (formats->ref_count > 0)
    return;

   g_free (formats->formats);
   g_free (formats);
}

/**
 * gdk_dmabuf_formats_get_n_formats:
 * @formats: a `GdkDmabufFormats`
 *
 * Returns the number of formats that the @formats object
 * contains.
 *
 * Note that DMA buffers are a Linux concept, so on other
 * platforms, [method@Gdk.DmabufFormats.get_n_formats] will
 * always return zero.
 *
 * Returns: the number of formats
 *
 * Since: 4.14
 */
gsize
gdk_dmabuf_formats_get_n_formats (GdkDmabufFormats *formats)
{
  return formats->n_formats;
}

/**
 * gdk_dmabuf_formats_get_format:
 * @formats: a `GdkDmabufFormats`
 * @idx: the index of the format to return
 * @fourcc: (out): return location for the format code
 * @modifier: (out): return location for the format modifier
 *
 * Gets the fourcc code and modifier for a format
 * that is contained in @formats.
 *
 * Since: 4.14
 */
void
gdk_dmabuf_formats_get_format (GdkDmabufFormats *formats,
                               gsize             idx,
                               guint32          *fourcc,
                               guint64          *modifier)
{
  GdkDmabufFormat *format;

  g_return_if_fail (idx < formats->n_formats);
  g_return_if_fail (fourcc != NULL);
  g_return_if_fail (modifier != NULL);

  format = &formats->formats[idx];

  *fourcc = format->fourcc;
  *modifier = format->modifier;
}

/**
 * gdk_dmabuf_formats_next_priority:
 * @formats: a `GdkDmabufFormats`
 * @idx: the index of the format to query
*
 * Returns the index of the next-lower-priority format.
 *
 * The formats in a `GdkDmabufFormats` are sorted by decreasing
 * priority. This function lets you identify formats with the
 * same priority: all the formats between @idx and the return
 * value of this function have the same priority.
 *
 * Returns: the index of the next lower priority format
 *
 * Since: 4.14
 */
gsize
gdk_dmabuf_formats_next_priority (GdkDmabufFormats *formats,
                                  gsize             idx)
{
  GdkDmabufFormat *format;

  g_return_val_if_fail (idx < formats->n_formats, G_MAXSIZE);

  format = &formats->formats[idx];

  return format->next_priority;
}

/**
 * gdk_dmabuf_format_contains:
 * @formats: a `GdkDmabufFormats`
 * @fourcc: a format code
 * @modfier: a format modifier
 *
 * Returns whether a given format is contained in @formats.
 *
 * Returns: `TRUE` if the format specified by the arguments
 *   is part of @formats
 *
 * Since: 4.14
 */
gboolean
gdk_dmabuf_formats_contains (GdkDmabufFormats *formats,
                             guint32           fourcc,
                             guint64           modifier)
{
  for (gsize i = 0; i < formats->n_formats; i++)
    {
      GdkDmabufFormat *format = &formats->formats[i];

      if (format->fourcc == fourcc && format->modifier == modifier)
        return TRUE;
    }

  return FALSE;
}

/*< private >
 * gdk_dmabuf_formats_new:
 * @formats: the formats
 * @n_formats: the length of @formats
 *
 * Creates a new `GdkDmabufFormats struct for
 * the given formats.
 *
 * The @formats array is expected to be sorted
 * by preference.
 *
 * Returns: (transfer full): the new `GdkDmabufFormats`
 *
 * Since: 4.14
 */
GdkDmabufFormats *
gdk_dmabuf_formats_new (GdkDmabufFormat *formats,
                        gsize            n_formats)
{
  GdkDmabufFormats *self;

  self = g_new0 (GdkDmabufFormats, 1);

  self->ref_count = 1;
  self->n_formats = n_formats;
  self->formats = g_new (GdkDmabufFormat, n_formats);

  memcpy (self->formats, formats, n_formats * sizeof (GdkDmabufFormat));

  return self;
}

const GdkDmabufFormat *
gdk_dmabuf_formats_peek_formats (GdkDmabufFormats *self)
{
  return self->formats;
}
