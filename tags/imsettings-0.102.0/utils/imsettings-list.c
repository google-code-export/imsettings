/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * imsettings-list.c
 * Copyright (C) 2008 Red Hat, Inc. All rights reserved.
 * 
 * Authors:
 *   Akira TAGOH  <tagoh@redhat.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>
#include "imsettings/imsettings.h"
#include "imsettings/imsettings-request.h"

int
main(int    argc,
     char **argv)
{
	IMSettingsRequest *imsettings_info, *imsettings;
	DBusConnection *connection;
	gchar **list, *locale;
	gchar *user_im, *system_im, *running_im;
	gint i;
	GError *error = NULL;
	guint n_retry = 0, n_info_retry = 0;

	setlocale(LC_ALL, "");
	locale = setlocale(LC_CTYPE, NULL);

	g_type_init();

	connection = dbus_bus_get(DBUS_BUS_SESSION, NULL);
	if (connection == NULL) {
		g_printerr("Failed to get a session bus.\n");
		return 1;
	}
	imsettings = imsettings_request_new(connection, IMSETTINGS_INTERFACE_DBUS);
	imsettings_info = imsettings_request_new(connection, IMSETTINGS_INFO_INTERFACE_DBUS);
	imsettings_request_set_locale(imsettings_info, locale);

  retry:
	if (imsettings_request_get_version(imsettings, NULL) != IMSETTINGS_SETTINGS_DAEMON_VERSION) {
		if (n_retry > 0) {
			g_printerr("Mismatch the version of im-settings-daemon.");
			exit(1);
		}
		/* version is inconsistent. try to reload the process */
		imsettings_request_reload(imsettings, TRUE);
		g_print("Waiting for reloading the process...\n");
		/* XXX */
		sleep(1);
		n_retry++;
		goto retry;
	}
  info_retry:
	if (imsettings_request_get_version(imsettings_info, NULL) != IMSETTINGS_IMINFO_DAEMON_VERSION) {
		if (n_info_retry > 0) {
			g_printerr("Mismatch the version of im-info-daemon.");
			exit(1);
		}
		/* version is inconsistent. try to reload the process */
		imsettings_request_reload(imsettings_info, TRUE);
		g_print("Waiting for reloading the process...\n");
		/* XXX */
		sleep(1);
		n_info_retry++;
		goto info_retry;
	}

	if ((list = imsettings_request_get_im_list(imsettings_info, &error)) == NULL) {
		g_printerr("Failed to get an IM list.\n");
	} else {
		user_im = imsettings_request_get_current_user_im(imsettings_info, &error);
		system_im = imsettings_request_get_current_system_im(imsettings_info, &error);
		running_im = imsettings_request_what_im_is_running(imsettings, &error);
		if (error) {
			g_printerr("%s\n", error->message);
			exit(1);
		}
		for (i = 0; list[i] != NULL; i++) {
			g_print("%s %d: %s %s\n",
				(strcmp(running_im, list[i]) == 0 ? "*" : (strcmp(user_im, list[i]) == 0 ? "-" : " ")),
				i + 1,
				list[i],
				(strcmp(system_im, list[i]) == 0 ? "(recommended)" : ""));
		}
		g_strfreev(list);
	}
	g_object_unref(imsettings);
	g_object_unref(imsettings_info);
	dbus_connection_unref(connection);

	return 0;
}
