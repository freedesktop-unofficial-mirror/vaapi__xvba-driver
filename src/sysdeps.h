/*
 *  sysdeps.h - System dependent definitions
 *
 *  xvba-video (C) 2009-2011 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef SYSDEPS_H
#define SYSDEPS_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

// Helper macros
#undef  MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#undef  MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#undef  ARRAY_ELEMS
#define ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))
#define UINT_TO_POINTER(i) ((void *)(uintptr_t)(i))
#define POINTER_TO_UINT(p) ((uintptr_t)(void *)(p))

#ifdef HAVE_VISIBILITY_ATTRIBUTE
# define attribute_hidden __attribute__((__visibility__("hidden")))
#else
# define attribute_hidden
#endif

#undef ASSERT
#if USE_DEBUG
# define ASSERT assert
#else
# define ASSERT(expr) do {                                              \
        if (!(expr)) {                                                  \
            xvba_error_message("Assertion failed in file %s at line %d\n", \
                               __FILE__, __LINE__);                     \
            abort();                                                    \
        }                                                               \
} while (0)
#endif

/* Check for a specific version of XvBA, or newer */
#ifndef XVBA_CHECK_VERSION
#define XVBA_CHECK_VERSION(major, minor)                                \
    (XVBA_VERSION_MAJOR > (major) ||                                    \
     (XVBA_VERSION_MAJOR == (major) && XVBA_VERSION_MINOR >= (minor)))
#endif

#endif /* SYSDEPS_H */
