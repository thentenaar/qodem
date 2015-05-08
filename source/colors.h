/*
 * colors.h
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

#ifndef __COLORS_H__
#define __COLORS_H__

/* Includes --------------------------------------------------------------- */

#include "scrollback.h"

/*
 * Colors are represented internally in two ways: 1) as one of the Q_COLORS
 * enum values, which is set by colors.cfg, and 2) as a 6-bit number
 * corresponding to a curses PAIR_NUMBER.  For the latter, bits 0-2 are the
 * background color and 3-5 are the foreground; these colors are the
 * Q_COLOR_X defined in input.h.
 *
 * The UI elements (forms, fields, etc) almost always refer to the Q_COLOR
 * kind of color, and use screen drawing functions like
 * screen_win_put_color_X().  The emulations use the PAIR_NUMBER kind of
 * color, figuring out which SGR number corresponds to Q_COLOR_X and then
 * setting q_current_color accordingly.
 *
 * Two of the PAIR_NUMBER indexes get special treatment in several places:
 *
 *   - 0x00 is black-on-black as far as bitmasks are concerned, but in curses
 *     it is white-on-black.
 *
 *   - 0x38 is white-on-black, but q_setup_colors() defines it as
 *     black-on-black.
 *
 * Emulations drawing to the scrollback will always use the "true" bitmask
 * value for an attr_t type color, i.e. 0x00 is black in the scrollback
 * buffer and 0x38 is white-on-black.  There is a little bit of magic in
 * screen.c to remap 0x38 to the "curses white color" and 0x00 to the "curses
 * black color".
 */

/* Defines ---------------------------------------------------------------- */

/**
 * Mask to remove the color attribute from an ncurses attr_t.  This is used
 * by the emulations to change colors without altering other attributes.
 */
#define NO_COLOR_MASK (~Q_A_COLOR)

/**
 * One entry in the colors.cfg list.  Each entry currently has a foreground,
 * background, and boldness flag, but we could easily add other attributes
 * later.
 */
struct q_text_color_struct {
    Q_BOOL bold;
    short fg;
    short bg;
};

/**
 * These are the colors used by the UI elements.
 */
typedef enum Q_COLORS {

    /**
     * Console banner ("You are now in TERMINAL mode").
     */
    Q_COLOR_CONSOLE,

    /**
     * Console normal text.
     */
    Q_COLOR_CONSOLE_TEXT,

    /**
     * Console background.
     */
    Q_COLOR_CONSOLE_BACKGROUND,

    /**
     * Debug emulation sent chars.
     */
    Q_COLOR_DEBUG_ECHO,

    /**
     * Status bar.
     */
    Q_COLOR_STATUS,

    /**
     * Status bar disabled toggle.
     */
    Q_COLOR_STATUS_DISABLED,

    /**
     * Window border.
     */
    Q_COLOR_WINDOW_BORDER,

    /**
     * Window background.
     */
    Q_COLOR_WINDOW,

    /**
     * Menu command help.
     */
    Q_COLOR_MENU_COMMAND,

    /**
     * Menu command - unavailable option.
     */
    Q_COLOR_MENU_COMMAND_UNAVAILABLE,

    /**
     * Menu normal text.
     */
    Q_COLOR_MENU_TEXT,

    /**
     * Highlighted field.
     */
    Q_COLOR_WINDOW_FIELD_HIGHLIGHTED,

    /**
     * Highlighted field text.
     */
    Q_COLOR_WINDOW_FIELD_TEXT_HIGHLIGHTED,

    /**
     * Normal entry.
     */
    Q_COLOR_PHONEBOOK_ENTRY,

    /**
     * Selected entry.
     */
    Q_COLOR_PHONEBOOK_SELECTED,

    /**
     * Selected and tagged entry.
     */
    Q_COLOR_PHONEBOOK_SELECTED_TAGGED,

    /**
     * Tagged entry.
     */
    Q_COLOR_PHONEBOOK_TAGGED,

    /**
     * Text entry boxes in phonebook.
     */
    Q_COLOR_PHONEBOOK_FIELD_TEXT,

    /**
     * Script is running.
     */
    Q_COLOR_SCRIPT_RUNNING,

    /**
     * Script is finished with rc != 0.
     */
    Q_COLOR_SCRIPT_FINISHED,

    /**
     * Script is finished with rc=0.
     */
    Q_COLOR_SCRIPT_FINISHED_OK,

    /**
     * Help text - border.
     */
    Q_COLOR_HELP_BORDER,

    /**
     * Help text - background.
     */
    Q_COLOR_HELP_BACKGROUND,

    /**
     * Help text - bolded text.
     */
    Q_COLOR_HELP_BOLD,

    /**
     * Help text - "See Also" link.
     */
    Q_COLOR_HELP_LINK,

    /**
     * Help text - "See Also" link.
     */
    Q_COLOR_HELP_LINK_SELECTED,

    /**
     * Max color index, not actually used.
     */
    Q_COLOR_MAX
} Q_COLOR;

/* Globals ---------------------------------------------------------------- */

/**
 * Global colormap table.  Stored in colors.c.
 */
extern struct q_text_color_struct q_text_colors[Q_COLOR_MAX];

/**
 * Global Alt-I information screen.  Stored in colors.c.
 */
extern unsigned char q_info_screen[];

/**
 * The color pair number that is white foreground black background.  Stored
 * in colors.c.
 */
extern short q_white_color_pair_num;

/* Functions -------------------------------------------------------------- */

extern void q_setup_colors();

extern void convert_thedraw_screen(const unsigned char * screen,
                                   const int length,
                                   struct q_scrolline_struct * output_line);

extern char * color_to_html(const attr_t color);


extern char * get_colors_filename();

extern void load_colors();

#endif /* __COLORS_H__ */
