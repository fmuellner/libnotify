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
#ifndef NOTIFICATION_PRIVATE_H
#define NOTIFICATION_PRIVATE_H

#include <glib.h>

G_BEGIN_DECLS

struct _NotifyNotificationPrivate
{
  guint32         id;
  char           *app_name;
  char           *summary;
  char           *body;

  /* NULL to use icon data. Anything else to have server lookup icon */
  char           *icon_name;

  /*
   * -1   = use server default
   *  0   = never timeout
   *  > 0 = Number of milliseconds before we timeout
   */
  gint            timeout;

  GSList         *actions;
  GHashTable     *action_map;
  GHashTable     *hints;

  gboolean        has_nondefault_actions;
  gboolean        updates_pending;

  gulong          proxy_action_invoked_handler;
  gulong          proxy_closed_handler;

  gint            closed_reason;
};


G_END_DECLS

#endif /* NOTIFICATION_PRIVATE_H */
