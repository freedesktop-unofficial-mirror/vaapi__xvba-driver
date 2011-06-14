/*
 *  xvba_video_glx.c - XvBA backend for VA-API (rendering to GLX)
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

#ifndef XVBA_VIDEO_GLX_H
#define XVBA_VIDEO_GLX_H

#include <va/va.h>
#include <va/va_backend.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <pthread.h>
#include "object_heap.h"
#include "xvba_video_x11.h"
#include "utils_glx.h"
#include "uasyncqueue.h"

#define XVBA_MAX_EVERGREEN_PARAMS 4

typedef struct object_glx_output   object_glx_output_t;
typedef struct object_glx_surface  object_glx_surface_t;
typedef struct object_glx_surface *object_glx_surface_p;

struct object_image_glx {
    GLenum               target;
    GLenum               formats[3];
    GLuint               textures[3];
    unsigned int         num_textures;
    unsigned int         width;
    unsigned int         height;
    GLShaderObject      *shader;
};

struct object_glx_output {
    VASurfaceStatus      va_surface_status;
    object_glx_output_p  parent;
    unsigned int         children_count;
    struct {
        Window           xid;
        unsigned int     width;
        unsigned int     height;
    }                    window;
    unsigned int         bgcolor;
    struct {
        Window           xid;
        XVisualInfo     *vi;
        Colormap         cmap;
    }                    gl_window;
    GLContextState      *gl_context;
    object_glx_surface_p gl_surface;
    UAsyncQueue         *render_comm;
    pthread_t            render_thread;
    unsigned int         render_thread_ok;
    GLContextState      *render_context;
    uint64_t             render_timestamp;
    uint64_t             render_ticks;
    uint64_t             render_start;
    pthread_mutex_t      lock;
};

struct object_glx_surface {
    unsigned int         refcount;
    GLContextState      *gl_context;
    GLenum               target;
    GLenum               format;
    GLuint               texture;
    unsigned int         width;
    unsigned int         height;
    unsigned int         va_scale;
    XVBASurface         *xvba_surface;
    GLFramebufferObject *fbo;
    unsigned int         use_procamp_shader;
    GLShaderObject      *procamp_shader;
    uint64_t             procamp_mtime;
    GLuint               tx_texture; // temporary used for transfer_surface()
    XVBASurface         *tx_xvba_surface;
    int                  evergreen_workaround;
    GLuint               evergreen_texture;
    GLFramebufferObject *evergreen_fbo;
    GLShaderObject      *evergreen_shader;
    float                evergreen_params[1 + XVBA_MAX_EVERGREEN_PARAMS][4];
    unsigned int         evergreen_params_count;
    GLShaderObject      *hqscaler;
    GLuint               hqscaler_texture;
};

// Destroys GLX output surface
void
glx_output_surface_destroy(
    xvba_driver_data_t *driver_data,
    object_glx_output_p obj_output
) attribute_hidden;

// Unreferences GLX surface
void
glx_surface_unref(
    xvba_driver_data_t  *driver_data,
    object_glx_surface_p obj_glx_surface
) attribute_hidden;

// References GLX surface
object_glx_surface_p
glx_surface_ref(
    xvba_driver_data_t  *driver_data,
    object_glx_surface_p obj_glx_surface
) attribute_hidden;

// Query GLX surface status
int
query_surface_status_glx(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface
) attribute_hidden;

// Render video surface (and subpictures) into the specified drawable
VAStatus
put_surface_glx(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface,
    Drawable            drawable,
    const VARectangle  *source_rect,
    const VARectangle  *target_rect,
    const VARectangle  *cliprects,
    unsigned int        num_cliprects,
    unsigned int        flags
) attribute_hidden;

// vaCreateSurfaceGLX
VAStatus xvba_CreateSurfaceGLX(
    VADriverContextP    ctx,
    unsigned int        target,
    unsigned int        texture,
    void              **gl_surface
) attribute_hidden;

// vaDestroySurfaceGLX
VAStatus xvba_DestroySurfaceGLX(
    VADriverContextP    ctx,
    void               *gl_surface
) attribute_hidden;

// vaCopySurfaceGLX
VAStatus xvba_CopySurfaceGLX(
    VADriverContextP    ctx,
    void               *gl_surface,
    VASurfaceID         surface,
    unsigned int        flags
) attribute_hidden;

#endif /* XVBA_VIDEO_GLX_H */
