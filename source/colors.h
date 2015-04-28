/*
 * colors.h
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

#ifndef __COLORS_H__
#define __COLORS_H__

/* Includes --------------------------------------------------------------- */

#include "scrollback.h"

/*
Two colors get special treatment in several places.

0x00 is black-on-black as far as bitmasks are concerned, but in curses
it is white-on-black.

0x38 is white-on-black, but q_setup_colors() defines it as
black-on-black.

A character in the scrollback buffer will have these values stored in
BITMASKS form.  q_set_color and q_set_scrollback_color will implicitly
switch these two values.
*/

/* Defines ---------------------------------------------------------------- */

/* Mask to remove the color attribute from an ncurses attr_t */
#define NO_COLOR_MASK (~Q_A_COLOR)

/*
 * Text type color map table.  Each text object has a foreground,
 * background, and boldness flag.
 */
struct q_text_color_struct {
        Q_BOOL bold;
        short fg;
        short bg;
};

enum Q_COLORS {
        Q_COLOR_RESERVED,                       /* RESERVED by curses */
        Q_COLOR_CONSOLE,                        /* Console header ("You are now in TERMINAL mode") */
        Q_COLOR_CONSOLE_TEXT,                   /* Console normal text */
        Q_COLOR_CONSOLE_BACKGROUND,             /* Console background */
        Q_COLOR_DEBUG_ECHO,                     /* Debug emulation sent chars */
        Q_COLOR_STATUS,                         /* Status bar */
        Q_COLOR_STATUS_DISABLED,                /* Status bar disabled toggle */
        Q_COLOR_WINDOW_BORDER,                  /* Window border */
        Q_COLOR_WINDOW,                         /* Window background */
        Q_COLOR_MENU_COMMAND,                   /* Menu command help */
        Q_COLOR_MENU_COMMAND_UNAVAILABLE,       /* Menu command - unavailable option */
        Q_COLOR_MENU_TEXT,                      /* Menu normal text */
        Q_COLOR_WINDOW_FIELD_HIGHLIGHTED,       /* Highlighted field */
        Q_COLOR_WINDOW_FIELD_TEXT_HIGHLIGHTED,  /* Highlighted field text */

        Q_COLOR_PHONEBOOK_ENTRY,                /* Normal entry */
        Q_COLOR_PHONEBOOK_SELECTED,             /* Selected entry */
        Q_COLOR_PHONEBOOK_SELECTED_TAGGED,      /* Selected and tagged entry */
        Q_COLOR_PHONEBOOK_TAGGED,               /* Tagged entry */
        Q_COLOR_PHONEBOOK_FIELD_TEXT,           /* Text entry boxes in phonebook */

        Q_COLOR_SCRIPT_RUNNING,                 /* Script is running */
        Q_COLOR_SCRIPT_FINISHED,                /* Script is finished with rc != 0 */
        Q_COLOR_SCRIPT_FINISHED_OK,             /* Script is finished with rc=0 */

        Q_COLOR_HELP_BORDER,                    /* Help text - border */
        Q_COLOR_HELP_BACKGROUND,                /* Help text - background */
        Q_COLOR_HELP_BOLD,                      /* Help text - bolded text */
        Q_COLOR_HELP_LINK,                      /* Help text - "See Also" link */
        Q_COLOR_HELP_LINK_SELECTED,             /* Help text - "See Also" link */

        Q_COLOR_MAX                             /* Max color index */
};

/* Globals ---------------------------------------------------------------- */

/* Global colormap table, stored in colors.c */
extern struct q_text_color_struct q_text_colors[Q_COLOR_MAX];

/* Global information screen, stored in colors.c */
extern unsigned char q_info_screen[];

/*
 * The color pair number that is white foreground black background,
 * stored in colors.c
 */
extern short q_white_color_pair_num;

/* Functions -------------------------------------------------------------- */

extern void q_setup_colors();
extern void convert_thedraw_screen(const unsigned char * screen, const int length, struct q_scrolline_struct * output_line);
extern char * color_to_html(const attr_t color);

extern char * get_colors_filename();
extern void load_colors();

#endif /* __COLORS_H__ */
