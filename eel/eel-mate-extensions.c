/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-mate-extensions.c - implementation of new functions that operate on
                            mate classes. Perhaps some of these should be
                            rolled into mate someday.

   Copyright (C) 1999, 2000, 2001 Eazel, Inc.

   The Mate Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Mate Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Mate Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Authors: Darin Adler <darin@eazel.com>
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define MATE_DESKTOP_USE_UNSTABLE_API

#include <gtk/gtk.h>
#include <libmate-desktop/mate-desktop-utils.h>

#include "eel-mate-extensions.h"

/* Return a command string containing the path to a terminal on this system. */

static char *try_terminal_command(const char *program, const char *args) {
  char *program_in_path, *quoted, *result;

  if (program == NULL) {
    return NULL;
  }

  program_in_path = g_find_program_in_path(program);
  if (program_in_path == NULL) {
    return NULL;
  }

  quoted = g_shell_quote(program_in_path);
  g_free(program_in_path);
  if (args == NULL || args[0] == '\0') {
    return quoted;
  }
  result = g_strconcat(quoted, " ", args, NULL);
  g_free(quoted);
  return result;
}

static char *try_terminal_command_argv(int argc, char **argv) {
  GString *string;
  int i;
  char *result;

  if (argc == 0) {
    return NULL;
  }

  if (argc == 1) {
    return try_terminal_command(argv[0], NULL);
  }

  string = g_string_new(argv[1]);
  for (i = 2; i < argc; i++) {
    char *quoted;

    quoted = g_shell_quote(argv[i]);
    g_string_append_c(string, ' ');
    g_string_append(string, quoted);
    g_free(quoted);
  }
  result = try_terminal_command(argv[0], string->str);
  g_string_free(string, TRUE);

  return result;
}

static char *get_terminal_command_prefix(gboolean for_command) {
  int argc;
  char **argv;
  char *command;
  guint i;
  static const char *const commands[][3] = {
      {"mate-terminal", "-x", ""}, {"dtterm", "-e", "-ls"},
      {"nxterm", "-e", "-ls"},     {"color-xterm", "-e", "-ls"},
      {"rxvt", "-e", "-ls"},       {"xterm", "-e", "-ls"},
  };

  /* Try the terminal from preferences. Use without any
   * arguments if we are just doing a standalone terminal.
   */
  argc = 0;
  argv = g_new0(char *, 1);
  mate_desktop_prepend_terminal_to_vector(&argc, &argv);

  command = NULL;
  if (argc != 0) {
    if (for_command) {
      command = try_terminal_command_argv(argc, argv);
    } else {
      /* Strip off the arguments in a lame attempt
       * to make it be an interactive shell.
       */
      command = try_terminal_command(argv[0], NULL);
    }
  }

  while (argc != 0) {
    g_free(argv[--argc]);
  }
  g_free(argv);

  if (command != NULL) {
    return command;
  }

  /* Try well-known terminal applications in same order that gmc did. */
  for (i = 0; i < G_N_ELEMENTS(commands); i++) {
    command =
        try_terminal_command(commands[i][0], commands[i][for_command ? 1 : 2]);
    if (command != NULL) {
      break;
    }
  }

  return command;
}

char *eel_mate_make_terminal_command(const char *command) {
  char *prefix, *quoted, *terminal_command;

  if (command == NULL) {
    return get_terminal_command_prefix(FALSE);
  }
  prefix = get_terminal_command_prefix(TRUE);
  quoted = g_shell_quote(command);
  terminal_command = g_strconcat(prefix, " /bin/sh -c ", quoted, NULL);
  g_free(prefix);
  g_free(quoted);
  return terminal_command;
}

void eel_mate_open_terminal_on_screen(const char *command, GdkScreen *screen) {
  char *command_line;

  if (screen == NULL) {
    screen = gdk_screen_get_default();
  }

  command_line = eel_mate_make_terminal_command(command);
  if (command_line == NULL) {
    g_message("Could not start a terminal");
    return;
  }

  mate_gdk_spawn_command_line_on_screen(screen, command_line, NULL);
  g_free(command_line);
}
