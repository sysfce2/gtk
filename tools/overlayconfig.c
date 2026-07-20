/*  Copyright 2026 Benjamin Otte
 *
 * GTK is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * GTK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GTK; see the file COPYING.  If not,
 * see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "overlayconfig.h"

#include <glib/gi18n-lib.h>

void
overlay_config_init (OverlayConfig *self)
{
  self->include_original = TRUE;
  self->color = (GdkRGBA) { 1, 0, 0, 0.5 };
}

static gboolean
overlay_config_parse_color (const char  *option_name,
                            const char  *value,
                            gpointer     data,
                            GError     **error)
{
  OverlayConfig *self = data;

  if (!gdk_rgba_parse (&self->color, value))
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE, _("Not a valid color"));
      return FALSE;
    }

  return TRUE;
}

GOptionGroup *
overlay_config_create_option_group (OverlayConfig *self)
{
  const GOptionEntry entries[] = {
    { "only", 'o', G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &self->include_original, N_("Only draw the opaque region, don't include the original image"), NULL },
    { "color", 'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, overlay_config_parse_color, N_("Color for the opaque region"), "COLOR" },
    { NULL, }
  };
  GOptionGroup *group;

  group = g_option_group_new ("overlay",
                              _("Overlay options"),
                              _("How to render the overlay"),
                              self,
                              NULL);
  g_option_group_set_translation_domain (group, GETTEXT_PACKAGE);
  g_option_group_add_entries (group, entries);

  return group;
}

GskRenderNode *
overlay_config_render_rect (const OverlayConfig   *self,
                            GskRenderNode         *node,
                            const graphene_rect_t *rect)
{
  GtkSnapshot *snapshot;

  snapshot = gtk_snapshot_new ();
  if (self->include_original)
    gtk_snapshot_append_node (snapshot, node);
  gtk_snapshot_append_color (snapshot,
                             &self->color,
                             rect);

  return gtk_snapshot_free_to_node (snapshot);
}
