/*
 * music.h
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

#ifndef __MUSIC_H__
#define __MUSIC_H__

/* Includes --------------------------------------------------------------- */

/* Defines ---------------------------------------------------------------- */

typedef enum {
        Q_MUSIC_CONNECT,        /* Connected to system */
        Q_MUSIC_CONNECT_MODEM,  /* Connected to system over modem */
        Q_MUSIC_UPLOAD,         /* Successfully uploaded file(s) */
        Q_MUSIC_DOWNLOAD,       /* Successfully downloaded file(s) */
        Q_MUSIC_PAGE_SYSOP      /* Page sysop in host mode */
} Q_MUSIC_SEQUENCE;

struct q_music_struct {
        int hertz;              /* Hertz of tone (middle A = 440 Hz) */
        int duration;           /* Duration of tone in milliseconds */

        struct q_music_struct * next;
};

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

extern void music_init();
extern void music_teardown();
extern void play_ansi_music(const unsigned char * buffer, const int buffer_n, const Q_BOOL interruptible);
extern void play_sequence(const Q_MUSIC_SEQUENCE sequence);
extern void play_music(const struct q_music_struct * music, const Q_BOOL interruptible);

#endif /* __MUSIC_H__ */
