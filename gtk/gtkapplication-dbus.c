/*
 * Copyright © 2010 Codethink Limited
 * Copyright © 2012 Red Hat, Inc.
 * Copyright © 2013 Canonical Limited
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the licence, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 *         Matthias Clasen <mclasen@redhat.com>
 */

#include "config.h"

#include "gtkapplicationprivate.h"
#include "gtksettings.h"
#include "gtkprivate.h"
#include <glib/gi18n-lib.h>

#include "gdk/gdkconstructorprivate.h"

G_DEFINE_TYPE (GtkApplicationImplDBus, gtk_application_impl_dbus, GTK_TYPE_APPLICATION_IMPL)

#define DBUS_BUS_NAME               "org.freedesktop.DBus"
#define DBUS_OBJECT_PATH            "/org/freedesktop/DBus"
#define DBUS_BUS_INTERFACE          "org.freedesktop.DBus"


static GDBusProxy*
gtk_application_get_proxy_if_service_present (GDBusConnection *connection,
                                              GDBusProxyFlags  flags,
                                              const char      *bus_name,
                                              const char      *object_path,
                                              const char      *interface,
                                              GError         **error)
{
  GDBusProxy *proxy;
  char *owner;

  proxy = g_dbus_proxy_new_sync (connection,
                                 flags,
                                 NULL,
                                 bus_name,
                                 object_path,
                                 interface,
                                 NULL,
                                 error);

  if (!proxy)
    return NULL;

  /* is there anyone actually providing the service? */
  owner = g_dbus_proxy_get_name_owner (proxy);
  if (owner == NULL)
    {
      g_clear_object (&proxy);
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER,
                   "The name %s is not owned", bus_name);
    }
  else
    g_free (owner);

  return proxy;
}

enum {
  UNKNOWN   = 0,
  RUNNING   = 1,
  QUERY_END = 2,
  ENDING    = 3
};

static void
screensaver_signal_portal (GDBusConnection *connection,
                           const char       *sender_name,
                           const char       *object_path,
                           const char       *interface_name,
                           const char       *signal_name,
                           GVariant         *parameters,
                           gpointer          data)
{
  GtkApplicationImplDBus *dbus = (GtkApplicationImplDBus *)data;
  GtkApplication *application = data;
  gboolean active;
  GVariant *state;
  guint32 session_state = UNKNOWN;

  if (!g_str_equal (signal_name, "StateChanged"))
    return;

  g_variant_get (parameters, "(o@a{sv})", NULL, &state);
  g_variant_lookup (state, "screensaver-active", "b", &active);
  gtk_application_set_screensaver_active (dbus->impl.application, active);

  g_variant_lookup (state, "session-state", "u", &session_state);
  g_variant_unref (state);
  if (session_state != dbus->session_state)
    {
      dbus->session_state = session_state;

      /* Note that we'll only ever get here if we get a session-state,
       * in which case, the interface is new enough to have QueryEndResponse.
       */
      if (session_state == ENDING)
        {
          g_application_quit (G_APPLICATION (application));
        }
      else if (session_state == QUERY_END)
        {
          g_signal_emit_by_name (dbus->impl.application, "query-end");

          g_dbus_proxy_call (dbus->inhibit_proxy,
                             "QueryEndResponse",
                             g_variant_new ("(o)", dbus->session_path),
                             G_DBUS_CALL_FLAGS_NONE,
                             G_MAXINT,
                             NULL,
                             NULL, NULL);
        }
    }
}

static void
create_monitor_cb (GObject      *source,
                   GAsyncResult *result,
                   gpointer      data)
{
  GDBusProxy *proxy = G_DBUS_PROXY (source);
  GError *error = NULL;
  GVariant *ret = NULL;

  ret = g_dbus_proxy_call_finish (proxy, result, &error);
  if (ret == NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Creating a portal monitor failed: %s",
                   error ? error->message : "");
      g_clear_error (&error);
      return;
    }

  g_variant_unref (ret);
}

