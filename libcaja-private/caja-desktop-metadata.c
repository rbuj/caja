/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Caja
 *
 * Copyright (C) 2011 Red Hat, Inc.
 *               2012 Stefano Karapetsas
 * Copyright (C) 2012-2021 The MATE developers
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
 * Authors: Cosimo Cecchi <cosimoc@redhat.com>
 *          Stefano Karapetsas <stefano@karapetsas.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "caja-desktop-metadata.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "caja-directory-notify.h"
#include "caja-file-private.h"
#include "caja-file-utilities.h"

static guint save_in_idle_source_id = 0;

static gchar *get_keyfile_path(void) {
  gchar *xdg_dir, *retval;

  xdg_dir = caja_get_user_directory();
  retval = g_build_filename(xdg_dir, "desktop-metadata", NULL);

  g_free(xdg_dir);

  return retval;
}

static gboolean save_in_idle_cb(gpointer data) {
  GKeyFile *keyfile = data;
  gchar *contents, *filename;
  gsize length;
  GError *error = NULL;

  save_in_idle_source_id = 0;

  contents = g_key_file_to_data(keyfile, &length, NULL);
  filename = get_keyfile_path();

  if (contents != NULL) {
    g_file_set_contents(filename, contents, length, &error);
    g_free(contents);
  }

  if (error != NULL) {
    g_warning("Couldn't save the desktop metadata keyfile to disk: %s",
              error->message);
    g_error_free(error);
  }

  g_free(filename);

  return FALSE;
}

static void save_in_idle(GKeyFile *keyfile) {
  if (save_in_idle_source_id != 0) {
    g_source_remove(save_in_idle_source_id);
  }

  save_in_idle_source_id = g_idle_add(save_in_idle_cb, keyfile);
}

static GKeyFile *load_metadata_keyfile(void) {
  GKeyFile *retval;
  GError *error = NULL;
  gchar *filename;

  retval = g_key_file_new();
  filename = get_keyfile_path();

  g_key_file_load_from_file(retval, filename, G_KEY_FILE_NONE, &error);

  if (error != NULL) {
    if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
      g_print("Unable to open the desktop metadata keyfile: %s\n",
              error->message);
    }

    g_error_free(error);
  }

  g_free(filename);

  return retval;
}

static GKeyFile *get_keyfile(void) {
  static gboolean keyfile_loaded = FALSE;
  static GKeyFile *keyfile = NULL;

  if (!keyfile_loaded) {
    keyfile = load_metadata_keyfile();
    keyfile_loaded = TRUE;
  }

  return keyfile;
}

void caja_desktop_set_metadata_string(CajaFile *file, const gchar *name,
                                      const gchar *key, const gchar *string) {
  GKeyFile *keyfile;
  GError *error = NULL;

  keyfile = get_keyfile();

  if (string != NULL) {
    g_key_file_set_string(keyfile, name, key, string);
  } else {
    /* NULL as value is taken to mean that we want to remove the key */

    g_key_file_remove_key(keyfile, name, key, &error);

    if (error != NULL) {
      g_warning("Couldn't remove the key '%s' from '%s' in the keyfile: %s",
                key, name, error->message);
      g_error_free(error);
    }
  }

  save_in_idle(keyfile);

  if (caja_desktop_update_metadata_from_keyfile(file, name)) {
    caja_file_changed(file);
  }
}

#define STRV_TERMINATOR "@x-caja-desktop-metadata-term@"

void caja_desktop_set_metadata_stringv(CajaFile *file, const char *name,
                                       const char *key,
                                       const char *const *stringv) {
  GKeyFile *keyfile;
  guint length;
  gchar **actual_stringv = NULL;
  gboolean free_strv = FALSE;

  keyfile = get_keyfile();

  /* if we would be setting a single-length strv, append a fake
   * terminator to the array, to be able to differentiate it later from
   * the single string case
   */
  length = g_strv_length((gchar **)stringv);

  if (length == 1) {
    actual_stringv = g_malloc0(3 * sizeof(gchar *));
    actual_stringv[0] = (gchar *)stringv[0];
    actual_stringv[1] = STRV_TERMINATOR;
    actual_stringv[2] = NULL;

    length = 2;
    free_strv = TRUE;
  } else {
    actual_stringv = (gchar **)stringv;
  }

  g_key_file_set_string_list(keyfile, name, key, (const gchar **)actual_stringv,
                             length);

  save_in_idle(keyfile);

  if (caja_desktop_update_metadata_from_keyfile(file, name)) {
    caja_file_changed(file);
  }

  if (free_strv) {
    g_free(actual_stringv);
  }
}

gboolean caja_desktop_update_metadata_from_keyfile(CajaFile *file,
                                                   const gchar *name) {
  gchar **keys, **values;
  const gchar *actual_values[2];
  const gchar *value;
  gsize length, values_length;
  GKeyFile *keyfile;
  GFileInfo *info;
  gsize idx;
  gboolean res;

  keyfile = get_keyfile();

  keys = g_key_file_get_keys(keyfile, name, &length, NULL);

  if (keys == NULL) {
    return FALSE;
  }

  info = g_file_info_new();

  for (idx = 0; idx < length; idx++) {
    const gchar *key;
    gchar *gio_key;

    key = keys[idx];
    values =
        g_key_file_get_string_list(keyfile, name, key, &values_length, NULL);

    gio_key = g_strconcat("metadata::", key, NULL);

    if (values_length < 1) {
      continue;
    } else if (values_length == 1) {
      g_file_info_set_attribute_string(info, gio_key, values[0]);
    } else if (values_length == 2) {
      /* deal with the fact that single-length strv are stored
       * with an additional terminator in the keyfile string, to differentiate
       * them from the regular string case.
       */
      value = values[1];

      if (g_strcmp0(value, STRV_TERMINATOR) == 0) {
        /* if the 2nd value is the terminator, remove it */
        actual_values[0] = values[0];
        actual_values[1] = NULL;

        g_file_info_set_attribute_stringv(info, gio_key,
                                          (gchar **)actual_values);
      } else {
        /* otherwise, set it as a regular strv */
        g_file_info_set_attribute_stringv(info, gio_key, values);
      }
    } else {
      g_file_info_set_attribute_stringv(info, gio_key, values);
    }

    g_free(gio_key);
    g_strfreev(values);
  }

  res = caja_file_update_metadata_from_info(file, info);

  g_strfreev(keys);
  g_object_unref(info);

  return res;
}
