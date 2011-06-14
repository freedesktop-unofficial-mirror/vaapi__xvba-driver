/*
 *  utils_glx.c - GLX utilities
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

#define _GNU_SOURCE 1 /* RTLD_DEFAULT */
#include "sysdeps.h"
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include <pthread.h>
#include "utils.h"
#include "utils_glx.h"
#include "utils_x11.h"

#define DEBUG 1
#include "debug.h"

/**
 * gl_get_error_string:
 * @error: an OpenGL error enumeration
 *
 * Retrieves the string representation the OpenGL @error.
 *
 * Return error: the static string representing the OpenGL @error
 */
const char *
gl_get_error_string(GLenum error)
{
    static const struct {
        GLenum val;
        const char *str;
    }
    gl_errors[] = {
        { GL_NO_ERROR,          "no error" },
        { GL_INVALID_ENUM,      "invalid enumerant" },
        { GL_INVALID_VALUE,     "invalid value" },
        { GL_INVALID_OPERATION, "invalid operation" },
        { GL_STACK_OVERFLOW,    "stack overflow" },
        { GL_STACK_UNDERFLOW,   "stack underflow" },
        { GL_OUT_OF_MEMORY,     "out of memory" },
#ifdef GL_INVALID_FRAMEBUFFER_OPERATION_EXT
        { GL_INVALID_FRAMEBUFFER_OPERATION_EXT, "invalid framebuffer operation" },
#endif
        { ~0, NULL }
    };

    unsigned int i;
    for (i = 0; gl_errors[i].str; i++) {
        if (gl_errors[i].val == error)
            return gl_errors[i].str;
    }
    return "unknown";
}

/**
 * gl_purge_errors:
 *
 * Purges all OpenGL errors. This function is generally useful to
 * clear up the pending errors prior to calling gl_check_error().
 */
void
gl_purge_errors(void)
{
    while (glGetError() != GL_NO_ERROR)
        ; /* nothing */
}

/**
 * gl_check_error:
 *
 * Checks whether there is any OpenGL error pending.
 *
 * Return value: 1 if an error was encountered
 */
int
gl_check_error(void)
{
    GLenum error;
    int has_errors = 0;

    while ((error = glGetError()) != GL_NO_ERROR) {
        D(bug("glError: %s caught", gl_get_error_string(error)));
        has_errors = 1;
    }
    return has_errors;
}

/**
 * gl_get_param:
 * @param: the parameter name
 * @pval: return location for the value
 *
 * This function is a wrapper around glGetIntegerv() that does extra
 * error checking.
 *
 * Return value: 1 on success
 */
int
gl_get_param(GLenum param, unsigned int *pval)
{
    GLint val;

    gl_purge_errors();
    glGetIntegerv(param, &val);
    if (gl_check_error())
        return 0;

    if (pval)
        *pval = val;
    return 1;
}

/**
 * gl_get_texture_param:
 * @target: the target to which the texture is bound
 * @param: the parameter name
 * @pval: return location for the value
 *
 * This function is a wrapper around glGetTexLevelParameteriv() that
 * does extra error checking.
 *
 * Return value: 1 on success
 */
int
gl_get_texture_param(GLenum target, GLenum param, unsigned int *pval)
{
    GLint val;

    gl_purge_errors();
    glGetTexLevelParameteriv(target, 0, param, &val);
    if (gl_check_error())
        return 0;

    if (pval)
        *pval = val;
    return 1;
}

/**
 * gl_set_bgcolor:
 * @color: the requested RGB color
 *
 * Sets background color to the RGB @color. This basically is a
 * wrapper around glClearColor().
 */
void
gl_set_bgcolor(uint32_t color)
{
    glClearColor(
        ((color >> 16) & 0xff) / 255.0f,
        ((color >>  8) & 0xff) / 255.0f,
        ( color        & 0xff) / 255.0f,
        1.0f
    );
}

/**
 * gl_set_texture_scaling:
 * @target: the target to which the texture is currently bound
 * @scale: scaling algorithm
 *
 * Sets scaling algorithm used for the texture currently bound.
 */
void
gl_set_texture_scaling(GLenum target, GLenum scale)
{
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, scale);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, scale);
}

