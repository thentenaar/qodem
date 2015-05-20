/*
 * translate.c
 *
 * This module is licensed under the GNU General Public License Version 2.
 * Please see the file "COPYING" in this directory for more information about
 * the GNU General Public License Version 2.
 *
 *     Copyright (C) 2015  Kevin Lamonte
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "screen.h"
#include "forms.h"
#include "console.h"
#include "states.h"
#include "field.h"
#include "help.h"
#include "translate.h"

/**
 * Which table is currently being edited.
 */
static struct q_translate_table_struct * editing_table;

/**
 * Which table entry is currently being edited.
 */
static int selected_entry = 0;

/**
 * The ASCII INPUT table.
 */
struct q_translate_table_struct q_translate_table_input;

/**
 * The ASCII OUTPUT table.
 */
struct q_translate_table_struct q_translate_table_output;

#define TRANSLATE_TABLE_LINE_SIZE       128

#define TRANSLATE_TABLE_FILENAME        "translate.tbl"

/**
 * Whether we have changed the table mapping in the editor.
 */
static Q_BOOL saved_changes = Q_TRUE;

/**
 * This must be called to initialize the translate tables from the config
 * file.
 */
void load_translate_tables() {
    FILE * file;
    char * full_filename;
    char line[TRANSLATE_TABLE_LINE_SIZE];
    char * key;
    char * value;
    char * endptr;
    unsigned char map_to;
    unsigned char map_from;
    int i;

    enum {
        SCAN_INPUT,
        SCAN_INPUT_VALUES,
        SCAN_OUTPUT_VALUES
    };

    int state = SCAN_INPUT;

    file = open_datadir_file(TRANSLATE_TABLE_FILENAME, &full_filename, "r");
    if (file == NULL) {

        /*
         * Reset the defaults
         */
        for (i = 0; i < 256; i++) {
            q_translate_table_input.map_to[i] = i;
            q_translate_table_output.map_to[i] = i;
        }

        /*
         * Quietly exit.
         */

        /*
         * No leak
         */
        Xfree(full_filename, __FILE__, __LINE__);
        return;
    }

    memset(line, 0, sizeof(line));
    while (!feof(file)) {

        if (fgets(line, sizeof(line), file) == NULL) {
            /*
             * This should cause the outer while's feof() check to fail
             */
            continue;
        }
        line[sizeof(line) - 1] = 0;

        if ((strlen(line) == 0) || (line[0] == '#')) {
            /*
             * Empty or comment line
             */
            continue;
        }

        /*
         * Nix trailing whitespace
         */
        while ((strlen(line) > 0) && isspace(line[strlen(line) - 1])) {
            line[strlen(line) - 1] = 0;
        }

        if (state == SCAN_INPUT) {
            /*
             * Looking for "[input]"
             */
            if (strcmp(line, "[input]") == 0) {
                state = SCAN_INPUT_VALUES;
            }
            continue;
        }

        /*
         * Looking for "[output]"
         */
        if (strcmp(line, "[output]") == 0) {
            state = SCAN_OUTPUT_VALUES;
            continue;
        }

        key = line;
        while ((strlen(key) > 0) && (isspace(*key))) {
            key++;
        }

        value = strchr(key, '=');
        if (value == NULL) {
            /*
             * Invalid line
             */
            continue;
        }

        *value = 0;
        value++;
        while ((strlen(value) > 0) && (isspace(*value))) {
            value++;
        }
        if (*value == 0) {
            /*
             * Invalid mapping
             */
            continue;
        }

        map_to = strtoul(value, &endptr, 10);
        if (*endptr != 0) {
            /*
             * Invalid mapping
             */
            continue;
        }

        while ((strlen(key) > 0) && (isspace(key[strlen(key) - 1]))) {
            key[strlen(key) - 1] = 0;
        }

        map_from = strtoul(key, &endptr, 10);
        if (*endptr != 0) {
            /*
             * Invalid mapping
             */
            continue;
        }

        /*
         * map_from and map_to are both valid unsigned integers
         */
        if (state == SCAN_INPUT_VALUES) {
            q_translate_table_input.map_to[map_from] = map_to;
        } else {
            q_translate_table_output.map_to[map_from] = map_to;
        }

    }

    Xfree(full_filename, __FILE__, __LINE__);
    fclose(file);

    /*
     * Note that we have no outstanding changes to save
     */
    saved_changes = Q_TRUE;
}

