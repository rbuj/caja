/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * Caja is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Caja is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; see the file COPYING.  If not,
 * write to the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Alexander Larsson <alexl@redhat.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "caja-query-editor.h"

#include <eel/eel-glib-extensions.h>
#include <eel/eel-gtk-macros.h>
#include <eel/eel-stock-dialogs.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libcaja-private/caja-global-preferences.h>
#include <string.h>

#include "caja-src-marshal.h"
#include "caja-window-slot.h"

enum {
  DURATION_INVALID,
  DURATION_ONE_HOUR,
  DURATION_ONE_DAY,
  DURATION_ONE_WEEK,
  DURATION_ONE_MONTH,
  DURATION_SIX_MONTHS,
  DURATION_ONE_YEAR,
};

typedef enum {
  CAJA_QUERY_EDITOR_ROW_LOCATION,
  CAJA_QUERY_EDITOR_ROW_TYPE,
  CAJA_QUERY_EDITOR_ROW_TAGS,
  CAJA_QUERY_EDITOR_ROW_TIME_MODIFIED,
  CAJA_QUERY_EDITOR_ROW_SIZE,
  CAJA_QUERY_EDITOR_ROW_CONTAINED_TEXT,

  CAJA_QUERY_EDITOR_ROW_LAST
} CajaQueryEditorRowType;

typedef struct {
  CajaQueryEditorRowType type;
  CajaQueryEditor *editor;
  GtkWidget *hbox;
  GtkWidget *combo;

  GtkWidget *type_widget;

  void *data;
} CajaQueryEditorRow;

typedef struct {
  const char *name;
  GtkWidget *(*create_widgets)(CajaQueryEditorRow *row);
  void (*add_to_query)(CajaQueryEditorRow *row, CajaQuery *query);
  void (*free_data)(CajaQueryEditorRow *row);
  void (*add_rows_from_query)(CajaQueryEditor *editor, CajaQuery *query);
} CajaQueryEditorRowOps;

struct CajaQueryEditorDetails {
  gboolean is_indexed;
  GtkWidget *entry;
  gboolean change_frozen;
  guint typing_timeout_id;
  gboolean is_visible;
  GtkWidget *invisible_vbox;
  GtkWidget *visible_vbox;

  GList *rows;
  char *last_set_query_text;

  CajaSearchBar *bar;
  CajaWindowSlot *slot;
};

enum { CHANGED, CANCEL, LAST_SIGNAL };

static guint signals[LAST_SIGNAL] = {0};

static void caja_query_editor_class_init(CajaQueryEditorClass *class);
static void caja_query_editor_init(CajaQueryEditor *editor);

static void go_search_cb(GtkButton *clicked_button, CajaQueryEditor *editor);

static void entry_activate_cb(GtkWidget *entry, CajaQueryEditor *editor);
static void entry_changed_cb(GtkWidget *entry, CajaQueryEditor *editor);
static void caja_query_editor_changed_force(CajaQueryEditor *editor,
                                            gboolean force);
static void caja_query_editor_changed(CajaQueryEditor *editor);
static CajaQueryEditorRow *caja_query_editor_add_row(
    CajaQueryEditor *editor, CajaQueryEditorRowType type);

static GtkWidget *location_row_create_widgets(CajaQueryEditorRow *row);
static void location_row_add_to_query(CajaQueryEditorRow *row,
                                      CajaQuery *query);
static void location_row_free_data(CajaQueryEditorRow *row);
static void location_add_rows_from_query(CajaQueryEditor *editor,
                                         CajaQuery *query);

static GtkWidget *tags_row_create_widgets(CajaQueryEditorRow *row);
static void tags_row_add_to_query(CajaQueryEditorRow *row, CajaQuery *query);
static void tags_row_free_data(CajaQueryEditorRow *row);
static void tags_add_rows_from_query(CajaQueryEditor *editor, CajaQuery *query);

static GtkWidget *type_row_create_widgets(CajaQueryEditorRow *row);
static void type_row_add_to_query(CajaQueryEditorRow *row, CajaQuery *query);
static void type_row_free_data(CajaQueryEditorRow *row);
static void type_add_rows_from_query(CajaQueryEditor *editor, CajaQuery *query);
static GtkWidget *modtime_row_create_widgets(CajaQueryEditorRow *row);
static void modtime_row_add_to_query(CajaQueryEditorRow *row, CajaQuery *query);
static void modtime_row_free_data(CajaQueryEditorRow *row);
static void modtime_add_rows_from_query(CajaQueryEditor *editor,
                                        CajaQuery *query);
static GtkWidget *size_row_create_widgets(CajaQueryEditorRow *row);
static void size_row_add_to_query(CajaQueryEditorRow *row, CajaQuery *query);
static void size_row_free_data(CajaQueryEditorRow *row);
static void size_add_rows_from_query(CajaQueryEditor *editor, CajaQuery *query);

static GtkWidget *contained_text_row_create_widgets(CajaQueryEditorRow *row);
static void contained_text_row_add_to_query(CajaQueryEditorRow *row,
                                            CajaQuery *query);
static void contained_text_row_free_data(CajaQueryEditorRow *row);
static void contained_text_add_rows_from_query(CajaQueryEditor *editor,
                                               CajaQuery *query);

static CajaQueryEditorRowOps row_type[] = {
    {N_("Location"), location_row_create_widgets, location_row_add_to_query,
     location_row_free_data, location_add_rows_from_query},
    {N_("File Type"), type_row_create_widgets, type_row_add_to_query,
     type_row_free_data, type_add_rows_from_query},
    {N_("Tags"), tags_row_create_widgets, tags_row_add_to_query,
     tags_row_free_data, tags_add_rows_from_query},
    {N_("Modification Time"), modtime_row_create_widgets,
     modtime_row_add_to_query, modtime_row_free_data,
     modtime_add_rows_from_query},
    {N_("Size"), size_row_create_widgets, size_row_add_to_query,
     size_row_free_data, size_add_rows_from_query},
    {N_("Contained text"), contained_text_row_create_widgets,
     contained_text_row_add_to_query, contained_text_row_free_data,
     contained_text_add_rows_from_query}};