/**
 * gl_set_texture_wrapping:
 * @target: the target to which the texture is currently bound
 * @wrap: wrapping mode
 *
 * Sets wrapping mode used for the texture currently bound.
 */
void
gl_set_texture_wrapping(GLenum target, GLenum wrap)
{
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
}

/**
 * gl_perspective:
 * @fovy: the field of view angle, in degrees, in the y direction
 * @aspect: the aspect ratio that determines the field of view in the
 *   x direction.  The aspect ratio is the ratio of x (width) to y
 *   (height)
 * @zNear: the distance from the viewer to the near clipping plane
 *   (always positive)
 * @zFar: the distance from the viewer to the far clipping plane
 *   (always positive)
 *
 * Specified a viewing frustum into the world coordinate system. This
 * basically is the Mesa implementation of gluPerspective().
 */
static void
frustum(GLdouble left, GLdouble right,
        GLdouble bottom, GLdouble top, 
        GLdouble nearval, GLdouble farval)
{
    GLdouble x, y, a, b, c, d;
    GLdouble m[16];

    x = (2.0 * nearval) / (right - left);
    y = (2.0 * nearval) / (top - bottom);
    a = (right + left) / (right - left);
    b = (top + bottom) / (top - bottom);
    c = -(farval + nearval) / ( farval - nearval);
    d = -(2.0 * farval * nearval) / (farval - nearval);

#define M(row,col)  m[col*4+row]
    M(0,0) = x;     M(0,1) = 0.0F;  M(0,2) = a;      M(0,3) = 0.0F;
    M(1,0) = 0.0F;  M(1,1) = y;     M(1,2) = b;      M(1,3) = 0.0F;
    M(2,0) = 0.0F;  M(2,1) = 0.0F;  M(2,2) = c;      M(2,3) = d;
    M(3,0) = 0.0F;  M(3,1) = 0.0F;  M(3,2) = -1.0F;  M(3,3) = 0.0F;
#undef M

    glMultMatrixd(m);
}

static void
gl_perspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
    GLdouble xmin, xmax, ymin, ymax;

    ymax = zNear * tan(fovy * M_PI / 360.0);
    ymin = -ymax;
    xmin = ymin * aspect;
    xmax = ymax * aspect;

    /* Don't call glFrustum() because of error semantics (covglu) */
    frustum(xmin, xmax, ymin, ymax, zNear, zFar);
}

/**
 * gl_resize:
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Resizes the OpenGL viewport to the specified dimensions, using an
 * orthogonal projection. (0,0) represents the top-left corner of the
 * window.
 */
void
gl_resize(unsigned int width, unsigned int height)
{
#if 0
#define FOVY     60.0f
#define ASPECT   1.0f
#define Z_NEAR   0.1f
#define Z_FAR    100.0f
#define Z_CAMERA 0.869f

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gl_perspective(FOVY, ASPECT, Z_NEAR, Z_FAR);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glTranslatef(-0.5f, -0.5f, -Z_CAMERA);
    glScalef(1.0f/width, -1.0f/height, 1.0f/width);
    glTranslatef(0.0f, -1.0f*height, 0.0f);
#else
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
#endif
}

/**
 * gl_set_current_context_cache:
 * @is_enabled: flag to enable the current context cache
 *
 * Toggles use of the (TLS-based) current context cache.
 *
 * The cache is important in multi-threaded environments since
 * glXGetCurrent*() functions can take up to 76 ms [fglrx 8.77.x] to
 * complete!! However, this cannot be used in standard VA/GLX
 * situations because foreign changes (from the user program) can
 * occur without us knowing about.
 */
static int                 gl_current_context_cache = 0;
static __thread Display   *gl_current_display;
static __thread Window     gl_current_window;
static __thread GLXContext gl_current_context;

void
gl_set_current_context_cache(int is_enabled)
{
    gl_current_context_cache = is_enabled;
}

static Bool
gl_make_current(Display *dpy, Window win, GLXContext ctx)
{
    Bool ret;

    if (gl_current_context_cache  &&
        gl_current_display == dpy &&
        gl_current_window  == win &&
        gl_current_context == ctx)
        return True;

    ret = glXMakeCurrent(dpy, win, ctx);

    if (ret && gl_current_context_cache) {
        gl_current_display = dpy;
        gl_current_window  = win;
        gl_current_context = ctx;
    }
    return ret;
}

