/*
 *  xvba_gate.c - XvBA hooks
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
#include "xvba_gate.h"
#include "xvba_dump.h"
#include "utils.h"
#include "fglrxinfo.h"

#define DEBUG 1
#include "debug.h"

#if USE_DLOPEN
# include <dlfcn.h>
#endif

#define OPENGL_LIBRARY  "libGL.so.1"
#define XVBA_LIBRARY    "libXvBAW.so.1"
//#define XVBA_LIBRARY  "libAMDXvBA.so.1"

/* Only call into the tracer if tracer is enabled */
#if USE_TRACER
# define TRACE(func, args) dump_##func args
#else
# define TRACE(func, args) do { } while (0)
#endif

/* Defined to 1 if lazy XvBA context destruction is used */
#define USE_REFCOUNT 1


/* ====================================================================== */
/* === XvBA VTable                                                    === */
/* ====================================================================== */

typedef Bool        (*XVBAQueryExtensionProc)       (Display *dpy, int *vers);
typedef Status      (*XVBACreateContextProc)        (void *input, void *output);
typedef Status      (*XVBADestroyContextProc)       (void *context);
typedef Bool        (*XVBAGetSessionInfoProc)       (void *input, void *output);
typedef Status      (*XVBACreateSurfaceProc)        (void *input, void *output);
typedef Status      (*XVBACreateGLSharedSurfaceProc)(void *input, void *output);
typedef Status      (*XVBADestroySurfaceProc)       (void *surface);
typedef Status      (*XVBACreateDecodeBuffersProc)  (void *input, void *output);
typedef Status      (*XVBADestroyDecodeBuffersProc) (void *input);
typedef Status      (*XVBAGetCapDecodeProc)         (void *input, void *output);
typedef Status      (*XVBACreateDecodeProc)         (void *input, void *output);
typedef Status      (*XVBADestroyDecodeProc)        (void *session);
typedef Status      (*XVBAStartDecodePictureProc)   (void *input);
typedef Status      (*XVBADecodePictureProc)        (void *input);
typedef Status      (*XVBAEndDecodePictureProc)     (void *input);
typedef Status      (*XVBASyncSurfaceProc)          (void *input, void *output);
typedef Status      (*XVBAGetSurfaceProc)           (void *input);
typedef Status      (*XVBATransferSurfaceProc)      (void *input);

static struct {
    XVBAQueryExtensionProc              QueryExtension;
    XVBACreateContextProc               CreateContext;
    XVBADestroyContextProc              DestroyContext;
    XVBAGetSessionInfoProc              GetSessionInfo;
    XVBACreateSurfaceProc               CreateSurface;
    XVBACreateGLSharedSurfaceProc       CreateGLSharedSurface;
    XVBADestroySurfaceProc              DestroySurface;
    XVBACreateDecodeBuffersProc         CreateDecodeBuffers;
    XVBADestroyDecodeBuffersProc        DestroyDecodeBuffers;
    XVBAGetCapDecodeProc                GetCapDecode;
    XVBACreateDecodeProc                CreateDecode;
    XVBADestroyDecodeProc               DestroyDecode;
    XVBAStartDecodePictureProc          StartDecodePicture;
    XVBADecodePictureProc               DecodePicture;
    XVBAEndDecodePictureProc            EndDecodePicture;
    XVBASyncSurfaceProc                 SyncSurface;
    XVBAGetSurfaceProc                  GetSurface;
    XVBATransferSurfaceProc             TransferSurface;
}   g_XVBA_vtable;

static unsigned int     g_init_count;
static void            *g_gl_lib_handle;
static void            *g_XVBA_lib_handle;

