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
#include <libgxim/gximattr.h>
#include <libgxim/gximerror.h>
#include <libgxim/gximmarshal.h>
#include <libgxim/gximmessage.h>
#include <libgxim/gximmisc.h>
#include <libgxim/gximprotocol.h>
#include <libgxim/gximtransport.h>
#include "client.h"
#include "proxy.h"


enum {
	PROP_0,
	PROP_CONNECT_TO,
	PROP_CLIENT_PROTO_SIGNALS,
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

static GXimClientConnection *
_get_client_connection(XimProxy     *proxy,
		       GXimProtocol *proto)
{
	GXimTransport *trans = G_XIM_TRANSPORT (proto);
	GdkNativeWindow client_window;
	XimClient *client;

	client_window = g_xim_transport_get_client_window(trans);
	client = g_hash_table_lookup(proxy->client_table,
				     G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
	if (client == NULL) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "No client connection to deliver XIM_CONNECT: %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}
	return G_XIM_CLIENT_CONNECTION (G_XIM_CL_TMPL (client)->connection);
}

static GXimServerConnection *
_get_server_connection(XimProxy     *proxy,
		       GXimProtocol *proto)
{
	GXimTransport *trans;
	GdkNativeWindow nw, comm_window;

	trans = G_XIM_TRANSPORT (proto);
	nw = g_xim_transport_get_native_channel(trans);
	comm_window = G_XIM_POINTER_TO_NATIVE_WINDOW (g_hash_table_lookup(proxy->comm_table,
									  G_XIM_NATIVE_WINDOW_TO_POINTER (nw)));
	return g_hash_table_lookup(proxy->sconn_table,
				   G_XIM_NATIVE_WINDOW_TO_POINTER (comm_window));
}

static gboolean
xim_proxy_client_real_notify_locales_cb(GXimClientTemplate  *client,
					gchar              **locales,
					gpointer             user_data)
{
	XimProxy *proxy = XIM_PROXY (user_data);
	GXimCore *core = G_XIM_CORE (client);
	GdkWindow *w;
	gchar *prop = NULL, *s = NULL;
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
	if (locales)
		s = g_strjoinv(",", locales);
	prop = g_strdup_printf("@locale=%s", s);
	g_free(s);
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
	GdkDisplay *dpy = g_xim_core_get_display(core);
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

static gboolean
xim_proxy_client_protocol_real_xim_connect_reply(GXimProtocol *proto,
						 guint16       major_version,
						 guint16       minor_version,
						 gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_connect_reply(conn, major_version, minor_version);
}

static gboolean
xim_proxy_client_protocol_real_xim_open_reply(GXimProtocol *proto,
					      guint16       imid,
					      GXimIMAttr   *imattr,
					      GXimICAttr   *icattr,
					      gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;
	GXimConnection *c;
	GdkNativeWindow client_window;
	GXimClientTemplate *client;
	GXimAttr *attr;
	GXimRawAttr *raw;
	GSList *l, *imlist = NULL, *iclist = NULL, *list;
	gboolean retval;

	conn = _get_server_connection(proxy, proto);
	c = G_XIM_CONNECTION (conn);
	c->imid = imid;
	c->imattr = g_object_ref(imattr);
	c->default_icattr = g_object_ref(icattr);
	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (conn));
	client = g_hash_table_lookup(proxy->client_table,
				     G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
	c = G_XIM_CONNECTION (client->connection);
	c->imid = imid;
	c->imattr = g_object_ref(imattr);
	c->default_icattr = g_object_ref(icattr);

	attr = G_XIM_ATTR (imattr);
	list = g_xim_attr_get_supported_attributes(attr);
	for (l = list; l != NULL; l = g_slist_next(l)) {
		if (l->data && g_xim_attr_attribute_is_enabled(attr, l->data)) {
			guint id;
			GType gtype;
			GXimValueType vtype;
			GString *s;

			id = g_xim_attr_get_attribute_id(attr, l->data);
			gtype = g_xim_attr_get_gtype_by_name(attr, l->data);
			vtype = g_xim_gtype_to_value_type(gtype);
			if (vtype == G_XIM_TYPE_INVALID) {
				g_xim_message_bug(G_XIM_CORE (proxy)->message,
						  "Unable to compose a XIMATTR for %s",
						  (gchar *)l->data);
				continue;
			}
			s = g_string_new(l->data);
			raw = g_xim_raw_attr_new_with_value(id, s, vtype);
			imlist = g_slist_append(imlist, raw);
		}
		g_free(l->data);
	}
	g_slist_free(list);

	attr = G_XIM_ATTR (icattr);
	list = g_xim_attr_get_supported_attributes(attr);
	for (l = list; l != NULL; l = g_slist_next(l)) {
		if (l->data && g_xim_attr_attribute_is_enabled(attr, l->data)) {
			guint id;
			GType gtype;
			GXimValueType vtype;
			GString *s;

			id = g_xim_attr_get_attribute_id(attr, l->data);
			gtype = g_xim_attr_get_gtype_by_name(attr, l->data);
			vtype = g_xim_gtype_to_value_type(gtype);
			if (vtype == G_XIM_TYPE_INVALID) {
				g_xim_message_bug(G_XIM_CORE (proxy)->message,
						  "Unable to compose a XICATTR for %s",
						  (gchar *)l->data);
				continue;
			}
			s = g_string_new(l->data);
			raw = g_xim_raw_attr_new_with_value(id, s, vtype);
			iclist = g_slist_append(iclist, raw);
		}
		g_free(l->data);
	}
	g_slist_free(list);

	retval = g_xim_server_connection_open_reply(conn, imid, imlist, iclist);

	g_slist_foreach(imlist, (GFunc)g_xim_raw_attr_free, NULL);
	g_slist_foreach(iclist, (GFunc)g_xim_raw_attr_free, NULL);
	g_slist_free(imlist);
	g_slist_free(iclist);

	return retval;
}

static gboolean
xim_proxy_client_protocol_real_xim_query_extension_reply(GXimProtocol *proto,
							 guint16       imid,
							 const GSList *extensions,
							 gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_query_extension_reply(conn, imid, extensions);
}

static gboolean
xim_proxy_client_protocol_real_xim_encoding_negotiation_reply(GXimProtocol *proto,
							      guint16       imid,
							      guint16       category,
							      gint16        index,
							      gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_encoding_negotiation_reply(conn, imid, category, index);
}

static gboolean
xim_proxy_client_protocol_real_xim_get_im_values_reply(GXimProtocol *proto,
						       guint16       imid,
						       const GSList *attributes,
						       gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_get_im_values_reply(conn, imid, attributes);
}

static gboolean
xim_proxy_client_protocol_real_xim_create_ic_reply(GXimProtocol *proto,
						   guint16       imid,
						   guint16       icid,
						   gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_create_ic_reply(conn, imid, icid);
}

static gboolean
xim_proxy_client_protocol_real_xim_set_event_mask(GXimProtocol *proto,
						  guint16       imid,
						  guint16       icid,
						  guint32       forward_event,
						  guint32       sync_event,
						  gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_set_event_mask(conn, imid, icid, forward_event, sync_event);
}

static gboolean
xim_proxy_client_protocol_real_xim_get_ic_values_reply(GXimProtocol *proto,
						       guint16       imid,
						       guint16       icid,
						       const GSList *attributes,
						       gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_get_ic_values_reply(conn, imid, icid, attributes);
}

static gboolean
xim_proxy_client_protocol_real_xim_destroy_ic_reply(GXimProtocol *proto,
						    guint16       imid,
						    guint16       icid,
						    gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_destroy_ic_reply(conn, imid, icid);
}

static gboolean
xim_proxy_client_protocol_real_xim_close_reply(GXimProtocol *proto,
					       guint16       imid,
					       gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_close_reply(conn, imid);
}

static gboolean
xim_proxy_client_protocol_real_xim_disconnect_reply(GXimProtocol *proto,
						    gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_disconnect_reply(conn);
}

static gboolean
xim_proxy_client_protocol_real_xim_preedit_start(GXimProtocol *proto,
						 guint16       imid,
						 guint16       icid,
						 gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_preedit_start(conn, imid, icid);
}

static gboolean
xim_proxy_client_protocol_real_xim_preedit_caret(GXimProtocol     *proto,
						 guint16           imid,
						 guint16           icid,
						 GXimPreeditCaret *caret,
						 gpointer          data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_preedit_caret(conn, imid, icid, caret);
}

static gboolean
xim_proxy_client_protocol_real_xim_preedit_done(GXimProtocol *proto,
						guint16       imid,
						guint16       icid,
						gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_preedit_done(conn, imid, icid);
}

static gboolean
xim_proxy_client_protocol_real_xim_forward_event(GXimProtocol *proto,
						 guint16       imid,
						 guint16       icid,
						 guint16       flag,
						 GdkEvent     *event,
						 gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_forward_event(conn, imid, icid, flag, event);
}

static gboolean
xim_proxy_client_protocol_real_xim_sync_reply(GXimProtocol *proto,
					      guint16       imid,
					      guint16       icid,
					      gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_sync_reply(conn, imid, icid);
}

static gboolean
xim_proxy_client_protocol_real_xim_preedit_draw(GXimProtocol    *proto,
						guint16          imid,
						guint16          icid,
						GXimPreeditDraw *draw,
						gpointer         data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_preedit_draw(conn, imid, icid, draw);
}

static gboolean
xim_proxy_client_protocol_real_xim_commit(GXimProtocol *proto,
					  guint16       imid,
					  guint16       icid,
					  guint16       flag,
					  guint32       keysym,
					  GString      *string,
					  gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_commit(conn, imid, icid, flag, keysym, string);
}

static gboolean
xim_proxy_client_protocol_real_xim_status_start(GXimProtocol *proto,
						guint16       imid,
						guint16       icid,
						gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_status_start(conn, imid, icid);
}

static gboolean
xim_proxy_client_protocol_real_xim_status_done(GXimProtocol *proto,
					       guint16       imid,
					       guint16       icid,
					       gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_status_done(conn, imid, icid);
}

static gboolean
xim_proxy_client_protocol_real_xim_register_triggerkeys(GXimProtocol *proto,
							guint16       imid,
							const GSList *onkeys,
							const GSList *offkeys,
							gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_register_triggerkeys(conn, imid, onkeys, offkeys);
}

static gboolean
xim_proxy_client_protocol_real_xim_trigger_notify_reply(GXimProtocol *proto,
							guint16       imid,
							guint16       icid,
							gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_trigger_notify_reply(conn, imid, icid);
}

static gboolean
xim_proxy_client_protocol_real_xim_status_draw(GXimProtocol   *proto,
					       guint16         imid,
					       guint16         icid,
					       GXimStatusDraw *draw,
					       gpointer        data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_status_draw(conn, imid, icid, draw);
}

static gboolean
xim_proxy_client_protocol_real_xim_set_ic_values_reply(GXimProtocol *proto,
						       guint16       imid,
						       guint16       icid,
						       gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_set_ic_values_reply(conn, imid, icid);
}

static gboolean
xim_proxy_client_protocol_real_xim_reset_ic_reply(GXimProtocol  *proto,
						  guint16        imid,
						  guint16        icid,
						  const GString *preedit_string,
						  gpointer       data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_server_connection_reset_ic_reply(conn, imid, icid, preedit_string);
}

static gboolean
xim_proxy_client_protocol_real_xim_error(GXimProtocol  *proto,
					 guint16        imid,
					 guint16        icid,
					 GXimErrorMask  flag,
					 GXimErrorCode  error_code,
					 guint16        detail,
					 const gchar   *error_message,
					 gpointer       data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimServerConnection *conn;

	conn = _get_server_connection(proxy, proto);

	return g_xim_connection_error(G_XIM_CONNECTION (conn), imid, icid, flag, error_code, detail, error_message);
}

static void
xim_proxy_real_set_property(GObject      *object,
			     guint         prop_id,
			     const GValue *value,
			     GParamSpec   *pspec)
{
	XimProxy *proxy = XIM_PROXY (object);
	GXimLazySignalConnector *sigs;
	gsize len, i;

	switch (prop_id) {
	    case PROP_CONNECT_TO:
		    proxy->connect_to = g_strdup(g_value_get_string(value));
		    break;
	    case PROP_CLIENT_PROTO_SIGNALS:
		    if (proxy->client_proto_signals != NULL) {
			    g_xim_message_warning(G_XIM_CORE (object)->message,
						  "Unable to update the signal list.");
			    break;
		    }
		    sigs = g_value_get_pointer(value);
		    for (len = 0; sigs[len].signal_name != NULL; len++);
		    proxy->client_proto_signals = g_new0(GXimLazySignalConnector, len + 1);
		    for (i = 0; sigs[i].signal_name != NULL; i++) {
			    proxy->client_proto_signals[i].signal_name = g_strdup(sigs[i].signal_name);
			    proxy->client_proto_signals[i].function = sigs[i].function;
			    proxy->client_proto_signals[i].data = sigs[i].data;
		    }
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
	    case PROP_CLIENT_PROTO_SIGNALS:
		    g_value_set_pointer(value, proxy->client_proto_signals);
		    break;
	    default:
		    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		    break;
	}
}

static void
xim_proxy_real_dispose(GObject *object)
{
	XimProxy *proxy = XIM_PROXY (object);
	GHashTableIter iter;
	gpointer key, val;

	g_hash_table_iter_init(&iter, proxy->sconn_table);
	while (g_hash_table_iter_next(&iter, &key, &val)) {
		g_xim_srv_tmpl_remove_connection(G_XIM_SRV_TMPL (proxy),
						 G_XIM_POINTER_TO_NATIVE_WINDOW (key));
		/* hash might be changed */
		g_hash_table_iter_init(&iter, proxy->sconn_table);
	}

	if (G_OBJECT_CLASS (xim_proxy_parent_class)->dispose)
		(* G_OBJECT_CLASS (xim_proxy_parent_class)->dispose) (object);
}

static void
xim_proxy_real_finalize(GObject *object)
{
	XimProxy *proxy = XIM_PROXY (object);
	gint i;

	g_hash_table_destroy(proxy->sconn_table);
	g_hash_table_destroy(proxy->client_table);
	g_hash_table_destroy(proxy->selection_table);
	g_hash_table_destroy(proxy->comm_table);
	g_free(proxy->connect_to);
	for (i = 0; proxy->client_proto_signals[i].signal_name != NULL; i++) {
		g_free(proxy->client_proto_signals[i].signal_name);
	}
	g_free(proxy->client_proto_signals);

	if (G_OBJECT_CLASS (xim_proxy_parent_class)->finalize)
		(* G_OBJECT_CLASS (xim_proxy_parent_class)->finalize) (object);
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
	GdkNativeWindow nw, cnw;
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
		cnw = g_xim_transport_get_client_window(G_XIM_TRANSPORT (object));
		g_hash_table_remove(XIM_PROXY (server)->sconn_table,
				    G_XIM_NATIVE_WINDOW_TO_POINTER (cnw));
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
	g_print("live server connection: %d [%d]\n",
		g_hash_table_size(server->conn_table),
		g_hash_table_size(XIM_PROXY (server)->sconn_table));
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
	GError *error = NULL;

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
		g_object_set(G_OBJECT (client), "proto_signals", proxy->client_proto_signals, NULL);
		g_signal_connect(client, "xconnect",
				 G_CALLBACK (xim_proxy_client_real_xconnect_cb),
				 server);
	}

	if (!g_xim_cl_tmpl_connect_to_server(G_XIM_CL_TMPL (client), &error)) {
		g_xim_message_gerror(core->message, error);
		g_error_free(error);

		return NULL;
	}
	comm_window = g_xim_transport_get_native_channel(G_XIM_TRANSPORT (G_XIM_CL_TMPL (client)->connection));
	g_hash_table_insert(proxy->comm_table,
			    G_XIM_NATIVE_WINDOW_TO_POINTER (comm_window),
			    G_XIM_NATIVE_WINDOW_TO_POINTER (w));
	g_hash_table_insert(proxy->sconn_table,
			    G_XIM_NATIVE_WINDOW_TO_POINTER (w),
			    conn);
	g_xim_message_debug(core->message, "proxy/event",
			    "%p ->%p-> XIM_XCONNECT",
			    G_XIM_NATIVE_WINDOW_TO_POINTER (w),
			    G_XIM_NATIVE_WINDOW_TO_POINTER (comm_window));
	G_XIM_CL_TMPL (client)->is_connection_initialized = GXC_NEGOTIATING;

	return conn;
}

static gboolean
xim_proxy_protocol_real_xim_connect(GXimProtocol *proto,
				    guint16       major_version,
				    guint16       minor_version,
				    const GSList *list,
				    gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_connect(conn,
					     major_version,
					     minor_version,
					     (GSList *)list,
					     TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_CONNECT for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_open(GXimProtocol  *proto,
				 const GXimStr *locale,
				 gpointer       data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_open_im(conn,
					     locale,
					     TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_OPEN for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_query_extension(GXimProtocol *proto,
					    guint16       imid,
					    const GSList *extensions,
					    gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_query_extension(conn, imid, extensions, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_QUERY_EXTENSION for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_encoding_negotiation(GXimProtocol *proto,
						 guint16       imid,
						 const GSList *encodings,
						 const GSList *details,
						 gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_encoding_negotiation(conn, imid, encodings, details, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_ENCODING_NEGOTIATION for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_get_im_values(GXimProtocol *proto,
					  guint16       imid,
					  const GSList *attr_id,
					  gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_get_im_values(conn, imid, attr_id, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_GET_IM_VALUES for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_create_ic(GXimProtocol *proto,
				      guint16       imid,
				      const GSList *attributes,
				      gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_create_ic(conn, imid, attributes, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_CREATE_IC for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_get_ic_values(GXimProtocol *proto,
					  guint16       imid,
					  guint16       icid,
					  const GSList *attr_id,
					  gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_get_ic_values(conn, imid, icid, attr_id, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_GET_IC_VALUES for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_destroy_ic(GXimProtocol *proto,
				       guint16       imid,
				       guint16       icid,
				       gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_destroy_ic(conn, imid, icid, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_DESTROY_IC for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_close(GXimProtocol *proto,
				  guint16       imid,
				  gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_close_im(conn, imid, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_CLOSE for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_disconnect(GXimProtocol *proto,
				       gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	/* We don't want to unref a client connection by the request from
	 * the XIM server. it should be done by triggering destroying
	 * the server connection.
	 */
	g_object_ref(conn);

	if (!g_xim_client_connection_disconnect(conn, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_DISCONNECT for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_set_ic_focus(GXimProtocol *proto,
					 guint16       imid,
					 guint16       icid,
					 gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_set_ic_focus(conn, imid, icid)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_SET_IC_FOCUS for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_unset_ic_focus(GXimProtocol *proto,
					   guint16       imid,
					   guint16       icid,
					   gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_unset_ic_focus(conn, imid, icid)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_UNSET_IC_FOCUS for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_preedit_start_reply(GXimProtocol *proto,
						guint16       imid,
						guint16       icid,
						gint32        return_value,
						gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_preedit_start_reply(conn, imid, icid, return_value)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_PREEDIT_START_REPLY for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_preedit_caret_reply(GXimProtocol *proto,
						guint16       imid,
						guint16       icid,
						guint32       position,
						gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_preedit_caret_reply(conn, imid, icid, position)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_PREEDIT_CARET_REPLY for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_forward_event(GXimProtocol *proto,
					  guint16       imid,
					  guint16       icid,
					  guint16       flag,
					  GdkEvent     *event,
					  gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_forward_event(conn, imid, icid, flag, event)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_FORWARD_EVENT for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_sync_reply(GXimProtocol *proto,
				       guint16       imid,
				       guint16       icid,
				       gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_sync_reply(conn, imid, icid)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_SYNC_REPLY for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_trigger_notify(GXimProtocol *proto,
					   guint16       imid,
					   guint16       icid,
					   guint32       flag,
					   guint32       index,
					   guint32       event_mask,
					   gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_trigger_notify(conn, imid, icid, flag, index, event_mask, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_TRIGGER_NOTIFY for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_set_ic_values(GXimProtocol *proto,
					  guint16       imid,
					  guint16       icid,
					  const GSList *attributes,
					  gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_set_ic_values(conn, imid, icid, attributes, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_TRIGGER_NOTIFY for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_reset_ic(GXimProtocol *proto,
				     guint16       imid,
				     guint16       icid,
				     gpointer      data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_client_connection_reset_ic(conn, imid, icid, TRUE)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_RESET_IC for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static gboolean
xim_proxy_protocol_real_xim_error(GXimProtocol  *proto,
				  guint16        imid,
				  guint16        icid,
				  GXimErrorMask  flag,
				  GXimErrorCode  error_code,
				  guint16        detail,
				  const gchar   *error_message,
				  gpointer       data)
{
	XimProxy *proxy = XIM_PROXY (data);
	GXimClientConnection *conn = _get_client_connection(proxy, proto);
	GdkNativeWindow	client_window = g_xim_transport_get_client_window(G_XIM_TRANSPORT (proto));

	if (!g_xim_connection_error(G_XIM_CONNECTION (conn), imid, icid, flag, error_code, detail, error_message)) {
		g_xim_message_warning(G_XIM_PROTOCOL_GET_IFACE (proto)->message,
				      "Unable to deliver XIM_ERROR for %p",
				      G_XIM_NATIVE_WINDOW_TO_POINTER (client_window));
		return FALSE;
	}

	return TRUE;
}

static void
xim_proxy_class_init(XimProxyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GXimCoreClass *core_class = G_XIM_CORE_CLASS (klass);
	GXimServerTemplateClass *srvtmpl_class = G_XIM_SRV_TMPL_CLASS (klass);

	object_class->set_property = xim_proxy_real_set_property;
	object_class->get_property = xim_proxy_real_get_property;
	object_class->dispose      = xim_proxy_real_dispose;
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
	g_object_class_install_property(object_class, PROP_CLIENT_PROTO_SIGNALS,
					g_param_spec_pointer("client_proto_signals",
							     _("Signals for Protocol class"),
							     _("A structure of signals for Protocol class"),
							     G_PARAM_READWRITE));

	/* signals */
}

static void
xim_proxy_init(XimProxy *proxy)
{
	GXimLazySignalConnector sigs[] = {
		{"XIM_CONNECT", G_CALLBACK (xim_proxy_protocol_real_xim_connect), proxy},
		{"XIM_OPEN", G_CALLBACK (xim_proxy_protocol_real_xim_open), proxy},
		{"XIM_QUERY_EXTENSION", G_CALLBACK (xim_proxy_protocol_real_xim_query_extension), proxy},
		{"XIM_ENCODING_NEGOTIATION", G_CALLBACK (xim_proxy_protocol_real_xim_encoding_negotiation), proxy},
		{"XIM_GET_IM_VALUES", G_CALLBACK (xim_proxy_protocol_real_xim_get_im_values), proxy},
		{"XIM_CREATE_IC", G_CALLBACK (xim_proxy_protocol_real_xim_create_ic), proxy},
		{"XIM_GET_IC_VALUES", G_CALLBACK (xim_proxy_protocol_real_xim_get_ic_values), proxy},
		{"XIM_DESTROY_IC", G_CALLBACK (xim_proxy_protocol_real_xim_destroy_ic), proxy},
		{"XIM_CLOSE", G_CALLBACK (xim_proxy_protocol_real_xim_close), proxy},
		{"XIM_DISCONNECT", G_CALLBACK (xim_proxy_protocol_real_xim_disconnect), proxy},
		{"XIM_SET_IC_FOCUS", G_CALLBACK (xim_proxy_protocol_real_xim_set_ic_focus), proxy},
		{"XIM_UNSET_IC_FOCUS", G_CALLBACK (xim_proxy_protocol_real_xim_unset_ic_focus), proxy},
		{"XIM_PREEDIT_START_REPLY", G_CALLBACK (xim_proxy_protocol_real_xim_preedit_start_reply), proxy},
		{"XIM_PREEDIT_CARET_REPLY", G_CALLBACK (xim_proxy_protocol_real_xim_preedit_caret_reply), proxy},
		{"XIM_FORWARD_EVENT", G_CALLBACK (xim_proxy_protocol_real_xim_forward_event), proxy},
		{"XIM_SYNC_REPLY", G_CALLBACK (xim_proxy_protocol_real_xim_sync_reply), proxy},
		{"XIM_TRIGGER_NOTIFY", G_CALLBACK (xim_proxy_protocol_real_xim_trigger_notify), proxy},
		{"XIM_SET_IC_VALUES", G_CALLBACK (xim_proxy_protocol_real_xim_set_ic_values), proxy},
		{"XIM_RESET_IC", G_CALLBACK (xim_proxy_protocol_real_xim_reset_ic), proxy},
		{"XIM_ERROR", G_CALLBACK (xim_proxy_protocol_real_xim_error), proxy},
		{NULL, NULL, NULL}
	};
	GXimLazySignalConnector csigs[] = {
		{"XIM_CONNECT_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_connect_reply), proxy},
		{"XIM_OPEN_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_open_reply), proxy},
		{"XIM_QUERY_EXTENSION_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_query_extension_reply), proxy},
		{"XIM_ENCODING_NEGOTIATION_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_encoding_negotiation_reply), proxy},
		{"XIM_GET_IM_VALUES_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_get_im_values_reply), proxy},
		{"XIM_CREATE_IC_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_create_ic_reply), proxy},
		{"XIM_SET_EVENT_MASK", G_CALLBACK (xim_proxy_client_protocol_real_xim_set_event_mask), proxy},
		{"XIM_GET_IC_VALUES_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_get_ic_values_reply), proxy},
		{"XIM_DESTROY_IC_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_destroy_ic_reply), proxy},
		{"XIM_CLOSE_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_close_reply), proxy},
		{"XIM_DISCONNECT_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_disconnect_reply), proxy},
		{"XIM_PREEDIT_START", G_CALLBACK (xim_proxy_client_protocol_real_xim_preedit_start), proxy},
		{"XIM_PREEDIT_CARET", G_CALLBACK (xim_proxy_client_protocol_real_xim_preedit_caret), proxy},
		{"XIM_PREEDIT_DONE", G_CALLBACK (xim_proxy_client_protocol_real_xim_preedit_done), proxy},
		{"XIM_FORWARD_EVENT", G_CALLBACK (xim_proxy_client_protocol_real_xim_forward_event), proxy},
		{"XIM_SYNC_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_sync_reply), proxy},
		{"XIM_PREEDIT_DRAW", G_CALLBACK (xim_proxy_client_protocol_real_xim_preedit_draw), proxy},
		{"XIM_COMMIT", G_CALLBACK (xim_proxy_client_protocol_real_xim_commit), proxy},
		{"XIM_STATUS_START", G_CALLBACK (xim_proxy_client_protocol_real_xim_status_start), proxy},
		{"XIM_STATUS_DONE", G_CALLBACK (xim_proxy_client_protocol_real_xim_status_done), proxy},
		{"XIM_REGISTER_TRIGGERKEYS", G_CALLBACK (xim_proxy_client_protocol_real_xim_register_triggerkeys), proxy},
		{"XIM_TRIGGER_NOTIFY_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_trigger_notify_reply), proxy},
		{"XIM_STATUS_DRAW", G_CALLBACK (xim_proxy_client_protocol_real_xim_status_draw), proxy},
		{"XIM_SET_IC_VALUES_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_set_ic_values_reply), proxy},
		{"XIM_RESET_IC_REPLY", G_CALLBACK (xim_proxy_client_protocol_real_xim_reset_ic_reply), proxy},
		{"XIM_ERROR", G_CALLBACK (xim_proxy_client_protocol_real_xim_error), proxy},
		{NULL, NULL, NULL}
	};

	proxy->client_table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
						    NULL, g_object_unref);
	proxy->selection_table = g_hash_table_new(g_direct_hash, g_direct_equal);
	proxy->comm_table = g_hash_table_new(g_direct_hash, g_direct_equal);
	proxy->sconn_table = g_hash_table_new(g_direct_hash, g_direct_equal);
	g_object_set(proxy, "proto_signals", sigs, NULL);
	g_object_set(proxy, "client_proto_signals", csigs, NULL);
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