/**
 * Save the translate tables to the config file.
 */
static void save_translate_tables() {
    char notify_message[DIALOG_MESSAGE_SIZE];
    char * full_filename;
    FILE * file;
    int i;

    file = open_datadir_file(TRANSLATE_TABLE_FILENAME, &full_filename, "w");
    if (file == NULL) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error opening file \"%s\" for writing: %s"),
                 TRANSLATE_TABLE_FILENAME, strerror(errno));
        notify_form(notify_message, 0);
        /*
         * No leak
         */
        Xfree(full_filename, __FILE__, __LINE__);
        return;
    }

    /*
     * Header
     */
    fprintf(file, "# Qodem ASCII translate tables file\n");
    fprintf(file, "#\n");

    /*
     * Input
     */
    fprintf(file, "\n[input]\n");
    for (i = 0; i < 256; i++) {
        fprintf(file, "%d = %d\n", i, q_translate_table_input.map_to[i]);
    }

    /*
     * Output
     */
    fprintf(file, "\n[output]\n");
    for (i = 0; i < 256; i++) {
        fprintf(file, "%d = %d\n", i, q_translate_table_output.map_to[i]);
    }

    Xfree(full_filename, __FILE__, __LINE__);
    fclose(file);

    /*
     * Note that we have no outstanding changes to save
     */
    saved_changes = Q_TRUE;
}

/**
 * Create the config file for the translate tables (translate.tbl).
 */
void create_translate_table_file() {
    FILE * file;
    char buffer[FILENAME_SIZE];
    char * full_filename;
    int i;

    /*
     * Set defaults
     */
    for (i = 0; i < 256; i++) {
        q_translate_table_input.map_to[i] = i;
        q_translate_table_output.map_to[i] = i;
    }

    sprintf(buffer, TRANSLATE_TABLE_FILENAME);
    file = open_datadir_file(buffer, &full_filename, "a");
    if (file != NULL) {
        fclose(file);
    } else {
        fprintf(stderr, _("Error creating file \"%s\": %s"), full_filename,
                strerror(errno));
    }
    Xfree(full_filename, __FILE__, __LINE__);

    /*
     * Now save the default values
     */
    save_translate_tables();
}

/**
 * Draw screen for the Alt-A translation table selection dialog.
 */
void translate_table_menu_refresh() {
    char * status_string;
    int status_left_stop;
    char * message;
    int message_left;
    int window_left;
    int window_top;
    int window_height = 9;
    int window_length;

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    /*
     * Clear screen for when it resizes
     */
    console_refresh(Q_FALSE);

    window_length = 24;
    window_left = WIDTH - 1 - window_length;
    if (window_left < 0) {
        window_left = 0;
    } else {
        window_left /= 2;
    }
    window_top = HEIGHT - 1 - window_height;
    if (window_top < 0) {
        window_top = 0;
    } else {
        window_top /= 10;
    }

    screen_draw_box(window_left, window_top, window_left + window_length,
                    window_top + window_height);
    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);

    status_string = _(" Select the Strip/Replace Table to Edit   ESC/`-Exit ");
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    message = _("Table Selection");
    message_left = window_length - (strlen(message) + 2);
    if (message_left < 0) {
        message_left = 0;
    } else {
        message_left /= 2;
    }
    screen_put_color_printf_yx(window_top + 0, window_left + message_left,
                               Q_COLOR_WINDOW_BORDER, " %s ", message);

    screen_put_color_str_yx(window_top + 2, window_left + 2,
                            _("Select Table to Edit"), Q_COLOR_MENU_TEXT);

    screen_put_color_str_yx(window_top + 4, window_left + 7, "1",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, " -  %s", _("INPUT"));
    screen_put_color_str_yx(window_top + 5, window_left + 7, "2",
                            Q_COLOR_MENU_COMMAND);
    screen_put_color_printf(Q_COLOR_MENU_TEXT, " -  %s", _("OUTPUT"));

    screen_put_color_str_yx(window_top + 7, window_left + 2,
                            _("Your Choice ? "), Q_COLOR_MENU_COMMAND);

    screen_flush();
    q_screen_dirty = Q_FALSE;
}