int xvba_gate_init(void)
{
    void *handle;

    if (g_init_count > 0) {
        g_init_count++;
        return 0;
    }

#if USE_DLOPEN
    /* XXX: workaround broken XvBA libraries */
    dlerror();
    if ((handle = dlopen(OPENGL_LIBRARY, RTLD_LAZY|RTLD_GLOBAL)) == NULL) {
        xvba_error_message("dlopen(%s): %s\n", OPENGL_LIBRARY, dlerror());
        return -1;
    }
    g_gl_lib_handle = handle;

    dlerror();
    if ((handle = dlopen(XVBA_LIBRARY, RTLD_LAZY)) == NULL) {
        xvba_error_message("dlopen(%s): %s\n", XVBA_LIBRARY, dlerror());
        return -1;
    }
    g_XVBA_lib_handle = handle;

#define INIT_PROC(PREFIX, PROC) do {                            \
        g_##PREFIX##_vtable.PROC = (PREFIX##PROC##Proc)         \
            dlsym(g_##PREFIX##_lib_handle, #PREFIX #PROC);      \
    } while (0)

#define INIT_PROC_CHECK(PREFIX, PROC) do {                      \
        dlerror();                                              \
        INIT_PROC(PREFIX, PROC);                                \
        if (dlerror()) {                                        \
            dlclose(g_##PREFIX##_lib_handle);                   \
            g_##PREFIX##_lib_handle = NULL;                     \
            return -1;                                          \
        }                                                       \
    } while (0)

#define XVBA_INIT_PROC(PROC) INIT_PROC_CHECK(XVBA, PROC)

    XVBA_INIT_PROC(QueryExtension);
    XVBA_INIT_PROC(CreateContext);
    XVBA_INIT_PROC(DestroyContext);
    XVBA_INIT_PROC(GetSessionInfo);
    XVBA_INIT_PROC(CreateSurface);
    XVBA_INIT_PROC(CreateGLSharedSurface);
    XVBA_INIT_PROC(DestroySurface);
    XVBA_INIT_PROC(CreateDecodeBuffers);
    XVBA_INIT_PROC(DestroyDecodeBuffers);
    XVBA_INIT_PROC(GetCapDecode);
    XVBA_INIT_PROC(CreateDecode);
    XVBA_INIT_PROC(DestroyDecode);
    XVBA_INIT_PROC(StartDecodePicture);
    XVBA_INIT_PROC(DecodePicture);
    XVBA_INIT_PROC(EndDecodePicture);
    XVBA_INIT_PROC(SyncSurface);

    /* Optional hooks (XvBA >= 0.74) */
    INIT_PROC(XVBA, GetSurface);
    INIT_PROC(XVBA, TransferSurface);

#undef XVBA_INIT_PROC
#undef INIT_PROC

#endif

    g_init_count++;
    return 0;
}

void xvba_gate_exit(void)
{
#if USE_DLOPEN
    if (--g_init_count > 0)
        return;

    if (g_XVBA_lib_handle) {
        dlclose(g_XVBA_lib_handle);
        g_XVBA_lib_handle = NULL;
    }

    if (g_gl_lib_handle) {
        dlclose(g_gl_lib_handle);
        g_gl_lib_handle = NULL;
    }
#endif
}