/**
 * gl_create_context:
 * @dpy: an X11 #Display
 * @screen: the associated screen of @dpy
 * @parent: the parent #GLContextState, or %NULL if none is to be used
 *
 * Creates a GLX context sharing textures and displays lists with
 * @parent, if not %NULL.
 *
 * Return value: the newly created GLX context
 */
GLContextState *
gl_create_context(Display *dpy, int screen, GLContextState *parent)
{
    GLContextState *cs;
    GLXFBConfig *fbconfigs = NULL;
    int fbconfig_id, val, n, n_fbconfigs;
    Status status;

    static GLint fbconfig_attrs[] = {
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE,   GLX_RGBA_BIT,
        GLX_DOUBLEBUFFER,  True,
        GLX_RED_SIZE,      8,
        GLX_GREEN_SIZE,    8, 
        GLX_BLUE_SIZE,     8,
        None
    };

    cs = malloc(sizeof(*cs));
    if (!cs)
        goto error;

    cs->display = dpy;
    cs->window  = parent ? parent->window : None;
    cs->visual  = NULL;
    cs->context = NULL;

    if (parent && parent->context) {
        status = glXQueryContext(
            parent->display,
            parent->context,
            GLX_FBCONFIG_ID, &fbconfig_id
        );
        if (status != Success)
            goto error;

        fbconfigs = glXGetFBConfigs(dpy, screen, &n_fbconfigs);
        if (!fbconfigs)
            goto error;

        /* Find out a GLXFBConfig compatible with the parent context */
        for (n = 0; n < n_fbconfigs; n++) {
            status = glXGetFBConfigAttrib(
                dpy,
                fbconfigs[n],
                GLX_FBCONFIG_ID, &val
            );
            if (status == Success && val == fbconfig_id)
                break;
        }
        if (n == n_fbconfigs)
            goto error;
    }
    else {
        fbconfigs = glXChooseFBConfig(
            dpy,
            screen,
            fbconfig_attrs, &n_fbconfigs
        );
        if (!fbconfigs)
            goto error;

        /* Select the first one */
        n = 0;
    }

    cs->visual  = glXGetVisualFromFBConfig(dpy, fbconfigs[n]);
    cs->context = glXCreateNewContext(
        dpy,
        fbconfigs[n],
        GLX_RGBA_TYPE,
        parent ? parent->context : NULL,
        True
    );
    if (cs->context)
        goto end;

error:
    gl_destroy_context(cs);
    cs = NULL;
end:
    if (fbconfigs)
        XFree(fbconfigs);
    return cs;
}

/**
 * gl_destroy_context:
 * @cs: a #GLContextState
 *
 * Destroys the GLX context @cs
 */
void
gl_destroy_context(GLContextState *cs)
{
    if (!cs)
        return;

    if (cs->visual) {
        XFree(cs->visual);
        cs->visual = NULL;
    }

    if (cs->display && cs->context) {
        if (glXGetCurrentContext() == cs->context)
            gl_make_current(cs->display, None, NULL);
        glXDestroyContext(cs->display, cs->context);
        cs->display = NULL;
        cs->context = NULL;
    }
    free(cs);
}

/**
 * gl_init_context:
 * @cs: a #GLContextState
 *
 * Initializes the GLX context @cs with a base environment.
 */
void
gl_init_context(GLContextState *cs)
{
    GLContextState old_cs, tmp_cs;

    if (!gl_set_current_context(cs, &old_cs))
        return;

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDrawBuffer(GL_BACK);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl_set_current_context(&old_cs, &tmp_cs);
}

/**
 * gl_get_current_context:
 * @cs: return location to the current #GLContextState
 *
 * Retrieves the current GLX context, display and drawable packed into
 * the #GLContextState struct.
 */
void
gl_get_current_context(GLContextState *cs)
{
    if (gl_current_context_cache) {
        cs->display = gl_current_display;
        cs->window  = gl_current_window;
        cs->context = gl_current_context;
    }
    else {
        cs->display = glXGetCurrentDisplay();
        cs->window  = glXGetCurrentDrawable();
        cs->context = glXGetCurrentContext();
    }
}

