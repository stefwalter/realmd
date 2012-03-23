/* identity-config - Identity configuration service
 *
 * Copyright 2012 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Stef Walter <stefw@gnome.org>
 */

#include "config.h"

#include "ic-service.h"

static GObject *current_invocation = NULL;


static void
on_invocation_gone (gpointer unused,
                    GObject *where_the_object_was)
{
	g_warning ("a GDBusMethodInvocation was released but the invocation was "
	           "registered as part of a ic_service_lock_for_action()");
	g_assert (where_the_object_was == current_invocation);
	current_invocation = NULL;
}

gboolean
ic_service_lock_for_action (GDBusMethodInvocation *invocation)
{
	g_return_val_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation), FALSE);

	if (current_invocation)
		return FALSE;

	current_invocation = G_OBJECT (invocation);
	g_object_weak_ref (current_invocation, on_invocation_gone, NULL);
	return TRUE;
}

void
ic_service_unlock_for_action (GDBusMethodInvocation *invocation)
{
	g_return_if_fail (G_IS_DBUS_METHOD_INVOCATION (invocation));

	if (current_invocation != G_OBJECT (invocation)) {
		g_warning ("trying to ic_service_unlock_for_action() with an invocation "
		           "that is not registered as the current locked action.");
		return;
	}

	g_object_weak_unref (current_invocation, on_invocation_gone, NULL);
	current_invocation = NULL;
}
