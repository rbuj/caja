/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright © 2002 Christophe Fergeau
 *  Copyright © 2003, 2004 Marco Pesenti Gritti
 *  Copyright © 2003, 2004, 2005 Christian Persch
 *    (ephy-notebook.c)
 *
 *  Copyright © 2008 Free Software Foundation, Inc.
 *    (caja-notebook.c)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "caja-notebook.h"

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libcaja-private/caja-dnd.h>

#include "caja-navigation-window-pane.h"
#include "caja-navigation-window.h"
#include "caja-window-manage-views.h"
#include "caja-window-private.h"
#include "caja-window-slot.h"

#define AFTER_ALL_TABS -1

static void caja_notebook_constructed(GObject *object);

static int caja_notebook_insert_page(GtkNotebook *notebook, GtkWidget *child,
                                     GtkWidget *tab_label,
                                     GtkWidget *menu_label, int position);

static void caja_notebook_remove(GtkContainer *container,
                                 GtkWidget *tab_widget);

static gboolean caja_notebook_scroll_event(GtkWidget *widget,
                                           GdkEventScroll *event);

enum { TAB_CLOSE_REQUEST, LAST_SIGNAL };

static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE(CajaNotebook, caja_notebook, GTK_TYPE_NOTEBOOK);

static void caja_notebook_class_init(CajaNotebookClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  GtkNotebookClass *notebook_class = GTK_NOTEBOOK_CLASS(klass);

  object_class->constructed = caja_notebook_constructed;

  container_class->remove = caja_notebook_remove;

  widget_class->scroll_event = caja_notebook_scroll_event;

  notebook_class->insert_page = caja_notebook_insert_page;

  signals[TAB_CLOSE_REQUEST] = g_signal_new(
      "tab-close-request", G_OBJECT_CLASS_TYPE(object_class), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(CajaNotebookClass, tab_close_request), NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, CAJA_TYPE_WINDOW_SLOT);
}

static gint find_tab_num_at_pos(CajaNotebook *notebook, gint abs_x,
                                gint abs_y) {
  GtkPositionType tab_pos;
  int page_num = 0;
  GtkNotebook *nb = GTK_NOTEBOOK(notebook);
  GtkWidget *page;
  GtkAllocation allocation;

  tab_pos = gtk_notebook_get_tab_pos(GTK_NOTEBOOK(notebook));

  while ((page = gtk_notebook_get_nth_page(nb, page_num))) {
    GtkWidget *tab;
    gint max_x, max_y;
    gint x_root, y_root;

    tab = gtk_notebook_get_tab_label(nb, page);
    g_return_val_if_fail(tab != NULL, -1);

    if (!gtk_widget_get_mapped(GTK_WIDGET(tab))) {
      page_num++;
      continue;
    }

    gdk_window_get_origin(gtk_widget_get_window(tab), &x_root, &y_root);
    gtk_widget_get_allocation(tab, &allocation);

    max_x = x_root + allocation.x + allocation.width;
    max_y = y_root + allocation.y + allocation.height;

    if (((tab_pos == GTK_POS_TOP) || (tab_pos == GTK_POS_BOTTOM)) &&
        (abs_x <= max_x)) {
      return page_num;
    } else if (((tab_pos == GTK_POS_LEFT) || (tab_pos == GTK_POS_RIGHT)) &&
               (abs_y <= max_y)) {
      return page_num;
    }

    page_num++;
  }
  return AFTER_ALL_TABS;
}

static gboolean button_press_cb(CajaNotebook *notebook, GdkEventButton *event,
                                gpointer data) {
  int tab_clicked;

  tab_clicked = find_tab_num_at_pos(notebook, event->x_root, event->y_root);

  if (event->type == GDK_BUTTON_PRESS &&
      (event->button == 3 || event->button == 2) &&
      (event->state & gtk_accelerator_get_default_mod_mask()) == 0) {
    if (tab_clicked == -1) {
      /* consume event, so that we don't pop up the context menu when
       * the mouse if not over a tab label
       */
      return TRUE;
    }

    /* switch to the page the mouse is over, but don't consume the event */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), tab_clicked);
  }

  return FALSE;
}

static void caja_notebook_init(CajaNotebook *notebook) {
  GtkStyleContext *context;

  context = gtk_widget_get_style_context(GTK_WIDGET(notebook));
  gtk_style_context_add_class(context, "caja-notebook");

  gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
  gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);

  g_signal_connect(notebook, "button-press-event", (GCallback)button_press_cb,
                   NULL);
}

