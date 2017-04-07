/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "proxy.h"
#include "notify.h"
#include "internal.h"
#include "notification-private.h"

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

typedef struct
{
  GDBusProxy *real_proxy;
  gboolean in_flatpak_sandbox;

  int g_signal_id;
} NotifyProxyPrivate;

struct _NotifyProxy
{
  GObject parent;

  NotifyProxyPrivate *priv;
};

enum
{
  ACTION_INVOKED,
  CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (NotifyProxy, notify_proxy, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (NotifyProxy)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init))

NotifyProxy *
notify_proxy_new (GCancellable  *cancellable,
                  GError       **error)
{
  GInitable *initable = g_initable_new (NOTIFY_TYPE_PROXY,
                                        cancellable,
                                        error,
                                        NULL);
  if (initable != NULL)
    return NOTIFY_PROXY (initable);
  else
    return NULL;
}

static GList *
get_server_caps_fdo (NotifyProxy   *proxy,
                     GCancellable  *cancellable,
                     GError       **error)
{
  GVariant *result;
  char **cap, **caps = NULL;
  GList *list = NULL;

  result = g_dbus_proxy_call_sync (proxy->priv->real_proxy,
                                   "GetCapabilities",
                                   g_variant_new ("()"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1 /* FIXME shorter timeout? */,
                                   cancellable,
                                   error);

  if (result == NULL)
    goto out;

  if (!g_variant_is_of_type (result, G_VARIANT_TYPE ("(as)")))
    goto out;

  g_variant_get (result, "(^as)", &caps);

  for (cap = caps; *cap != NULL; cap++)
    list = g_list_prepend (list, *cap);

out:
  g_clear_pointer (&result, g_variant_unref);
  g_clear_pointer (&caps, g_free);

  return g_list_reverse (list);
}

static GList *
get_server_caps_portal (NotifyProxy   *proxy,
                        GCancellable  *cancellable,
                        GError       **error)
{
  const char *caps[] = { "actions", "body", "persistence" };
  GList *list = NULL;
  int i;

  for (i = 0; i < G_N_ELEMENTS (caps); i++)
    list = g_list_prepend (list, g_strdup (caps[i]));

  return g_list_reverse (list);
}

GList *
notify_proxy_get_server_capabilities (NotifyProxy   *proxy,
                                      GCancellable  *cancellable,
                                      GError       **error)
{
  g_return_val_if_fail (NOTIFY_IS_PROXY (proxy), NULL);
  g_return_val_if_fail (G_IS_DBUS_PROXY (proxy->priv->real_proxy), NULL);

  if (proxy->priv->in_flatpak_sandbox)
    return get_server_caps_portal (proxy, cancellable, error);
  else
    return get_server_caps_fdo (proxy, cancellable, error);
}

static gboolean
get_server_info_fdo (NotifyProxy   *proxy,
                     char         **name,
                     char         **vendor,
                     char         **version,
                     char         **spec_version,
                     GCancellable  *cancellable,
                     GError       **error)
{
  GVariant *result;
  gboolean rv = FALSE;

  result = g_dbus_proxy_call_sync (proxy->priv->real_proxy,
                                   "GetServerInformation",
                                   g_variant_new ("()"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1 /* FIXME shorter timeout? */,
                                   cancellable,
                                   error);

  if (result == NULL)
    goto out;

  if (!g_variant_is_of_type (result, G_VARIANT_TYPE ("(ssss)")))
    {
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "Unexpected reply type");
      goto out;
    }

  g_variant_get (result, "(ssss)",
                 name,
                 vendor,
                 version,
                 spec_version);
  rv = TRUE;

out:
  g_clear_pointer (&result, g_variant_unref);

  return rv;
}

static gboolean
get_server_info_portal (NotifyProxy   *proxy,
                        char         **name,
                        char         **vendor,
                        char         **version,
                        char         **spec_version,
                        GCancellable  *cancellable,
                        GError       **error)
{
  GVariant *prop = NULL;
  gboolean rv = FALSE;

  prop = g_dbus_proxy_get_cached_property (proxy->priv->real_proxy, "version");
  if (prop == NULL)
    goto out;

  if (!g_variant_is_of_type (prop, G_VARIANT_TYPE ("u")))
    goto out;

  if (version != NULL)
    *version = g_strdup_printf ("%u", g_variant_get_uint32 (prop));

  if (name != NULL)
    *name = g_strdup ("Desktop portal");

  if (vendor != NULL)
    *vendor = g_strdup ("Freedesktop.org");

  if (spec_version != NULL)
    *spec_version = g_strdup ("1.2");

  rv = TRUE;

out:
  g_clear_pointer (&prop, g_variant_unref);

  return rv;
}

gboolean
notify_proxy_get_server_info (NotifyProxy   *proxy,
                              char         **name,
                              char         **vendor,
                              char         **version,
                              char         **spec_version,
                              GCancellable  *cancellable,
                              GError       **error)
{
  g_return_val_if_fail (NOTIFY_IS_PROXY (proxy), FALSE);
  g_return_val_if_fail (G_IS_DBUS_PROXY (proxy->priv->real_proxy), FALSE);

  if (proxy->priv->in_flatpak_sandbox)
    return get_server_info_portal (proxy, name, vendor, version, spec_version, cancellable, error);
  else
    return get_server_info_fdo (proxy, name, vendor, version, spec_version, cancellable, error);
}

static gboolean
add_notification_fdo (NotifyProxy         *proxy,
                      NotifyNotification  *notification,
                      GCancellable        *cancellable,
                      GError             **error)
{
  NotifyNotificationPrivate *n;
  GVariantBuilder            actions_builder, hints_builder;
  GSList                    *l;
  GHashTableIter             iter;
  gpointer                   key, data;
  GVariant                  *result = NULL;
  gboolean                   rv = FALSE;

  n = notification->priv;

  g_variant_builder_init (&actions_builder, G_VARIANT_TYPE ("as"));
  for (l = n->actions; l != NULL; l = l->next)
    g_variant_builder_add (&actions_builder, "s", l->data);

  g_variant_builder_init (&hints_builder, G_VARIANT_TYPE ("a{sv}"));
  g_hash_table_iter_init (&iter, n->hints);
  while (g_hash_table_iter_next (&iter, &key, &data))
    g_variant_builder_add (&hints_builder, "{sv}", key, data);

  result = g_dbus_proxy_call_sync (proxy->priv->real_proxy,
                                   "Notify",
                                   g_variant_new ("(susssasa{sv}i)",
                                                  n->app_name ? n->app_name : notify_get_app_name (),
                                                  n->id,
                                                  n->icon_name ? n->icon_name : "",
                                                  n->summary ? n->summary : "",
                                                  n->body ? n->body : "",
                                                  &actions_builder,
                                                  &hints_builder,
                                                  n->timeout),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1 /* FIXME ? */,
                                   NULL,
                                   error);

  if (result == NULL)
    goto out;

  if (!g_variant_is_of_type (result, G_VARIANT_TYPE ("(u)")))
    {
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "Unexpected reply type");
      goto out;
    }

  g_variant_get (result, "(u)", &n->id);
  rv = TRUE;

out:
  g_clear_pointer (&result, g_variant_unref);

  return rv;
}

