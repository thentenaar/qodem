/*
 * field.c
 *
 * This module is licensed under the GNU General Public License
 * Version 2.  Please see the file "COPYING" in this directory for
 * more information about the GNU General Public License Version 2.
 *
 *     Copyright (C) 2015  Kevin Lamonte
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "screen.h"
#include "codepage.h"
#include "field.h"

static void field_render(struct field * field, void * window, Q_BOOL active) {
        int data_i;
        int i;
        int color;

        if (active == Q_TRUE) {
                color = field->color_active;
        } else {
                color = field->color_inactive;
        }
        data_i = field->window_start;

        if (field->invisible == Q_TRUE) {
                for (i = 0; (i < field->width); i++) {
                        screen_win_put_color_char_yx(window, field->y, field->x + i,
                                ' ', color);
                }
                return;
        }

        for (i = 0; (data_i < field->data_n) && (i < field->width); i++, data_i++) {
                screen_win_put_color_char_yx(window, field->y, field->x + i,
                        field->data[data_i], color);
        }
        /* Pad with HATCH the rest of the way */
        for (;i < field->width; i++) {
                screen_win_put_color_char_yx(window, field->y, field->x + i,
                        cp437_chars[HATCH], color);
        }
} /* ---------------------------------------------------------------------- */

static void append_char(struct field * field, int keystroke) {
        /* Append the LAST character */
        field->data[field->data_n] = keystroke;
        field->data_n++;
        field->position++;
        if (field->fixed == Q_TRUE) {
                if (field->position == field->width) {
                        field->position--;
                }
        } else {
                if ((field->position - field->window_start) == field->width) {
                        field->window_start++;
                }
        }
} /* ---------------------------------------------------------------------- */

static void insert_char(struct field * field, int keystroke) {
        memmove(field->data + (field->position + 1),
                field->data + (field->position),
                sizeof(wchar_t) * (field->data_n - field->position));
        field->data[field->position] = keystroke;
        field->data_n++;
        field->position++;
        if ((field->position - field->window_start) == field->width) {
                assert(field->fixed == Q_FALSE);
                field->window_start++;
        }
} /* ---------------------------------------------------------------------- */

void fieldset_keystroke(struct fieldset * fieldset, int keystroke) {
        struct field * field = fieldset->active_field;
        assert(fieldset->active_field != NULL);
        assert(q_key_code_yes(keystroke) == 0);
        assert(keystroke != -1);

        /* Disregard enter key */
        if ((keystroke == Q_KEY_ENTER) || (keystroke == C_CR)) {
                return;
        }

        /* Process keystroke */
        if ((field->position == field->data_n) && (field->data_n < field->width)) {
                /* Append case */
                append_char(field, keystroke);
        } else if ((field->position < field->data_n) && (field->data_n < field->width)) {
                /* Overwrite or insert a character */
                if (field->insert_mode == Q_FALSE) {
                        /* Replace character */
                        field->data[field->position] = keystroke;
                        field->position++;
                } else {
                        /* Insert character */
                        insert_char(field, keystroke);
                }
        } else if ((field->position < field->data_n) && (field->data_n >= field->width)) {
                /* Multiple cases here */
                if ((field->fixed == Q_TRUE) && (field->insert_mode == Q_TRUE)) {
                        /* Buffer is full, do nothing */
                } else if ((field->fixed == Q_TRUE) && (field->insert_mode == Q_FALSE)) {
                        /* Overwrite the last character, maybe move position */
                        field->data[field->position] = keystroke;
                        if (field->position < field->width - 1) {
                                field->position++;
                        }
                } else if ((field->fixed == Q_FALSE) && (field->insert_mode == Q_FALSE)) {
                        /* Overwrite the last character, do move position */
                        field->data[field->position] = keystroke;
                        field->position++;
                } else {
                        if (field->position == field->data_n) {
                                /* Append this character */
                                append_char(field, keystroke);
                        } else {
                                /* Insert this character */
                                insert_char(field, keystroke);
                        }
                }
        } else {
                assert(field->fixed == Q_FALSE);

                /* Append this character */
                append_char(field, keystroke);
        }

        /* Update the display */
        fieldset_render(fieldset);
} /* ---------------------------------------------------------------------- */

void fieldset_home_char(struct fieldset * fieldset) {
        struct field * field = fieldset->active_field;
        assert(fieldset->active_field != NULL);
        field->position = 0;
        field->window_start = 0;

        /* Update the display */
        fieldset_render(fieldset);
} /* ---------------------------------------------------------------------- */