EEL_CLASS_BOILERPLATE(CajaQueryEditor, caja_query_editor, GTK_TYPE_BOX)

static void caja_query_editor_finalize(GObject *object) {
  CajaQueryEditor *editor;

  editor = CAJA_QUERY_EDITOR(object);

  g_free(editor->details);

  EEL_CALL_PARENT(G_OBJECT_CLASS, finalize, (object));
}

static void caja_query_editor_dispose(GObject *object) {
  CajaQueryEditor *editor;

  editor = CAJA_QUERY_EDITOR(object);

  if (editor->details->typing_timeout_id) {
    g_source_remove(editor->details->typing_timeout_id);
    editor->details->typing_timeout_id = 0;
  }

  if (editor->details->bar != NULL) {
    g_signal_handlers_disconnect_by_func(editor->details->entry,
                                         entry_activate_cb, editor);
    g_signal_handlers_disconnect_by_func(editor->details->entry,
                                         entry_changed_cb, editor);

    caja_search_bar_return_entry(editor->details->bar);
    eel_remove_weak_pointer(&editor->details->bar);
  }

  EEL_CALL_PARENT(G_OBJECT_CLASS, dispose, (object));
}

static void caja_query_editor_class_init(CajaQueryEditorClass *class) {
  GObjectClass *gobject_class;
  GtkBindingSet *binding_set;

  gobject_class = G_OBJECT_CLASS(class);
  gobject_class->finalize = caja_query_editor_finalize;
  gobject_class->dispose = caja_query_editor_dispose;

  signals[CHANGED] =
      g_signal_new("changed", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET(CajaQueryEditorClass, changed), NULL, NULL,
                   caja_src_marshal_VOID__OBJECT_BOOLEAN, G_TYPE_NONE, 2,
                   CAJA_TYPE_QUERY, G_TYPE_BOOLEAN);

  signals[CANCEL] = g_signal_new(
      "cancel", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET(CajaQueryEditorClass, cancel), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  binding_set = gtk_binding_set_by_class(class);
  gtk_binding_entry_add_signal(binding_set, GDK_KEY_Escape, 0, "cancel", 0);
}

static void entry_activate_cb(GtkWidget *entry, CajaQueryEditor *editor) {
  if (editor->details->typing_timeout_id) {
    g_source_remove(editor->details->typing_timeout_id);
    editor->details->typing_timeout_id = 0;
  }

  caja_query_editor_changed_force(editor, TRUE);
}

static gboolean typing_timeout_cb(gpointer user_data) {
  CajaQueryEditor *editor;

  editor = CAJA_QUERY_EDITOR(user_data);

  caja_query_editor_changed(editor);

  editor->details->typing_timeout_id = 0;

  return FALSE;
}

#define TYPING_TIMEOUT 750

static void entry_changed_cb(GtkWidget *entry, CajaQueryEditor *editor) {
  if (editor->details->change_frozen) {
    return;
  }

  if (editor->details->typing_timeout_id) {
    g_source_remove(editor->details->typing_timeout_id);
  }

  editor->details->typing_timeout_id =
      g_timeout_add(TYPING_TIMEOUT, typing_timeout_cb, editor);
}

static void edit_clicked(GtkButton *button, CajaQueryEditor *editor) {
  caja_query_editor_set_visible(editor, TRUE);
  caja_query_editor_grab_focus(editor);
}

/* Location */

static GtkWidget *location_row_create_widgets(CajaQueryEditorRow *row) {
  GtkWidget *chooser;

  chooser = gtk_file_chooser_button_new(_("Select folder to search in"),
                                        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(chooser), TRUE);
  gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser),
                                      g_get_home_dir());
  gtk_widget_show(chooser);

  g_signal_connect_swapped(chooser, "current-folder-changed",
                           G_CALLBACK(caja_query_editor_changed), row->editor);

  gtk_box_pack_start(GTK_BOX(row->hbox), chooser, FALSE, FALSE, 0);

  return chooser;
}

static void location_row_add_to_query(CajaQueryEditorRow *row,
                                      CajaQuery *query) {
  char *folder, *uri;

  folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(row->type_widget));
  if (folder == NULL) {
    /* I don't know why, but i got NULL here on initial search in browser mode
       even with the location set to the homedir in create_widgets... */
    folder = g_strdup(g_get_home_dir());
  }

  uri = g_filename_to_uri(folder, NULL, NULL);
  g_free(folder);

  caja_query_set_location(query, uri);
  g_free(uri);
}

static void location_row_free_data(CajaQueryEditorRow *row) {}

static void location_add_rows_from_query(CajaQueryEditor *editor,
                                         CajaQuery *query) {
  CajaQueryEditorRow *row;
  char *uri, *folder;

  uri = caja_query_get_location(query);

  if (uri == NULL) {
    return;
  }
  folder = g_filename_from_uri(uri, NULL, NULL);
  g_free(uri);
  if (folder == NULL) {
    return;
  }

  row = caja_query_editor_add_row(editor, CAJA_QUERY_EDITOR_ROW_LOCATION);
  gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(row->type_widget),
                                      folder);

  g_free(folder);
}

/* Tags */
static void tags_entry_changed_cb(GtkWidget *entry, gpointer *data) {
  /* remove commas from string */
  const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
  if (g_strrstr(text, ",") == NULL) {
    return;
  }

  gchar **words = g_strsplit(text, ",", -1);
  gchar *sanitized = g_strjoinv("", words);
  g_strfreev(words);

  gtk_entry_set_text(GTK_ENTRY(entry), sanitized);
  g_free(sanitized);
}

#define MAX_TAGS_ENTRY_LEN 4096  // arbitrary value.

static GtkWidget *tags_row_create_widgets(CajaQueryEditorRow *row) {
  GtkWidget *entry = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(entry), MAX_TAGS_ENTRY_LEN);
  gtk_widget_set_tooltip_text(
      entry, _("Tags separated by spaces. "
               "Matches files that contains ALL specified tags."));

  gtk_entry_set_placeholder_text(
      GTK_ENTRY(entry), _("Tags separated by spaces. "
                          "Matches files that contains ALL specified tags."));

  gtk_widget_show(entry);
  gtk_box_pack_start(GTK_BOX(row->hbox), entry, TRUE, TRUE, 0);
  g_signal_connect(entry, "changed", G_CALLBACK(tags_entry_changed_cb), entry);
  g_signal_connect(entry, "activate", G_CALLBACK(go_search_cb), row->editor);

  return entry;
}