/**
 * gl_set_current_context:
 * @new_cs: the requested new #GLContextState
 * @old_cs: return location to the context that was previously current
 *
 * Makes the @new_cs GLX context the current GLX rendering context of
 * the calling thread, replacing the previously current context if
 * there was one.
 *
 * If @old_cs is non %NULL, the previously current GLX context and
 * window are recorded.
 *
 * Return value: 1 on success
 */
int
gl_set_current_context(GLContextState *new_cs, GLContextState *old_cs)
{
    /* If display is NULL, this could be that new_cs was retrieved from
       gl_get_current_context() with none set previously. If that case,
       the other fields are also NULL and we don't return an error */
    if (!new_cs->display)
        return !new_cs->window && !new_cs->context;

    if (old_cs) {
        if (old_cs == new_cs)
            return 1;

        gl_get_current_context(old_cs);
        if (old_cs->display == new_cs->display &&
            old_cs->window  == new_cs->window  &&
            old_cs->context == new_cs->context)
            return 1;

        /* Make sure gl_set_current_context(old_cs, NULL); releases
           the current GLX context */
        if (!old_cs->display)
            old_cs->display = new_cs->display;
    }
    return gl_make_current(new_cs->display, new_cs->window, new_cs->context);
}

/**
 * gl_swap_buffers:
 * @cs: a #GLContextState
 *
 * Promotes the contents of the back buffer of the @win window to
 * become the contents of the front buffer. This simply is wrapper
 * around glXSwapBuffers().
 */
void
gl_swap_buffers(GLContextState *cs)
{
    glXSwapBuffers(cs->display, cs->window);
}

/**
 * get_proc_address:
 * @name: the name of the OpenGL extension function to lookup
 *
 * Returns the specified OpenGL extension function
 *
 * Return value: the OpenGL extension matching @name, or %NULL if none
 *   was found
 */
typedef void (*GLFuncPtr)(void);
typedef GLFuncPtr (*GLXGetProcAddressProc)(const char *);

static GLFuncPtr
get_proc_address_default(const char *name)
{
    return NULL;
}

static GLXGetProcAddressProc
get_proc_address_func(void)
{
    GLXGetProcAddressProc get_proc_func;

    dlerror();
    get_proc_func = (GLXGetProcAddressProc)
        dlsym(RTLD_DEFAULT, "glXGetProcAddress");
    if (!dlerror())
        return get_proc_func;

    get_proc_func = (GLXGetProcAddressProc)
        dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    if (!dlerror())
        return get_proc_func;

    return get_proc_address_default;
}

static inline GLFuncPtr
get_proc_address(const char *name)
{
    static GLXGetProcAddressProc get_proc_func = NULL;
    if (!get_proc_func)
        get_proc_func = get_proc_address_func();
    return get_proc_func(name);
}

/**
 * gl_init_vtable:
 *
 * Initializes the global #GLVTable.
 *
 * Return value: the #GLVTable filled in with OpenGL extensions, or
 *   %NULL on error.
 */
static GLVTable gl_vtable_static;

