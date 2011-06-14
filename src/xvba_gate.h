/*
 *  xvba_gate.h - XvBA hooks
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

#ifndef XVBA_GATE_H
#define XVBA_GATE_H

#include <X11/Xlib.h>
#include <amdxvba.h>

#if USE_GLX
#include <GL/glx.h>
#endif

#define XVBA_FAKE                       XVBA_FOURCC('F','A','K','E')
#define XVBA_MAX_SUBPICTURES            16
#define XVBA_MAX_SUBPICTURE_FORMATS     3

typedef struct _XVBAContext {
    void                       *context;
    unsigned int                refcount;
} XVBAContext;

typedef struct _XVBASession {
    XVBAContext                *context;
    void                       *session;
    unsigned int                refcount;
    int                       (*destroy)(struct _XVBASession *);
} XVBASession;

typedef struct _XVBASurfaceCap {
    XVBA_SURFACE_FORMAT format;
    XVBA_SURFACE_FLAG flag;
} XVBASurfaceCap;

typedef enum {
    XVBA_SURFACETYPE_NORMAL = 0,
    XVBA_SURFACETYPE_GLSHARED
} XVBASurfaceType;

typedef struct {
    XVBASession                *session;
    XVBASurfaceType             type;
    void                       *surface;
    union {
        struct {
            unsigned int        width;
            unsigned int        height;
            XVBA_SURFACE_FORMAT format;
        }                       normal;
        struct {
            void               *glcontext;
            unsigned int        gltexture;
        }                       glshared;
    }                           info;
} XVBASurface;

/** Initialize XvBA API hooks */
int xvba_gate_init(void)
    attribute_hidden;

/** Deinitialize XvBA API hooks */
void xvba_gate_exit(void)
    attribute_hidden;

/** Get version major, minor */
int xvba_get_version(int *major, int *minor)
    attribute_hidden;

/** Check the minimal version requirement is met */
int xvba_check_version(int major, int minor)
    attribute_hidden;

/** XVBAQueryExtension */
int xvba_query_extension(Display *display, int *version)
    attribute_hidden;

/** XVBACreateContext */
XVBAContext *xvba_create_context(Display *display, Drawable drawable)
    attribute_hidden;

/** XVBADestroyContext */
int xvba_destroy_context(XVBAContext *context)
    attribute_hidden;

/** XVBACreateSurface */
XVBASurface *
xvba_create_surface(
    XVBASession        *session,
    unsigned int        width,
    unsigned int        height,
    XVBA_SURFACE_FORMAT format
) attribute_hidden;

#if USE_GLX
/** XVBACreateGLSharedSurface */
XVBASurface *
xvba_create_surface_gl(
    XVBASession        *session,
    GLXContext          gl_context,
    unsigned int        gl_texture
) attribute_hidden;
#endif

/** XVBADestroySurface */
int xvba_destroy_surface(XVBASurface *surface)
    attribute_hidden;

/** XVBAGetSessionInfo */
int
xvba_get_session_info(
    XVBAContext        *context,
    unsigned int       *getcapdecode_size
) attribute_hidden;

/** XVBAGetCapDecode (XVBA_GetCapDecode_Output.decode_caps_list) */
int
xvba_get_decode_caps(
    XVBAContext        *context,
    unsigned int       *pdecode_caps_count,
    XVBADecodeCap     **pdecode_caps
) attribute_hidden;

/** XVBAGetCapDecode (XVBA_GetCapDecode_Output.getsurface_target_list) */
int
xvba_get_surface_caps(
    XVBAContext        *context,
    unsigned int       *psurface_caps_count,
    XVBASurfaceCap    **psurface_caps
) attribute_hidden;

/** XVBACreateDecode */
XVBASession *
xvba_create_decode_session(
    XVBAContext        *context,
    unsigned int        width,
    unsigned int        height,
    XVBADecodeCap      *decode_cap
) attribute_hidden;

/** XVBADestroyDecode */
int xvba_destroy_decode_session(XVBASession *session)
    attribute_hidden;

/** XVBACreateDecodeBuffers */
void *
xvba_create_decode_buffers(
    XVBASession        *session,
    int                 type,
    unsigned int        num_buffers
) attribute_hidden;

/** XVBADestroyDecodeBuffers */
int
xvba_destroy_decode_buffers(
    XVBASession        *session,
    void               *buffers,
    unsigned int        num_buffers
) attribute_hidden;

/** XVBAStartDecodePicture */
int xvba_decode_picture_start(XVBASession *session, XVBASurface *surface)
    attribute_hidden;

/** XVBADecodePicture */
int
xvba_decode_picture(
    XVBASession           *session,
    XVBABufferDescriptor **buffers,
    unsigned int           num_buffers
) attribute_hidden;

/** XVBAEndDecodePicture */
int xvba_decode_picture_end(XVBASession *session)
    attribute_hidden;

/** XVBASyncSurface */
int xvba_sync_surface(XVBASession *session, XVBASurface *surface, int query)
    attribute_hidden;

/** XVBAGetSurface */
int
xvba_get_surface(
    XVBASession        *session,
    XVBASurface        *surface,
    XVBA_SURFACE_FORMAT format,
    uint8_t            *target,
    unsigned int        pitch,
    unsigned int        width,
    unsigned int        height
) attribute_hidden;

/** XVBATransferSurface */
int
xvba_transfer_surface(
    XVBASession        *session,
    XVBASurface        *dst_surface,
    const XVBASurface  *src_surface,
    unsigned int        flags
) attribute_hidden;

#endif /* XVBA_GATE_H */
