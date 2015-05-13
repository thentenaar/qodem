/*
 * screen.h
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

#ifndef __SCREEN_H__
#define __SCREEN_H__

/* Includes --------------------------------------------------------------- */

#include "input.h"              /* attr_t */
#include "colors.h"

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * This must be called to initialize the curses UI.
 */
extern void screen_setup();

/**
 * Shut down the curses UI.
 */
extern void screen_teardown();

/**
 * Play a short beep.  Note that Linux emulations will use the duration and
 * tone set by the Linux-specific CSI sequence ('man console_codes' to see
 * more).
 */
extern void screen_beep();

/**
 * Clear the line from the current cursor position to the right edge.
 *
 * @param double_width is true, only clear up the WIDTH / 2
 */
extern void screen_clear_remaining_line(Q_BOOL double_width);

/**
 * Turn a Q_COLOR enum into a ncurses attr_t.  This is used to specify the
 * background (normal) terminal color for the emulations.  Note that even if
 * one specifies a terminal default like "bold yellow on blue", then the
 * background might not have the A_BOLD attribute set depending on the number
 * of colors the UI can support.
 *
 * @param q_color the color enum
 * @return a curses attr
 */
extern attr_t scrollback_full_attr(const Q_COLOR q_color);

/**
 * Given an attr, find the color index (pair number).
 *
 * @param attr a curses attr
 * @return the pair number
 */
extern short color_from_attr(const attr_t attr);

/**
 * Given a color index (pair number), find the attr.
 *
 * @param color the pair number
 * @return a curses attr
 */
extern attr_t color_to_attr(const short color);

/**
 * Handle reverse-video and A_REVERSE in light of VT100 flags to provide the
 * same output as DOS used to.
 *
 * @param color the attr
 * @param reverse if true, reverse video (DECSCNM) is enabled
 * @return a curses attr that does not have A_REVERSE set, and might have
 * foreground and background colors reversed.
 */
extern attr_t vt100_check_reverse_color(const attr_t color,
                                        const Q_BOOL reverse);

/**
 * Draw a character from the scrollback to the screen.  This function also
 * performs some caching to reduce calls to setcchar().  This is the color
 * code path for the scrollback.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param attr the attributes to use
 */
extern void screen_put_scrollback_char_yx(const int y, const int x,
                                          const wchar_t ch, const attr_t attr);

/**
 * Turn a Q_COLOR enum into a color pair index.  This is the color code path
 * for UI elements to screen.
 *
 * @param q_color the color enum
 * @return the pair number
 */
extern short screen_color(const Q_COLOR q_color);

/**
 * Turn a Q_COLOR enum into a color pair index.  This is the color code path
 * for UI elements to screen.
 *
 * @param q_color the color enum
 * @return a curses attr
 */
extern attr_t screen_attr(const Q_COLOR q_color);

/**
 * Draw a character to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param attr the attributes to use
 * @param color the pair number
 */
extern void screen_put_char_yx(const int y, const int x, const wchar_t ch,
                               const attr_t attr, const short color);

/**
 * Draw a string to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param attr the attributes to use
 * @param color the pair number
 */
extern void screen_put_str_yx(const int y, const int x, const char * str,
                              const attr_t attr, const short color);

/**
 * Draw a printf-style format string plus optional arguments to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param attr the attributes to use
 * @param color the pair number
 * @param format the format string
 */
extern void screen_put_printf_yx(const int y, const int x, const attr_t attr,
                                 const short color, const char * format, ...);

/**
 * Draw a character to the screen at the current drawing position.
 *
 * @param ch the character to write
 * @param q_color the color enum
 */
extern void screen_put_color_char(const wchar_t ch, const Q_COLOR q_color);

/**
 * Draw a character to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param q_color the color enum
 */
extern void screen_put_color_char_yx(const int y, const int x, const wchar_t ch,
                                     const Q_COLOR q_color);

/**
 * Draw a string to the screen at the current drawing position.
 *
 * @param str the string to write
 * @param q_color the color enum
 */
extern void screen_put_color_str(const char * str, const Q_COLOR q_color);

/**
 * Draw a wide string to the screen at the current drawing position.
 *
 * @param wcs the string to write
 * @param q_color the color enum
 */
extern void screen_put_color_wcs(const wchar_t * wcs, const Q_COLOR q_color);

/**
 * Draw a string to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param q_color the color enum
 */
extern void screen_put_color_str_yx(const int y, const int x, const char * str,
                                    const Q_COLOR q_color);