/**
 * Keyboard handler for the Alt-A translation table selection dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void translate_table_menu_keyboard_handler(const int keystroke,
                                           const int flags) {

    /*
     * Default to invalid
     */
    editing_table = NULL;

    switch (keystroke) {

    case '1':
        editing_table = &q_translate_table_input;
        break;

    case '2':
        editing_table = &q_translate_table_output;
        break;

    case '`':
        /*
         * Backtick works too
         */
    case KEY_ESCAPE:
        /*
         * ESC return to TERMINAL mode
         */
        switch_state(Q_STATE_CONSOLE);

        /*
         * The ABORT exit point
         */
        return;

    default:
        /*
         * Ignore keystroke
         */
        return;
    }

    /*
     * The OK exit point
     */
    q_screen_dirty = Q_TRUE;
    console_refresh(Q_FALSE);
    switch_state(Q_STATE_TRANSLATE_EDITOR);
}

/* A form + fields to handle the editing of a given key binding value */
static void * edit_table_entry_window;
static struct fieldset * edit_table_entry_form;
static struct field * edit_table_entry_field;
static Q_BOOL editing_entry = Q_FALSE;
static Q_BOOL editing_high_128 = Q_FALSE;
static int window_left;
static int window_top;
static int window_length = 80;
static int window_height = 22;