#if USE_DLOPEN
#define DEFINE_VTABLE_ENTRY(PREFIX, PROC, RETVAL, DECL_ARGS, CALL_ARGS) \
static inline RETVAL PREFIX##_##PROC DECL_ARGS                          \
{                                                                       \
    ASSERT(g_##PREFIX##_vtable.PROC);                                   \
    return g_##PREFIX##_vtable.PROC CALL_ARGS;                          \
}
#else
#define DEFINE_VTABLE_ENTRY(PREFIX, PROC, RETVAL, DECL_ARGS, CALL_ARGS) \
static inline RETVAL PREFIX##_##PROC DECL_ARGS                          \
{                                                                       \
    return PREFIX##PROC CALL_ARGS;                                      \
}
#endif

DEFINE_VTABLE_ENTRY(
    XVBA, QueryExtension,
    Bool, (Display *dpy, int *version),
    (dpy, version));
DEFINE_VTABLE_ENTRY(
    XVBA, CreateContext,
    Status, (void *input, void *output),
    (input, output));
DEFINE_VTABLE_ENTRY(
    XVBA, DestroyContext,
    Status, (void *context),
    (context));
DEFINE_VTABLE_ENTRY(
    XVBA, GetSessionInfo,
    Bool, (void *input, void *output),
    (input, output));
DEFINE_VTABLE_ENTRY(
    XVBA, CreateSurface,
    Status, (void *input, void *output),
    (input, output));
DEFINE_VTABLE_ENTRY(
    XVBA, CreateGLSharedSurface,
    Status, (void *input, void *output),
    (input, output));
DEFINE_VTABLE_ENTRY(
    XVBA, DestroySurface,
    Status, (void *surface),
    (surface));
DEFINE_VTABLE_ENTRY(
    XVBA, CreateDecodeBuffers,
    Status, (void *input, void *output),
    (input, output));
DEFINE_VTABLE_ENTRY(
    XVBA, DestroyDecodeBuffers,
    Status, (void *input),
    (input));
DEFINE_VTABLE_ENTRY(
    XVBA, GetCapDecode,
    Status, (void *input, void *output),
    (input, output));
DEFINE_VTABLE_ENTRY(
    XVBA, CreateDecode,
    Status, (void *input, void *output),
    (input, output));
DEFINE_VTABLE_ENTRY(
    XVBA, DestroyDecode,
    Status, (void *session),
    (session));
DEFINE_VTABLE_ENTRY(
    XVBA, StartDecodePicture,
    Status, (void *input),
    (input));
DEFINE_VTABLE_ENTRY(
    XVBA, DecodePicture,
    Status, (void *input),
    (input));
DEFINE_VTABLE_ENTRY(
    XVBA, EndDecodePicture,
    Status, (void *input),
    (input));
DEFINE_VTABLE_ENTRY(
    XVBA, SyncSurface,
    Status, (void *input, void *output),
    (input, output));
#if XVBA_CHECK_VERSION(0,74)
DEFINE_VTABLE_ENTRY(
    XVBA, GetSurface,
    Status, (void *input),
    (input));
DEFINE_VTABLE_ENTRY(
    XVBA, TransferSurface,
    Status, (void *input),
    (input));
#endif

#undef DEFINE_VTABLE_ENTRY

static int g_version_major = -1;
static int g_version_minor = -1;

int xvba_get_version(int *pmajor, int *pminor)
{
    /* XXX: call XVBAQueryExtension() with a dummy display here? */
    if (g_version_major < 0 || g_version_minor < 0)
        return -1;

    if (pmajor)
        *pmajor = g_version_major;
    if (pminor)
        *pminor = g_version_minor;
    return 0;
}

static inline int check_version(int major, int minor)
{
    /* Compile-time checks */
    if (XVBA_VERSION_MAJOR < major)
        return 0;
    if (XVBA_VERSION_MAJOR == major && XVBA_VERSION_MINOR < minor)
        return 0;

    /* Run-time checks */
    if (g_version_major < major)
        return 0;
    if (g_version_major == major && g_version_minor < minor)
        return 0;

    return 1;
}

int xvba_check_version(int major, int minor)
{
    if (xvba_get_version(NULL, NULL) < 0) /* XXX: ensure global version init */
        return 0;
    return check_version(major, minor);
}


/* ====================================================================== */
/* === XvBA Wrappers                                                  === */
/* ====================================================================== */

static int xvba_destroy_context_real(XVBAContext *context);
static int xvba_destroy_decode_session_real(XVBASession *session);

static inline XVBAContext *xvba_context_ref(XVBAContext *context)
{
    ++context->refcount;
    return context;
}

static inline void xvba_context_unref(XVBAContext *context)
{
    if (--context->refcount == 0)
        xvba_destroy_context_real(context);
}

static inline XVBASession *xvba_session_ref(XVBASession *session)
{
    ++session->refcount;
    return session;
}

static inline void xvba_session_unref(XVBASession *session)
{
    if (--session->refcount == 0)
        session->destroy(session);
}

static int xvba_check_status(Status status, const char *msg)
{
    if (status != Success) {
        xvba_information_message("%s: status %d\n", msg, status);
        return 0;
    }
    return 1;
}

int xvba_query_extension(Display *display, int *pversion)
{
    int version;

    if (pversion)
        *pversion = 0;

    if (!XVBA_QueryExtension(display, &version))
        return -1;

    if (pversion)
        *pversion = version;

    g_version_major = (version >> 16) & 0xffff;
    g_version_minor = version & 0xffff;
    return 0;
}

XVBAContext *xvba_create_context(Display *display, Drawable drawable)
{
    XVBA_Create_Context_Input input;
    XVBA_Create_Context_Output output;
    XVBAContext *context = NULL;
    Status status;

    context = malloc(sizeof(*context));
    if (!context)
        goto error;

    input.size          = sizeof(input);
    input.display       = display;
    input.draw          = drawable;
    output.size         = sizeof(output);
    output.context      = NULL;

    TRACE(XVBACreateContext, (&input));

    status = XVBA_CreateContext(&input, &output);

    if (!xvba_check_status(status, "XVBA_CreateContext()"))
        goto error;
    if (!output.context)
        goto error;

    context->context    = output.context;
    context->refcount   = 1;

    TRACE(XVBACreateContext_output, (&output));
    return context;

error:
    free(context);
    return NULL;
}

static int xvba_destroy_context_real(XVBAContext *context)
{
    TRACE(XVBADestroyContext, (context->context));

    Status status = XVBA_DestroyContext(context->context);
    free(context);
    if (!xvba_check_status(status, "XVBA_DestroyContext()"))
        return -1;
    return 0;
}

int xvba_destroy_context(XVBAContext *context)
{
    if (USE_REFCOUNT) {
        xvba_context_unref(context);
        return 0;
    }
    return xvba_destroy_context_real(context);
}

XVBASurface *
xvba_create_surface(
    XVBASession        *session,
    unsigned int        width,
    unsigned int        height,
    XVBA_SURFACE_FORMAT format
)
{
    XVBA_Create_Surface_Input input;
    XVBA_Create_Surface_Output output;
    XVBASurface *surface;
    Status status;

    surface = malloc(sizeof(*surface));
    if (!surface)
        return NULL;

    input.size          = sizeof(input);
    input.session       = session->session;
    input.width         = width;
    input.height        = height;
    input.surface_type  = format;
    output.size         = sizeof(output);
    output.surface      = NULL;

    TRACE(XVBACreateSurface, (&input));

    status = XVBA_CreateSurface(&input, &output);
    if (!xvba_check_status(status, "XVBA_CreateSurface()"))
        goto error;
    if (!output.surface)
        goto error;

    TRACE(XVBACreateSurface_output, (&output));

    surface->session            = xvba_session_ref(session);
    surface->type               = XVBA_SURFACETYPE_NORMAL;
    surface->surface            = output.surface;
    surface->info.normal.width  = width;
    surface->info.normal.height = height;
    surface->info.normal.format = format;
    return surface;

error:
    free(surface);
    return NULL;
}

#if USE_GLX
XVBASurface *
xvba_create_surface_gl(
    XVBASession        *session,
    GLXContext          gl_context,
    unsigned int        gl_texture
)
{
    XVBA_Create_GLShared_Surface_Input input;
    XVBA_Create_GLShared_Surface_Output output;
    XVBASurface *surface;
    Status status;

    surface = malloc(sizeof(*surface));
    if (!surface)
        return NULL;

    input.size          = sizeof(input);
    input.session       = session->session;
    input.glcontext     = gl_context;
    input.gltexture     = gl_texture;
    output.size         = sizeof(output);
    output.surface      = NULL;

    TRACE(XVBACreateGLSharedSurface, (&input));

    status = XVBA_CreateGLSharedSurface(&input, &output);
    if (!xvba_check_status(status, "XVBA_CreateGLSharedSurface()"))
        goto error;
    if (!output.surface)
        goto error;

    TRACE(XVBACreateGLSharedSurface_output, (&output));

    surface->session                 = xvba_session_ref(session);
    surface->type                    = XVBA_SURFACETYPE_GLSHARED;
    surface->surface                 = output.surface;
    surface->info.glshared.glcontext = gl_context;
    surface->info.glshared.gltexture = gl_texture;
    return surface;

error:
    free(surface);
    return NULL;
}
#endif

int xvba_destroy_surface(XVBASurface *surface)
{
    TRACE(XVBADestroySurface, (surface->surface));

    Status status = XVBA_DestroySurface(surface->surface);
    xvba_session_unref(surface->session);
    free(surface);
    if (!xvba_check_status(status, "XVBA_DestroySurface()"))
        return -1;
    return 0;
}

int
xvba_get_session_info(
    XVBAContext        *context,
    unsigned int       *getcapdecode_size
)
{
    XVBA_GetSessionInfo_Input input;
    XVBA_GetSessionInfo_Output output;
    Status status;

    if (getcapdecode_size)
        *getcapdecode_size = 0;

    input.size          = sizeof(input);
    input.context       = context->context;
    output.size         = sizeof(output);

    status = XVBA_GetSessionInfo(&input, &output);
    if (!xvba_check_status(status, "XVBA_GetSessionInfo()"))
        return -1;

    if (getcapdecode_size)
        *getcapdecode_size = output.getcapdecode_output_size;
    return 0;
}

/* This data structure is compatible with XvBA >= 0.73 */
typedef struct {
    unsigned int  size;
    unsigned int  num_decode_caps;
    XVBADecodeCap decode_caps_list[];
} XVBA_GetCapDecode_Output_Base;

int
xvba_get_decode_caps(
    XVBAContext        *context,
    unsigned int       *pdecode_caps_count,
    XVBADecodeCap     **pdecode_caps
)
{
    XVBA_GetCapDecode_Input input;
    XVBA_GetCapDecode_Output_Base *output;
    XVBADecodeCap *decode_caps;
    unsigned int output_size, num_decode_caps;
    Status status;

    if (pdecode_caps_count)
        *pdecode_caps_count = 0;
    if (pdecode_caps)
        *pdecode_caps = NULL;

    input.size          = sizeof(input);
    input.context       = context->context;
    if (xvba_get_session_info(context, &output_size) < 0)
        return -1;
    output_size         = MAX(output_size, sizeof(XVBA_GetCapDecode_Output));
    output              = alloca(output_size);
    output->size        = output_size;

    TRACE(XVBAGetCapDecode, (&input));

    status = XVBA_GetCapDecode(&input, output);
    if (!xvba_check_status(status, "XVBA_GetCapDecode()"))
        return -1;

    num_decode_caps = output->num_decode_caps;
    decode_caps = malloc(num_decode_caps * sizeof(decode_caps[0]));
    if (!decode_caps)
        return -1;
    memcpy(decode_caps, output->decode_caps_list,
           num_decode_caps * sizeof(decode_caps[0]));

    if (pdecode_caps_count)
        *pdecode_caps_count = num_decode_caps;
    if (pdecode_caps)
        *pdecode_caps = decode_caps;

    TRACE(XVBAGetCapDecode_output_decode_caps, (num_decode_caps, decode_caps));
    return 0;
}

int
xvba_get_surface_caps(
    XVBAContext        *context,
    unsigned int       *psurface_caps_count,
    XVBASurfaceCap    **psurface_caps
)
{
    if (psurface_caps_count)
        *psurface_caps_count = 0;
    if (psurface_caps)
        *psurface_caps = NULL;

    if (!check_version(0,74))
        return 0;

#if XVBA_CHECK_VERSION(0,74)
    XVBA_GetCapDecode_Input input;
    XVBA_GetCapDecode_Output *output;
    XVBASurfaceCap *surface_caps;
    unsigned int i, output_size, num_surface_caps;
    Status status;

    input.size          = sizeof(input);
    input.context       = context->context;
    if (xvba_get_session_info(context, &output_size) < 0)
        return -1;
    output_size         = MAX(output_size, sizeof(XVBA_GetCapDecode_Output));
    output              = alloca(output_size);
    output->size        = output_size;

    TRACE(XVBAGetCapDecode, (&input));

    status = XVBA_GetCapDecode(&input, output);
    if (!xvba_check_status(status, "XVBA_GetCapDecode()"))
        return -1;

    num_surface_caps = output->num_of_getsurface_target;
    surface_caps = malloc(num_surface_caps * sizeof(surface_caps[0]));
    if (!surface_caps)
        return -1;
    for (i = 0; i < num_surface_caps; i++) {
        surface_caps[i].format = output->getsurface_target_list[i].surfaceType;
        surface_caps[i].flag   = output->getsurface_target_list[i].flag;
    }

    if (psurface_caps_count)
        *psurface_caps_count = num_surface_caps;
    if (psurface_caps)
        *psurface_caps = surface_caps;

    TRACE(XVBAGetCapDecode_output_surface_caps, (num_surface_caps, surface_caps));
    return 0;
#endif
    return -1;
}

XVBASession *
xvba_create_decode_session(
    XVBAContext        *context,
    unsigned int        width,
    unsigned int        height,
    XVBADecodeCap      *decode_cap
)
{
    XVBA_Create_Decode_Session_Input input;
    XVBA_Create_Decode_Session_Output output;
    XVBASession *session = NULL;
    Status status;

    session = malloc(sizeof(*session));
    if (!session)
        goto error;

    input.size          = sizeof(input);
    input.width         = width;
    input.height        = height;
    input.context       = context->context;
    input.decode_cap    = decode_cap;
    output.size         = sizeof(output);
    output.session      = NULL;

    TRACE(XVBACreateDecode, (&input));

    status = XVBA_CreateDecode(&input, &output);
    if (!xvba_check_status(status, "XVBA_CreateDecode()"))
        return NULL;
    if (!output.session)
        return NULL;

    session->context    = xvba_context_ref(context);
    session->session    = output.session;
    session->refcount   = 1;
    session->destroy    = xvba_destroy_decode_session_real;

    TRACE(XVBACreateDecode_output, (&output));
    return session;

error:
    free(session);
    return NULL;
}

static int xvba_destroy_decode_session_real(XVBASession *session)
{
    TRACE(XVBADestroyDecode, (session->session));

    Status status = XVBA_DestroyDecode(session->session);
    xvba_context_unref(session->context);
    free(session);
    if (!xvba_check_status(status, "XVBA_DestroyDecode()"))
        return -1;
    return 0;
}

int xvba_destroy_decode_session(XVBASession *session)
{
    if (USE_REFCOUNT) {
        xvba_session_unref(session);
        return 0;
    }
    return xvba_destroy_decode_session_real(session);
}

void *
xvba_create_decode_buffers(
    XVBASession        *session,
    int                 type,
    unsigned int        num_buffers
)
{
    XVBA_Create_DecodeBuff_Input input;
    XVBA_Create_DecodeBuff_Output output;
    Status status;

    input.size                          = sizeof(input);
    input.session                       = session->session;
    input.buffer_type                   = type;
    input.num_of_buffers                = num_buffers;
    output.size                         = sizeof(output);

    TRACE(XVBACreateDecodeBuffers, (&input));

    status = XVBA_CreateDecodeBuffers(&input, &output);
    if (!xvba_check_status(status, "XVBA_CreateDecodeBuffers()"))
        return NULL;
    if (output.num_of_buffers_in_list != num_buffers)
        return NULL;

    TRACE(XVBACreateDecodeBuffers_output, (&output));
    return output.buffer_list;
}

int
xvba_destroy_decode_buffers(
    XVBASession        *session,
    void               *buffers,
    unsigned int        num_buffers
)
{
    XVBA_Destroy_Decode_Buffers_Input input;
    Status status;

    if (buffers == NULL || num_buffers == 0)
        return 0;

    input.size                          = sizeof(input);
    input.session                       = session->session;
    input.num_of_buffers_in_list        = num_buffers;
    input.buffer_list                   = buffers;

    TRACE(XVBADestroyDecodeBuffers, (&input));

    status = XVBA_DestroyDecodeBuffers(&input);
    if (!xvba_check_status(status, "XVBA_DestroyDecodeBuffers()"))
        return -1;
    return 0;
}

int xvba_decode_picture_start(XVBASession *session, XVBASurface *surface)
{
    XVBA_Decode_Picture_Start_Input input;
    Status status;

    input.size                  = sizeof(input);
    input.session               = session->session;
    input.target_surface        = surface->surface;

    TRACE(XVBAStartDecodePicture, (&input));

    status = XVBA_StartDecodePicture(&input);
    if (!xvba_check_status(status, "XVBA_StartDecodePicture()"))
        return -1;
    return 0;
}

int
xvba_decode_picture(
    XVBASession           *session,
    XVBABufferDescriptor **buffers,
    unsigned int           num_buffers
)
{
    XVBA_Decode_Picture_Input input;
    Status status;

    input.size                  = sizeof(input);
    input.session               = session->session;
    input.num_of_buffers_in_list= num_buffers;
    input.buffer_list           = buffers;

    TRACE(XVBADecodePicture, (&input));

    status = XVBA_DecodePicture(&input);
    if (!xvba_check_status(status, "XVBA_DecodePicture()"))
        return -1;
    return 0;
}

int xvba_decode_picture_end(XVBASession *session)
{
    XVBA_Decode_Picture_End_Input input;
    Status status;

    input.size          = sizeof(input);
    input.session       = session->session;

    TRACE(XVBAEndDecodePicture, (&input));

    status = XVBA_EndDecodePicture(&input);
    if (!xvba_check_status(status, "XVBA_EndDecodePicture()"))
        return -1;
    return 0;
}

int xvba_sync_surface(XVBASession *session, XVBASurface *surface, int query)
{
    XVBA_Surface_Sync_Input input;
    XVBA_Surface_Sync_Output output;
    int status;

    input.size          = sizeof(input);
    input.session       = session->session;
    input.surface       = surface->surface;
    input.query_status  = query;
    output.size         = sizeof(output);

    status = XVBA_SyncSurface(&input, &output);
    if (!xvba_check_status(status, "XVBA_SyncSurface()"))
        return -1;

    switch (query) {
    case XVBA_GET_SURFACE_STATUS:
        status = output.status_flags;
        break;
    case XVBA_GET_DECODE_ERRORS:
        status = output.decode_error.type;
        break;
    default:
        status = -1;
        break;
    }

    return status;
}

int
xvba_get_surface(
    XVBASession        *session,
    XVBASurface        *surface,
    XVBA_SURFACE_FORMAT format,
    uint8_t            *target,
    unsigned int        pitch,
    unsigned int        width,
    unsigned int        height
)
{
    if (!check_version(0,74))
        return -1;

#if XVBA_CHECK_VERSION(0,74)
    XVBA_Get_Surface_Input input;
    Status status;

    input.size                  = sizeof(input);
    input.session               = session->session;
    input.src_surface           = surface->surface;
    input.target_buffer         = target;
    input.target_pitch          = pitch;
    input.target_width          = width;
    input.target_height         = height;
    input.target_parameter.size = sizeof(input.target_parameter);
    input.target_parameter.surfaceType = format;
    input.target_parameter.flag = XVBA_FRAME;

    TRACE(XVBAGetSurface, (&input));

    status = XVBA_GetSurface(&input);
    if (!xvba_check_status(status, "XVBA_GetSurface()"))
        return -1;
    return 0;
#endif
    return -1;
}

int
xvba_transfer_surface(
    XVBASession        *session,
    XVBASurface        *dst_surface,
    const XVBASurface  *src_surface,
    unsigned int        flags
)
{
    if (!check_version(0,74))
        return -1;

#if XVBA_CHECK_VERSION(0,74)
    XVBA_Transfer_Surface_Input input;
    Status status;

    input.size                  = sizeof(input);
    input.session               = session->session;
    input.src_surface           = src_surface->surface;
    input.target_surface        = dst_surface->surface;
    input.flag                  = flags;

    TRACE(XVBATransferSurface, (&input));

    status = XVBA_TransferSurface(&input);
    if (!xvba_check_status(status, "XVBA_TransferSurface()"))
        return -1;
    return 0;
#endif
    return -1;
}