static GLVTable *
gl_init_vtable(void)
{
    GLVTable * const gl_vtable = &gl_vtable_static;
    const char *gl_extensions = (const char *)glGetString(GL_EXTENSIONS);
    int has_extension;

    /* GL_ARB_texture_non_power_of_two */
    has_extension = (
        find_string("GL_ARB_texture_non_power_of_two", gl_extensions, " ")
    );
    if (has_extension)
        gl_vtable->has_texture_non_power_of_two = 1;

    /* GL_ARB_texture_rectangle */
    has_extension = (
        find_string("GL_ARB_texture_rectangle", gl_extensions, " ")
    );
    if (has_extension)
        gl_vtable->has_texture_rectangle = 1;

    /* GL_ARB_texture_float */
    has_extension = (
        find_string("GL_ARB_texture_float", gl_extensions, " ")
    );
    if (has_extension)
        gl_vtable->has_texture_float = 1;

    /* GL_ARB_framebuffer_object */
    has_extension = (
        find_string("GL_ARB_framebuffer_object", gl_extensions, " ") ||
        find_string("GL_EXT_framebuffer_object", gl_extensions, " ")
    );
    if (has_extension) {
        gl_vtable->gl_gen_framebuffers = (PFNGLGENFRAMEBUFFERSEXTPROC)
            get_proc_address("glGenFramebuffersEXT");
        if (!gl_vtable->gl_gen_framebuffers)
            return NULL;
        gl_vtable->gl_delete_framebuffers = (PFNGLDELETEFRAMEBUFFERSEXTPROC)
            get_proc_address("glDeleteFramebuffersEXT");
        if (!gl_vtable->gl_delete_framebuffers)
            return NULL;
        gl_vtable->gl_bind_framebuffer = (PFNGLBINDFRAMEBUFFEREXTPROC)
            get_proc_address("glBindFramebufferEXT");
        if (!gl_vtable->gl_bind_framebuffer)
            return NULL;
        gl_vtable->gl_gen_renderbuffers = (PFNGLGENRENDERBUFFERSEXTPROC)
            get_proc_address("glGenRenderbuffersEXT");
        if (!gl_vtable->gl_gen_renderbuffers)
            return NULL;
        gl_vtable->gl_delete_renderbuffers = (PFNGLDELETERENDERBUFFERSEXTPROC)
            get_proc_address("glDeleteRenderbuffersEXT");
        if (!gl_vtable->gl_delete_renderbuffers)
            return NULL;
        gl_vtable->gl_bind_renderbuffer = (PFNGLBINDRENDERBUFFEREXTPROC)
            get_proc_address("glBindRenderbufferEXT");
        if (!gl_vtable->gl_bind_renderbuffer)
            return NULL;
        gl_vtable->gl_renderbuffer_storage = (PFNGLRENDERBUFFERSTORAGEEXTPROC)
            get_proc_address("glRenderbufferStorageEXT");
        if (!gl_vtable->gl_renderbuffer_storage)
            return NULL;
        gl_vtable->gl_framebuffer_renderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)
            get_proc_address("glFramebufferRenderbufferEXT");
        if (!gl_vtable->gl_framebuffer_renderbuffer)
            return NULL;
        gl_vtable->gl_framebuffer_texture_2d = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
            get_proc_address("glFramebufferTexture2DEXT");
        if (!gl_vtable->gl_framebuffer_texture_2d)
            return NULL;
        gl_vtable->gl_check_framebuffer_status = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)
            get_proc_address("glCheckFramebufferStatusEXT");
        if (!gl_vtable->gl_check_framebuffer_status)
            return NULL;
        gl_vtable->has_framebuffer_object = 1;
    }

    /* GL_ARB_fragment_program */
    has_extension = (
        find_string("GL_ARB_fragment_program", gl_extensions, " ")
    );
    if (has_extension) {
        gl_vtable->gl_gen_programs = (PFNGLGENPROGRAMSARBPROC)
            get_proc_address("glGenProgramsARB");
        if (!gl_vtable->gl_gen_programs)
            return NULL;
        gl_vtable->gl_delete_programs = (PFNGLDELETEPROGRAMSARBPROC)
            get_proc_address("glDeleteProgramsARB");
        if (!gl_vtable->gl_delete_programs)
            return NULL;
        gl_vtable->gl_bind_program = (PFNGLBINDPROGRAMARBPROC)
            get_proc_address("glBindProgramARB");
        if (!gl_vtable->gl_bind_program)
            return NULL;
        gl_vtable->gl_program_string = (PFNGLPROGRAMSTRINGARBPROC)
            get_proc_address("glProgramStringARB");
        if (!gl_vtable->gl_program_string)
            return NULL;
        gl_vtable->gl_get_program_iv = (PFNGLGETPROGRAMIVARBPROC)
            get_proc_address("glGetProgramivARB");
        if (!gl_vtable->gl_get_program_iv)
            return NULL;
        gl_vtable->gl_program_local_parameter_4fv = (PFNGLPROGRAMLOCALPARAMETER4FVARBPROC)
            get_proc_address("glProgramLocalParameter4fvARB");
        if (!gl_vtable->gl_program_local_parameter_4fv)
            return NULL;
        gl_vtable->has_fragment_program = 1;
    }

    /* GL_ARB_multitexture */
    has_extension = (
        find_string("GL_ARB_multitexture", gl_extensions, " ")
    );
    if (has_extension) {
        gl_vtable->gl_active_texture = (PFNGLACTIVETEXTUREPROC)
            get_proc_address("glActiveTextureARB");
        if (!gl_vtable->gl_active_texture)
            return NULL;
        gl_vtable->gl_multi_tex_coord_2f = (PFNGLMULTITEXCOORD2FPROC)
            get_proc_address("glMultiTexCoord2fARB");
        if (!gl_vtable->gl_multi_tex_coord_2f)
            return NULL;
        gl_vtable->has_multitexture = 1;
    }

    /* GL_ARB_gpu_shader5 */
    has_extension = (
        find_string("GL_ARB_gpu_shader5", gl_extensions, " ")
    );
    if (has_extension) {
        gl_vtable->has_gpu_shader5 = 1;
    }
    return gl_vtable;
}