static void tags_row_add_to_query(CajaQueryEditorRow *row, CajaQuery *query) {
  GtkEntry *entry = GTK_ENTRY(row->type_widget);
  const gchar *tags = gtk_entry_get_text(entry);

  char **strv = g_strsplit(tags, " ", -1);
  guint len = g_strv_length(strv);
  guint i;

  for (i = 0; i < len; ++i) {
    strv[i] = g_strstrip(strv[i]);
    if (strlen(strv[i]) > 0) {
      caja_query_add_tag(query, strv[i]);
    }
  }
  g_strfreev(strv);
}

static void tags_row_free_data(CajaQueryEditorRow *row) {}

static gchar *xattr_tags_list_to_str(const GList *tags) {
  gchar *result = NULL;

  const GList *tags_iter = NULL;
  for (tags_iter = tags; tags_iter; tags_iter = tags_iter->next) {
    gchar *tmp;

    if (result != NULL) {
      tmp = g_strconcat(result, ",", tags_iter->data, NULL);
      g_free(result);
    } else {
      tmp = g_strdup(tags_iter->data);
    }

    result = tmp;
  }

  return result;
}

static void tags_add_rows_from_query(CajaQueryEditor *editor,
                                     CajaQuery *query) {
  GList *tags = caja_query_get_tags(query);
  if (tags == NULL) {
    return;
  }

  CajaQueryEditorRow *row;
  row = caja_query_editor_add_row(editor, CAJA_QUERY_EDITOR_ROW_TAGS);

  gchar *tags_str = xattr_tags_list_to_str(tags);
  g_list_free_full(tags, g_free);

  gtk_entry_set_text(GTK_ENTRY(row->type_widget), tags_str);
  g_free(tags_str);
}

/* Type */

static gboolean type_separator_func(GtkTreeModel *model, GtkTreeIter *iter,
                                    gpointer data) {
  char *text;
  gboolean res;

  gtk_tree_model_get(model, iter, 0, &text, -1);

  res = text != NULL && strcmp(text, "---") == 0;

  g_free(text);
  return res;
}

struct {
  char *name;
  char *mimetypes[20];
} mime_type_groups[] = {
    {N_("Documents"),
     {"application/rtf", "application/msword", "application/vnd.sun.xml.writer",
      "application/vnd.sun.xml.writer.global",
      "application/vnd.sun.xml.writer.template",
      "application/vnd.oasis.opendocument.text",
      "application/vnd.oasis.opendocument.text-template",
      "application/x-abiword", "application/x-applix-word",
      "application/x-mswrite", "application/docbook+xml", "application/x-kword",
      "application/x-kword-crypt", "application/x-lyx", NULL}},
    {N_("Music"),
     {"application/ogg", "audio/ac3", "audio/basic", "audio/midi",
      "audio/x-flac", "audio/mp4", "audio/mpeg", "audio/x-mpeg",
      "audio/x-ms-asx", "audio/x-pn-realaudio", NULL}},
    {N_("Video"),
     {"video/mp4", "video/3gpp", "video/mpeg", "video/quicktime", "video/vivo",
      "video/x-avi", "video/x-mng", "video/x-ms-asf", "video/x-ms-wmv",
      "video/x-msvideo", "video/x-nsv", "video/x-real-video", NULL}},
    {N_("Picture"),
     {"application/vnd.oasis.opendocument.image", "application/x-krita",
      "image/bmp", "image/cgm", "image/gif", "image/jpeg", "image/jpeg2000",
      "image/png", "image/svg+xml", "image/tiff", "image/x-compressed-xcf",
      "image/x-pcx", "image/x-photo-cd", "image/x-psd", "image/x-tga",
      "image/x-xcf", NULL}},
    {N_("Illustration"),
     {"application/illustrator", "application/vnd.corel-draw",
      "application/vnd.stardivision.draw",
      "application/vnd.oasis.opendocument.graphics",
      "application/x-dia-diagram", "application/x-karbon",
      "application/x-killustrator", "application/x-kivio",
      "application/x-kontour", "application/x-wpg", NULL}},
    {N_("Spreadsheet"),
     {"application/vnd.lotus-1-2-3", "application/vnd.ms-excel",
      "application/vnd.stardivision.calc", "application/vnd.sun.xml.calc",
      "application/vnd.oasis.opendocument.spreadsheet",
      "application/x-applix-spreadsheet", "application/x-gnumeric",
      "application/x-kspread", "application/x-kspread-crypt",
      "application/x-quattropro", "application/x-sc", "application/x-siag",
      NULL}},
    {N_("Presentation"),
     {"application/vnd.ms-powerpoint", "application/vnd.sun.xml.impress",
      "application/vnd.oasis.opendocument.presentation",
      "application/x-magicpoint", "application/x-kpresenter", NULL}},
    {N_("Pdf / Postscript"),
     {"application/pdf", "application/postscript", "application/x-dvi",
      "image/x-eps", NULL}},
    {N_("Text File"), {"text/plain", NULL}}};

static void type_add_custom_type(CajaQueryEditorRow *row, const char *mime_type,
                                 const char *description, GtkTreeIter *iter) {
  GtkTreeModel *model;
  GtkListStore *store;

  model = gtk_combo_box_get_model(GTK_COMBO_BOX(row->type_widget));
  store = GTK_LIST_STORE(model);

  gtk_list_store_append(store, iter);
  gtk_list_store_set(store, iter, 0, description, 2, mime_type, -1);
}

