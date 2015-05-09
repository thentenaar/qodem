/*
 * states.h
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

#ifndef __STATES_H__
#define __STATES_H__

/* Includes --------------------------------------------------------------- */

/* Defines ---------------------------------------------------------------- */

/**
 * Program states.  First state is always initialization, from there it can
 * be dialing directory or console.
 */
typedef enum Q_PROGRAM_STATES {
    Q_STATE_INITIALIZATION,     /* Initialization */

    /**
     * Modem/network dialer.  See phonebook.c and modem.c.
     */
    Q_STATE_DIALER,             /* Modem dialer */

#ifndef Q_NO_SERIAL
    Q_STATE_MODEM_CONFIG,       /* Modem configuration screen */
#endif /* Q_NO_SERIAL */

    /**
     * Phonebook.  See phonebook.c.
     */
    Q_STATE_PHONEBOOK,          /* Dialing directory */

    /**
     * TERMINAL mode.  See console.c.
     */
    Q_STATE_CONSOLE,            /* Console */
    Q_STATE_CONSOLE_MENU,       /* Console menu */
    Q_STATE_SCROLLBACK,         /* Console scrollback */
    Q_STATE_INFO,               /* Program info screen */

    /**
     * Uploads and downloads.  See protocols.c.
     */
    Q_STATE_DOWNLOAD,                   /* Downloading file */
    Q_STATE_DOWNLOAD_MENU,              /* Download file menu */
    Q_STATE_DOWNLOAD_PATHDIALOG,        /* Download file/path dialog */
    Q_STATE_UPLOAD,                     /* Uploading file */
    Q_STATE_UPLOAD_BATCH,               /* Uploading many files */
    Q_STATE_UPLOAD_BATCH_DIALOG,        /* Uploading many files dialog */
    Q_STATE_UPLOAD_MENU,                /* Upload file menu */
    Q_STATE_UPLOAD_PATHDIALOG,          /* Upload file/path dialog */

    /**
     * Screensaver.  It's so small that it is just combined with states.c.
     */
    Q_STATE_SCREENSAVER,        /* Screensaver active */

    /**
     * Emulations.  See emulation.c.
     */
    Q_STATE_EMULATION_MENU,     /* Emulation select menu */

    /**
     * Function key editor.  See keyboard.c.
     */
    Q_STATE_FUNCTION_KEY_EDITOR,        /* Function key editor */

    /**
     * Translate table.  See translate.c.
     */
    Q_STATE_TRANSLATE_MENU,     /* Translation table select menu */
    Q_STATE_TRANSLATE_EDITOR,   /* Translation table editor */

    /**
     * Codepage dialog.  See codepage.c.
     */
    Q_STATE_CODEPAGE,           /* Codepage dialog */

    /**
     * Script execution.  See script.c.
     */
    Q_STATE_SCRIPT_EXECUTE,     /* Script executing */

    /**
     * Host mode.  See host.c.
     */
    Q_STATE_HOST,               /* In host mode */

    Q_STATE_EXIT                /* Exit program */
} Q_PROGRAM_STATE;

/* Globals ---------------------------------------------------------------- */

/**
 * Global state.
 */
extern Q_PROGRAM_STATE q_program_state;

/**
 * Whether or not the keyboard is supposed to be blocking (last argument to
 * nodelay()).
 */
extern Q_BOOL q_keyboard_blocks;

/**
 * State we were in before the screensaver was activated.
 */
extern Q_PROGRAM_STATE original_state;

/* Functions -------------------------------------------------------------- */

/**
 * Switch to a new state, handling things like visible cursor, blocking
 * keyboard, etc.
 *
 * @param new_state state to switch to
 */
extern void switch_state(const Q_PROGRAM_STATE new_state);

/**
 * Look for input from the keyboard and mouse.  If input came in, dispatch it
 * to the appropriate keyboard handler for the current program state.
 */
extern void keyboard_handler();

/**
 * Dispatch to the appropriate draw function for the current program state.
 */
extern void refresh_handler();

/**
 * Keyboard handler for the screensaver.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void screensaver_keyboard_handler(const int keystroke, const int flags);

/**
 * Draw screen for the screensaver.
 */
extern void screensaver_refresh();

#endif /* __STATES_H__ */
