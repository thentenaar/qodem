/*      $NetBSD: basename.c,v 1.8 2008/05/10 22:39:40 christos Exp $    */

/*-
 * Copyright (c) 1997, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Klaus Klein and Jason R. Thorpe.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <libgen.h>
#include <stddef.h>

#ifdef __BORLANDC__
#ifndef __FLAT__
#define __FLAT__
#define undef_FLAT
#endif
#ifndef _POSIX_
#define _POSIX_
#define undef_POSIX
#endif
#include <limits.h>
#ifdef undef_POSIX
#undef _POSIX_
#endif
#ifdef undef_FLAT
#undef __FLAT__
#endif
#include <mem.h>
#endif /* __BORLANDC__ */

char *
basename(char *path)
{
        static char singledot[] = ".";
        static char result[PATH_MAX];
        const char *p, *lastp;
        size_t len;

        /*
         * If `path' is a null pointer or points to an empty string,
         * return a pointer to the string ".".
         */
        if ((path == NULL) || (*path == '\0'))
                return (singledot);

        /* Strip trailing slashes, if any. */
        lastp = path + strlen(path) - 1;
        while (lastp != path && ((*lastp == '/') || (*lastp == '\\')))
                lastp--;

        /* Now find the beginning of this (final) component. */
        p = lastp;
        while (p != path && (*(p - 1) != '/') && (*(p - 1) != '\\'))
                p--;

        /* ...and copy the result into the result buffer. */
        len = (lastp - p) + 1 /* last char */;
        if (len > (PATH_MAX - 1))
                len = PATH_MAX - 1;

        memcpy(result, p, len);
        result[len] = '\0';

        return (result);
}