static void type_combo_changed(GtkComboBox *combo_box,
                               CajaQueryEditorRow *row) {
  GtkTreeIter iter;
  gboolean other;
  GtkTreeModel *model;

  if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(row->type_widget), &iter)) {
    return;
  }

  model = gtk_combo_box_get_model(GTK_COMBO_BOX(row->type_widget));
  gtk_tree_model_get(model, &iter, 3, &other, -1);

  if (other) {
    GList *mime_infos, *l;
    GtkWidget *dialog;
    GtkWidget *scrolled, *treeview;
    GtkListStore *store;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkWidget *toplevel;
    GtkTreeSelection *selection;

    mime_infos = g_content_types_get_registered();

    store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    for (l = mime_infos; l != NULL; l = l->next) {
      GtkTreeIter it;
      char *mime_type = l->data;
      char *description;

      description = g_content_type_get_description(mime_type);
      if (description == NULL) {
        description = g_strdup(mime_type);
      }

      gtk_list_store_append(store, &it);
      gtk_list_store_set(store, &it, 0, description, 1, mime_type, -1);

      g_free(mime_type);
      g_free(description);
    }
    g_list_free(mime_infos);

    toplevel = gtk_widget_get_toplevel(GTK_WIDGET(combo_box));

    dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("Select type"));
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(toplevel));

    eel_dialog_add_button(GTK_DIALOG(dialog), _("_OK"), "gtk-ok",
                          GTK_RESPONSE_OK);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 600);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                        GTK_SHADOW_IN);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scrolled),
                                              FALSE);

    gtk_widget_show(scrolled);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       scrolled, TRUE, TRUE, 6);

    treeview = gtk_tree_view_new();
    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(store));
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 0,
                                         GTK_SORT_ASCENDING);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text",
                                                      0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);

    gtk_widget_show(treeview);
    gtk_container_add(GTK_CONTAINER(scrolled), treeview);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      char *mimetype, *description;

      gtk_tree_selection_get_selected(selection, NULL, &iter);
      gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &description, 1,
                         &mimetype, -1);

      type_add_custom_type(row, mimetype, description, &iter);
      gtk_combo_box_set_active_iter(GTK_COMBO_BOX(row->type_widget), &iter);
    } else {
      gtk_combo_box_set_active(GTK_COMBO_BOX(row->type_widget), 0);
    }

    gtk_widget_destroy(dialog);
  }

  caja_query_editor_changed(row->editor);
}

static GtkWidget *type_row_create_widgets(CajaQueryEditorRow *row) {
  GtkWidget *combo;
  GtkCellRenderer *cell;
  GtkListStore *store;
  GtkTreeIter iter;
  gsize i;

  store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_STRING,
                             G_TYPE_BOOLEAN);
  combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);

  cell = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "text", 0, NULL);
  gtk_combo_box_set_row_separator_func(GTK_COMBO_BOX(combo),
                                       type_separator_func, NULL, NULL);

  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter, 0, _("Any"), -1);
  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter, 0, "---", -1);

  for (i = 0; i < G_N_ELEMENTS(mime_type_groups); i++) {
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, gettext(mime_type_groups[i].name), 1,
                       mime_type_groups[i].mimetypes, -1);
  }

  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter, 0, "---", -1);
  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter, 0, _("Other Type..."), 3, TRUE, -1);

  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

  g_signal_connect(combo, "changed", G_CALLBACK(type_combo_changed), row);

  gtk_widget_show(combo);

  gtk_box_pack_start(GTK_BOX(row->hbox), combo, FALSE, FALSE, 0);

  return combo;
}

static void type_row_add_to_query(CajaQueryEditorRow *row, CajaQuery *query) {
  GtkTreeIter iter;
  char **mimetypes;
  char *mimetype;
  GtkTreeModel *model;

  if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(row->type_widget), &iter)) {
    return;
  }

  model = gtk_combo_box_get_model(GTK_COMBO_BOX(row->type_widget));
  gtk_tree_model_get(model, &iter, 1, &mimetypes, 2, &mimetype, -1);

  if (mimetypes != NULL) {
    while (*mimetypes != NULL) {
      caja_query_add_mime_type(query, *mimetypes);
      mimetypes++;
    }
  }
  if (mimetype) {
    caja_query_add_mime_type(query, mimetype);
    g_free(mimetype);
  }
}

static void type_row_free_data(CajaQueryEditorRow *row) {}

static gboolean all_group_types_in_list(char **group_types, GList *mime_types) {
  GList *l;
  char **group_type;
  char *mime_type;
  gboolean found;

  group_type = group_types;
  while (*group_type != NULL) {
    found = FALSE;

    for (l = mime_types; l != NULL; l = l->next) {
      mime_type = l->data;

      if (strcmp(mime_type, *group_type) == 0) {
        found = TRUE;
        break;
      }
    }

    if (!found) {
      return FALSE;
    }
    group_type++;
  }
  return TRUE;
}

static GList *remove_group_types_from_list(char **group_types,
                                           GList *mime_types) {
  GList *l, *next;
  char **group_type;
  char *mime_type;

  group_type = group_types;
  while (*group_type != NULL) {
    for (l = mime_types; l != NULL; l = next) {
      mime_type = l->data;
      next = l->next;

      if (strcmp(mime_type, *group_type) == 0) {
        mime_types = g_list_remove_link(mime_types, l);
        g_free(mime_type);
        break;
      }
    }

    group_type++;
  }
  return mime_types;
}

static void type_add_rows_from_query(CajaQueryEditor *editor,
                                     CajaQuery *query) {
  GList *mime_types;
  char *mime_type;
  CajaQueryEditorRow *row;
  GtkTreeIter iter;
  gsize i;
  GtkTreeModel *model;
  GList *l;

  mime_types = caja_query_get_mime_types(query);

  if (mime_types == NULL) {
    return;
  }

  for (i = 0; i < G_N_ELEMENTS(mime_type_groups); i++) {
    if (all_group_types_in_list(mime_type_groups[i].mimetypes, mime_types)) {
      mime_types = remove_group_types_from_list(mime_type_groups[i].mimetypes,
                                                mime_types);

      row = caja_query_editor_add_row(editor, CAJA_QUERY_EDITOR_ROW_TYPE);

      model = gtk_combo_box_get_model(GTK_COMBO_BOX(row->type_widget));

      gtk_tree_model_iter_nth_child(model, &iter, NULL, ((gint)i) + 2);
      gtk_combo_box_set_active_iter(GTK_COMBO_BOX(row->type_widget), &iter);
    }
  }

  for (l = mime_types; l != NULL; l = l->next) {
    const char *desc;

    mime_type = l->data;

    desc = g_content_type_get_description(mime_type);
    if (desc == NULL) {
      desc = mime_type;
    }

    row = caja_query_editor_add_row(editor, CAJA_QUERY_EDITOR_ROW_TYPE);
    model = gtk_combo_box_get_model(GTK_COMBO_BOX(row->type_widget));

    type_add_custom_type(row, mime_type, desc, &iter);
    gtk_combo_box_set_active_iter(GTK_COMBO_BOX(row->type_widget), &iter);
  }

  g_list_free_full(mime_types, g_free);
}