static void caja_notebook_constructed(GObject *object) {
  GtkWidget *widget = GTK_WIDGET(object);

  G_OBJECT_CLASS(caja_notebook_parent_class)->constructed(object);

  /* Necessary for scroll events */
  gtk_widget_add_events(widget, GDK_SCROLL_MASK);
}

void caja_notebook_sync_loading(CajaNotebook *notebook, CajaWindowSlot *slot) {
  GtkWidget *tab_label, *spinner, *icon;
  gboolean active;

  g_return_if_fail(CAJA_IS_NOTEBOOK(notebook));
  g_return_if_fail(CAJA_IS_WINDOW_SLOT(slot));

  tab_label =
      gtk_notebook_get_tab_label(GTK_NOTEBOOK(notebook), slot->content_box);
  g_return_if_fail(GTK_IS_WIDGET(tab_label));

  spinner = GTK_WIDGET(g_object_get_data(G_OBJECT(tab_label), "spinner"));
  icon = GTK_WIDGET(g_object_get_data(G_OBJECT(tab_label), "icon"));
  g_return_if_fail(spinner != NULL && icon != NULL);

  active = FALSE;
  g_object_get(spinner, "active", &active, NULL);
  if (active == slot->allow_stop) {
    return;
  }

  if (slot->allow_stop) {
    gtk_widget_hide(icon);
    gtk_widget_show(spinner);
    gtk_spinner_start(GTK_SPINNER(spinner));
  } else {
    gtk_spinner_stop(GTK_SPINNER(spinner));
    gtk_widget_hide(spinner);
    gtk_widget_show(icon);
  }
}

void caja_notebook_sync_tab_label(CajaNotebook *notebook,
                                  CajaWindowSlot *slot) {
  GtkWidget *hbox, *label;

  g_return_if_fail(CAJA_IS_NOTEBOOK(notebook));
  g_return_if_fail(CAJA_IS_WINDOW_SLOT(slot));
  g_return_if_fail(GTK_IS_WIDGET(slot->content_box));

  hbox = gtk_notebook_get_tab_label(GTK_NOTEBOOK(notebook), slot->content_box);
  g_return_if_fail(GTK_IS_WIDGET(hbox));

  label = GTK_WIDGET(g_object_get_data(G_OBJECT(hbox), "label"));
  g_return_if_fail(GTK_IS_WIDGET(label));

  gtk_label_set_text(GTK_LABEL(label), slot->title);

  if (slot->location != NULL) {
    char *location_name;

    /* Set the tooltip on the label's parent (the tab label hbox),
     * so it covers all of the tab label.
     */
    location_name = g_file_get_parse_name(slot->location);
    gtk_widget_set_tooltip_text(gtk_widget_get_parent(label), location_name);
    g_free(location_name);
  } else {
    gtk_widget_set_tooltip_text(gtk_widget_get_parent(label), NULL);
  }
}

static void close_button_clicked_cb(GtkWidget *widget, CajaWindowSlot *slot) {
  GtkWidget *notebook;

  notebook = gtk_widget_get_ancestor(slot->content_box, CAJA_TYPE_NOTEBOOK);
  if (notebook != NULL) {
    g_signal_emit(notebook, signals[TAB_CLOSE_REQUEST], 0, slot);
  }
}

