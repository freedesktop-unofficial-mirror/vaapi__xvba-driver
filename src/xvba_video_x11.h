/*
 *  xvba_video_x11.h - XvBA backend for VA-API (rendering to X11 through PCOM)
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

#ifndef XVBA_VIDEO_X11_H
#define XVBA_VIDEO_X11_H

#include "xvba_driver.h"

typedef struct object_glx_output  *object_glx_output_p;

typedef struct object_output object_output_t;
struct object_output {
    struct object_base   base;
    unsigned int         refcount;
    Drawable             drawable;
    object_glx_output_p  glx;
};

// Destroy output surface
void
output_surface_destroy(
    xvba_driver_data_t *driver_data,
    object_output_p     obj_output
) attribute_hidden;

// Reference output surface
object_output_p
output_surface_ref(
    xvba_driver_data_t *driver_data,
    object_output_p     obj_output
) attribute_hidden;

// Unreference output surface
// NOTE: this destroys the surface if refcount reaches zero
void
output_surface_unref(
    xvba_driver_data_t *driver_data,
    object_output_p     obj_output
) attribute_hidden;

// Lookup output surface in the whole output heap
object_output_p
output_surface_lookup(
    xvba_driver_data_t *driver_data,
    Drawable            drawable
) attribute_hidden;

// Ensure an output surface is created for the specified surface and drawable
object_output_p
output_surface_ensure(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface,
    Drawable            drawable
) attribute_hidden;

// vaPutSurface
VAStatus
xvba_PutSurface(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    VADrawable          draw,
    short               srcx,
    short               srcy,
    unsigned short      srcw,
    unsigned short      srch,
    short               destx,
    short               desty,
    unsigned short      destw,
    unsigned short      desth,
    VARectangle        *cliprects,
    unsigned int        number_cliprects,
    unsigned int        flags
) attribute_hidden;

#endif /* XVBA_VIDEO_X11_H */
