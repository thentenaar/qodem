/*
 * screen.h
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

#ifndef __SCREEN_H__
#define __SCREEN_H__

/* Includes --------------------------------------------------------------- */

#include "input.h"      /* attr_t */
#include "colors.h"

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

extern void screen_setup();
extern void screen_teardown();

extern void screen_beep();
extern void screen_clear_remaining_line(Q_BOOL double_width);

/* emulations and scrollback have a slightly different path to the screen */
extern attr_t scrollback_full_attr(const int q_color);
extern short color_from_attr(const attr_t attr);
extern attr_t color_to_attr(const short color);
extern attr_t vt100_check_reverse_color(const attr_t color, const Q_BOOL reverse);
extern void screen_put_scrollback_char_yx(const int y, const int x, const wchar_t ch, const attr_t attr);

/* keyboard and console do some direct screen writes */
extern short screen_color(const int q_color);
extern attr_t screen_attr(const int q_color);
extern void screen_put_char_yx(const int y, const int x, const wchar_t ch, const attr_t attr, const short color);
extern void screen_put_str_yx(const int y, const int x, const char * str, const attr_t attr, const short color);
extern void screen_put_printf_yx(const int y, const int x, const attr_t attr, const short color, const char * format, ...);

/* The UI elements use colors from colors.cfg */
extern void screen_put_color_char(const wchar_t ch, const int q_color);
extern void screen_put_color_char_yx(const int y, const int x, const wchar_t ch, const int q_color);
extern void screen_put_color_str(const char * str, const int q_color);
extern void screen_put_color_wcs(const wchar_t * wcs, const int q_color);
extern void screen_put_color_str_yx(const int y, const int x, const char * str, const int q_color);
extern void screen_put_color_wcs_yx(const int y, const int x, const wchar_t * wcs, const int q_color);
extern void screen_put_color_strn(const char * str, const int n, const int q_color);
extern void screen_put_color_strn_yx(const int y, const int x, const char * str, const int n, const int q_color);
extern void screen_put_color_hline_yx(const int y, const int x, const wchar_t ch, const int n, const int q_color);
extern void screen_put_color_vline_yx(const int y, const int x, const wchar_t ch, const int n, const int q_color);
extern void screen_put_color_printf(const int q_color, const char * format, ...);
extern void screen_put_color_printf_yx(const int y, const int x, const int q_color, const char * format, ...);
extern void screen_win_put_color_char(void * win, const wchar_t ch, const int q_color);
extern void screen_win_put_color_char_yx(void * win, const int y, const int x, const wchar_t ch, const int q_color);
extern void screen_win_put_color_str(void * win, const char * str, const int q_color);
extern void screen_win_put_color_wcs(void * win, const wchar_t * wcs, const int q_color);
extern void screen_win_put_color_str_yx(void * win, const int y, const int x, const char * str, const int q_color);
extern void screen_win_put_color_wcs_yx(void * win, const int y, const int x, const wchar_t * wcs, const int q_color);
extern void screen_win_put_color_strn(void * win, const char * str, const int n, const int q_color);
extern void screen_win_put_color_strn_yx(void * win, const int y, const int x, const char * str, const int n, const int q_color);
extern void screen_win_put_color_hline_yx(void * win, const int y, const int x, const wchar_t ch, const int n, const int q_color);
extern void screen_win_put_color_vline_yx(void * win, const int y, const int x, const wchar_t ch, const int n, const int q_color);
extern void screen_win_put_color_printf(void * win, const int q_color, const char * format, ...);
extern void screen_win_put_color_printf_yx(void * win, const int y, const int x, const int q_color, const char * format, ...);

extern void screen_move_yx(const int y, const int x);
extern void screen_win_move_yx(void * win, const int y, const int x);
extern void screen_flush();
extern void screen_win_flush(void * win);
extern void screen_clear();
extern void screen_really_clear();

extern void screen_get_dimensions(int * height, int * width);
extern void screen_win_get_yx(void * win, int * y, int * x);

extern void * screen_subwin(int height, int width, int top, int left);
extern void screen_delwin(void * win);

extern void screen_draw_box(const int left, const int top, const int right, const int bottom);
extern void screen_win_draw_box(void * window, const int left, const int top, const int right, const int bottom);
extern void screen_win_draw_box_color(void * window, const int left, const int top, const int right, const int bottom, const int border, const int background);

#endif /* __SCREEN_H__ */