/**
 * gl_get_vtable:
 *
 * Retrieves a VTable for OpenGL extensions.
 *
 * Return value: VTable for OpenGL extensions
 */
GLVTable *
gl_get_vtable(void)
{
    static pthread_mutex_t mutex          = PTHREAD_MUTEX_INITIALIZER;
    static int             gl_vtable_init = 1;
    static GLVTable       *gl_vtable      = NULL;

    pthread_mutex_lock(&mutex);
    if (gl_vtable_init) {
        gl_vtable_init = 0;
        gl_vtable      = gl_init_vtable();
    }
    pthread_mutex_unlock(&mutex);
    return gl_vtable;
}

/**
 * gl_create_texture:
 * @target: the target to which the texture is bound
 * @format: the format of the pixel data
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Creates a texture with the specified dimensions and @format. The
 * internal format will be automatically derived from @format.
 *
 * Return value: the newly created texture name
 */
GLuint
gl_create_texture(
    GLenum       target,
    GLenum       format,
    unsigned int width,
    unsigned int height
)
{
    GLVTable * const gl_vtable = gl_get_vtable();
    GLenum internal_format, type;
    GLuint texture;
    unsigned int bytes_per_component;

    if (!gl_vtable)
        return 0;

    switch (target) {
    case GL_TEXTURE_1D:
        break;
    case GL_TEXTURE_2D:
        if (!gl_vtable->has_texture_non_power_of_two)
            return 0;
        break;
    case GL_TEXTURE_RECTANGLE_ARB:
        if (!gl_vtable->has_texture_rectangle)
            return 0;
        break;
    default:
        D(bug("Unsupported texture target 0x%04x\n", target));
        return 0;
    }

    internal_format = format;
    type = GL_UNSIGNED_BYTE;
    switch (format) {
    case GL_LUMINANCE:
        bytes_per_component = 1;
        break;
    case GL_LUMINANCE_ALPHA:
        bytes_per_component = 2;
        break;
    case GL_RGBA:
    case GL_BGRA:
        internal_format = GL_RGBA;
        bytes_per_component = 4;
        break;
    case GL_RGBA32F_ARB:
        if (!gl_vtable->has_texture_float)
            return 0;
        internal_format = GL_RGBA32F_ARB;
        format = GL_RGBA;
        type = GL_FLOAT;
        bytes_per_component = 4 * 4;
        break;
    default:
        bytes_per_component = 0;
        break;
    }
    ASSERT(bytes_per_component > 0);

    glEnable(target);
    glGenTextures(1, &texture);
    glBindTexture(target, texture);
    gl_set_texture_scaling(target, GL_LINEAR);
    gl_set_texture_wrapping(target, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, bytes_per_component);
    switch (target) {
    case GL_TEXTURE_1D:
        glTexImage1D(
            target,
            0,
            internal_format,
            width,
            0,
            format,
            type,
            NULL
        );
        break;
    case GL_TEXTURE_2D:
    case GL_TEXTURE_RECTANGLE_ARB:
        glTexImage2D(
            target,
            0,
            internal_format,
            width, height,
            0,
            format,
            type,
            NULL
        );
        break;
    }
    glBindTexture(target, 0);
    return texture;
}

/**
 * gl_create_texture_object:
 * @target: the target to which the texture is bound
 * @format: the format of the pixel data
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Creates a texture with the specified dimensions and @format. The
 * internal format will be automatically derived from @format.
 *
 * Return value: the newly created #GLTextureObject, or %NULL if
 *   an error occurred
 */
