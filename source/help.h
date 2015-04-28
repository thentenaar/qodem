/*
 * help.h
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

#ifndef __HELP_H__
#define __HELP_H__

/* Includes --------------------------------------------------------------- */

/* Defines ---------------------------------------------------------------- */

typedef enum {
        Q_HELP_BATCH_ENTRY_WINDOW,      /* At the Batch Entry Window */

#ifndef Q_NO_SERIAL
        Q_HELP_MODEM_CONFIG,            /* Modem configuration screen */
        Q_HELP_COMM_PARMS,              /* Serial port parameters screen */
#endif /* Q_NO_SERIAL */

        Q_HELP_PHONEBOOK,               /* Dialing directory */
        Q_HELP_PHONEBOOK_REVISE_ENTRY,  /* Dialing directory */
        Q_HELP_CONSOLE_MENU,            /* Console menu */
        Q_HELP_PROTOCOLS,               /* Upload/Download file menu */
        Q_HELP_EMULATION_MENU,          /* Emulation select menu */
        Q_HELP_TRANSLATE_EDITOR,        /* Translation table editor */
        Q_HELP_CODEPAGE,                /* Codepage dialog */
        Q_HELP_FUNCTION_KEYS,           /* Function key editor */
} Q_HELP_SCREEN;

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

extern void setup_help();
extern void launch_help(Q_HELP_SCREEN help_screen);

#endif /* __HELP_H__ */