static GIcon *
get_icon_from_name (const char *name)
{
  GFile *icon_file = NULL;
  GIcon *icon;

  g_return_val_if_fail (name && *name, NULL);

  if (g_str_has_prefix (name, "file://"))
    icon_file = g_file_new_for_uri (name);
  else if (*name == '/')
    icon_file = g_file_new_for_path (name);

  if (icon_file)
    icon = g_file_icon_new (icon_file);
  else
    icon = g_themed_icon_new (name);

  g_clear_object (&icon_file);

  return icon;
}

static GIcon *
get_icon_from_hint (GVariant *hint)
{
  GdkPixbuf *pixbuf;
  int width, height, rowstride, bps, n_channels;
  gboolean has_alpha;
  guchar *data;

  g_return_val_if_fail (hint != NULL &&
                        g_variant_is_of_type (hint, G_VARIANT_TYPE ("(iiibiiay)")), NULL);

  g_variant_get (hint, "(iiibii^&ay)",
                 &width,
                 &height,
                 &rowstride,
                 &has_alpha,
                 &bps,
                 &n_channels,
                 &data);

  pixbuf = gdk_pixbuf_new_from_data (data,
                                     GDK_COLORSPACE_RGB,
                                     has_alpha,
                                     bps,
                                     width,
                                     height,
                                     rowstride,
                                     NULL, NULL);
  return G_ICON (pixbuf);
}

static const char *
get_properity_from_hint (GVariant *hint)
{
  g_return_val_if_fail (hint != NULL &&
                        g_variant_is_of_type (hint, G_VARIANT_TYPE_BYTE), NULL);

  switch (g_variant_get_byte (hint))
    {
    case NOTIFY_URGENCY_LOW:
      return "low";
    case NOTIFY_URGENCY_NORMAL:
      return "normal";
    case NOTIFY_URGENCY_CRITICAL:
      return "urgent";
    }

  return NULL;
}