/* End of row types */

static GtkWidget *modtime_row_create_widgets(CajaQueryEditorRow *row) {
  GtkWidget *hbox = NULL;
  GtkWidget *combo = NULL;
  GtkWidget *duration_combo = NULL;
  GtkCellRenderer *cell = NULL;
  GtkListStore *store = NULL;
  GtkListStore *duration_store = NULL;
  GtkTreeIter iter;

  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);

  store = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);
  combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);

  cell = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "text", 1, NULL);

  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter, 0, FALSE, 1, _("Less than or equal to"), -1);
  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter, 0, TRUE, 1, _("Greater than or equal to"),
                     -1);

  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

  duration_store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
  duration_combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(duration_store));
  g_object_unref(duration_store);

  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(duration_combo), cell, TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(duration_combo), cell, "text",
                                 1, NULL);

  gtk_list_store_append(duration_store, &iter);
  gtk_list_store_set(duration_store, &iter, 0, DURATION_ONE_HOUR, 1,
                     _("1 Hour"), -1);
  gtk_list_store_append(duration_store, &iter);
  gtk_list_store_set(duration_store, &iter, 0, DURATION_ONE_DAY, 1, _("1 Day"),
                     -1);
  gtk_list_store_append(duration_store, &iter);
  gtk_list_store_set(duration_store, &iter, 0, DURATION_ONE_WEEK, 1,
                     _("1 Week"), -1);
  gtk_list_store_append(duration_store, &iter);
  gtk_list_store_set(duration_store, &iter, 0, DURATION_ONE_MONTH, 1,
                     _("1 Month"), -1);
  gtk_list_store_append(duration_store, &iter);
  gtk_list_store_set(duration_store, &iter, 0, DURATION_SIX_MONTHS, 1,
                     _("6 Months"), -1);
  gtk_list_store_append(duration_store, &iter);
  gtk_list_store_set(duration_store, &iter, 0, DURATION_ONE_YEAR, 1,
                     _("1 Year"), -1);

  gtk_combo_box_set_active(GTK_COMBO_BOX(duration_combo), 0);

  gtk_box_pack_start(GTK_BOX(hbox), combo, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), duration_combo, FALSE, FALSE, 0);
  gtk_widget_show_all(hbox);

  gtk_box_pack_start(GTK_BOX(row->hbox), hbox, FALSE, FALSE, 0);

  return hbox;
}

static void modtime_row_add_to_query(CajaQueryEditorRow *row,
                                     CajaQuery *query) {
  GList *children = NULL;
  GtkWidget *combo = NULL;
  GtkWidget *duration_combo = NULL;
  GtkTreeModel *model = NULL;
  GtkTreeModel *duration_model = NULL;
  GtkTreeIter iter;
  GtkTreeIter duration_iter;
  gboolean is_greater = FALSE;
  GDateTime *now, *datetime;
  gint duration;
  gint64 timestamp;

  if (!GTK_IS_CONTAINER(row->type_widget)) return;

  children = gtk_container_get_children(GTK_CONTAINER(row->type_widget));
  if (g_list_length(children) != 2) return;

  combo = GTK_WIDGET(g_list_nth(children, 0)->data);
  duration_combo = GTK_WIDGET(g_list_nth(children, 1)->data);
  if (!combo || !duration_combo) return;

  if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter) ||
      !gtk_combo_box_get_active_iter(GTK_COMBO_BOX(duration_combo),
                                     &duration_iter)) {
    return;
  }

  model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
  gtk_tree_model_get(model, &iter, 0, &is_greater, -1);

  duration_model = gtk_combo_box_get_model(GTK_COMBO_BOX(duration_combo));
  gtk_tree_model_get(duration_model, &duration_iter, 0, &duration, -1);

  now = g_date_time_new_now_local();
  datetime = now;
  switch (duration) {
    case DURATION_ONE_HOUR:
      datetime = g_date_time_add_hours(now, -1);
      break;
    case DURATION_ONE_DAY:
      datetime = g_date_time_add_days(now, -1);
      break;
    case DURATION_ONE_WEEK:
      datetime = g_date_time_add_weeks(now, -1);
      break;
    case DURATION_ONE_MONTH:
      datetime = g_date_time_add_months(now, -1);
      break;
    case DURATION_SIX_MONTHS:
      datetime = g_date_time_add_months(now, -6);
      break;
    case DURATION_ONE_YEAR:
      datetime = g_date_time_add_years(now, -1);
      break;
    default:
      g_assert_not_reached();
  }

  g_date_time_unref(now);
  timestamp = g_date_time_to_unix(datetime);
  g_date_time_unref(datetime);

  caja_query_set_timestamp(query, is_greater ? timestamp : -timestamp);
}

static void modtime_row_free_data(CajaQueryEditorRow *row) {}

static void modtime_add_rows_from_query(CajaQueryEditor *editor,
                                        CajaQuery *query) {}

