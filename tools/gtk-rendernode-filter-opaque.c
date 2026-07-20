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

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include "gtk-rendernode-tool.h"
#include "gtk-tool-utils.h"

typedef struct _OverlayConfig OverlayConfig;

struct _OverlayConfig
{
  gboolean include_original;
  GdkRGBA color;
};

static void
overlay_config_init (OverlayConfig *config)
{
  config->include_original = TRUE;
  config->color = (GdkRGBA) { 1, 0, 0, 0.5 };
}

static gboolean
overlay_config_parse_color (const char  *option_name,
                            const char  *value,
                            gpointer     data,
                            GError     **error)
{
  OverlayConfig *config = data;

  if (!gdk_rgba_parse (&config->color, value))
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE, _("Not a valid color"));
      return FALSE;
    }

  return TRUE;
}

static GOptionGroup *
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

static GskRenderNode *
render_rect (GskRenderNode         *node,
             const OverlayConfig   *config,
             const graphene_rect_t *rect)
{
  GtkSnapshot *snapshot;

  snapshot = gtk_snapshot_new ();
  if (config->include_original)
    gtk_snapshot_append_node (snapshot, node);
  gtk_snapshot_append_color (snapshot,
                             &config->color,
                             rect);

  return gtk_snapshot_free_to_node (snapshot);
}

GskRenderNode *
filter_opaque (GskRenderNode  *node,
               int             argc,
               const char    **argv)
{
  const GOptionEntry entries[] = {
    { NULL, }
  };
  OverlayConfig config;
  GOptionContext *context;
  GError *error = NULL;
  GskRenderNode *result;
  graphene_rect_t opaque;

  overlay_config_init (&config);

  context = g_option_context_new (NULL);
  g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
  g_option_context_set_main_group (context, overlay_config_create_option_group (&config));
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_set_summary (context, _("Show the opaque part"));

  if (!g_option_context_parse (context, &argc, (char ***) &argv, &error))
    {
      g_printerr ("opaque: %s\n", error->message);
      g_error_free (error);
      exit (1);
    }

  if (argc != 1)
    {
      g_printerr ("opaque: Unexpected arguments\n");
      exit (1);
    }
  
  if (gsk_render_node_get_opaque_rect (node, &opaque))
    {
      result = render_rect (node, &config, &opaque);
      gsk_render_node_unref (node);
    }
  else
    result = g_steal_pointer (&node);

  return result;
}
