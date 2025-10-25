/*
This puts the NotificationWatcher on the bus, kinda.  Enough to
trick the Item into unfalling back.

Copyright 2010 Canonical Ltd.

Authors:
    Ted Gould <ted@canonical.com>

This program is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License version 3, as published
by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranties of
MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <gio/gio.h>

#include "../src/dbus-shared.h"

typedef struct
{
  int        status;

  GMainLoop *loop;
} TestData;

static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='" NOTIFICATION_WATCHER_DBUS_IFACE "'>"
  "    <method name='RegisterStatusNotifierItem'>"
  "      <arg type='s' name='service' direction='in'/>"
  "    </method>"
  "    <property type='b' name='IsStatusNotifierHostRegistered' access='read'/>"
  "  </interface>"
  "</node>";

static gboolean
kill_func (gpointer user_data)
{
  TestData *data;

  data = user_data;
  data->status = EXIT_FAILURE;

  g_main_loop_quit (data->loop);

  return G_SOURCE_REMOVE;
}

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
  TestData *data;

  data = user_data;

  if (g_strcmp0 (method_name, "RegisterStatusNotifierItem") == 0)
    {
      g_dbus_method_invocation_return_value (invocation, NULL);
      g_main_loop_quit (data->loop);
    }
}

static GVariant *
handle_get_property (GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GError          **error,
                     gpointer          user_data)
{
  if (g_strcmp0 (property_name, "IsStatusNotifierHostRegistered") == 0)
    return g_variant_new_boolean (TRUE);

  return NULL;
}

static const GDBusInterfaceVTable interface_vtable =
{
  handle_method_call,
  handle_get_property,
  NULL
};

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  
  GDBusNodeInfo *introspection_info;

  introspection_info = g_dbus_node_info_new_for_xml (introspection_xml, NULL);

  g_dbus_connection_register_object (connection,
                                     NOTIFICATION_WATCHER_DBUS_OBJ,
                                     introspection_info->interfaces[0],
                                     &interface_vtable,
                                     user_data,
                                     NULL,
                                     NULL);

  g_dbus_node_info_unref (introspection_info);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  TestData *data;

  data = user_data;
  data->status = EXIT_FAILURE;

  g_main_loop_quit (data->loop);
}

int
main (int   argc,
      char *argv[])
{
  GDBusProxy *proxy;
  gboolean has_owner;
  gint owner_count;
  TestData data;
  guint owner_id;

  g_debug ("Waiting to init.");

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.freedesktop.DBus",
                                         "/org/freedesktop/DBus",
                                         "org.freedesktop.DBus",
                                         NULL,
                                         NULL);

  has_owner = FALSE;
  owner_count = 0;

  while (!has_owner && owner_count < 10000)
    {
      GVariant *variant;

      variant = g_dbus_proxy_call_sync (proxy,
                                        "NameHasOwner",
                                        g_variant_new ("(s)", "org.test"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        NULL);

      if (variant != NULL)
        {
          g_variant_get (variant, "(b)", &has_owner);
          g_variant_unref (variant);
        }

      g_usleep (500000);
      owner_count++;
    }

  g_object_unref (proxy);

  if (owner_count == 10000)
    {
      g_error ("Unable to get name owner after 10000 tries");
      return EXIT_FAILURE;
    }

  g_usleep (500000);
  g_debug ("Initing");

  data.status = EXIT_SUCCESS;
  data.loop = g_main_loop_new (NULL, FALSE);

  owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                             NOTIFICATION_WATCHER_DBUS_ADDR,
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             bus_acquired_cb,
                             NULL,
                             name_lost_cb,
                             &data,
                             NULL);

  /* This is the final kill function. It really shouldn't happen
   * unless we get an error.
   */
  g_timeout_add_seconds (20, kill_func, &data);

  g_debug ("Entering Mainloop");

  g_main_loop_run (data.loop);

  g_main_loop_unref (data.loop);
  g_bus_unown_name (owner_id);

  g_debug ("Exiting");

  return data.status;
}
