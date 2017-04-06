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

#ifndef _LIBNOTIFY_PROXY_H
#define _LIBNOTIFY_PROXY_H

#include <glib-object.h>

#include "notification.h"

G_BEGIN_DECLS

#define NOTIFY_TYPE_PROXY (notify_proxy_get_type())
G_DECLARE_FINAL_TYPE (NotifyProxy, notify_proxy, NOTIFY, PROXY, GObject)

NotifyProxy *notify_proxy_new (GCancellable  *cancellable,
                               GError       **error);

GList    *notify_proxy_get_server_capabilities (NotifyProxy   *proxy,
                                                GCancellable  *cancellable,
                                                GError       **error);

gboolean  notify_proxy_get_server_info (NotifyProxy   *proxy,
                                        char         **name,
                                        char         **vendor,
                                        char         **version,
                                        char         **spec_version,
                                        GCancellable  *cancellable,
                                        GError       **error);

gboolean  notify_proxy_add_notification (NotifyProxy         *proxy,
                                         NotifyNotification  *notification,
                                         GCancellable        *cancellable,
                                         GError             **error);

gboolean  notify_proxy_remove_notification (NotifyProxy         *proxy,
                                            NotifyNotification  *notification,
                                            GCancellable        *cancellable,
                                            GError             **error);

G_END_DECLS

#endif /* _LIBNOTIFY_PROXY_H */