/**
 * Draw a wide string to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param wcs the string to write
 * @param q_color the color enum
 */
extern void screen_put_color_wcs_yx(const int y, const int x,
                                    const wchar_t * wcs, const Q_COLOR q_color);

/**
 * Draw a string to the screen at the current drawing position.
 *
 * @param str the string to write
 * @param n the maximum number of characters to draw
 * @param q_color the color enum
 */
extern void screen_put_color_strn(const char * str, const int n,
                                  const Q_COLOR q_color);

/**
 * Draw a string to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param n the maximum number of characters to draw
 * @param q_color the color enum
 */
extern void screen_put_color_strn_yx(const int y, const int x, const char * str,
                                     const int n, const Q_COLOR q_color);

/**
 * Draw a horizontal line to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param n the number of characters to draw
 * @param q_color the color enum
 */
extern void screen_put_color_hline_yx(const int y, const int x,
                                      const wchar_t ch, const int n,
                                      const Q_COLOR q_color);

/**
 * Draw a vertical line to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param n the number of characters to draw
 * @param q_color the color enum
 */
extern void screen_put_color_vline_yx(const int y, const int x,
                                      const wchar_t ch, const int n,
                                      const Q_COLOR q_color);

/**
 * Draw a printf-style format string plus optional arguments to the screen at
 * the current drawing position.
 *
 * @param color the pair number
 * @param format the format string
 */
extern void screen_put_color_printf(const Q_COLOR q_color, const char * format,
                                    ...);

/**
 * Draw a printf-style format string plus optional arguments to the screen.
 *
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param color the pair number
 * @param format the format string
 */
extern void screen_put_color_printf_yx(const int y, const int x,
                                       const Q_COLOR q_color,
                                       const char * format, ...);

/**
 * Draw a character to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param ch the character to write
 * @param q_color the color enum
 */
extern void screen_win_put_color_char(void * win, const wchar_t ch,
                                      const Q_COLOR q_color);

/**
 * Draw a character to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param q_color the color enum
 */
extern void screen_win_put_color_char_yx(void * win, const int y, const int x,
                                         const wchar_t ch,
                                         const Q_COLOR q_color);

/**
 * Draw a string to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param str the string to write
 * @param q_color the color enum
 */
extern void screen_win_put_color_str(void * win, const char * str,
                                     const Q_COLOR q_color);

/**
 * Draw a wide string to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param wcs the string to write
 * @param q_color the color enum
 */
extern void screen_win_put_color_wcs(void * win, const wchar_t * wcs,
                                     const Q_COLOR q_color);

/**
 * Draw a string to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param q_color the color enum
 */
extern void screen_win_put_color_str_yx(void * win, const int y, const int x,
                                        const char * str,
                                        const Q_COLOR q_color);

/**
 * Draw a wide string to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param wcs the string to write
 * @param q_color the color enum
 */
extern void screen_win_put_color_wcs_yx(void * win, const int y, const int x,
                                        const wchar_t * wcs,
                                        const Q_COLOR q_color);

/**
 * Draw a string to a window at the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param str the string to write
 * @param n the maximum number of characters to draw
 * @param q_color the color enum
 */
extern void screen_win_put_color_strn(void * win, const char * str, const int n,
                                      const Q_COLOR q_color);

/**
 * Draw a string to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param str the string to write
 * @param n the maximum number of characters to draw
 * @param q_color the color enum
 */
extern void screen_win_put_color_strn_yx(void * win, const int y, const int x,
                                         const char * str, const int n,
                                         const Q_COLOR q_color);

/**
 * Draw a horizontal line to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param n the number of characters to draw
 * @param q_color the color enum
 */
extern void screen_win_put_color_hline_yx(void * win, const int y, const int x,
                                          const wchar_t ch, const int n,
                                          const Q_COLOR q_color);

/**
 * Draw a vertical line to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param ch the character to write
 * @param n the number of characters to draw
 * @param q_color the color enum
 */
extern void screen_win_put_color_vline_yx(void * win, const int y, const int x,
                                          const wchar_t ch, const int n,
                                          const Q_COLOR q_color);

/**
 * Draw a printf-style format string plus optional arguments to a window at
 * the current drawing position.
 *
 * @param win the WINDOW to draw to
 * @param color the pair number
 * @param format the format string
 */
extern void screen_win_put_color_printf(void * win, const Q_COLOR q_color,
                                        const char * format, ...);