GLTextureObject *
gl_create_texture_object(
    GLenum       target,
    GLenum       format,
    unsigned int width,
    unsigned int height
)
{
    GLTextureObject *to;

    to = calloc(1, sizeof(*to));
    if (!to)
        return NULL;

    to->texture = gl_create_texture(target, format, width, height);
    if (!to->texture)
        goto error;
    to->target = target;
    to->format = format;
    to->width  = width;
    to->height = height;
    return to;

error:
    gl_destroy_texture_object(to);
    return NULL;
}

/**
 * gl_destroy_texture_object:
 * @to: a #GLTextureObject
 *
 * Destroys the #GLTextureObject object.
 */
void
gl_destroy_texture_object(GLTextureObject *to)
{
    if (!to)
        return;

    if (to->texture) {
        glDeleteTextures(1, &to->texture);
        to->texture = 0;
    }
    free(to);
}

/**
 * gl_create_framebuffer_object:
 * @target: the target to which the texture is bound
 * @texture: the GL texture to hold the framebuffer
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Creates an FBO with the specified texture and size.
 *
 * Return value: the newly created #GLFramebufferObject, or %NULL if
 *   an error occurred
 */
GLFramebufferObject *
gl_create_framebuffer_object(
    GLenum       target,
    GLuint       texture,
    unsigned int width,
    unsigned int height
)
{
    GLVTable * const gl_vtable = gl_get_vtable();
    GLFramebufferObject *fbo;
    GLenum status;

    if (!gl_vtable || !gl_vtable->has_framebuffer_object)
        return NULL;

    fbo = calloc(1, sizeof(*fbo));
    if (!fbo)
        return NULL;

    fbo->width    = width;
    fbo->height   = height;
    fbo->fbo      = 0;
    fbo->is_bound = 0;

    gl_vtable->gl_gen_framebuffers(1, &fbo->fbo);
    gl_vtable->gl_bind_framebuffer(GL_FRAMEBUFFER_EXT, fbo->fbo);
    gl_vtable->gl_framebuffer_texture_2d(
        GL_FRAMEBUFFER_EXT,
        GL_COLOR_ATTACHMENT0_EXT,
        target, texture,
        0
    );

    status = gl_vtable->gl_check_framebuffer_status(GL_DRAW_FRAMEBUFFER_EXT);
    gl_vtable->gl_bind_framebuffer(GL_FRAMEBUFFER_EXT, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
        goto error;
    return fbo;

error:
    gl_destroy_framebuffer_object(fbo);
    return NULL;
}

/**
 * gl_destroy_framebuffer_object:
 * @fbo: a #GLFramebufferObject
 *
 * Destroys the @fbo object.
 */
void
gl_destroy_framebuffer_object(GLFramebufferObject *fbo)
{
    GLVTable * const gl_vtable = gl_get_vtable();

    if (!fbo)
        return;

    gl_unbind_framebuffer_object(fbo);

    if (fbo->fbo) {
        gl_vtable->gl_delete_framebuffers(1, &fbo->fbo);
        fbo->fbo = 0;
    }
    free(fbo);
}

/**
 * gl_bind_framebuffer_object:
 * @fbo: a #GLFramebufferObject
 *
 * Binds @fbo object.
 *
 * Return value: 1 on success
 */
int
gl_bind_framebuffer_object(GLFramebufferObject *fbo)
{
    GLVTable * const gl_vtable = gl_get_vtable();
    const unsigned int width   = fbo->width;
    const unsigned int height  = fbo->height;

    const unsigned int attribs = (GL_VIEWPORT_BIT |
                                  GL_CURRENT_BIT  |
                                  GL_ENABLE_BIT   |
                                  GL_TEXTURE_BIT  |
                                  GL_COLOR_BUFFER_BIT);

    if (fbo->is_bound)
        return 1;

    gl_vtable->gl_bind_framebuffer(GL_FRAMEBUFFER_EXT, fbo->fbo);
    glPushAttrib(attribs);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glViewport(0, 0, width, height);
    glTranslatef(-1.0f, -1.0f, 0.0f);
    glScalef(2.0f / width, 2.0f / height, 1.0f);

    fbo->is_bound = 1;
    return 1;
}

/**
 * gl_unbind_framebuffer_object:
 * @fbo: a #GLFramebufferObject
 *
 * Releases @fbo object.
 *
 * Return value: 1 on success
 */
int
gl_unbind_framebuffer_object(GLFramebufferObject *fbo)
{
    GLVTable * const gl_vtable = gl_get_vtable();

    if (!fbo->is_bound)
        return 1;

    glPopAttrib();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    gl_vtable->gl_bind_framebuffer(GL_FRAMEBUFFER_EXT, 0);

    fbo->is_bound = 0;
    return 1;
}

/**
 * gl_create_shader_object:
 * @shader_fp: the shader program source
 * @shader_fp_length: the total length of the shader program source
 *
 * Creates a shader object from the specified program source.
 *
 * Return value: the newly created #GLShaderObject, or %NULL if
 *   an error occurred
 */
GLShaderObject *
gl_create_shader_object(
    const char  **shader_fp,
    unsigned int  shader_fp_length
)
{
    GLVTable * const gl_vtable = gl_get_vtable();
    GLShaderObject *so;

    if (!gl_vtable || !gl_vtable->has_fragment_program)
        return NULL;

    if (!shader_fp || !shader_fp_length)
        return NULL;

    so = calloc(1, sizeof(*so));
    if (!so)
        return NULL;

    char *shader = malloc(shader_fp_length + 1);
    if (!shader)
        goto error;
    string_array_to_char_array(shader, shader_fp);

    glEnable(GL_FRAGMENT_PROGRAM);
    gl_vtable->gl_gen_programs(1, &so->shader);
    gl_vtable->gl_bind_program(GL_FRAGMENT_PROGRAM, so->shader);
    gl_vtable->gl_program_string(
        GL_FRAGMENT_PROGRAM,
        GL_PROGRAM_FORMAT_ASCII,
        shader_fp_length, shader
    );
    free(shader);

    GLint error_position;
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION, &error_position);
    if (error_position != -1) {
        D(bug("Error while loading fragment program: %s\n",
              glGetString(GL_PROGRAM_ERROR_STRING)));
        goto error;
    }

    GLint is_native;
    gl_vtable->gl_get_program_iv(
        GL_FRAGMENT_PROGRAM,
        GL_PROGRAM_UNDER_NATIVE_LIMITS,
        &is_native
    );
    if (!is_native) {
        D(bug("Fragment program is not native\n"));
        goto error;
    }
    gl_vtable->gl_bind_program(GL_FRAGMENT_PROGRAM, 0);
    glDisable(GL_FRAGMENT_PROGRAM);
    return so;

error:
    gl_destroy_shader_object(so);
    return NULL;
}

