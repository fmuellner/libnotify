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

#include <gio/gio.h>

typedef struct
{
  GDBusProxy *real_proxy;

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

GList *
notify_proxy_get_server_capabilities (NotifyProxy   *proxy,
                                      GCancellable  *cancellable,
                                      GError       **error)
{
  GVariant *result;
  char **cap, **caps = NULL;
  GList *list = NULL;

  g_return_val_if_fail (NOTIFY_IS_PROXY (proxy), NULL);
  g_return_val_if_fail (G_IS_DBUS_PROXY (proxy->priv->real_proxy), NULL);

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

gboolean
notify_proxy_get_server_info (NotifyProxy   *proxy,
                              char         **name,
                              char         **vendor,
                              char         **version,
                              char         **spec_version,
                              GCancellable  *cancellable,
                              GError       **error)
{
  GVariant *result;
  gboolean rv = FALSE;

  g_return_val_if_fail (NOTIFY_IS_PROXY (proxy), FALSE);
  g_return_val_if_fail (G_IS_DBUS_PROXY (proxy->priv->real_proxy), FALSE);

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

gboolean
notify_proxy_add_notification (NotifyProxy         *proxy,
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

  g_return_val_if_fail (NOTIFY_IS_PROXY (proxy), FALSE);
  g_return_val_if_fail (G_IS_DBUS_PROXY (proxy->priv->real_proxy), FALSE);
  g_return_val_if_fail (NOTIFY_IS_NOTIFICATION (notification), FALSE);

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

gboolean
notify_proxy_remove_notification (NotifyProxy         *proxy,
                                  NotifyNotification  *notification,
                                  GCancellable        *cancellable,
                                  GError             **error)
{
  NotifyNotificationPrivate *n;
  GVariant                  *result = NULL;
  gboolean                   rv = FALSE;

  g_return_val_if_fail (NOTIFY_IS_PROXY (proxy), FALSE);
  g_return_val_if_fail (G_IS_DBUS_PROXY (proxy->priv->real_proxy), FALSE);
  g_return_val_if_fail (NOTIFY_IS_NOTIFICATION (notification), FALSE);

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
}

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  NotifyProxy *proxy = NOTIFY_PROXY (initable);
  GDBusProxy *real_proxy;

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