/**
 * Draw a printf-style format string plus optional arguments to a window.
 *
 * @param win the WINDOW to draw to
 * @param y row position to write to.  The top-most row is 0.
 * @param x column position to write to.  The left-most column is 0.
 * @param color the pair number
 * @param format the format string
 */
extern void screen_win_put_color_printf_yx(void * win, const int y, const int x,
                                           const Q_COLOR q_color,
                                           const char * format, ...);

/**
 * Change the current drawing position on the screen.
 *
 * @param y new row position to write to.  The top-most row is 0.
 * @param x new column position to write to.  The left-most column is 0.
 */
extern void screen_move_yx(const int y, const int x);

/**
 * Change the current drawing position on a window.
 *
 * @param win the curses WINDOW
 * @param y new row position to write to.  The top-most row is 0.
 * @param x new column position to write to.  The left-most column is 0.
 */
extern void screen_win_move_yx(void * win, const int y, const int x);

/**
 * Force any pending updates to be written to the physical terminal.
 */
extern void screen_flush();

/**
 * Force any pending updates to be written to the physical terminal.
 *
 * @param win the curses WINDOW
 */
extern void screen_win_flush(void * win);

/**
 * Clear the entire screen using the curses werase() call.
 */
extern void screen_clear();

/**
 * Clear the entire screen by explicitly writing to every cell and then
 * calling refresh().  This is used to restore the screen after a system
 * call.
 */
extern void screen_really_clear();

/**
 * Write the screen's current dimensions to height and width.
 *
 * @param height the location to store the height
 * @param height the location to store the width
 */
extern void screen_get_dimensions(int * height, int * width);

/**
 * Write a WINDOW's current dimensions to height and width.
 *
 * @param win the curses WINDOW
 * @param height the location to store the height
 * @param height the location to store the width
 */
extern void screen_win_get_yx(void * win, int * y, int * x);

/**
 * Create a new window from stdscr.
 *
 * @param height the desired height
 * @param width the desired width
 * @param top row position for the top-left corner of the new window.  The
 * top-most row on the screen is 0.
 * @param left column position for the top-left corner of the new window.
 * The left-most column on the screen is 0.
 * @return the new WINDOW, or NULL if subwin() failed
 */
extern void * screen_subwin(int height, int width, int top, int left);

/**
 * Delete a window created by screen_subwin().
 *
 * @param win the curses WINDOW
 */
extern void screen_delwin(void * win);

/**
 * Draw a box on the screen.  It will have box-drawing characters on the
 * border and use the Q_COLOR_WINDOW and Q_COLOR_WINDOW_BACKGROUND colors.
 *
 * @param left column position for the top-left corner of the box.  The
 * left-most column on the screen is 0.
 * @param top row position for the top-left corner of the box.  The top-most
 * row on the screen is 0.
 * @param right column position for the bottom-right corner of the box.
 * @param bottom row position for the bottom-right corner of the box.
 */
extern void screen_draw_box(const int left, const int top, const int right,
                            const int bottom);

/**
 * Draw a box inside a curses WINDOW.  It will have box-drawing characters on
 * the border and use the Q_COLOR_WINDOW and Q_COLOR_WINDOW_BACKGROUND
 * colors.
 *
 * @param win the curses WINDOW
 * @param left column position for the top-left corner of the box.  The
 * left-most column on the screen is 0.
 * @param top row position for the top-left corner of the box.  The top-most
 * row on the screen is 0.
 * @param right column position for the bottom-right corner of the box.
 * @param bottom row position for the bottom-right corner of the box.
 */
extern void screen_win_draw_box(void * window, const int left, const int top,
                                const int right, const int bottom);

/**
 * Draw a box inside a curses WINDOW.  It will have box-drawing characters on
 * the border.
 *
 * @param win the curses WINDOW
 * @param left column position for the top-left corner of the box.  The
 * left-most column on the screen is 0.
 * @param top row position for the top-left corner of the box.  The top-most
 * row on the screen is 0.
 * @param right column position for the bottom-right corner of the box.
 * @param bottom row position for the bottom-right corner of the box.
 * @param border a Q_COLOR enum for the border color
 * @param background a Q_COLOR enum for the background color
 */
extern void screen_win_draw_box_color(void * window, const int left,
                                      const int top, const int right,
                                      const int bottom, const Q_COLOR border,
                                      const Q_COLOR background);

#endif /* __SCREEN_H__ */
