/*
 *  utils_glx.h - GLX utilities
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

#ifndef UTILS_GLX_H
#define UTILS_GLX_H

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING GL_FRAMEBUFFER_BINDING_EXT
#endif
#ifndef GL_FRAGMENT_PROGRAM
#define GL_FRAGMENT_PROGRAM GL_FRAGMENT_PROGRAM_ARB
#endif
#ifndef GL_PROGRAM_FORMAT_ASCII
#define GL_PROGRAM_FORMAT_ASCII GL_PROGRAM_FORMAT_ASCII_ARB
#endif
#ifndef GL_PROGRAM_ERROR_POSITION
#define GL_PROGRAM_ERROR_POSITION GL_PROGRAM_ERROR_POSITION_ARB
#endif
#ifndef GL_PROGRAM_ERROR_STRING
#define GL_PROGRAM_ERROR_STRING GL_PROGRAM_ERROR_STRING_ARB
#endif
#ifndef GL_PROGRAM_UNDER_NATIVE_LIMITS
#define GL_PROGRAM_UNDER_NATIVE_LIMITS GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB
#endif

const char *
gl_get_error_string(GLenum error)
    attribute_hidden;

void
gl_purge_errors(void)
    attribute_hidden;

int
gl_check_error(void)
    attribute_hidden;

int
gl_get_param(GLenum param, unsigned int *pval)
    attribute_hidden;

int
gl_get_texture_param(GLenum target, GLenum param, unsigned int *pval)
    attribute_hidden;

void
gl_set_bgcolor(uint32_t color)
    attribute_hidden;

void
gl_set_texture_scaling(GLenum target, GLenum scale)
    attribute_hidden;

void
gl_set_texture_wrapping(GLenum target, GLenum wrap)
    attribute_hidden;

void
gl_resize(unsigned int width, unsigned int height)
    attribute_hidden;

typedef struct _GLContextState GLContextState;
struct _GLContextState {
    Display     *display;
    Window       window;
    XVisualInfo *visual;
    GLXContext   context;
};

GLContextState *
gl_create_context(Display *dpy, int screen, GLContextState *parent)
    attribute_hidden;

void
gl_destroy_context(GLContextState *cs)
    attribute_hidden;

void
gl_init_context(GLContextState *cs)
    attribute_hidden;

void
gl_get_current_context(GLContextState *cs)
    attribute_hidden;

int
gl_set_current_context(GLContextState *new_cs, GLContextState *old_cs)
    attribute_hidden;

void
gl_set_current_context_cache(int is_enabled)
    attribute_hidden;

void
gl_swap_buffers(GLContextState *cs)
    attribute_hidden;

typedef struct _GLVTable GLVTable;
struct _GLVTable {
    PFNGLGENFRAMEBUFFERSEXTPROC          gl_gen_framebuffers;
    PFNGLDELETEFRAMEBUFFERSEXTPROC       gl_delete_framebuffers;
    PFNGLBINDFRAMEBUFFEREXTPROC          gl_bind_framebuffer;
    PFNGLGENRENDERBUFFERSEXTPROC         gl_gen_renderbuffers;
    PFNGLDELETERENDERBUFFERSEXTPROC      gl_delete_renderbuffers;
    PFNGLBINDRENDERBUFFEREXTPROC         gl_bind_renderbuffer;
    PFNGLRENDERBUFFERSTORAGEEXTPROC      gl_renderbuffer_storage;
    PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC  gl_framebuffer_renderbuffer;
    PFNGLFRAMEBUFFERTEXTURE2DEXTPROC     gl_framebuffer_texture_2d;
    PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC   gl_check_framebuffer_status;
    PFNGLGENPROGRAMSARBPROC              gl_gen_programs;
    PFNGLDELETEPROGRAMSARBPROC           gl_delete_programs;
    PFNGLBINDPROGRAMARBPROC              gl_bind_program;
    PFNGLPROGRAMSTRINGARBPROC            gl_program_string;
    PFNGLGETPROGRAMIVARBPROC             gl_get_program_iv;
    PFNGLPROGRAMLOCALPARAMETER4FVARBPROC gl_program_local_parameter_4fv;
    PFNGLACTIVETEXTUREPROC               gl_active_texture;
    PFNGLMULTITEXCOORD2FPROC             gl_multi_tex_coord_2f;
    unsigned int                         has_texture_non_power_of_two   : 1;
    unsigned int                         has_texture_rectangle          : 1;
    unsigned int                         has_texture_float              : 1;
    unsigned int                         has_framebuffer_object         : 1;
    unsigned int                         has_fragment_program           : 1;
    unsigned int                         has_multitexture               : 1;
    unsigned int                         has_gpu_shader5                : 1;
};

GLVTable *
gl_get_vtable(void)
    attribute_hidden;

typedef struct _GLTextureObject GLTextureObject;
struct _GLTextureObject {
    GLenum       target;
    GLenum       format;
    GLuint       texture;
    unsigned int width;
    unsigned int height;
};

GLuint
gl_create_texture(
    GLenum       target,
    GLenum       format,
    unsigned int width,
    unsigned int height
) attribute_hidden;

GLTextureObject *
gl_create_texture_object(
    GLenum       target,
    GLenum       format,
    unsigned int width,
    unsigned int height
) attribute_hidden;

void
gl_destroy_texture_object(GLTextureObject *to)
    attribute_hidden;

typedef struct _GLFramebufferObject GLFramebufferObject;
struct _GLFramebufferObject {
    unsigned int    width;
    unsigned int    height;
    GLuint          fbo;
    unsigned int    is_bound    : 1;
};

GLFramebufferObject *
gl_create_framebuffer_object(
    GLenum       target,
    GLuint       texture,
    unsigned int width,
    unsigned int height
) attribute_hidden;

void
gl_destroy_framebuffer_object(GLFramebufferObject *fbo)
    attribute_hidden;

int
gl_bind_framebuffer_object(GLFramebufferObject *fbo)
    attribute_hidden;

int
gl_unbind_framebuffer_object(GLFramebufferObject *fbo)
    attribute_hidden;

typedef struct _GLShaderObject GLShaderObject;
struct _GLShaderObject {
    GLuint          shader;
    unsigned int    is_bound    : 1;
};

GLShaderObject *
gl_create_shader_object(
    const char  **shader_fp,
    unsigned int  shader_fp_length
) attribute_hidden;

void
gl_destroy_shader_object(GLShaderObject *so)
    attribute_hidden;

int
gl_bind_shader_object(GLShaderObject *so)
    attribute_hidden;

int
gl_unbind_shader_object(GLShaderObject *so)
    attribute_hidden;

#endif /* UTILS_GLX_H */
