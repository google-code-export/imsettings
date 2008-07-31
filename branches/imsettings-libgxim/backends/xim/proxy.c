/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * proxy.c
 * Copyright (C) 2008 Red Hat, Inc. All rights reserved.
 * 
 * Authors:
 *   Akira TAGOH  <tagoh@redhat.com>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <glib/gi18n-lib.h>
#include <gdk/gdkx.h>
#include <libgxim/gximsrvconn.h>
#include <libgxim/gximerror.h>
#include <libgxim/gximmarshal.h>
#include "client.h"
#include "proxy.h"


enum {
	PROP_0,
	PROP_CONNECT_TO,
	LAST_PROP
};
enum {
	SIGNAL_0,
	LAST_SIGNAL
};


static gboolean xim_proxy_client_real_notify_locales_cb  (GXimClientTemplate  *client,
							  gchar              **locales,
							  gpointer             user_data);
static gboolean xim_proxy_client_real_notify_transport_cb(GXimClientTemplate  *client,
							  const gchar         *transport,
							  gpointer             user_data);


//static guint signals[LAST_SIGNAL] = { 0 };


G_DEFINE_TYPE (XimProxy, xim_proxy, G_TYPE_XIM_SRV_TMPL);
G_DEFINE_TYPE (XimProxyConnection, xim_proxy_connection, G_TYPE_XIM_SERVER_CONNECTION);

/*
 * Private functions
 */
static XimClient *
_create_client(XimProxy        *proxy,
	       GdkNativeWindow  client_window)
{
	GXimCore *core = G_XIM_CORE (proxy);
	XimClient *client;
	GdkDisplay *dpy = g_xim_core_get_display(core);
	GdkNativeWindow nw;

	client = xim_client_new(dpy, proxy->connect_to);
	if (client == NULL) {
		g_xim_message_critical(core->message,
				       "Unable to create a client instance.");
		return NULL;
	}
	g_signal_connect(client, "notify_locales",
			 G_CALLBACK (xim_proxy_client_real_notify_locales_cb),
			 proxy);
	g_signal_connect(client, "notify_transport",
			 G_CALLBACK (xim_proxy_client_real_notify_transport_cb),
			 proxy);
	g_xim_message_debug(core->message, "client/conn",
			    "Inserting a client connection %p to the table with %p",
			    client,
			    G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
	g_hash_table_insert(proxy->client_table,
			    G_XIM_NATIVE_WINDOW_TO_POINTER (client_window),
			    client);
	nw = GDK_WINDOW_XID (g_xim_core_get_selection_window(G_XIM_CORE (client)));
	g_hash_table_insert(proxy->selection_table,
			    G_XIM_NATIVE_WINDOW_TO_POINTER (nw),
			    G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));

	return client;
}