void fieldset_end_char(struct fieldset * fieldset) {
        struct field * field = fieldset->active_field;
        assert(fieldset->active_field != NULL);
        field->position = field->data_n;
        if (field->fixed == Q_TRUE) {
                if (field->position >= field->width) {
                        field->position = field->data_n - 1;
                }
        } else {
                field->window_start = field->data_n - field->width + 1;
                if (field->window_start < 0) {
                        field->window_start = 0;
                }
        }

        /* Update the display */
        fieldset_render(fieldset);
} /* ---------------------------------------------------------------------- */

void fieldset_left(struct fieldset * fieldset) {
        struct field * field = fieldset->active_field;
        assert(fieldset->active_field != NULL);
        if (field->position > 0) {
                field->position--;
        }
        if (field->fixed == Q_FALSE) {
                if ((field->position == field->window_start) && (field->window_start > 0)) {
                        field->window_start--;
                }
        }

        /* Update the display */
        fieldset_render(fieldset);
} /* ---------------------------------------------------------------------- */

void fieldset_right(struct fieldset * fieldset) {
        struct field * field = fieldset->active_field;
        assert(fieldset->active_field != NULL);
        if (field->position < field->data_n) {
                field->position++;
                if (field->fixed == Q_TRUE) {
                        if (field->position == field->width) {
                                field->position--;
                        }
                } else {
                        if ((field->position - field->window_start) == field->width) {
                                field->window_start++;
                        }
                }
        }

        /* Update the display */
        fieldset_render(fieldset);
} /* ---------------------------------------------------------------------- */

void fieldset_first(struct fieldset * fieldset) {
        assert(fieldset->fields != NULL);
        assert(fieldset->fields[0] != NULL);
        fieldset->active_field = fieldset->fields[0];

        /* Update the display */
        fieldset_render(fieldset);
} /* ---------------------------------------------------------------------- */

void fieldset_backspace(struct fieldset * fieldset) {
        struct field * field = fieldset->active_field;
        assert(fieldset->active_field != NULL);
        if (field->position > 0) {
                field->position--;
                memmove(field->data + field->position,
                        field->data + field->position + 1,
                        sizeof(wchar_t) * (field->data_n - field->position));
                field->data_n--;
        }
        if (field->fixed == Q_FALSE) {
                if ((field->position == field->window_start) &&
                        (field->window_start > 0)
                ) {
                        field->window_start--;
                }
        }

        /* Update the display */
        fieldset_render(fieldset);
} /* ---------------------------------------------------------------------- */

void fieldset_insert_char(struct fieldset * fieldset) {
        struct field * field = fieldset->active_field;
        assert(fieldset->active_field != NULL);
        if (field->insert_mode == Q_TRUE) {
                field->insert_mode = Q_FALSE;
        } else {
                field->insert_mode = Q_TRUE;
        }
} /* ---------------------------------------------------------------------- */

void fieldset_delete_char(struct fieldset * fieldset) {
        struct field * field = fieldset->active_field;
        assert(fieldset->active_field != NULL);
        if ((field->data_n > 0) && (field->position < field->data_n)) {
                memmove(field->data + field->position,
                        field->data + field->position + 1,
                        sizeof(wchar_t) * (field->data_n - field->position));
                field->data_n--;
        }

        /* Update the display */
        fieldset_render(fieldset);
} /* ---------------------------------------------------------------------- */

void fieldset_render(struct fieldset * fieldset) {
        int i;
        int x;
        struct field * field = fieldset->active_field;
        assert(fieldset->active_field != NULL);

        /* Render the fields */
        for (i = 0; i < fieldset->fields_n; i++) {
                if ((fieldset->active_field == fieldset->fields[i]) && (fieldset->inactive == Q_FALSE)) {
                        field_render(fieldset->fields[i], fieldset->window, Q_TRUE);
                } else {
                        field_render(fieldset->fields[i], fieldset->window, Q_FALSE);
                }
        }

        /* Drop the cursor */
        if ((field->position > field->width) && (field->fixed == Q_TRUE)) {
                x = field->x + field->width;
        } else if ((field->position - field->window_start == field->width) && (field->fixed == Q_FALSE)) {
                x = field->x + field->width - 1;
        } else {
                x = field->x + field->position - field->window_start;
        }
        screen_win_move_yx(fieldset->window, field->y, x);

        /* Push the changes to the display */
        screen_win_flush(fieldset->window);
        wcursyncup((WINDOW *)fieldset->window);
} /* ---------------------------------------------------------------------- */

