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

static GskRenderNode *
render_rect (GskRenderNode         *node,
             const graphene_rect_t *rect)
{
  GtkSnapshot *snapshot;

  snapshot = gtk_snapshot_new ();
  gtk_snapshot_append_node (snapshot, node);
  gtk_snapshot_append_color (snapshot,
                             &(GdkRGBA) { 1, 0, 0, 0.5 },
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
  GOptionContext *context;
  GError *error = NULL;
  GskRenderNode *result;
  graphene_rect_t opaque;

  context = g_option_context_new (NULL);
  g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
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
      result = render_rect (node, &opaque);
      gsk_render_node_unref (node);
    }
  else
    result = g_steal_pointer (&node);

  return result;
}
