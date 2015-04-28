/*
 * field.h
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

#ifndef __FIELD_H__
#define __FIELD_H__

/* Includes --------------------------------------------------------------- */

/* Defines ---------------------------------------------------------------- */

#define Q_FIELD_SIZE 256

/*
 * This encapsulates a text field on the screen.  Every field has a
 * height of 1.
 */
struct field {

        /* Data in field */
        wchar_t data[Q_FIELD_SIZE];
        int data_n;

        /* Window coordinates */
        int x;
        int y;

        /* Visible width on screen */
        int width;

        /*
         * If true, only allow enough characters that will fit in the
         * width.  If false, allow the field to scroll to the right.
         */
        Q_BOOL fixed;

        /* Current editing position within data[] */
        int position;

        /* Beginning of visible portion */
        int window_start;

        /* If true, new characters are inserted at position. */
        Q_BOOL insert_mode;

        /* One of the Q_COLOR colors */
        int color_active;

        /* One of the Q_COLOR colors */
        int color_inactive;

        /* If true, this field will be rendered as background. */
        Q_BOOL invisible;

};

struct fieldset {
        /* The rendering window */
        void * window;

        /* All fields in this fieldset */
        struct field ** fields;
        int fields_n;

        /* The currently-selected field */
        struct field * active_field;
        int active_field_i;

        /* Special case for phonebook */
        Q_BOOL inactive;
};

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

extern void fieldset_keystroke(struct fieldset * fieldset, const int keystroke);
extern void fieldset_left(struct fieldset * fieldset);
extern void fieldset_right(struct fieldset * fieldset);
extern void fieldset_first(struct fieldset * fieldset);
extern void fieldset_backspace(struct fieldset * fieldset);
extern void fieldset_delete_char(struct fieldset * fieldset);
extern void fieldset_insert_char(struct fieldset * fieldset);
extern void fieldset_home_char(struct fieldset * fieldset);
extern void fieldset_end_char(struct fieldset * fieldset);

extern void field_set_colors(const int forecolor, const int backcolor);

extern void fieldset_next_field(struct fieldset * fieldset);
extern void fieldset_prev_field(struct fieldset * fieldset);

extern wchar_t * field_get_value(struct field * field);
extern char * field_get_char_value(struct field * field);
extern void field_set_value(struct field * field, const wchar_t * value);
extern void field_set_char_value(struct field * field, const char * value);

extern struct field * field_malloc(const int width, const int toprow,
        const int leftcol, Q_BOOL fixed, int color_active, int color_inactive);

extern void fieldset_render(struct fieldset * fieldset);
extern struct fieldset * fieldset_malloc(struct field ** fields,
        const int fields_n, void * window);
extern void fieldset_free(struct fieldset * fieldset);

#endif /* __FIELD_H__ */