static GVariant *
get_buttons_from_actions (GSList   *actions,
                          gboolean *has_default_action)
{
  GVariantBuilder builder;
  GSList *l;

  if (has_default_action != NULL)
    *has_default_action = FALSE;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  for (l = actions; l != NULL; l = l->next)
    {
      const char *action = l->data;
      const char *label;

      l = l->next;
      if (l == NULL)
        break;

      label = l->data;

      if (strcmp (action, "default") == 0)
        {
          if (has_default_action != NULL)
            *has_default_action = TRUE;
          continue;
        }

      g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&builder, "{sv}", "label", g_variant_new_string (label));
      g_variant_builder_add (&builder, "{sv}", "action", g_variant_new_string (action));
      g_variant_builder_close (&builder);
    }

  return g_variant_builder_end (&builder);
}

static gboolean
add_notification_portal (NotifyProxy         *proxy,
                         NotifyNotification  *notification,
                         GCancellable        *cancellable,
                         GError             **error)
{
  static guint32             next_id = 1;

  NotifyNotificationPrivate *n;
  GVariantDict               notification_dict;
  GVariant                  *buttons = NULL;
  GVariant                  *hint;
  GIcon                     *icon = NULL;
  const char                *priority = NULL;
  char                      *id;
  GVariant                  *result = NULL;
  gboolean                   has_default_action = FALSE;
  gboolean                   rv = FALSE;

  n = notification->priv;

  if (n->id == 0)
    n->id = next_id++;

  id = g_strdup_printf ("%u", n->id);

  g_variant_dict_init (&notification_dict, NULL);

  if (n->summary != NULL)
    g_variant_dict_insert (&notification_dict, "title", "s", n->summary);

  if (n->body != NULL)
    g_variant_dict_insert (&notification_dict, "body", "s", n->body);

  if (n->icon_name != NULL)
    icon = get_icon_from_name (n->icon_name);
  else if ((hint = g_hash_table_lookup (n->hints, "image-data")) != NULL)
    icon = get_icon_from_hint (hint);

  if (icon != NULL)
    g_variant_dict_insert (&notification_dict, "icon", "@*", g_icon_serialize (icon));

  if ((hint = g_hash_table_lookup (n->hints, "urgency")) != NULL)
    priority = get_properity_from_hint (hint);

  if (priority)
    g_variant_dict_insert (&notification_dict, "priority", "s", priority);

  if (n->actions != NULL)
    buttons = get_buttons_from_actions (n->actions, &has_default_action);

  if (has_default_action)
    g_variant_dict_insert (&notification_dict, "default-action", "s", "default");

  if (buttons != NULL)
    g_variant_dict_insert (&notification_dict, "buttons", "@aa{sv}", buttons);

  result = g_dbus_proxy_call_sync (proxy->priv->real_proxy,
                                   "AddNotification",
                                   g_variant_new ("(s@a{sv})",
                                                  id,
                                                  g_variant_dict_end (&notification_dict)),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1 /* FIXME ? */,
                                   NULL,
                                   error);

  if (result == NULL)
    goto out;

  rv = TRUE;

out:
  g_clear_pointer (&id, g_free);
  g_clear_object (&icon);
  g_clear_pointer (&result, g_variant_unref);

  return rv;
}

gboolean
notify_proxy_add_notification (NotifyProxy         *proxy,
                               NotifyNotification  *notification,
                               GCancellable        *cancellable,
                               GError             **error)
{
  g_return_val_if_fail (NOTIFY_IS_PROXY (proxy), FALSE);
  g_return_val_if_fail (G_IS_DBUS_PROXY (proxy->priv->real_proxy), FALSE);
  g_return_val_if_fail (NOTIFY_IS_NOTIFICATION (notification), FALSE);

  if (proxy->priv->in_flatpak_sandbox)
    return add_notification_portal (proxy, notification, cancellable, error);
  else
    return add_notification_fdo (proxy, notification, cancellable, error);

}

static gboolean
remove_notification_fdo (NotifyProxy         *proxy,
                         NotifyNotification  *notification,
                         GCancellable        *cancellable,
                         GError             **error)
{
  NotifyNotificationPrivate *n;
  GVariant                  *result = NULL;
  gboolean                   rv = FALSE;

  n = notification->priv;

  result = g_dbus_proxy_call_sync (proxy->priv->real_proxy,
                                   "CloseNotification",
                                   g_variant_new ("(u)", n->id),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1 /* FIXME! */,
                                   cancellable,
                                   error);

  if (result == NULL)
    goto out;

  rv = TRUE;

out:
  g_clear_pointer (&result, g_variant_unref);

  return rv;
}