/**
 * Keyboard handler for the Alt-A translation table editor screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
void translate_table_editor_keyboard_handler(const int keystroke,
                                             const int flags) {
    int row;
    int col;
    int new_keystroke;
    wchar_t * value;
    char buffer[5];

    col = (selected_entry % 128) / 16;
    row = (selected_entry % 128) % 16;

    switch (keystroke) {

    case 'S':
    case 's':
        /*
         * Switch editing tables
         */
        if (editing_entry == Q_FALSE) {
            if (editing_high_128 == Q_TRUE) {
                editing_high_128 = Q_FALSE;
                selected_entry = 0;
            } else {
                editing_high_128 = Q_TRUE;
                selected_entry = 128;
            }
            q_screen_dirty = Q_TRUE;
            return;
        }
        break;

    case ' ':
        if (editing_entry == Q_FALSE) {
            /*
             * SPACEBAR - Begin editing
             */
            editing_entry = Q_TRUE;
        }
        break;

    default:
        break;
    }

    switch (keystroke) {

    case Q_KEY_F(1):
        launch_help(Q_HELP_TRANSLATE_EDITOR);
        console_refresh(Q_FALSE);
        q_screen_dirty = Q_TRUE;
        return;

    case '`':
        /*
         * Backtick works too
         */
    case KEY_ESCAPE:
        /*
         * ESC return to TERMINAL mode
         */
        if (editing_entry == Q_TRUE) {
            editing_entry = Q_FALSE;
            q_cursor_off();

            /*
             * Delete the editing form
             */
            fieldset_free(edit_table_entry_form);
            screen_delwin(edit_table_entry_window);

        } else {

            if (saved_changes == Q_FALSE) {
                /*
                 * Ask if the user wants to save changes.
                 */
                new_keystroke = notify_prompt_form(_("Attention!"),
                    _("Changes have been made!  Save them? [Y/n] "),
                    _(" Y-Save Changes   N-Exit "),
                    Q_TRUE, 0.0,
                    "YyNn\r");
                new_keystroke = tolower(new_keystroke);

                /*
                 * Save if the user said so
                 */
                if ((new_keystroke == 'y') || (new_keystroke == C_CR)) {
                    save_translate_tables();
                } else {
                    /*
                     * Abandon changes
                     */
                    load_translate_tables();
                }

            }

            /*
             * Editing form is already deleted, so just escape out
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            switch_state(Q_STATE_TRANSLATE_MENU);
        }

        q_screen_dirty = Q_TRUE;
        return;

    case Q_KEY_F(10):
        if (editing_entry == Q_FALSE) {
            /*
             * Save
             */
            save_translate_tables();

            /*
             * Editing form is already deleted, so just escape out
             */
            q_screen_dirty = Q_TRUE;
            console_refresh(Q_FALSE);
            switch_state(Q_STATE_TRANSLATE_MENU);
        }
        return;

    case Q_KEY_DOWN:
        if (editing_entry == Q_FALSE) {
            if (row < 16 - 1) {
                selected_entry++;
            }
            q_screen_dirty = Q_TRUE;
        }
        return;

    case Q_KEY_UP:
        if (editing_entry == Q_FALSE) {
            if (row > 0) {
                selected_entry--;
            }
            q_screen_dirty = Q_TRUE;
        }
        return;

    case Q_KEY_LEFT:
        if (editing_entry == Q_FALSE) {
            if (col > 0) {
                selected_entry -= 16;
            }
            q_screen_dirty = Q_TRUE;
        } else {
            fieldset_left(edit_table_entry_form);
        }
        return;

    case Q_KEY_RIGHT:
        if (editing_entry == Q_FALSE) {
            if (col < (128 / 16) - 1) {
                selected_entry += 16;
            }
            q_screen_dirty = Q_TRUE;
        } else {
            fieldset_right(edit_table_entry_form);
        }
        return;

    case Q_KEY_BACKSPACE:
    case 0x08:
        if (editing_entry == Q_TRUE) {
            fieldset_backspace(edit_table_entry_form);
        }
        return;

    case Q_KEY_HOME:
        if (editing_entry == Q_TRUE) {
            fieldset_home_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_END:
        if (editing_entry == Q_TRUE) {
            fieldset_end_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_DC:
        if (editing_entry == Q_TRUE) {
            fieldset_delete_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_IC:
        if (editing_entry == Q_TRUE) {
            fieldset_insert_char(edit_table_entry_form);
        }
        return;

    case Q_KEY_ENTER:
    case C_CR:
        if (editing_entry == Q_TRUE) {
            /*
             * The OK exit point
             */
            value = field_get_value(edit_table_entry_field);
            editing_table->map_to[selected_entry] = wcstoul(value, NULL, 10);
            Xfree(value, __FILE__, __LINE__);

            saved_changes = Q_FALSE;

            fieldset_free(edit_table_entry_form);
            screen_delwin(edit_table_entry_window);
            editing_entry = Q_FALSE;
            q_cursor_off();
        }
        q_screen_dirty = Q_TRUE;
        return;

    case ' ':
        /*
         * Ignore.  We either switched into editing mode, was already editing
         * and spacebar should not be passed to the form field anyway.
         */
        break;

    default:
        /*
         * Pass to form handler
         */
        if (editing_entry == Q_TRUE) {
            if (!q_key_code_yes(keystroke)) {
                if (isdigit(keystroke)) {
                    /*
                     * Pass only digit keys to field
                     */
                    fieldset_keystroke(edit_table_entry_form, keystroke);
                }
            }
        }

        /*
         * Return here.  The logic below the switch is all about switching
         * the editing key.
         */
        return;
    }

    /*
     * If we got here, the user hit space to begin editing a key.
     */
    if (selected_entry < 10) {
        edit_table_entry_window =
            screen_subwin(1, 3, window_top + window_height - 3,
                          window_left + 49);
    } else if (selected_entry < 100) {
        edit_table_entry_window =
            screen_subwin(1, 3, window_top + window_height - 3,
                          window_left + 50);
    } else {
        edit_table_entry_window =
            screen_subwin(1, 3, window_top + window_height - 3,
                          window_left + 51);
    }
    if (check_subwin_result(edit_table_entry_window) == Q_FALSE) {
        editing_entry = Q_FALSE;
        q_cursor_off();
        q_screen_dirty = Q_TRUE;
        return;
    }

    edit_table_entry_field = field_malloc(3, 0, 0, Q_TRUE,
                                          Q_COLOR_WINDOW_FIELD_TEXT_HIGHLIGHTED,
                                          Q_COLOR_WINDOW_FIELD_HIGHLIGHTED);
    edit_table_entry_form =
        fieldset_malloc(&edit_table_entry_field, 1, edit_table_entry_window);

    screen_put_color_printf_yx(window_top + window_height - 3, window_left + 25,
                               Q_COLOR_MENU_COMMAND,
                               _("Enter new value for %d >"), selected_entry);

    snprintf(buffer, sizeof(buffer), "%d",
             editing_table->map_to[selected_entry]);
    field_set_char_value(edit_table_entry_field, buffer);

    /*
     * Render everything above the edit field.
     */
    screen_flush();

    /*
     * Render the field.  Must be called AFTER screen_flush() to put the
     * cursor on the right spot.
     */
    q_cursor_on();
    fieldset_render(edit_table_entry_form);

    screen_flush();
}

/**
 * Draw screen for the Alt-A translation table editor screen.
 */
void translate_table_editor_refresh() {
    char * status_string;
    int status_left_stop;
    int title_left;
    char * title;
    int i, end_i;
    int row, col;

    window_left = (WIDTH - window_length) / 2;
    window_top = (HEIGHT - window_height) / 2;

    if (q_screen_dirty == Q_FALSE) {
        return;
    }

    /*
     * Clear screen for when it resizes
     */
    console_refresh(Q_FALSE);

    screen_draw_box(window_left, window_top, window_left + window_length,
                    window_top + window_height);

    if (editing_table == &q_translate_table_input) {
        title = _("INPUT Strip/Replace Table");
    } else {
        title = _("OUTPUT Strip/Replace Table");
    }
    title_left = window_length - (strlen(title) + 2);
    if (title_left < 0) {
        title_left = 0;
    } else {
        title_left /= 2;
    }
    screen_put_color_printf_yx(window_top, window_left + title_left,
                               Q_COLOR_WINDOW_BORDER, " %s ", title);
    screen_put_color_str_yx(window_top + window_height - 1,
                            window_left + window_length - 10, _("F1 Help"),
                            Q_COLOR_WINDOW_BORDER);

    screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH,
                              Q_COLOR_STATUS);
    if (editing_entry == Q_FALSE) {
        status_string = _(""
                          " ARROWS-Movement   S-SWAP   SPACEBAR-Change   F10-Save   ESC/`-Exit ");
    } else {
        status_string = _(" ENTER-Save Changes  ESC/`-Exit ");
    }
    status_left_stop = WIDTH - strlen(status_string);
    if (status_left_stop <= 0) {
        status_left_stop = 0;
    } else {
        status_left_stop /= 2;
    }
    screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string,
                            Q_COLOR_STATUS);

    screen_put_color_str_yx(window_top + 1, window_left + 21,
                            _("In Character | |  Out Character | |"),
                            Q_COLOR_MENU_TEXT);
    screen_put_color_char_yx(window_top + 1, window_left + 21 + 14,
                             cp437_chars[selected_entry & 0xFF],
                             Q_COLOR_MENU_COMMAND);
    screen_put_color_char_yx(window_top + 1, window_left + 21 + 33,
                             cp437_chars[editing_table->
                                         map_to[selected_entry] & 0xFF],
                             Q_COLOR_MENU_COMMAND);

    if (editing_high_128) {
        i = 128;
    } else {
        i = 0;
    }
    end_i = i + 128;

    for (; i < end_i; i++) {
        col = (i % 128) / 16;
        row = (i % 128) % 16;

        if (i == selected_entry) {
            screen_put_color_printf_yx(window_top + 2 + row,
                                       window_left + 3 + (col * 9),
                                       Q_COLOR_MENU_COMMAND, "[%3d-%3d]", i,
                                       editing_table->map_to[i]);
        } else {
            screen_put_color_printf_yx(window_top + 2 + row,
                                       window_left + 3 + (col * 9),
                                       Q_COLOR_MENU_TEXT, " %3d-%3d ", i,
                                       editing_table->map_to[i]);
        }
    }

    q_screen_dirty = Q_FALSE;
    screen_flush();
}