static void
gtk_application_impl_dbus_startup (GtkApplicationImpl *impl)
{
  GtkApplicationImplDBus *dbus = (GtkApplicationImplDBus *) impl;
  GError *error = NULL;
  char *token;
  GVariantBuilder opt_builder;

#ifndef G_HAS_CONSTRUCTORS
  /* Unset DESKTOP_AUTOSTART_ID in order to avoid child processes to
   * use the same client id.
   */
  g_unsetenv ("DESKTOP_AUTOSTART_ID");
#endif

  dbus->session = g_application_get_dbus_connection (G_APPLICATION (impl->application));

  if (!dbus->session)
    return;

  dbus->application_id = g_application_get_application_id (G_APPLICATION (impl->application));
  dbus->object_path = g_application_get_dbus_object_path (G_APPLICATION (impl->application));
  dbus->unique_name = g_dbus_connection_get_unique_name (dbus->session);

  if (!gdk_display_should_use_portal (impl->display, PORTAL_INHIBIT_INTERFACE, 0))
    return;

  dbus->inhibit_proxy = gtk_application_get_proxy_if_service_present (dbus->session,
                                                                      G_DBUS_PROXY_FLAGS_NONE,
                                                                      PORTAL_BUS_NAME,
                                                                      PORTAL_OBJECT_PATH,
                                                                      PORTAL_INHIBIT_INTERFACE,
                                                                      &error);
  if (error)
    {
      g_debug ("Failed to get an inhibit portal proxy: %s", error->message);
      g_clear_error (&error);
      return;
    }

  /* Monitor screensaver state */

  dbus->session_path = gtk_get_portal_session_path (dbus->session, &token);
  dbus->state_changed_handler =
      g_dbus_connection_signal_subscribe (dbus->session,
                                          PORTAL_BUS_NAME,
                                          PORTAL_INHIBIT_INTERFACE,
                                          "StateChanged",
                                          PORTAL_OBJECT_PATH,
                                          NULL,
                                          G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
                                          screensaver_signal_portal,
                                          dbus,
                                          NULL);

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&opt_builder, "{sv}",
                         "session_handle_token", g_variant_new_string (token));

  dbus->cancellable = g_cancellable_new ();

  g_dbus_proxy_call (dbus->inhibit_proxy,
                     "CreateMonitor",
                     g_variant_new ("(sa{sv})", "", &opt_builder),
                     G_DBUS_CALL_FLAGS_NONE,
                     G_MAXINT,
                     dbus->cancellable,
                     create_monitor_cb, dbus);
  g_free (token);
}

static void
gtk_application_impl_dbus_shutdown (GtkApplicationImpl *impl)
{
  GtkApplicationImplDBus *dbus = (GtkApplicationImplDBus *) impl;
  g_cancellable_cancel (dbus->cancellable);
}

GQuark gtk_application_impl_dbus_export_id_quark (void);

G_DEFINE_QUARK (GtkApplicationImplDBus export id, gtk_application_impl_dbus_export_id)

static void
gtk_application_impl_dbus_window_added (GtkApplicationImpl *impl,
                                        GtkWindow          *window)
{
  GtkApplicationImplDBus *dbus = (GtkApplicationImplDBus *) impl;
  GActionGroup *actions;
  char *path;
  guint id;

  if (!dbus->session || !GTK_IS_APPLICATION_WINDOW (window))
    return;

  /* Export the action group of this window, based on its id */
  actions = gtk_application_window_get_action_group (GTK_APPLICATION_WINDOW (window));

  path = gtk_application_impl_dbus_get_window_path (dbus, window);
  id = g_dbus_connection_export_action_group (dbus->session, path, actions, NULL);
  g_free (path);

  g_object_set_qdata (G_OBJECT (window), gtk_application_impl_dbus_export_id_quark (), GUINT_TO_POINTER (id));
}