static gboolean
xim_proxy_client_real_notify_locales_cb(GXimClientTemplate  *client,
					gchar              **locales,
					gpointer             user_data)
{
	XimProxy *proxy = XIM_PROXY (user_data);
	GXimCore *core = G_XIM_CORE (client);
	GdkWindow *w;
	gchar *prop = NULL;
	gboolean retval = TRUE;
	GdkNativeWindow client_window = 0, selection_window = 0;
	GdkEventSelection ev;

	w = g_xim_core_get_selection_window(core);
	selection_window = GDK_WINDOW_XID (w);
	client_window = G_XIM_POINTER_TO_NATIVE_WINDOW (g_hash_table_lookup(proxy->selection_table,
									    G_XIM_NATIVE_WINDOW_TO_POINTER (selection_window)));
	if (client_window == 0) {
		g_xim_message_warning(core->message,
				      "Received SelectionNotify from unknown sender: %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (selection_window));
		goto end;
	}

	ev.requestor = client_window;
	ev.selection = client->atom_server;
	ev.target = core->atom_locales;
	ev.property = core->atom_locales;
	ev.requestor = client_window;
	prop = g_strjoinv(",", locales);
	g_xim_message_debug(core->message, "proxy/event",
			    "%p <-%p<- SelectionNotify[%s]",
			    G_XIM_NATIVE_WINDOW_TO_POINTER (client_window),
			    G_XIM_NATIVE_WINDOW_TO_POINTER (selection_window),
			    prop);
	retval = g_xim_srv_tmpl_send_selection_notify(G_XIM_SRV_TMPL (proxy), &ev, prop, strlen(prop) + 1, NULL);
  end:
	g_xim_message_debug(core->message, "client/conn",
			    "Removing a client connection from the table for %p",
			    G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
	g_hash_table_remove(proxy->client_table,
			    G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
	g_hash_table_remove(proxy->selection_table,
			    G_XIM_NATIVE_WINDOW_TO_POINTER (selection_window));
	g_free(prop);

	return retval;
}

static gboolean
xim_proxy_client_real_notify_transport_cb(GXimClientTemplate *client,
					  const gchar        *transport,
					  gpointer            user_data)
{
	XimProxy *proxy = user_data;
	GXimCore *core = G_XIM_CORE (client);
	GdkWindow *w;
	gboolean retval = TRUE;
	GdkNativeWindow client_window = 0, selection_window = 0;
	GdkEventSelection ev;

	w = g_xim_core_get_selection_window(core);
	selection_window = GDK_WINDOW_XID (w);
	client_window = G_XIM_POINTER_TO_NATIVE_WINDOW (g_hash_table_lookup(proxy->selection_table,
									    G_XIM_NATIVE_WINDOW_TO_POINTER (selection_window)));
	if (client_window == 0) {
		g_xim_message_warning(core->message,
				      "Received SelectionNotify from unknown sender: %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (selection_window));
		goto end;
	}

	g_xim_message_debug(core->message, "proxy/event",
			    "%p <-%p<- SelectionNotify[%s]",
			    G_XIM_NATIVE_WINDOW_TO_POINTER (client_window),
			    G_XIM_NATIVE_WINDOW_TO_POINTER (selection_window),
			    transport);
	ev.requestor = client_window;
	ev.selection = client->atom_server;
	ev.target = core->atom_transport;
	ev.property = core->atom_transport;
	ev.requestor = client_window;
	retval = g_xim_srv_tmpl_send_selection_notify(G_XIM_SRV_TMPL (proxy), &ev, transport, strlen(transport) + 1, NULL);
  end:
	g_hash_table_remove(proxy->client_table,
			    G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
	g_hash_table_remove(proxy->selection_table,
			    G_XIM_NATIVE_WINDOW_TO_POINTER (selection_window));

	return retval;
}

static gboolean
xim_proxy_client_real_xconnect_cb(GXimClientTemplate *client,
				  GdkEventClient     *event,
				  gpointer            data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimCore *core = G_XIM_CORE (proxy);
	GXimConnection *cconn, *sconn;
	GXimTransport *ctrans, *strans;
	GdkNativeWindow nw, comm_window;
	GdkWindow *w;
	GdkDisplay *dpy = g_xim_core_get_display(core);
	GdkEventMask mask;
	GdkEvent *ev;

	nw = GDK_WINDOW_XID (event->window);
	comm_window = G_XIM_POINTER_TO_NATIVE_WINDOW (g_hash_table_lookup(proxy->comm_table,
									  G_XIM_NATIVE_WINDOW_TO_POINTER (nw)));
	if (comm_window == 0) {
		g_xim_message_warning(core->message,
				      "Got a response of XIM_XCONNECT for unknown client: %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (nw));
		return TRUE;
	}
	sconn = g_xim_srv_tmpl_lookup_connection_with_native_window(G_XIM_SRV_TMPL (proxy), comm_window);
	if (sconn == NULL) {
		g_xim_message_warning(core->message,
				      "No connection %p for a response of XIM_XCONNECT",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (comm_window));
		return TRUE;
	}
	cconn = client->connection;
	strans = G_XIM_TRANSPORT (sconn);
	ctrans = G_XIM_TRANSPORT (cconn);
	g_xim_transport_set_version(strans, event->data.l[1], event->data.l[2]);
	g_xim_transport_set_version(ctrans, event->data.l[1], event->data.l[2]);
	g_xim_transport_set_transport_max(strans, event->data.l[3]);
	g_xim_transport_set_transport_max(ctrans, event->data.l[3]);
	g_xim_transport_set_client_window(ctrans, event->data.l[0]);
	g_xim_core_add_client_message_filter(core,
					     g_xim_transport_get_atom(strans));
	g_xim_core_add_client_message_filter(G_XIM_CORE (client),
					     g_xim_transport_get_atom(ctrans));
	w = g_xim_get_window(dpy, event->data.l[0]);
	mask = gdk_window_get_events(w);
	gdk_window_set_events(w, mask | GDK_STRUCTURE_MASK);
	g_xim_core_watch_event(core, w);

	ev = gdk_event_new(GDK_CLIENT_EVENT);
	ev->client.window = g_object_ref(event->window);
	ev->client.message_type = event->message_type;
	ev->client.data_format = 32;
	ev->client.data.l[0] = (long)g_xim_transport_get_native_channel(strans);
	ev->client.data.l[1] = event->data.l[1];
	ev->client.data.l[2] = event->data.l[2];
	ev->client.data.l[3] = event->data.l[3];

	g_xim_message_debug(core->message, "proxy/event",
			    "%p <-%p<- XIM_XCONNECT[%p]",
			    G_XIM_NATIVE_WINDOW_TO_POINTER (comm_window),
			    G_XIM_NATIVE_WINDOW_TO_POINTER (nw),
			    G_XIM_NATIVE_WINDOW_TO_POINTER (event->data.l[0]));
	gdk_event_send_client_message_for_display(dpy, ev, comm_window);

	gdk_event_free(ev);

	client->is_connection_initialized = GXC_ESTABLISHED;

	return TRUE;
}

static void
xim_proxy_real_set_property(GObject      *object,
			     guint         prop_id,
			     const GValue *value,
			     GParamSpec   *pspec)
{
	XimProxy *proxy = XIM_PROXY (object);

	switch (prop_id) {
	    case PROP_CONNECT_TO:
		    proxy->connect_to = g_strdup(g_value_get_string(value));
		    break;
	    default:
		    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		    break;
	}
}

static void
xim_proxy_real_get_property(GObject    *object,
			     guint       prop_id,
			     GValue     *value,
			     GParamSpec *pspec)
{
	XimProxy *proxy = XIM_PROXY (object);

	switch (prop_id) {
	    case PROP_CONNECT_TO:
		    g_value_set_string(value, proxy->connect_to);
		    break;
	    default:
		    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		    break;
	}
}

static void
xim_proxy_real_finalize(GObject *object)
{
	XimProxy *proxy = XIM_PROXY (object);

	g_hash_table_destroy(proxy->client_table);
	g_hash_table_destroy(proxy->selection_table);
	g_hash_table_destroy(proxy->comm_table);
	g_free(proxy->connect_to);

	if (G_OBJECT_CLASS (xim_proxy_parent_class)->finalize)
		G_OBJECT_CLASS (xim_proxy_parent_class)->finalize(object);
}

static void
xim_proxy_real_setup_connection(GXimCore       *core,
				GXimConnection *conn)
{
}

static gboolean
xim_proxy_real_selection_request_event(GXimCore          *core,
				       GdkEventSelection *event)
{
	XimProxy *proxy = XIM_PROXY (core);
	XimClient *client;
	gboolean retval = FALSE;
	GdkNativeWindow nw;
	gchar *s;
	GError *error = NULL;

	client = _create_client(proxy, event->requestor);
	if (client == NULL)
		return FALSE;
	nw = GDK_WINDOW_XID (g_xim_core_get_selection_window(G_XIM_CORE (client)));
	s = gdk_atom_name(event->property);
	g_xim_message_debug(core->message, "proxy/event",
			    "%p ->%p-> SelectionRequest[%s]",
			    G_XIM_NATIVE_WINDOW_TO_POINTER (event->requestor),
			    G_XIM_NATIVE_WINDOW_TO_POINTER (nw),
			    s);
	g_free(s);
	if (event->property == core->atom_locales) {
		if (!g_xim_cl_tmpl_send_selection_request(G_XIM_CL_TMPL (client),
							  core->atom_locales,
							  &error)) {
			g_xim_message_error(core->message,
					    "Unable to get the supported locales from the server `%s': %s",
					    G_XIM_SRV_TMPL (proxy)->server_name,
					    error->message);
			g_error_free(error);
			/* XXX: fallback to the default behavior wouldn't be ideal.
			 * however no response from the server makes
			 * the applications getting on stuck easily.
			 * That may be a good idea to send back anything.
			 */
			retval = FALSE;
		}
	} else if (event->property == core->atom_transport) {
		if (!g_xim_cl_tmpl_send_selection_request(G_XIM_CL_TMPL (client),
							  core->atom_transport,
							  &error)) {
			g_xim_message_error(core->message,
					    "Unable to get the supported transport from the server `%s': %s",
					    G_XIM_SRV_TMPL (proxy)->server_name,
					    error->message);
			g_error_free(error);
			/* XXX: fallback to the default behavior wouldn't be ideal.
			 * however no response from the server makes
			 * the applications getting on stuck easily.
			 * That may be a good idea to send back anything.
			 */
			retval = FALSE;
		}
	} else {
		gchar *s = gdk_atom_name(event->property);

		g_xim_message_warning(core->message,
				      "%s: Unknown/unsupported Property received: %s",
				      __FUNCTION__, s);
		g_free(s);

		return FALSE;
	}

	return retval;
}

static gchar *
xim_proxy_real_get_supported_locales(GXimServerTemplate *server)
{
	return g_strdup("");
}

static gchar *
xim_proxy_real_get_supported_transport(GXimServerTemplate *server)
{
	return g_strdup("");
}

static void
_weak_notify_conn_cb(gpointer  data,
		     GObject  *object)
{
	GXimServerTemplate *server = data;
	GXimCore *core = G_XIM_CORE (server);
	GdkDisplay *dpy = g_xim_core_get_display(core);
	GdkNativeWindow nw;
	GdkWindow *w;

	nw = g_xim_transport_get_native_channel(G_XIM_TRANSPORT (object));
	if (nw) {
		g_xim_message_debug(core->message, "server/conn",
				    "Removing a connection from the table for %p",
				    G_XIM_NATIVE_WINDOW_TO_POINTER (nw));
		w = g_xim_get_window(dpy, nw);
		g_xim_core_unwatch_event(core, w);
		g_hash_table_remove(server->conn_table,
				    G_XIM_NATIVE_WINDOW_TO_POINTER (nw));
	}
	nw = g_xim_transport_get_client_window(G_XIM_TRANSPORT (object));
	if (nw) {
		g_xim_message_debug(core->message, "server/conn",
				    "Removing a connection from the table for %p",
				    G_XIM_NATIVE_WINDOW_TO_POINTER (nw));
		w = g_xim_get_window(dpy, nw);
		g_xim_core_unwatch_event(core, w);
		g_hash_table_remove(server->conn_table,
				    G_XIM_NATIVE_WINDOW_TO_POINTER (nw));
		g_xim_message_debug(core->message, "client/conn",
				    "Removing a client connection from the table for %p",
				    G_XIM_NATIVE_WINDOW_TO_POINTER (nw));
		g_hash_table_remove(XIM_PROXY (server)->client_table,
				    G_XIM_NATIVE_WINDOW_TO_POINTER (nw));
	}
#ifdef GNOME_ENABLE_DEBUG
	g_print("live server connection: %d\n", g_hash_table_size(server->conn_table));
	g_print("live client connection: %d\n", g_hash_table_size(XIM_PROXY (server)->client_table));
#endif
}

static GXimConnection *
xim_proxy_real_xconnect(GXimServerTemplate *server,
			GdkEventClient     *event)
{
	GdkNativeWindow w = event->data.l[0], comm_window;
	GXimConnection *conn = g_xim_srv_tmpl_lookup_connection_with_native_window(server, w);
	GXimCore *core = G_XIM_CORE (server);
	GXimTransport *trans;
	GdkDisplay *dpy = g_xim_core_get_display(core);
	GType gtype;
	XimProxy *proxy = XIM_PROXY (server);
	XimClient *client;

	if (conn) {
		g_xim_message_warning(core->message,
				      "Received XIM_XCONNECT event from %p again: duplicate connection",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (w));
		g_object_ref(conn);
		goto client_proc;
	}
	gtype = g_xim_core_get_connection_gtype(core);
	if (!g_type_is_a(gtype, G_TYPE_XIM_CONNECTION)) {
		g_xim_message_bug(core->message,
				  "given GObject type isn't inherited from GXimConnection");
		return NULL;
	}
	conn = G_XIM_CONNECTION (g_object_new(gtype,
					      "proto_signals", g_xim_core_get_protocol_signal_connector(core),
					      NULL));
	g_xim_core_setup_connection(core, conn);
	trans = G_XIM_TRANSPORT (conn);
	g_xim_transport_set_display(trans, dpy);
	g_xim_transport_set_client_window(trans, w);
	/* supposed to do create a comm_window.
	 * but what we really want here is GdkNativeWindow.
	 * so the return value of g_xim_transport_get_channel
	 * won't be used then.
	 */
	g_xim_transport_get_channel(trans, event->window);
	g_xim_core_add_client_message_filter(core,
					     g_xim_transport_get_atom(G_XIM_TRANSPORT (conn)));
	g_xim_srv_tmpl_add_connection(server, conn, w);
	g_object_weak_ref(G_OBJECT (conn),
			  _weak_notify_conn_cb,
			  server);

  client_proc:
	if ((client = g_hash_table_lookup(proxy->client_table,
					  G_XIM_NATIVE_WINDOW_TO_POINTER (w)))) {
		g_xim_message_warning(core->message,
				      "Received XIM_XCONNECT event from %p again: duplicate client",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (w));
		/* XXX: ignore it so far */
	} else {
		client = _create_client(XIM_PROXY (server), w);
		if (client == NULL) {
			return NULL;
		}
		g_signal_connect(client, "xconnect",
				 G_CALLBACK (xim_proxy_client_real_xconnect_cb),
				 server);
	}

	if (!g_xim_cl_tmpl_connect_to_server(G_XIM_CL_TMPL (client), NULL))
		return NULL;
	comm_window = g_xim_transport_get_native_channel(G_XIM_TRANSPORT (G_XIM_CL_TMPL (client)->connection));
	g_hash_table_insert(proxy->comm_table,
			    G_XIM_NATIVE_WINDOW_TO_POINTER (comm_window),
			    G_XIM_NATIVE_WINDOW_TO_POINTER (w));
	g_xim_message_debug(core->message, "proxy/event",
			    "%p ->%p-> XIM_XCONNECT",
			    G_XIM_NATIVE_WINDOW_TO_POINTER (w),
			    G_XIM_NATIVE_WINDOW_TO_POINTER (comm_window));
	G_XIM_CL_TMPL (client)->is_connection_initialized = GXC_NEGOTIATING;

	return conn;
}

static void
xim_proxy_class_init(XimProxyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GXimCoreClass *core_class = G_XIM_CORE_CLASS (klass);
	GXimServerTemplateClass *srvtmpl_class = G_XIM_SRV_TMPL_CLASS (klass);

	object_class->set_property = xim_proxy_real_set_property;
	object_class->get_property = xim_proxy_real_get_property;
	object_class->finalize     = xim_proxy_real_finalize;

	core_class->setup_connection        = xim_proxy_real_setup_connection;
	core_class->selection_request_event = xim_proxy_real_selection_request_event;

	srvtmpl_class->get_supported_locales   = xim_proxy_real_get_supported_locales;
	srvtmpl_class->get_supported_transport = xim_proxy_real_get_supported_transport;
	srvtmpl_class->xconnect                = xim_proxy_real_xconnect;

	/* properties */
	g_object_class_install_property(object_class, PROP_CONNECT_TO,
					g_param_spec_string("connect_to",
							    _("A XIM server name"),
							    _("A XIM server name would connects to"),
							    NULL,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/* signals */
}

static void
xim_proxy_init(XimProxy *proxy)
{
	GXimLazySignalConnector sigs[] = {
		{NULL, NULL, NULL}
	};

	proxy->client_table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
						    NULL, g_object_unref);
	proxy->selection_table = g_hash_table_new(g_direct_hash, g_direct_equal);
	proxy->comm_table = g_hash_table_new(g_direct_hash, g_direct_equal);
	g_object_set(proxy, "proto_signals", sigs, NULL);
}

static void
xim_proxy_connection_class_init(XimProxyConnectionClass *klass)
{
}

static void
xim_proxy_connection_init(XimProxyConnection *proto)
{
}

/*
 * Public functions
 */
XimProxy *
xim_proxy_new(GdkDisplay  *dpy,
	      const gchar *server_name,
	      const gchar *connect_to)
{
	XimProxy *retval;

	g_return_val_if_fail (GDK_IS_DISPLAY (dpy), NULL);
	g_return_val_if_fail (server_name != NULL, NULL);
	g_return_val_if_fail (connect_to != NULL, NULL);

	retval = XIM_PROXY (g_object_new(XIM_TYPE_PROXY,
					 "display", dpy,
					 "server_name", server_name,
					 "connection_gtype", XIM_TYPE_PROXY_CONNECTION,
					 "connect_to", connect_to,
					  NULL));

	return retval;
}

gboolean
xim_proxy_take_ownership(XimProxy  *proxy,
			  gboolean    force,
			  GError    **error)
{
	return g_xim_srv_tmpl_take_ownership(G_XIM_SRV_TMPL (proxy),
					     force,
					     error);
}