static GtkWidget *size_row_create_widgets(CajaQueryEditorRow *row) {
  GtkWidget *hbox = NULL;
  GtkWidget *combo = NULL;
  GtkWidget *size_combo = NULL;
  GtkCellRenderer *cell = NULL;
  GtkListStore *store = NULL;
  GtkListStore *size_store = NULL;
  GtkTreeIter iter;

  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);

  store = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);
  combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
  g_object_unref(store);

  cell = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "text", 1, NULL);

  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter, 0, FALSE, 1, _("Less than or equal to"), -1);
  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter, 0, TRUE, 1, _("Greater than or equal to"),
                     -1);

  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

  size_store = gtk_list_store_new(2, G_TYPE_INT64, G_TYPE_STRING);
  size_combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(size_store));
  g_object_unref(size_store);

  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(size_combo), cell, TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(size_combo), cell, "text", 1,
                                 NULL);

  if (g_settings_get_boolean(caja_preferences,
                             CAJA_PREFERENCES_USE_IEC_UNITS)) {
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 10240, 1, _("10 KiB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 102400, 1, _("100 KiB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 512000, 1, _("500 KiB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 1048576, 1, _("1 MiB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 5242880, 1, _("5 MiB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 10485760, 1, _("10 MiB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 104857600, 1, _("100 MiB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 524288000, 1, _("500 MiB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 1073741824, 1, _("1 GiB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 2147483648, 1, _("2 GiB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 4294967296, 1, _("4 GiB"), -1);
  } else {
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 10000, 1, _("10 KB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 100000, 1, _("100 KB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 500000, 1, _("500 KB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 1000000, 1, _("1 MB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 5000000, 1, _("5 MB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 10000000, 1, _("10 MB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 100000000, 1, _("100 MB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 500000000, 1, _("500 MB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 1000000000, 1, _("1 GB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 2000000000, 1, _("2 GB"), -1);
    gtk_list_store_append(size_store, &iter);
    gtk_list_store_set(size_store, &iter, 0, 4000000000, 1, _("4 GB"), -1);
  }

  gtk_combo_box_set_active(GTK_COMBO_BOX(size_combo), 0);

  gtk_box_pack_start(GTK_BOX(hbox), combo, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), size_combo, FALSE, FALSE, 0);
  gtk_widget_show_all(hbox);

  gtk_box_pack_start(GTK_BOX(row->hbox), hbox, FALSE, FALSE, 0);

  return hbox;
}

static void size_row_add_to_query(CajaQueryEditorRow *row, CajaQuery *query) {
  GList *children = NULL;
  GtkWidget *combo = NULL;
  GtkWidget *size_combo = NULL;
  GtkTreeModel *model = NULL;
  GtkTreeModel *size_model = NULL;
  GtkTreeIter iter;
  GtkTreeIter size_iter;
  gboolean is_greater = FALSE;
  gint64 size;

  if (!GTK_IS_CONTAINER(row->type_widget)) return;

  children = gtk_container_get_children(GTK_CONTAINER(row->type_widget));
  if (g_list_length(children) != 2) return;

  combo = GTK_WIDGET(g_list_nth(children, 0)->data);
  size_combo = GTK_WIDGET(g_list_nth(children, 1)->data);
  if (!combo || !size_combo) return;

  if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter) ||
      !gtk_combo_box_get_active_iter(GTK_COMBO_BOX(size_combo), &size_iter)) {
    return;
  }

  model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
  gtk_tree_model_get(model, &iter, 0, &is_greater, -1);

  size_model = gtk_combo_box_get_model(GTK_COMBO_BOX(size_combo));
  gtk_tree_model_get(size_model, &size_iter, 0, &size, -1);

  caja_query_set_size(query, is_greater ? size : -size);
}

static void size_row_free_data(CajaQueryEditorRow *row) {}

static void size_add_rows_from_query(CajaQueryEditor *editor,
                                     CajaQuery *query) {}

static GtkWidget *contained_text_row_create_widgets(CajaQueryEditorRow *row) {
  GtkWidget *entry = gtk_entry_new();
  gtk_widget_set_tooltip_text(entry,
                              _("Matches files that contains specified text."));

  gtk_entry_set_placeholder_text(
      GTK_ENTRY(entry), _("Matches files that contains specified text."));

  gtk_widget_show(entry);
  gtk_box_pack_start(GTK_BOX(row->hbox), entry, TRUE, TRUE, 0);
  g_signal_connect(entry, "activate", G_CALLBACK(go_search_cb), row->editor);

  return entry;
}

static void contained_text_row_add_to_query(CajaQueryEditorRow *row,
                                            CajaQuery *query) {
  GtkEntry *entry = GTK_ENTRY(row->type_widget);
  const gchar *text = gtk_entry_get_text(entry);

  caja_query_set_contained_text(query, text);
}

static void contained_text_row_free_data(CajaQueryEditorRow *row) {}

static void contained_text_add_rows_from_query(CajaQueryEditor *editor,
                                               CajaQuery *query) {}

static CajaQueryEditorRowType get_next_free_type(CajaQueryEditor *editor) {
  CajaQueryEditorRow *row;
  CajaQueryEditorRowType type;
  gboolean found;
  GList *l;

  for (type = 0; type < CAJA_QUERY_EDITOR_ROW_LAST; type++) {
    found = FALSE;
    for (l = editor->details->rows; l != NULL; l = l->next) {
      row = l->data;
      if (row->type == type) {
        found = TRUE;
        break;
      }
    }
    if (!found) {
      return type;
    }
  }
  return CAJA_QUERY_EDITOR_ROW_TYPE;
}

static void remove_row_cb(GtkButton *clicked_button, CajaQueryEditorRow *row) {
  CajaQueryEditor *editor;

  editor = row->editor;
  gtk_container_remove(GTK_CONTAINER(editor->details->visible_vbox), row->hbox);

  editor->details->rows = g_list_remove(editor->details->rows, row);

  row_type[row->type].free_data(row);
  g_free(row);

  caja_query_editor_changed(editor);
}

static void create_type_widgets(CajaQueryEditorRow *row) {
  row->type_widget = row_type[row->type].create_widgets(row);
}

static void row_type_combo_changed_cb(GtkComboBox *combo_box,
                                      CajaQueryEditorRow *row) {
  CajaQueryEditorRowType type;

  type = gtk_combo_box_get_active(combo_box);

  if (type == row->type) {
    return;
  }

  if (row->type_widget != NULL) {
    gtk_widget_destroy(row->type_widget);
    row->type_widget = NULL;
  }

  row_type[row->type].free_data(row);
  row->data = NULL;

  row->type = type;

  create_type_widgets(row);

  caja_query_editor_changed(row->editor);
}

static CajaQueryEditorRow *caja_query_editor_add_row(
    CajaQueryEditor *editor, CajaQueryEditorRowType type) {
  GtkWidget *hbox, *button, *image, *combo;
  CajaQueryEditorRow *row;
  int i;

  row = g_new0(CajaQueryEditorRow, 1);
  row->editor = editor;
  row->type = type;

  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  row->hbox = hbox;
  gtk_widget_show(hbox);
  gtk_box_pack_start(GTK_BOX(editor->details->visible_vbox), hbox, FALSE, FALSE,
                     0);

  combo = gtk_combo_box_text_new();
  row->combo = combo;
  for (i = 0; i < CAJA_QUERY_EDITOR_ROW_LAST; i++) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),
                                   gettext(row_type[i].name));
  }
  gtk_widget_show(combo);
  gtk_box_pack_start(GTK_BOX(hbox), combo, FALSE, FALSE, 0);

  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), row->type);

  editor->details->rows = g_list_append(editor->details->rows, row);

  g_signal_connect(combo, "changed", G_CALLBACK(row_type_combo_changed_cb),
                   row);

  create_type_widgets(row);

  button = gtk_button_new();
  image = gtk_image_new_from_icon_name("remove", GTK_ICON_SIZE_SMALL_TOOLBAR);
  gtk_container_add(GTK_CONTAINER(button), image);
  gtk_widget_show(image);
  gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
  gtk_widget_show(button);

  g_signal_connect(button, "clicked", G_CALLBACK(remove_row_cb), row);
  gtk_widget_set_tooltip_text(button,
                              _("Remove this criterion from the search"));

  gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);

  return row;
}