static GtkWidget *build_tab_label(CajaNotebook *nb, CajaWindowSlot *slot) {
  CajaDragSlotProxyInfo *drag_info;
  GtkWidget *hbox, *label, *close_button, *image, *spinner, *icon;

  /* set hbox spacing and label padding (see below) so that there's an
   * equal amount of space around the label */
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_show(hbox);

  /* setup load feedback */
  spinner = gtk_spinner_new();
  gtk_box_pack_start(GTK_BOX(hbox), spinner, FALSE, FALSE, 0);

  /* setup site icon, empty by default */
  icon = gtk_image_new();
  gtk_box_pack_start(GTK_BOX(hbox), icon, FALSE, FALSE, 0);
  /* don't show the icon */

  /* setup label */
  label = gtk_label_new(NULL);
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_label_set_yalign(GTK_LABEL(label), 0.5);

  gtk_widget_set_margin_start(label, 0);
  gtk_widget_set_margin_end(label, 0);
  gtk_widget_set_margin_top(label, 0);
  gtk_widget_set_margin_bottom(label, 0);

  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
  gtk_widget_show(label);

  /* setup close button */
  close_button = gtk_button_new();
  gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);
  /* don't allow focus on the close button */
  gtk_widget_set_focus_on_click(close_button, FALSE);

  gtk_widget_set_name(close_button, "caja-tab-close-button");

  image = gtk_image_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
  gtk_widget_set_tooltip_text(close_button, _("Close tab"));
  g_signal_connect_object(close_button, "clicked",
                          G_CALLBACK(close_button_clicked_cb), slot, 0);

  gtk_container_add(GTK_CONTAINER(close_button), image);
  gtk_widget_show(image);

  gtk_box_pack_start(GTK_BOX(hbox), close_button, FALSE, FALSE, 0);
  gtk_widget_show(close_button);

  drag_info = g_new0(CajaDragSlotProxyInfo, 1);
  drag_info->target_slot = slot;
  g_object_set_data_full(G_OBJECT(hbox), "proxy-drag-info", drag_info,
                         (GDestroyNotify)g_free);

  caja_drag_slot_proxy_init(hbox, drag_info);

  g_object_set_data(G_OBJECT(hbox), "label", label);
  g_object_set_data(G_OBJECT(hbox), "spinner", spinner);
  g_object_set_data(G_OBJECT(hbox), "icon", icon);
  g_object_set_data(G_OBJECT(hbox), "close-button", close_button);

  return hbox;
}

static int caja_notebook_insert_page(GtkNotebook *gnotebook,
                                     GtkWidget *tab_widget,
                                     GtkWidget *tab_label,
                                     GtkWidget *menu_label, int position) {
  g_assert(GTK_IS_WIDGET(tab_widget));

  position =
      GTK_NOTEBOOK_CLASS(caja_notebook_parent_class)
          ->insert_page(gnotebook, tab_widget, tab_label, menu_label, position);

  gtk_notebook_set_show_tabs(gnotebook,
                             gtk_notebook_get_n_pages(gnotebook) > 1);
  gtk_notebook_set_tab_reorderable(gnotebook, tab_widget, TRUE);

  return position;
}

int caja_notebook_add_tab(CajaNotebook *notebook, CajaWindowSlot *slot,
                          int position, gboolean jump_to) {
  GtkNotebook *gnotebook = GTK_NOTEBOOK(notebook);
  GtkWidget *tab_label;

  g_return_val_if_fail(CAJA_IS_NOTEBOOK(notebook), -1);
  g_return_val_if_fail(CAJA_IS_WINDOW_SLOT(slot), -1);

  tab_label = build_tab_label(notebook, slot);

  position = gtk_notebook_insert_page(GTK_NOTEBOOK(notebook), slot->content_box,
                                      tab_label, position);

  gtk_container_child_set(GTK_CONTAINER(notebook), slot->content_box,
                          "tab-expand", TRUE, NULL);

  caja_notebook_sync_tab_label(notebook, slot);
  caja_notebook_sync_loading(notebook, slot);

  /* FIXME gtk bug! */
  /* FIXME: this should be fixed in gtk 2.12; check & remove this! */
  /* The signal handler may have reordered the tabs */
  position = gtk_notebook_page_num(gnotebook, slot->content_box);

  if (jump_to) {
    gtk_notebook_set_current_page(gnotebook, position);
  }

  return position;
}

static void caja_notebook_remove(GtkContainer *container,
                                 GtkWidget *tab_widget) {
  GtkNotebook *gnotebook = GTK_NOTEBOOK(container);
  GTK_CONTAINER_CLASS(caja_notebook_parent_class)
      ->remove(container, tab_widget);

  gtk_notebook_set_show_tabs(gnotebook,
                             gtk_notebook_get_n_pages(gnotebook) > 1);
}

void caja_notebook_reorder_current_child_relative(CajaNotebook *notebook,
                                                  int offset) {
  GtkNotebook *gnotebook;
  GtkWidget *child;
  int page;

  g_return_if_fail(CAJA_IS_NOTEBOOK(notebook));

  if (!caja_notebook_can_reorder_current_child_relative(notebook, offset)) {
    return;
  }

  gnotebook = GTK_NOTEBOOK(notebook);

  page = gtk_notebook_get_current_page(gnotebook);
  child = gtk_notebook_get_nth_page(gnotebook, page);
  gtk_notebook_reorder_child(gnotebook, child, page + offset);
}

