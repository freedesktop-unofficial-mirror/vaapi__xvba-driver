/*
 *  xvba_video_x11.c - XvBA backend for VA-API (rendering to X11 through PCOM)
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

#include "sysdeps.h"
#include "utils.h"
#include "xvba_video.h"
#include "xvba_video_x11.h"
#if USE_GLX
#include "xvba_video_glx.h"
#endif

#define DEBUG 1
#include "debug.h"


// Create output surface
static object_output_p
output_surface_create(
    xvba_driver_data_t *driver_data,
    Drawable            drawable
)
{
    VASurfaceID surface = object_heap_allocate(&driver_data->output_heap);
    if (surface == VA_INVALID_ID)
        return NULL;

    object_output_p obj_output = XVBA_OUTPUT(surface);
    if (!obj_output)
        return NULL;

    obj_output->refcount = 1;
    obj_output->drawable = drawable;
    obj_output->glx      = NULL;
    return obj_output;
}

// Destroy output surface
void
output_surface_destroy(
    xvba_driver_data_t *driver_data,
    object_output_p     obj_output
)
{
#if USE_GLX
    if (obj_output->glx) {
        glx_output_surface_destroy(driver_data, obj_output->glx);
        obj_output->glx = NULL;
    }
#endif

    object_heap_free(&driver_data->output_heap, (object_base_p)obj_output);
}

// Reference output surface
object_output_p
output_surface_ref(
    xvba_driver_data_t *driver_data,
    object_output_p     obj_output
)
{
    if (obj_output)
        ++obj_output->refcount;
    return obj_output;
}

// Unreference output surface
// NOTE: this destroys the surface if refcount reaches zero
void
output_surface_unref(
    xvba_driver_data_t *driver_data,
    object_output_p     obj_output
)
{
    if (obj_output && --obj_output->refcount == 0)
        output_surface_destroy(driver_data, obj_output);
}

// Lookup output surface in the whole output heap
object_output_p
output_surface_lookup(
    xvba_driver_data_t *driver_data,
    Drawable            drawable
)
{
    object_heap_iterator iter;
    object_base_p obj = object_heap_first(&driver_data->output_heap, &iter);
    while (obj) {
        object_output_p obj_output = (object_output_p)obj;
        if (obj_output->drawable == drawable)
            return obj_output;
        obj = object_heap_next(&driver_data->output_heap, &iter);
    }
    return NULL;
}

// Ensure an output surface is created for the specified surface and drawable
object_output_p
output_surface_ensure(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface,
    Drawable            drawable
)
{
    object_output_p obj_output = NULL;
    int new_obj_output = 0;
    unsigned int i;

    if (!obj_surface)
        return NULL;

    /* Check for an output surface matching Drawable */
    for (i = 0; i < obj_surface->output_surfaces_count; i++) {
        ASSERT(obj_surface->output_surfaces[i]);
        if (obj_surface->output_surfaces[i]->drawable == drawable) {
            obj_output = obj_surface->output_surfaces[i];
            break;
        }
    }

    /* ... that might have been created for another video surface */
    if (!obj_output) {
        object_output_p m = output_surface_lookup(driver_data, drawable);
        if (m) {
            obj_output = output_surface_ref(driver_data, m);
            new_obj_output = 1;
        }
    }

    /* Fallback: create a new output surface */
    if (!obj_output) {
        obj_output = output_surface_create(driver_data, drawable);
        if (!obj_output)
            return NULL;
        new_obj_output = 1;
    }

    /* Append output surface */
    if (new_obj_output) {
        if (realloc_buffer(&obj_surface->output_surfaces,
                           &obj_surface->output_surfaces_count_max,
                           1 + obj_surface->output_surfaces_count,
                           sizeof(*obj_surface->output_surfaces)) == NULL)
            return NULL;
        obj_surface->output_surfaces[obj_surface->output_surfaces_count++] = obj_output;
    }
    return obj_output;
}

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
)
{
    XVBA_DRIVER_DATA_INIT;

    xvba_set_display_type(driver_data, VA_DISPLAY_X11);

    D(bug("vaPutSurface(): surface 0x%08x, drawable 0x%08x, "
          "src rect (%d,%d):%dx%d, dest rect (%d,%d):%dx%d\n",
          surface, POINTER_TO_UINT(draw),
          srcx, srcy, srcw, srch,
          destx, desty, destw, desth));

    /* Make sure drawable is valid */
    /* XXX: check the drawable is actually a window */
    if (draw == None)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    object_surface_p obj_surface = XVBA_SURFACE(surface);
    if (!obj_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    VARectangle src_rect, dst_rect;
    src_rect.x      = srcx;
    src_rect.y      = srcy;
    src_rect.width  = srcw;
    src_rect.height = srch;
    dst_rect.x      = destx;
    dst_rect.y      = desty;
    dst_rect.width  = destw;
    dst_rect.height = desth;

    const XID xid = POINTER_TO_UINT(draw);
#if USE_GLX
    return put_surface_glx(driver_data, obj_surface,
                           xid,
                           &src_rect, &dst_rect,
                           cliprects, number_cliprects,
                           flags);
#endif
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}