struct field * field_malloc(const int width, const int toprow, const int leftcol, Q_BOOL fixed, int color_active, int color_inactive) {
        struct field * field = (struct field *)Xmalloc(sizeof(struct field), __FILE__, __LINE__);
        field->width = width;
        field->y = toprow;
        field->x = leftcol;
        field->fixed = fixed;
        field->color_active = color_active;
        field->color_inactive = color_inactive;
        field->insert_mode = Q_TRUE;
        field->position = 0;
        field->window_start = 0;
        memset(field->data, 0, sizeof(field->data));
        field->data_n= 0;
        field->invisible = Q_FALSE;
        return field;
} /* ---------------------------------------------------------------------- */

struct fieldset * fieldset_malloc(struct field ** fields, const int fields_n, void * window) {
        struct fieldset * fieldset = (struct fieldset *)Xmalloc(sizeof(struct fieldset), __FILE__, __LINE__);
        fieldset->fields = fields;
        fieldset->fields_n = fields_n;
        fieldset->window = window;
        fieldset->active_field_i = 0;
        fieldset->active_field = fieldset->fields[fieldset->active_field_i];
        fieldset->inactive = Q_FALSE;
        return fieldset;
} /* ---------------------------------------------------------------------- */

void fieldset_free(struct fieldset * fieldset) {
        int i;
        for (i = 0; i < fieldset->fields_n; i++) {
                Xfree(fieldset->fields[i], __FILE__, __LINE__);
        }
        Xfree(fieldset, __FILE__, __LINE__);
} /* ---------------------------------------------------------------------- */

wchar_t * field_get_value(struct field * field) {
        wchar_t * value;
        value = (wchar_t *)Xmalloc(sizeof(wchar_t) * (field->data_n + 1), __FILE__, __LINE__);
        memcpy(value, field->data, sizeof(wchar_t) * field->data_n);
        value[field->data_n] = 0;
        return value;
} /* ---------------------------------------------------------------------- */

char * field_get_char_value(struct field * field) {
        char * value;
        int i;
        value = (char *)Xmalloc(sizeof(char) * (field->data_n + 1), __FILE__, __LINE__);
        for (i = 0; i < field->data_n; i++) {
                value[i] = field->data[i];
        }
        value[field->data_n] = 0;
        return value;
} /* ---------------------------------------------------------------------- */

void field_set_value(struct field * field, const wchar_t * value) {
        assert(wcslen(value) <= Q_FIELD_SIZE);
        memcpy(field->data, value, sizeof(wchar_t) * wcslen(value));
        if (field->fixed == Q_TRUE) {
                assert(wcslen(value) <= field->width);
                field->data_n = wcslen(value);
                field->position = wcslen(value);
                if (field->position >= field->width) {
                        field->position = field->data_n - 1;
                }
                field->window_start = 0;
        } else {
                field->data_n = wcslen(value);
                field->position = wcslen(value);
                field->window_start = field->data_n - field->width + 1;
                if (field->window_start < 0) {
                        field->window_start = 0;
                }
        }
} /* ---------------------------------------------------------------------- */

void field_set_char_value(struct field * field, const char * value) {
        int i;
        assert(strlen(value) <= Q_FIELD_SIZE);

        if (field->fixed == Q_TRUE) {
                assert(strlen(value) <= field->width);
                field->data_n = strlen(value);
                field->position = strlen(value);
                if (field->position >= field->width) {
                        field->position = field->data_n - 1;
                }
                field->window_start = 0;
        } else {
                field->data_n = strlen(value);
                field->position = strlen(value);
                field->window_start = field->data_n - field->width + 1;
                if (field->window_start < 0) {
                        field->window_start = 0;
                }
        }
        for (i = 0; i < strlen(value); i++) {
                field->data[i] = value[i];
        }
} /* ---------------------------------------------------------------------- */

void fieldset_next_field(struct fieldset * fieldset) {
        if (fieldset->active_field_i < fieldset->fields_n - 1) {
                fieldset->active_field_i++;
        }
        fieldset->active_field = fieldset->fields[fieldset->active_field_i];
} /* ---------------------------------------------------------------------- */

void fieldset_prev_field(struct fieldset * fieldset) {
        if (fieldset->active_field_i > 0) {
                fieldset->active_field_i--;
        }
        fieldset->active_field = fieldset->fields[fieldset->active_field_i];
} /* ---------------------------------------------------------------------- */