static void
gtk_application_impl_dbus_window_removed (GtkApplicationImpl *impl,
                                          GtkWindow          *window)
{
  GtkApplicationImplDBus *dbus = (GtkApplicationImplDBus *) impl;
  guint id;

  id = GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (window), gtk_application_impl_dbus_export_id_quark ()));
  if (id)
    {
      g_dbus_connection_unexport_action_group (dbus->session, id);
      g_object_set_qdata (G_OBJECT (window), gtk_application_impl_dbus_export_id_quark (), NULL);
    }
}

static void
gtk_application_impl_dbus_active_window_changed (GtkApplicationImpl *impl,
                                                 GtkWindow          *window)
{
}

static void
gtk_application_impl_dbus_publish_menu (GtkApplicationImplDBus  *dbus,
                                        const char              *type,
                                        GMenuModel              *model,
                                        guint                   *id,
                                        char                   **path)
{
  int i;

  if (dbus->session == NULL)
    return;

  /* unexport any existing menu */
  if (*id)
    {
      g_dbus_connection_unexport_menu_model (dbus->session, *id);
      g_free (*path);
      *path = NULL;
      *id = 0;
    }

  /* export the new menu, if there is one */
  if (model != NULL)
    {
      /* try getting the preferred name */
      *path = g_strconcat (dbus->object_path, "/menus/", type, NULL);
      *id = g_dbus_connection_export_menu_model (dbus->session, *path, model, NULL);

      /* keep trying until we get a working name... */
      for (i = 0; *id == 0; i++)
        {
          g_free (*path);
          *path = g_strdup_printf ("%s/menus/%s%d", dbus->object_path, type, i);
          *id = g_dbus_connection_export_menu_model (dbus->session, *path, model, NULL);
        }
    }
}

static void
gtk_application_impl_dbus_set_app_menu (GtkApplicationImpl *impl,
                                        GMenuModel         *app_menu)
{
  GtkApplicationImplDBus *dbus = (GtkApplicationImplDBus *) impl;

  gtk_application_impl_dbus_publish_menu (dbus, "appmenu", app_menu, &dbus->app_menu_id, &dbus->app_menu_path);
}

static void
gtk_application_impl_dbus_set_menubar (GtkApplicationImpl *impl,
                                       GMenuModel         *menubar)
{
  GtkApplicationImplDBus *dbus = (GtkApplicationImplDBus *) impl;

  gtk_application_impl_dbus_publish_menu (dbus, "menubar", menubar, &dbus->menubar_id, &dbus->menubar_path);
}

static int next_cookie;

typedef struct {
  char *handle;
  int cookie;
} InhibitHandle;

static void
inhibit_handle_free (gpointer data)
{
  InhibitHandle *handle = data;

  g_free (handle->handle);
  g_free (handle);
}

static guint
gtk_application_impl_dbus_inhibit (GtkApplicationImpl         *impl,
                                   GtkWindow                  *window,
                                   GtkApplicationInhibitFlags  flags,
                                   const char                 *reason)
{
  GtkApplicationImplDBus *dbus = (GtkApplicationImplDBus *) impl;
  GVariant *res;
  GError *error = NULL;
  static gboolean warned = FALSE;

  if (dbus->inhibit_proxy)
    {
      GVariantBuilder options;

      if (reason == NULL)
        /* Translators: This is the 'reason' given when inhibiting
         * suspend or screen locking, and the caller hasn't specified
         * a reason.
         */
        reason = _("Reason not specified");

      g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&options, "{sv}", "reason", g_variant_new_string (reason));
      res = g_dbus_proxy_call_sync (dbus->inhibit_proxy,
                                    "Inhibit",
                                    g_variant_new ("(su@a{sv})",
                                                   "", /* window */
                                                   flags,
                                                   g_variant_builder_end (&options)),
                                    G_DBUS_CALL_FLAGS_NONE,
                                    G_MAXINT,
                                    NULL,
                                    &error);
      if (res)
        {
          InhibitHandle *handle;

          handle = g_new (InhibitHandle, 1);
          handle->cookie = ++next_cookie;

          g_variant_get (res, "(o)", &handle->handle);
          g_variant_unref (res);

          dbus->inhibit_handles = g_slist_prepend (dbus->inhibit_handles, handle);

          return handle->cookie;
        }

      if (error)
        {
          if (!warned)
            {
              g_warning ("Calling %s.Inhibit failed: %s",
                         g_dbus_proxy_get_interface_name (dbus->inhibit_proxy),
                         error->message);
              warned = TRUE;
            }
          g_clear_error (&error);
        }
    }

  return 0;
}