static gboolean
remove_notification_portal (NotifyProxy         *proxy,
                            NotifyNotification  *notification,
                            GCancellable        *cancellable,
                            GError             **error)
{
  char *id;
  GVariant *result = NULL;
  gboolean  rv = FALSE;

  id = g_strdup_printf ("%u", notification->priv->id);
  result = g_dbus_proxy_call_sync (proxy->priv->real_proxy,
                                   "RemoveNotification",
                                   g_variant_new ("(s)", id),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1 /* FIXME! */,
                                   cancellable,
                                   error);

  if (result == NULL)
    goto out;

  rv = TRUE;

out:
  g_clear_pointer (&id, g_free);
  g_clear_pointer (&result, g_variant_unref);

  return rv;
}


gboolean
notify_proxy_remove_notification (NotifyProxy         *proxy,
                                  NotifyNotification  *notification,
                                  GCancellable        *cancellable,
                                  GError             **error)
{
  g_return_val_if_fail (NOTIFY_IS_PROXY (proxy), FALSE);
  g_return_val_if_fail (G_IS_DBUS_PROXY (proxy->priv->real_proxy), FALSE);
  g_return_val_if_fail (NOTIFY_IS_NOTIFICATION (notification), FALSE);

  if (proxy->priv->in_flatpak_sandbox)
    return remove_notification_portal (proxy, notification, cancellable, error);
  else
    return remove_notification_fdo (proxy, notification, cancellable, error);
}

static void
g_signal_cb (GDBusProxy *proxy,
             const char *sender_name,
             const char *signal_name,
             GVariant   *parameters,
             gpointer    user_data)
{
  NotifyProxy *self = user_data;

  if (g_strcmp0 (signal_name, "NotificationClosed") == 0 &&
      g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(uu)")))
    {
      guint32 id, reason;

      g_variant_get (parameters, "(uu)", &id, &reason);
      g_signal_emit (self, signals[CLOSED], 0, id, reason);
    }
  else if (g_strcmp0 (signal_name, "ActionInvoked") == 0 &&
           g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(us)")))
    {
      guint32 id;
      const char *action;

      g_variant_get (parameters, "(u&s)", &id, &action);
      g_signal_emit (self, signals[ACTION_INVOKED], 0, id, action);
    }
  else if (g_strcmp0 (signal_name, "ActionInvoked") == 0 &&
           g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(ss)")))
    {
      guint32 id;
      const char *str_id, *action;
      char *end_ptr;

      g_variant_get (parameters, "(&s&s)", &str_id, &action);
      id = strtoul (str_id, &end_ptr, 10);

      if (str_id != end_ptr)
        g_signal_emit (self, signals[ACTION_INVOKED], 0, id, action);
    }
}

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  NotifyProxy *proxy = NOTIFY_PROXY (initable);
  GDBusProxy *real_proxy;

  proxy->priv->in_flatpak_sandbox = g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS);

  if (proxy->priv->in_flatpak_sandbox)
    real_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                NULL,
                                                "org.freedesktop.portal.Desktop",
                                                "/org/freedesktop/portal/desktop",
                                                "org.freedesktop.portal.Notification",
                                                cancellable,
                                                error);
  else
    real_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                                NULL,
                                                NOTIFY_DBUS_NAME,
                                                NOTIFY_DBUS_CORE_OBJECT,
                                                NOTIFY_DBUS_CORE_INTERFACE,
                                                cancellable,
                                                error);
  proxy->priv->real_proxy = real_proxy;

  if (real_proxy)
    proxy->priv->g_signal_id = g_signal_connect (real_proxy,
                                                 "g-signal",
                                                 G_CALLBACK (g_signal_cb), proxy);

  return real_proxy != NULL;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = initable_init;
}

static void
notify_proxy_dispose (GObject *object)
{
  NotifyProxy *self = (NotifyProxy *)object;
  NotifyProxyPrivate *priv = self->priv;

  if (priv->g_signal_id)
    g_signal_handler_disconnect (priv->real_proxy, priv->g_signal_id);
  priv->g_signal_id = 0;

  g_clear_object (&priv->real_proxy);

  G_OBJECT_CLASS (notify_proxy_parent_class)->dispose (object);
}

static void
notify_proxy_class_init (NotifyProxyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = notify_proxy_dispose;

  signals[ACTION_INVOKED] = g_signal_new ("action-invoked",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL,
                                          NULL,
                                          NULL,
                                          G_TYPE_NONE,
                                          2, G_TYPE_UINT, G_TYPE_STRING);

  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL,
                                  NULL,
                                  NULL,
                                  G_TYPE_NONE,
                                  2, G_TYPE_UINT, G_TYPE_UINT);
}

static void
notify_proxy_init (NotifyProxy *self)
{
  self->priv = notify_proxy_get_instance_private (self);
}