static void go_search_cb(GtkButton *clicked_button, CajaQueryEditor *editor) {
  caja_query_editor_changed_force(editor, TRUE);
}

static void add_new_row_cb(GtkButton *clicked_button, CajaQueryEditor *editor) {
  caja_query_editor_add_row(editor, get_next_free_type(editor));
  caja_query_editor_changed(editor);
}

static void caja_query_editor_init(CajaQueryEditor *editor) {
  GtkWidget *hbox, *label, *button;
  char *label_markup;

  editor->details = g_new0(CajaQueryEditorDetails, 1);
  editor->details->is_visible = TRUE;

  editor->details->invisible_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(editor),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_box_pack_start(GTK_BOX(editor), editor->details->invisible_vbox, FALSE,
                     FALSE, 0);
  editor->details->visible_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(editor),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_box_pack_start(GTK_BOX(editor), editor->details->visible_vbox, FALSE,
                     FALSE, 0);
  /* Only show visible vbox */
  gtk_widget_show(editor->details->visible_vbox);

  /* Create invisible part: */
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start(GTK_BOX(editor->details->invisible_vbox), hbox, FALSE,
                     FALSE, 0);
  gtk_widget_show(hbox);

  label = gtk_label_new("");
  label_markup = g_strconcat("<b>", _("Search Folder"), "</b>", NULL);
  gtk_label_set_markup(GTK_LABEL(label), label_markup);
  g_free(label_markup);
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
  gtk_widget_show(label);

  button = gtk_button_new_with_label(_("Edit"));
  gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
  gtk_widget_show(button);

  g_signal_connect(button, "clicked", G_CALLBACK(edit_clicked), editor);

  gtk_widget_set_tooltip_text(button, _("Edit the saved search"));
}

void caja_query_editor_set_default_query(CajaQueryEditor *editor) {
  if (!editor->details->is_indexed) {
    caja_query_editor_add_row(editor, CAJA_QUERY_EDITOR_ROW_LOCATION);
    caja_query_editor_changed(editor);
  }
}

static void finish_first_line(CajaQueryEditor *editor, GtkWidget *hbox,
                              gboolean use_go) {
  GtkWidget *button, *image;

  button = gtk_button_new();
  image = gtk_image_new_from_icon_name("add", GTK_ICON_SIZE_SMALL_TOOLBAR);
  gtk_container_add(GTK_CONTAINER(button), image);
  gtk_widget_show(image);
  gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
  gtk_widget_show(button);

  g_signal_connect(button, "clicked", G_CALLBACK(add_new_row_cb), editor);

  gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);

  gtk_widget_set_tooltip_text(button, _("Add a new criterion to this search"));

  if (!editor->details->is_indexed) {
    if (use_go) {
      button = gtk_button_new_with_label(_("Go"));
    } else {
      button = gtk_button_new_with_label(_("Reload"));
    }
    gtk_widget_show(button);

    gtk_widget_set_tooltip_text(button, _("Perform or update the search"));

    g_signal_connect(button, "clicked", G_CALLBACK(go_search_cb), editor);

    gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
  }
}

static void setup_internal_entry(CajaQueryEditor *editor) {
  GtkWidget *hbox, *label;
  char *label_markup;

  /* Create visible part: */
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_show(hbox);
  gtk_box_pack_start(GTK_BOX(editor->details->visible_vbox), hbox, FALSE, FALSE,
                     0);

  label = gtk_label_new("");
  label_markup = g_strconcat("<b>", _("_Search for:"), "</b>", NULL);
  gtk_label_set_markup_with_mnemonic(GTK_LABEL(label), label_markup);
  g_free(label_markup);
  gtk_widget_show(label);
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

  editor->details->entry = gtk_entry_new();
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), editor->details->entry);
  gtk_box_pack_start(GTK_BOX(hbox), editor->details->entry, TRUE, TRUE, 0);

  g_signal_connect(editor->details->entry, "activate",
                   G_CALLBACK(entry_activate_cb), editor);
  g_signal_connect(editor->details->entry, "changed",
                   G_CALLBACK(entry_changed_cb), editor);
  gtk_widget_show(editor->details->entry);

  finish_first_line(editor, hbox, TRUE);
}

static void setup_external_entry(CajaQueryEditor *editor, GtkWidget *entry) {
  GtkWidget *hbox, *label;

  /* Create visible part: */
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_show(hbox);
  gtk_box_pack_start(GTK_BOX(editor->details->visible_vbox), hbox, FALSE, FALSE,
                     0);

  label = gtk_label_new(_("Search results"));
  gtk_widget_show(label);
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

  editor->details->entry = entry;
  g_signal_connect(editor->details->entry, "activate",
                   G_CALLBACK(entry_activate_cb), editor);
  g_signal_connect(editor->details->entry, "changed",
                   G_CALLBACK(entry_changed_cb), editor);

  finish_first_line(editor, hbox, FALSE);
}

