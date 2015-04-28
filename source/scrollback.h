/*
 * scrollback.h
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

#ifndef __SCROLLBACK_H__
#define __SCROLLBACK_H__

/* Includes --------------------------------------------------------------- */

#include <stdio.h>              /* FILE */
#include <stddef.h>             /* wchar_t */
#include "common.h"             /* Q_BOOL */

/* Defines ---------------------------------------------------------------- */

#define Q_MAX_LINE_LENGTH               256

/* Struct representing a single line of scrollback buffer */
struct q_scrolline_struct {
        int length;                             /* Actual length of line */

        attr_t colors[Q_MAX_LINE_LENGTH];       /* Color values for each char */

        wchar_t chars[Q_MAX_LINE_LENGTH];       /* Char values of line */

        struct q_scrolline_struct * next;       /* Pointer to next line */
        struct q_scrolline_struct * prev;       /* Pointer to previous line */

        Q_BOOL dirty;                           /* Dirty flag */
        Q_BOOL double_width;                    /* Double-width line */

        int double_height;                      /*
                                                 * Double-height line flag:
                                                 *
                                                 *   0 = single height
                                                 *   1 = top half double height
                                                 *   2 = bottom half double
                                                 *       height
                                                 */

        Q_BOOL reverse_color;                   /* DECSCNM - reverse video */

        attr_t search_colors[Q_MAX_LINE_LENGTH];        /*
                                                         * Color values for each
                                                         * char after a search
                                                         * function.
                                                         */

        Q_BOOL search_match;                    /*
                                                 * If true, render with
                                                 * search_colors
                                                 */

};

/* Globals ---------------------------------------------------------------- */

/* The scrollback buffer, stored in scrollback.c */
extern struct q_scrolline_struct * q_scrollback_buffer;

/* The last line of the scrollback buffer, stored in scrollback.c */
extern struct q_scrolline_struct * q_scrollback_last;

/* The current view position in the scrollback buffer, stored in scrollback.c */
extern struct q_scrolline_struct * q_scrollback_position;

/* The current editing position in the scrollback buffer, stored in scrollback.c */
extern struct q_scrolline_struct * q_scrollback_current;

/* The maximum size of the scrollback buffer. Default is 20000. */
extern int q_scrollback_max;

/* The Find and Find Again search string and found flag */
extern wchar_t * q_scrollback_search_string;
extern Q_BOOL q_scrollback_highlight_search_string;

/* Functions -------------------------------------------------------------- */

extern void scrollback_keyboard_handler(const int keystroke, const int flags);
extern void scrollback_refresh();
extern void new_scrollback_line();
extern void render_scrollback(const int skip_lines);
extern void print_character(const wchar_t character);
extern Q_BOOL screen_dump(const char * filename);

extern void cursor_up(const int count, const Q_BOOL honor_scroll_region);
extern void cursor_down(const int count, const Q_BOOL honor_scroll_region);
extern void cursor_left(const int count, const Q_BOOL honor_scroll_region);
extern void cursor_right(const int count, const Q_BOOL honor_scroll_region);
extern void cursor_position(int row, int col);
extern void erase_line(const int start, const int end, const Q_BOOL honor_protected);
extern void fill_line_with_character(const int start, const int end, wchar_t character, const Q_BOOL honor_protected);
extern void erase_screen(const int start_row, const int start_col, const int end_row, const int end_col, const Q_BOOL honor_protected);
extern void cursor_formfeed();
extern void cursor_carriage_return();
extern void cursor_linefeed(const Q_BOOL new_line_mode);
extern void scrolling_region_scroll_down(const int region_top, const int region_bottom, const int count);
extern void scrolling_region_scroll_up(const int region_top, const int region_bottom, const int count);
extern void rectangle_scroll_down(const int top, const int left, const int bottom, const int right, const int count);
extern void rectangle_scroll_up(const int top, const int left, const int bottom, const int right, const int count);
extern void scroll_down(const int count);
extern void scroll_up(const int count);
extern void delete_character(const int count);
extern void insert_blanks(const int count);
extern void invert_scrollback_colors();
extern void deinvert_scrollback_colors();
extern void set_double_width(Q_BOOL double_width);
extern void set_double_height(int double_height);

extern void render_screen_to_debug_file(FILE * file);

extern Q_BOOL has_true_doublewidth();

#endif /* __SCROLLBACK_H__ */
