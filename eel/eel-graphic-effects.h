/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

   eel-graphic-effects.h: Pixmap manipulation routines for graphical effects.

   Copyright (C) 2000 Eazel, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Authors: Andy Hertzfeld <andy@eazel.com>
 */

#ifndef EEL_GRAPHIC_EFFECTS_H
#define EEL_GRAPHIC_EFFECTS_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

/* return a lightened pixbuf for pre-lighting */
GdkPixbuf *eel_create_spotlight_pixbuf(GdkPixbuf *source_pixbuf);
/* return a lightened surface for pre-lighting */
cairo_surface_t *eel_create_spotlight_surface(cairo_surface_t *source_surface,
                                              int scale);

/* return a darkened pixbuf for selection hiliting */
GdkPixbuf *eel_create_darkened_pixbuf(GdkPixbuf *source_pixbuf, int saturation,
                                      int darken);

/* return a pixbuf colorized with the color specified by the parameters */
GdkPixbuf *eel_create_colorized_pixbuf(GdkPixbuf *source_pixbuf,
                                       GdkRGBA *color);

/* stretch a image frame */
GdkPixbuf *eel_stretch_frame_image(GdkPixbuf *frame_image, int left_offset,
                                   int top_offset, int right_offset,
                                   int bottom_offset, int dest_width,
                                   int dest_height, gboolean fill_flag);

/* embed in image in a frame */
GdkPixbuf *eel_embed_image_in_frame(GdkPixbuf *source_image,
                                    GdkPixbuf *frame_image, int left_offset,
                                    int top_offset, int right_offset,
                                    int bottom_offset);

#endif /* EEL_GRAPHIC_EFFECTS_H */