void caja_query_editor_set_visible(CajaQueryEditor *editor, gboolean visible) {
  editor->details->is_visible = visible;
  if (visible) {
    gtk_widget_show(editor->details->visible_vbox);
    gtk_widget_hide(editor->details->invisible_vbox);
  } else {
    gtk_widget_hide(editor->details->visible_vbox);
    gtk_widget_show(editor->details->invisible_vbox);
  }
}

static gboolean query_is_valid(CajaQueryEditor *editor) {
  const char *text;

  text = gtk_entry_get_text(GTK_ENTRY(editor->details->entry));

  return text != NULL && text[0] != '\0';
}

static void caja_query_editor_changed_force(CajaQueryEditor *editor,
                                            gboolean force_reload) {
  if (editor->details->change_frozen) {
    return;
  }

  if (query_is_valid(editor)) {
    CajaQuery *query;

    query = caja_query_editor_get_query(editor);
    g_signal_emit(editor, signals[CHANGED], 0, query,
                  editor->details->is_indexed || force_reload);
    g_object_unref(query);
  }
}

static void caja_query_editor_changed(CajaQueryEditor *editor) {
  caja_query_editor_changed_force(editor, FALSE);
}

void caja_query_editor_grab_focus(CajaQueryEditor *editor) {
  if (editor->details->is_visible) {
    gtk_widget_grab_focus(editor->details->entry);
  }
}

CajaQuery *caja_query_editor_get_query(CajaQueryEditor *editor) {
  const char *query_text;
  CajaQuery *query;
  GList *l;
  CajaQueryEditorRow *row = NULL;

  if (editor == NULL || editor->details == NULL ||
      editor->details->entry == NULL) {
    return NULL;
  }

  query_text = gtk_entry_get_text(GTK_ENTRY(editor->details->entry));

  /* Empty string is a NULL query */
  if (query_text && query_text[0] == '\0') {
    return NULL;
  }

  query = caja_query_new();
  caja_query_set_text(query, query_text);

  for (l = editor->details->rows; l != NULL; l = l->next) {
    row = l->data;

    row_type[row->type].add_to_query(row, query);
  }

  return query;
}

void caja_query_editor_clear_query(CajaQueryEditor *editor) {
  editor->details->change_frozen = TRUE;
  gtk_entry_set_text(GTK_ENTRY(editor->details->entry), "");

  g_free(editor->details->last_set_query_text);
  editor->details->last_set_query_text = g_strdup("");

  editor->details->change_frozen = FALSE;
}

GtkWidget *caja_query_editor_new(gboolean start_hidden, gboolean is_indexed) {
  GtkWidget *editor;

  editor = g_object_new(CAJA_TYPE_QUERY_EDITOR, NULL);

  CAJA_QUERY_EDITOR(editor)->details->is_indexed = is_indexed;

  caja_query_editor_set_visible(CAJA_QUERY_EDITOR(editor), !start_hidden);

  setup_internal_entry(CAJA_QUERY_EDITOR(editor));

  return editor;
}

static void detach_from_external_entry(CajaQueryEditor *editor) {
  if (editor->details->bar != NULL) {
    caja_search_bar_return_entry(editor->details->bar);
    g_signal_handlers_block_by_func(editor->details->entry, entry_activate_cb,
                                    editor);
    g_signal_handlers_block_by_func(editor->details->entry, entry_changed_cb,
                                    editor);
  }
}

static void attach_to_external_entry(CajaQueryEditor *editor) {
  if (editor->details->bar != NULL) {
    caja_search_bar_borrow_entry(editor->details->bar);
    g_signal_handlers_unblock_by_func(editor->details->entry, entry_activate_cb,
                                      editor);
    g_signal_handlers_unblock_by_func(editor->details->entry, entry_changed_cb,
                                      editor);

    editor->details->change_frozen = TRUE;
    gtk_entry_set_text(GTK_ENTRY(editor->details->entry),
                       editor->details->last_set_query_text);
    editor->details->change_frozen = FALSE;
  }
}

GtkWidget *caja_query_editor_new_with_bar(gboolean start_hidden,
                                          gboolean is_indexed,
                                          gboolean start_attached,
                                          CajaSearchBar *bar,
                                          CajaWindowSlot *slot) {
  GtkWidget *entry;
  CajaQueryEditor *editor;

  editor = CAJA_QUERY_EDITOR(g_object_new(CAJA_TYPE_QUERY_EDITOR, NULL));
  editor->details->is_indexed = is_indexed;

  caja_query_editor_set_visible(editor, !start_hidden);

  editor->details->bar = bar;
  eel_add_weak_pointer(&editor->details->bar);

  editor->details->slot = slot;

  entry = caja_search_bar_borrow_entry(bar);
  setup_external_entry(editor, entry);
  if (!start_attached) {
    detach_from_external_entry(editor);
  }

  g_signal_connect_object(slot, "active", G_CALLBACK(attach_to_external_entry),
                          editor, G_CONNECT_SWAPPED);
  g_signal_connect_object(slot, "inactive",
                          G_CALLBACK(detach_from_external_entry), editor,
                          G_CONNECT_SWAPPED);

  return GTK_WIDGET(editor);
}

void caja_query_editor_set_query(CajaQueryEditor *editor, CajaQuery *query) {
  CajaQueryEditorRowType type;
  char *text;

  if (!query) {
    caja_query_editor_clear_query(editor);
    return;
  }

  text = caja_query_get_text(query);

  if (!text) {
    text = g_strdup("");
  }

  editor->details->change_frozen = TRUE;
  gtk_entry_set_text(GTK_ENTRY(editor->details->entry), text);

  for (type = 0; type < CAJA_QUERY_EDITOR_ROW_LAST; type++) {
    row_type[type].add_rows_from_query(editor, query);
  }

  editor->details->change_frozen = FALSE;

  g_free(editor->details->last_set_query_text);
  editor->details->last_set_query_text = text;
}