void caja_notebook_set_current_page_relative(CajaNotebook *notebook,
                                             int offset) {
  GtkNotebook *gnotebook;
  int page;

  g_return_if_fail(CAJA_IS_NOTEBOOK(notebook));

  if (!caja_notebook_can_set_current_page_relative(notebook, offset)) {
    return;
  }

  gnotebook = GTK_NOTEBOOK(notebook);

  page = gtk_notebook_get_current_page(gnotebook);
  gtk_notebook_set_current_page(gnotebook, page + offset);
}

static gboolean caja_notebook_is_valid_relative_position(CajaNotebook *notebook,
                                                         int offset) {
  GtkNotebook *gnotebook;
  int page;
  int n_pages;

  gnotebook = GTK_NOTEBOOK(notebook);

  page = gtk_notebook_get_current_page(gnotebook);
  n_pages = gtk_notebook_get_n_pages(gnotebook) - 1;
  if (page < 0 || (offset < 0 && page < -offset) ||
      (offset > 0 && page > n_pages - offset)) {
    return FALSE;
  }

  return TRUE;
}

gboolean caja_notebook_can_reorder_current_child_relative(
    CajaNotebook *notebook, int offset) {
  g_return_val_if_fail(CAJA_IS_NOTEBOOK(notebook), FALSE);

  return caja_notebook_is_valid_relative_position(notebook, offset);
}

gboolean caja_notebook_can_set_current_page_relative(CajaNotebook *notebook,
                                                     int offset) {
  g_return_val_if_fail(CAJA_IS_NOTEBOOK(notebook), FALSE);

  return caja_notebook_is_valid_relative_position(notebook, offset);
}

/* Tab scrolling was removed from GtkNotebook in gtk 3, so reimplement it here
 */
static gboolean caja_notebook_scroll_event(GtkWidget *widget,
                                           GdkEventScroll *event) {
  GtkNotebook *notebook = GTK_NOTEBOOK(widget);
  gboolean (*scroll_event)(GtkWidget *, GdkEventScroll *) =
      GTK_WIDGET_CLASS(caja_notebook_parent_class)->scroll_event;
  GtkWidget *child, *event_widget, *action_widget;

  if ((event->state & gtk_accelerator_get_default_mod_mask()) != 0)
    goto chain_up;

  child = gtk_notebook_get_nth_page(notebook,
                                    gtk_notebook_get_current_page(notebook));
  if (child == NULL) goto chain_up;

  event_widget = gtk_get_event_widget((GdkEvent *)event);

  /* Ignore scroll events from the content of the page */
  if (event_widget == NULL || event_widget == child ||
      gtk_widget_is_ancestor(event_widget, child))
    goto chain_up;

  /* And also from the action widgets */
  action_widget = gtk_notebook_get_action_widget(notebook, GTK_PACK_START);
  if (event_widget == action_widget ||
      (action_widget != NULL &&
       gtk_widget_is_ancestor(event_widget, action_widget)))
    goto chain_up;

  action_widget = gtk_notebook_get_action_widget(notebook, GTK_PACK_END);
  if (event_widget == action_widget ||
      (action_widget != NULL &&
       gtk_widget_is_ancestor(event_widget, action_widget)))
    goto chain_up;

  switch (event->direction) {
    case GDK_SCROLL_RIGHT:
    case GDK_SCROLL_DOWN:
      gtk_notebook_next_page(notebook);
      return TRUE;
    case GDK_SCROLL_LEFT:
    case GDK_SCROLL_UP:
      gtk_notebook_prev_page(notebook);
      return TRUE;
    case GDK_SCROLL_SMOOTH:
      switch (gtk_notebook_get_tab_pos(notebook)) {
        case GTK_POS_LEFT:
        case GTK_POS_RIGHT:
          if (event->delta_y > 0)
            gtk_notebook_next_page(notebook);
          else if (event->delta_y < 0)
            gtk_notebook_prev_page(notebook);
          break;
        case GTK_POS_TOP:
        case GTK_POS_BOTTOM:
          if (event->delta_x > 0)
            gtk_notebook_next_page(notebook);
          else if (event->delta_x < 0)
            gtk_notebook_prev_page(notebook);
          break;
      }
      return TRUE;
  }

chain_up:
  if (scroll_event) return scroll_event(widget, event);

  return FALSE;
}
