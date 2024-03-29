/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * imsettings-xsettings.h
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
#ifndef __IMSETTINGS_XSETTINGS_H__
#define __IMSETTINGS_XSETTINGS_H__

#include <glib.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

typedef struct _IMSettingsXSettings		IMSettingsXSettings;


gboolean             imsettings_xsettings_is_available     (GdkDisplay          *dpy);
IMSettingsXSettings *imsettings_xsettings_new              (GdkDisplay          *display,
							    GFreeFunc            term_func,
							    gpointer             data);
IMSettingsXSettings *imsettings_xsettings_new_with_gdkevent(GdkDisplay          *display,
							    GFreeFunc            term_func,
							    gpointer             data);
void                 imsettings_xsettings_free             (IMSettingsXSettings *xsettings);

G_END_DECLS

#endif /* __IMSETTINGS_XSETTINGS_H__ */