static void
gtk_application_impl_dbus_uninhibit (GtkApplicationImpl *impl,
                                     guint               cookie)
{
  GtkApplicationImplDBus *dbus = (GtkApplicationImplDBus *) impl;

  if (dbus->inhibit_proxy)
    {
      GSList *l;

      for (l = dbus->inhibit_handles; l; l = l->next)
        {
          InhibitHandle *handle = l->data;
          if (handle->cookie == cookie)
            {
              g_dbus_connection_call (dbus->session,
                                      PORTAL_BUS_NAME,
                                      handle->handle,
                                      PORTAL_REQUEST_INTERFACE,
                                      "Close",
                                      g_variant_new ("()"),
                                      G_VARIANT_TYPE_UNIT,
                                      G_DBUS_CALL_FLAGS_NONE,
                                      G_MAXINT,
                                      NULL, NULL, NULL);
              dbus->inhibit_handles = g_slist_remove (dbus->inhibit_handles, handle);
              inhibit_handle_free (handle);
              break;
            }
        }
    }
}

static void
gtk_application_impl_dbus_init (GtkApplicationImplDBus *dbus)
{
}

static void
gtk_application_impl_dbus_finalize (GObject *object)
{
  GtkApplicationImplDBus *dbus = (GtkApplicationImplDBus *) object;

  if (dbus->session_path)
    {
      g_dbus_connection_call (dbus->session,
                              PORTAL_BUS_NAME,
                              dbus->session_path,
                              PORTAL_SESSION_INTERFACE,
                              "Close",
                              NULL, NULL, 0, -1, NULL, NULL, NULL);

      g_free (dbus->session_path);
    }

  if (dbus->state_changed_handler)
    g_dbus_connection_signal_unsubscribe (dbus->session,
                                          dbus->state_changed_handler);

  g_clear_object (&dbus->inhibit_proxy);
  g_slist_free_full (dbus->inhibit_handles, inhibit_handle_free);
  g_free (dbus->app_menu_path);
  g_free (dbus->menubar_path);
  g_clear_object (&dbus->cancellable);

  G_OBJECT_CLASS (gtk_application_impl_dbus_parent_class)->finalize (object);
}

static void
gtk_application_impl_dbus_class_init (GtkApplicationImplDBusClass *class)
{
  GtkApplicationImplClass *impl_class = GTK_APPLICATION_IMPL_CLASS (class);
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);

  impl_class->startup = gtk_application_impl_dbus_startup;
  impl_class->shutdown = gtk_application_impl_dbus_shutdown;
  impl_class->window_added = gtk_application_impl_dbus_window_added;
  impl_class->window_removed = gtk_application_impl_dbus_window_removed;
  impl_class->active_window_changed = gtk_application_impl_dbus_active_window_changed;
  impl_class->set_app_menu = gtk_application_impl_dbus_set_app_menu;
  impl_class->set_menubar = gtk_application_impl_dbus_set_menubar;
  impl_class->inhibit = gtk_application_impl_dbus_inhibit;
  impl_class->uninhibit = gtk_application_impl_dbus_uninhibit;

  gobject_class->finalize = gtk_application_impl_dbus_finalize;
}

char *
gtk_application_impl_dbus_get_window_path (GtkApplicationImplDBus *dbus,
                                           GtkWindow *window)
{
  if (dbus->session && GTK_IS_APPLICATION_WINDOW (window))
    return g_strdup_printf ("%s/window/%d",
                            dbus->object_path,
                            gtk_application_window_get_id (GTK_APPLICATION_WINDOW (window)));
  else
    return NULL;
}