/**
 * gl_destroy_shader_object:
 * @fbo: a #GLShaderObject
 *
 * Destroys the shader program @so.
 */
void
gl_destroy_shader_object(GLShaderObject *so)
{
    GLVTable * const gl_vtable = gl_get_vtable();

    if (!so)
        return;

    gl_unbind_shader_object(so);

    if (so->shader) {
        gl_vtable->gl_delete_programs(1, &so->shader);
        so->shader = 0;
    }
    free(so);
}

/**
 * gl_bind_shader_object:
 * @fbo: a #GLShaderObject
 *
 * Binds @so shader program.
 *
 * Return value: 1 on success
 */
int
gl_bind_shader_object(GLShaderObject *so)
{
    GLVTable * const gl_vtable = gl_get_vtable();

    if (so->is_bound)
        return 1;

    glEnable(GL_FRAGMENT_PROGRAM);
    gl_vtable->gl_bind_program(GL_FRAGMENT_PROGRAM, so->shader);

    so->is_bound = 1;
    return 1;
}

/**
 * gl_unbind_shader_object:
 * @fbo: a #GLShaderObject
 *
 * Releases @so shader program.
 *
 * Return value: 1 on success
 */
int
gl_unbind_shader_object(GLShaderObject *so)
{
    GLVTable * const gl_vtable = gl_get_vtable();

    if (!so->is_bound)
        return 1;

    gl_vtable->gl_bind_program(GL_FRAGMENT_PROGRAM, 0);
    glDisable(GL_FRAGMENT_PROGRAM);

    so->is_bound = 0;
    return 1;
}
