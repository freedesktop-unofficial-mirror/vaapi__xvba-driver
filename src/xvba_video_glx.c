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

#define _GNU_SOURCE 1 /* RTLD_NEXT */
#include "sysdeps.h"
#include "fglrxinfo.h"
#include "xvba_video.h"
#include "xvba_video_glx.h"
#include "xvba_video_x11.h"
#include "xvba_decode.h"
#include "xvba_image.h"
#include "xvba_subpic.h"
#include "xvba_buffer.h"
#include "utils.h"
#include "utils_x11.h"
#include "utils_glx.h"
#include <dlfcn.h>
#include <GL/glext.h>
#include <GL/glxext.h>
#include "shaders/Evergreen_mix_1.h"
#include "shaders/Evergreen_mix_2.h"
#include "shaders/Evergreen_mix_3.h"
#include "shaders/Evergreen_mix_4.h"
#include "shaders/YV12.h"
#include "shaders/NV12.h"
#include "shaders/ProcAmp.h"
#include "shaders/Bicubic.h"
#include "shaders/Bicubic_FLOAT.h"

#define DEBUG 1
#include "debug.h"


/* Define which Evergreen rendering workaround to use */
#define EVERGREEN_WORKAROUND (EVERGREEN_WORKAROUND_AUTODETECT)

enum {
    EVERGREEN_WORKAROUND_SWAP8_X        = 1 << 0,
    EVERGREEN_WORKAROUND_SWAP8_Y        = 1 << 1,
    EVERGREEN_WORKAROUND_SWAP16_X       = 1 << 2,
    EVERGREEN_WORKAROUND_SWAP16_Y       = 1 << 3,
    EVERGREEN_WORKAROUND_SWAP32_X       = 1 << 4,
    EVERGREEN_WORKAROUND_SWAP32_Y       = 1 << 5,
    EVERGREEN_WORKAROUND_SWAP64_X       = 1 << 6,
    EVERGREEN_WORKAROUND_SWAP64_Y       = 1 << 7,
    EVERGREEN_WORKAROUND_COPY           = 1 << 8,
    EVERGREEN_WORKAROUND_AUTODETECT     = 1 << 31,
};

/* Defined to 1 to use a multi-threaded vaPutSurface() implementation */
#define USE_PUTSURFACE_FAST 0

static int get_use_putsurface_fast_env(void)
{
    int use_putsurface_fast;
    if (getenv_yesno("XVBA_VIDEO_PUTSURFACE_FAST", &use_putsurface_fast) < 0)
        use_putsurface_fast = USE_PUTSURFACE_FAST;
    return use_putsurface_fast;
}

static inline int use_putsurface_fast(void)
{
    static int g_use_putsurface_fast = -1;
    if (g_use_putsurface_fast < 0)
        g_use_putsurface_fast = get_use_putsurface_fast_env();
    return g_use_putsurface_fast;
}

static int get_evergreen_workaround_env(void)
{
    int evergreen_workaround;
    if (getenv_int("XVBA_VIDEO_EVERGREEN_WORKAROUND",
                   &evergreen_workaround) < 0) {
        const char *evergreen_workaround_str;
        evergreen_workaround_str = getenv("XVBA_VIDEO_EVERGREEN_WORKAROUND");
        if (evergreen_workaround_str &&
            strcmp(evergreen_workaround_str, "auto") == 0)
            evergreen_workaround = EVERGREEN_WORKAROUND_AUTODETECT;
        else
            evergreen_workaround = EVERGREEN_WORKAROUND;
    }
    return evergreen_workaround;
}

static inline int get_evergreen_workaround(void)
{
    static int g_evergreen_workaround = -1;
    if (g_evergreen_workaround < 0)
        g_evergreen_workaround = get_evergreen_workaround_env();
    return g_evergreen_workaround;
}

// Prototypes
static VAStatus
do_put_surface_glx(
    xvba_driver_data_t  *driver_data,
    object_glx_output_p  obj_output,
    object_surface_p     obj_surface,
    const VARectangle   *src_rect,
    const VARectangle   *dst_rect,
    const VARectangle   *cliprects,
    unsigned int         num_cliprects,
    unsigned int         flags
);

static VAStatus
flip_surface(
    xvba_driver_data_t *driver_data,
    object_glx_output_p obj_output
);

static void
glx_output_surface_lock(object_glx_output_p obj_output);

static void
glx_output_surface_unlock(object_glx_output_p obj_output);

// Renderer thread messenger
#define MSG2PTR(v) ((void *)(uintptr_t)(v))
#define PTR2MSG(v) ((uintptr_t)(void *)(v))
enum {
    MSG_TYPE_QUIT = 1,
    MSG_TYPE_FLIP
};

typedef struct {
    object_surface_p    obj_surface;
    VARectangle         src_rect;
    VARectangle         dst_rect;
    unsigned int        flags;
}  PutSurfaceMsg;

// Renderer thread
typedef struct {
    xvba_driver_data_t *driver_data;
    object_glx_output_p obj_output;
} RenderThreadArgs;

static const unsigned int VIDEO_REFRESH = 1000000 / 60;

static void *render_thread(void *arg)
{
    RenderThreadArgs * const   args        = arg;
    xvba_driver_data_t * const driver_data = args->driver_data;
    object_glx_output_p const  obj_output  = args->obj_output;
    unsigned int stop = 0, num_surfaces = 0;
    uint64_t next;

    /* RenderThreadArgs were allocated in the main thread and the
       render thread is responsible for deallocating them */
    free(args);

#if 0
    /* Create a new X connection so that glXSwapBuffers() doesn't get
       through the main thread X queue that probably wasn't set up as
       MT-safe [XInitThreads()].

       XXX: this assumes the Catalyst driver can still share GLX
       contexts from another Display struct, though actually the very
       same underlying X11 display (XDisplayString() shall match). */
    Display *x11_dpy;
    x11_dpy = XOpenDisplay(driver_data->x11_dpy_name);
    if (!x11_dpy) {
        obj_output->render_thread_ok = 0;
        return NULL;
    }
#else
    /* Use the xvba-video global X11 display */
    Display * const x11_dpy = driver_data->x11_dpy_local;
#endif

    GLContextState old_cs;
    obj_output->render_context = gl_create_context(
        x11_dpy,
        driver_data->x11_screen,
        obj_output->gl_context
    );
    if (!obj_output->render_context) {
        obj_output->render_thread_ok = 0;
        return NULL;
    }
    gl_set_current_context(obj_output->render_context, &old_cs);
    gl_init_context(obj_output->render_context);

    while (!stop) {
        PutSurfaceMsg *msg;

        // Handle message
        next = get_ticks_usec() + VIDEO_REFRESH;
        msg = async_queue_timed_pop(obj_output->render_comm, next);
        if (!msg) {
            /* No new surface received during this video time slice,
               make an explicit flip with what was received so far */
            goto do_flip;
        }

        switch (PTR2MSG(msg)) {
        case MSG_TYPE_QUIT:
            stop = 1;
            break;
        case MSG_TYPE_FLIP:
        do_flip:
            if (num_surfaces > 0) {
                glx_output_surface_lock(obj_output);
                gl_resize(obj_output->window.width, obj_output->window.height);
                flip_surface(driver_data, obj_output);
                gl_bind_framebuffer_object(obj_output->gl_surface->fbo);
                glClear(GL_COLOR_BUFFER_BIT);
                gl_unbind_framebuffer_object(obj_output->gl_surface->fbo);
                glClear(GL_COLOR_BUFFER_BIT);
                glx_output_surface_unlock(obj_output);
                num_surfaces = 0;
            }
            break;
        default:
            glx_output_surface_lock(obj_output);
            do_put_surface_glx(
                driver_data,
                obj_output,
                msg->obj_surface,
                &msg->src_rect,
                &msg->dst_rect,
                NULL, 0,
                msg->flags
            );
            glx_output_surface_unlock(obj_output);
            free(msg);
            num_surfaces++;
            break;
        }
    }
    gl_set_current_context(&old_cs, NULL);
    return NULL;
}

// Ensure FBO and shader extensions are available
static inline int ensure_extensions(void)
{
    GLVTable * const gl_vtable = gl_get_vtable();

    return (gl_vtable &&
            gl_vtable->has_framebuffer_object &&
            gl_vtable->has_fragment_program &&
            gl_vtable->has_multitexture);
}

// Ensure FBO surface is create
static int fbo_ensure(object_glx_surface_p obj_glx_surface)
{
    if (!obj_glx_surface->fbo) {
        obj_glx_surface->fbo = gl_create_framebuffer_object(
            obj_glx_surface->target,
            obj_glx_surface->texture,
            obj_glx_surface->width,
            obj_glx_surface->height
        );
        if (!obj_glx_surface->fbo)
            return 0;
    }
    ASSERT(obj_glx_surface->fbo);
    return 1;
}

// Destroy HW image
static void destroy_hw_image_glx(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image
)
{
    unsigned int i;

    if (!obj_image || !obj_image->hw.glx)
        return;

    object_image_glx_p const hwi = obj_image->hw.glx;

    if (hwi->num_textures > 0) {
        glDeleteTextures(hwi->num_textures, hwi->textures);
        for (i = 0; i < hwi->num_textures; i++) {
            hwi->formats[i]  = GL_NONE;
            hwi->textures[i] = 0;
        }
        hwi->num_textures = 0;
    }

    if (hwi->shader) {
        gl_destroy_shader_object(hwi->shader);
        hwi->shader = NULL;
    }

    free(hwi);
    obj_image->hw.glx = NULL;
}

// Create HW image
static VAStatus
create_hw_image_glx(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image,
    XVBASession        *session
)
{
    object_image_glx_p hwi = calloc(1, sizeof(*hwi));
    if (!hwi)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    obj_image->hw.glx = hwi;

    const char **shader_fp = NULL;
    unsigned int shader_fp_length = 0;
    switch (obj_image->image.format.fourcc) {
    case VA_FOURCC('B','G','R','A'):
        hwi->num_textures = 1;
        hwi->formats[0]   = GL_BGRA;
        break;
    case VA_FOURCC('R','G','B','A'):
        hwi->num_textures = 1;
        hwi->formats[0]   = GL_RGBA;
        break;
    case VA_FOURCC('Y','V','1','2'):
    case VA_FOURCC('I','4','2','0'):
        hwi->num_textures = 3;
        hwi->formats[0]   = GL_LUMINANCE;
        hwi->formats[1]   = GL_LUMINANCE;
        hwi->formats[2]   = GL_LUMINANCE;
        shader_fp         = YV12_fp;
        shader_fp_length  = YV12_FP_SZ;
        break;
    case VA_FOURCC('N','V','1','2'):
        hwi->num_textures = 2;
        hwi->formats[0]   = GL_LUMINANCE;
        hwi->formats[1]   = GL_LUMINANCE_ALPHA;
        shader_fp         = NV12_fp;
        shader_fp_length  = NV12_FP_SZ;
        break;
    default:
        hwi->num_textures = 0;
        break;
    }
    ASSERT(hwi->num_textures > 0);
    if (hwi->num_textures == 0) {
        destroy_hw_image_glx(driver_data, obj_image);
        return VA_STATUS_ERROR_INVALID_IMAGE;
    }

    unsigned int i;
    hwi->target = GL_TEXTURE_2D;
    for (i = 0; i < hwi->num_textures; i++) {
        hwi->textures[i] = gl_create_texture(
            hwi->target,
            hwi->formats[i],
            obj_image->xvba_width  >> (i > 0),
            obj_image->xvba_height >> (i > 0)
        );
        if (!hwi->textures[i]) {
            destroy_hw_image_glx(driver_data, obj_image);
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
        }
    }

    if (hwi->num_textures > 1) {
        ASSERT(shader_fp);
        ASSERT(shader_fp_length > 0);

        hwi->shader = gl_create_shader_object(shader_fp, shader_fp_length);
        if (!hwi->shader)
            return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    hwi->width  = obj_image->xvba_width;
    hwi->height = obj_image->xvba_height;
    return VA_STATUS_SUCCESS;
}

// Commit HW image
static VAStatus
commit_hw_image_glx(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image,
    object_buffer_p     obj_buffer,
    XVBASession        *session
)
{
    object_image_glx_p const hwi = obj_image->hw.glx;

    const int is_I420 = obj_image->image.format.fourcc == VA_FOURCC('I','4','2','0');
    unsigned int offsets[3];
    switch (obj_image->image.num_planes) {
    case 3:
        offsets[2] = obj_image->image.offsets[is_I420 ? 1 : 2];
    case 2:
        offsets[1] = obj_image->image.offsets[is_I420 ? 2 : 1];
    case 1:
        offsets[0] = obj_image->image.offsets[0];
    }

    unsigned int i;
    for (i = 0; i < hwi->num_textures; i++) {
        glBindTexture(hwi->target, hwi->textures[i]);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glTexSubImage2D(
            hwi->target,
            0,
            0,
            0,
            hwi->width  >> (i > 0),
            hwi->height >> (i > 0),
            hwi->formats[i], GL_UNSIGNED_BYTE,
            (uint8_t *)obj_buffer->buffer_data + offsets[i]
        );
        glBindTexture(hwi->target, 0);
    }
    return VA_STATUS_SUCCESS;
}

const HWImageHooks hw_image_hooks_glx = {
    create_hw_image_glx,
    destroy_hw_image_glx,
    commit_hw_image_glx
};

// Render subpictures
static VAStatus
render_subpicture(
    xvba_driver_data_t          *driver_data,
    object_subpicture_p          obj_subpicture,
    object_surface_p             obj_surface,
    const VARectangle           *surface_rect,
    const SubpictureAssociationP assoc
)
{
    VAStatus status = commit_subpicture(
        driver_data,
        obj_subpicture,
        NULL,
        HWIMAGE_TYPE_GLX
    );
    if (status != VA_STATUS_SUCCESS)
        return status;

    object_image_p const obj_image = XVBA_IMAGE(obj_subpicture->image_id);
    if (!obj_image)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    object_image_glx_p const hwi = obj_image->hw.glx;
    if (!hwi)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    float alpha = 1.0;
    if (assoc->flags & VA_SUBPICTURE_GLOBAL_ALPHA)
        alpha = obj_subpicture->alpha;

    /* XXX: we only support RGBA and BGRA subpictures */
    if (hwi->num_textures != 1 &&
        hwi->formats[0] != GL_RGBA && hwi->formats[0] != GL_BGRA)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    glBindTexture(hwi->target, hwi->textures[0]);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
    {
        VARectangle const * src_rect = &assoc->src_rect;
        VARectangle const * dst_rect = &assoc->dst_rect;
        int x1, x2, y1, y2;
        float tx1, tx2, ty1, ty2;

        /* Clip source area by visible area */
        const unsigned int subpic_width  = hwi->width;
        const unsigned int subpic_height = hwi->height;
        tx1 = src_rect->x / (float)subpic_width;
        ty1 = src_rect->y / (float)subpic_height;
        tx2 = (src_rect->x + src_rect->width) / (float)subpic_width;
        ty2 = (src_rect->y + src_rect->height) / (float)subpic_height;

        const float spx = src_rect->width / ((float)subpic_width * (float)obj_surface->width);
        const float spy = src_rect->height / ((float)subpic_height * (float)obj_surface->height);
        const float srx1 = tx1 + surface_rect->x * spx;
        const float srx2 = srx1 + surface_rect->width * spx;
        const float sry1 = ty1 + surface_rect->y * spy;
        const float sry2 = sry1 + surface_rect->height * spy;

        if (tx1 < srx1)
            tx1 = srx1;
        if (ty1 < sry1)
            ty1 = sry1;
        if (tx2 > srx2)
            tx2 = srx2;
        if (ty2 > sry2)
            ty2 = sry2;

        /* Clip dest area by visible area */
        x1 = dst_rect->x;
        y1 = dst_rect->y;
        x2 = dst_rect->x + dst_rect->width;
        y2 = dst_rect->y + dst_rect->height;

        if (x1 < surface_rect->x)
            x1 = surface_rect->x;
        if (y1 < surface_rect->y)
            y1 = surface_rect->y;
        if (x2 > surface_rect->x + surface_rect->width)
            x2 = surface_rect->x + surface_rect->width;
        if (y2 > surface_rect->y + surface_rect->height)
            y2 = surface_rect->y + surface_rect->height;

        /* Translate and scale to fit surface size */
        const float sx = obj_surface->width / (float)surface_rect->width;
        const float sy = obj_surface->height / (float)surface_rect->height;
        x1 = (float)(x1 - surface_rect->x) * sx;
        x2 = (float)(x2 - surface_rect->x) * sx;
        y1 = (float)(y1 - surface_rect->y) * sy;
        y2 = (float)(y2 - surface_rect->y) * sy;

        switch (hwi->target) {
        case GL_TEXTURE_RECTANGLE_ARB:
            tx1 *= subpic_width;
            tx2 *= subpic_width;
            ty1 *= subpic_height;
            ty2 *= subpic_height;
            break;
        }

        glTexCoord2f(tx1, ty1); glVertex2i(x1, y1);
        glTexCoord2f(tx1, ty2); glVertex2i(x1, y2);
        glTexCoord2f(tx2, ty2); glVertex2i(x2, y2);
        glTexCoord2f(tx2, ty1); glVertex2i(x2, y1);
    }
    glEnd();
    glBindTexture(hwi->target, 0);
    return VA_STATUS_SUCCESS;
}

static VAStatus
render_subpictures(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface,
    const VARectangle  *surface_rect
)
{
    unsigned int i;
    for (i = 0; i < obj_surface->assocs_count; i++) {
        SubpictureAssociationP const assoc = obj_surface->assocs[i];
        ASSERT(assoc);
        if (!assoc)
            continue;

        object_subpicture_p obj_subpicture = XVBA_SUBPICTURE(assoc->subpicture);
        ASSERT(obj_subpicture);
        if (!obj_subpicture)
            continue;

        VAStatus status = render_subpicture(
            driver_data,
            obj_subpicture,
            obj_surface,
            surface_rect,
            assoc
        );
        if (status != VA_STATUS_SUCCESS)
            return status;
    }
    return VA_STATUS_SUCCESS;
}

// Destroy VA/GLX surface
static void
destroy_glx_surface(
    xvba_driver_data_t  *driver_data,
    object_glx_surface_p obj_glx_surface
)
{
    if (!obj_glx_surface)
        return;

    if (obj_glx_surface->fbo) {
        gl_destroy_framebuffer_object(obj_glx_surface->fbo);
        obj_glx_surface->fbo = NULL;
    }

    if (obj_glx_surface->tx_xvba_surface) {
        xvba_destroy_surface(obj_glx_surface->tx_xvba_surface);
        obj_glx_surface->tx_xvba_surface = NULL;
    }

    if (obj_glx_surface->tx_texture) {
        glDeleteTextures(1, &obj_glx_surface->tx_texture);
        obj_glx_surface->tx_texture = 0;
    }

    if (obj_glx_surface->xvba_surface) {
        xvba_destroy_surface(obj_glx_surface->xvba_surface);
        obj_glx_surface->xvba_surface = NULL;
    }

    if (obj_glx_surface->procamp_shader) {
        gl_destroy_shader_object(obj_glx_surface->procamp_shader);
        obj_glx_surface->procamp_shader = NULL;
    }

    if (obj_glx_surface->evergreen_fbo) {
        gl_destroy_framebuffer_object(obj_glx_surface->evergreen_fbo);
        obj_glx_surface->evergreen_fbo = NULL;
    }

    if (obj_glx_surface->evergreen_texture) {
        glDeleteTextures(1, &obj_glx_surface->evergreen_texture);
        obj_glx_surface->evergreen_texture = 0;
    }

    if (obj_glx_surface->evergreen_shader) {
        gl_destroy_shader_object(obj_glx_surface->evergreen_shader);
        obj_glx_surface->evergreen_shader = NULL;
    }

    if (obj_glx_surface->hqscaler) {
        gl_destroy_shader_object(obj_glx_surface->hqscaler);
        obj_glx_surface->hqscaler = NULL;
    }

    if (obj_glx_surface->hqscaler_texture) {
        glDeleteTextures(1, &obj_glx_surface->hqscaler_texture);
        obj_glx_surface->hqscaler_texture = 0;
    }
    free(obj_glx_surface);
}

// Create VA/GLX surface
static object_glx_surface_p
create_glx_surface(
    xvba_driver_data_t *driver_data,
    unsigned int        width,
    unsigned int        height
)
{
    object_glx_surface_p obj_glx_surface = calloc(1, sizeof(*obj_glx_surface));
    if (!obj_glx_surface)
        return NULL;

    obj_glx_surface->refcount             = 1;
    obj_glx_surface->target               = GL_TEXTURE_2D;
    obj_glx_surface->format               = GL_BGRA;
    obj_glx_surface->texture              = gl_create_texture(
        obj_glx_surface->target,
        obj_glx_surface->format,
        width,
        height
    );
    obj_glx_surface->width                = width;
    obj_glx_surface->height               = height;
    obj_glx_surface->evergreen_workaround = -1;

    if (!obj_glx_surface->texture) {
        destroy_glx_surface(driver_data, obj_glx_surface);
        obj_glx_surface = NULL;
    }
    return obj_glx_surface;
}

// Check internal texture format is supported
static int
is_supported_internal_format(GLenum format)
{
    /* XXX: we don't support other textures than RGBA */
    switch (format) {
    case 4:
    case GL_RGBA:
    case GL_RGBA8:
        return 1;
    }
    return 0;
}

static object_glx_surface_p
create_glx_surface_from_texture(
    xvba_driver_data_t *driver_data,
    GLenum              target,
    GLuint              texture
)
{
    object_glx_surface_p obj_glx_surface = NULL;
    unsigned int iformat, border_width, width, height;
    int is_error = 1;

    obj_glx_surface = calloc(1, sizeof(*obj_glx_surface));
    if (!obj_glx_surface)
        goto end;

    obj_glx_surface->refcount             = 1;
    obj_glx_surface->target               = target;
    obj_glx_surface->format               = GL_NONE;
    obj_glx_surface->texture              = texture;
    obj_glx_surface->evergreen_workaround = -1;

    /* XXX: we don't support other textures than RGBA */
    glBindTexture(target, texture);
    if (!gl_get_texture_param(target, GL_TEXTURE_INTERNAL_FORMAT, &iformat))
        goto end;
    if (!is_supported_internal_format(iformat))
        goto end;

    /* Check texture format */
    /* XXX: huh, there does not seem to exist any way to achieve this... */

    /* Check texture dimensions */
    if (!gl_get_texture_param(target, GL_TEXTURE_BORDER, &border_width))
        goto end;
    if (!gl_get_texture_param(target, GL_TEXTURE_WIDTH, &width))
        goto end;
    if (!gl_get_texture_param(target, GL_TEXTURE_HEIGHT, &height))
        goto end;

    width  -= 2 * border_width;
    height -= 2 * border_width;
    if (width == 0 || height == 0)
        goto end;

    obj_glx_surface->width  = width;
    obj_glx_surface->height = height;

    is_error = 0;
end:
    glBindTexture(target, 0);
    if (is_error && obj_glx_surface) {
        destroy_glx_surface(driver_data, obj_glx_surface);
        obj_glx_surface = NULL;
    }
    return obj_glx_surface;
}

// Unreferences GLX surface
void
glx_surface_unref(
    xvba_driver_data_t  *driver_data,
    object_glx_surface_p obj_glx_surface
)
{
    if (obj_glx_surface && --obj_glx_surface->refcount == 0)
        destroy_glx_surface(driver_data, obj_glx_surface);
}

// References GLX surface
object_glx_surface_p
glx_surface_ref(
    xvba_driver_data_t  *driver_data,
    object_glx_surface_p obj_glx_surface
)
{
    if (!obj_glx_surface)
        return NULL;
    ++obj_glx_surface->refcount;
    return obj_glx_surface;
}

static object_glx_surface_p
glx_surface_lookup(
    xvba_driver_data_t *driver_data,
    unsigned int        width,
    unsigned int        height
)
{
    object_base_p obj;
    object_heap_iterator iter;
    obj = object_heap_first(&driver_data->surface_heap, &iter);
    while (obj) {
        object_surface_p const obj_surface = (object_surface_p)obj;
        if (obj_surface->gl_surface &&
            obj_surface->gl_surface->width  == width &&
            obj_surface->gl_surface->height == height)
            return obj_surface->gl_surface;
        obj = object_heap_next(&driver_data->surface_heap, &iter);
    }
    return NULL;
}

static object_glx_surface_p
glx_surface_ensure(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface,
    object_glx_output_p obj_output
)
{
    object_glx_surface_p gl_surface = obj_surface->gl_surface;

    /* Try to find a VA/GLX surface with the same dimensions */
    if (!gl_surface) {
        gl_surface = glx_surface_lookup(
            driver_data,
            obj_surface->xvba_surface_width,
            obj_surface->xvba_surface_height
        );
        if (gl_surface)
            obj_surface->gl_surface = glx_surface_ref(driver_data, gl_surface);
    }

    /* Allocate a new VA/GLX surface */
    if (!gl_surface) {
        gl_surface = create_glx_surface(
            driver_data,
            obj_surface->xvba_surface_width,
            obj_surface->xvba_surface_height
        );
        if (gl_surface) {
            gl_surface->gl_context  = obj_output->gl_context;
            obj_surface->gl_surface = gl_surface;
        }
    }
    return gl_surface;
}

// Translates vaPutSurface flags to XVBA_SURFACE_FLAG
static inline XVBA_SURFACE_FLAG get_XVBA_SURFACE_FLAG(unsigned int flags)
{
    switch (flags & (VA_TOP_FIELD|VA_BOTTOM_FIELD)) {
    case VA_TOP_FIELD:    return XVBA_TOP_FIELD;
    case VA_BOTTOM_FIELD: return XVBA_BOTTOM_FIELD;
    }
    return XVBA_FRAME;
}

// Check whether surface actually has contents to be displayed
static inline int is_empty_surface(object_surface_p obj_surface)
{
    return !obj_surface->used_for_decoding && !obj_surface->putimage_hacks;
}

static inline void
fill_evergreen_params(float **pparams, int n)
{
    float * const params = *pparams;
    params[0] = 1.0f / (n * 2);
    params[2] = (float)n;
    *pparams += 4;
}

// Transfer XvBA surface to GLX surface
static VAStatus
transfer_surface_native(
    xvba_driver_data_t  *driver_data,
    object_glx_surface_p obj_glx_surface,
    object_surface_p     obj_surface,
    unsigned int         flags
)
{
    GLVTable * const gl_vtable = gl_get_vtable();

    object_context_p obj_context = XVBA_CONTEXT(obj_surface->va_context);
    if (!obj_context || !obj_context->xvba_session)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    /* Create XvBA/GLX surface */
    if (!obj_glx_surface->xvba_surface) {
        obj_glx_surface->xvba_surface = xvba_create_surface_gl(
            obj_context->xvba_decoder,
            obj_glx_surface->gl_context->context,
            obj_glx_surface->texture
        );
        if (!obj_glx_surface->xvba_surface)
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }
    ASSERT(obj_glx_surface->xvba_surface);

    /* Make sure the picture is decoded */
    if (obj_surface->va_surface_status == VASurfaceRendering) {
        if (sync_surface(driver_data, obj_context, obj_surface) < 0)
            return VA_STATUS_ERROR_UNKNOWN;
    }

    XVBASurface *dst_xvba_surface, *src_xvba_surface;
    dst_xvba_surface = obj_glx_surface->xvba_surface;
    src_xvba_surface = (obj_surface->putimage_hacks ?
                        obj_surface->putimage_hacks->xvba_surface :
                        obj_surface->xvba_surface);

    /* Check for Evergreen workaround */
    int needs_evergreen_texture = 0;
    int evergreen_workaround = obj_glx_surface->evergreen_workaround;
    if (evergreen_workaround < 0) {
        evergreen_workaround = get_evergreen_workaround();

        if (evergreen_workaround == EVERGREEN_WORKAROUND_AUTODETECT) {
            evergreen_workaround = 0;
            if (driver_data->is_evergreen_gpu) {
                switch (driver_data->va_display_type) {
                case VA_DISPLAY_X11:
                    if (fglrx_check_version(0,80,5))
                        evergreen_workaround = 0;
                    else if (driver_data->is_fusion_igp)
                        evergreen_workaround = EVERGREEN_WORKAROUND_COPY;
                    else if (fglrx_check_version(8,78,6))
                        evergreen_workaround = (EVERGREEN_WORKAROUND_SWAP8_X  |
                                                EVERGREEN_WORKAROUND_SWAP8_Y  |
                                                EVERGREEN_WORKAROUND_SWAP16_X |
                                                EVERGREEN_WORKAROUND_SWAP16_Y);
                    break;
                case VA_DISPLAY_GLX:
                    if (fglrx_check_version(8,80,5))
                        evergreen_workaround = 0;
                    else if (fglrx_check_version(8,79,4) &&
                             driver_data->is_fusion_igp)
                        evergreen_workaround = (EVERGREEN_WORKAROUND_SWAP8_X |
                                                EVERGREEN_WORKAROUND_SWAP8_Y |
                                                EVERGREEN_WORKAROUND_SWAP16_X);
                    else if (fglrx_check_version(8,78,6) &&
                             driver_data->is_fusion_igp)
                        evergreen_workaround = (EVERGREEN_WORKAROUND_SWAP8_X |
                                                EVERGREEN_WORKAROUND_SWAP8_Y |
                                                EVERGREEN_WORKAROUND_SWAP16_Y);
                    else if (fglrx_check_version(8,78,6))
                        evergreen_workaround = (EVERGREEN_WORKAROUND_SWAP8_X  |
                                                EVERGREEN_WORKAROUND_SWAP16_Y |
                                                EVERGREEN_WORKAROUND_SWAP32_Y |
                                                EVERGREEN_WORKAROUND_SWAP64_Y);
                    break;
                }
            }
        }

        if (evergreen_workaround & EVERGREEN_WORKAROUND_COPY)
            evergreen_workaround = EVERGREEN_WORKAROUND_COPY;

        D(bug("Using Evergreen workaround %d\n", evergreen_workaround));
        obj_glx_surface->evergreen_workaround = evergreen_workaround;
    }

    if (evergreen_workaround) {
        needs_evergreen_texture = 1;

        if (!obj_glx_surface->evergreen_texture) {
            obj_glx_surface->evergreen_texture = gl_create_texture(
                GL_TEXTURE_2D,
                GL_BGRA,
                src_xvba_surface->info.normal.width,
                src_xvba_surface->info.normal.height
                );
            if (!obj_glx_surface->evergreen_texture)
                return VA_STATUS_ERROR_ALLOCATION_FAILED;

            /* XXX: some algorithms work modulo the texture size */
            glBindTexture(GL_TEXTURE_2D, obj_glx_surface->evergreen_texture);
            gl_set_texture_wrapping(GL_TEXTURE_2D, GL_REPEAT);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        if (!obj_glx_surface->evergreen_fbo) {
            obj_glx_surface->evergreen_fbo = gl_create_framebuffer_object(
                GL_TEXTURE_2D,
                obj_glx_surface->evergreen_texture,
                src_xvba_surface->info.normal.width,
                src_xvba_surface->info.normal.height
            );
            if (!obj_glx_surface->evergreen_fbo)
                return VA_STATUS_ERROR_ALLOCATION_FAILED;
        }

        if (evergreen_workaround != EVERGREEN_WORKAROUND_COPY &&
            !obj_glx_surface->evergreen_shader) {
            const char **shader_fp = NULL;
            unsigned int shader_fp_length = 0;
            float *params;
            int i, n_params, n_x_params, n_y_params;

            // program.local[0] = textureSize
            params = obj_glx_surface->evergreen_params[0];
            params[0] = (float)src_xvba_surface->info.normal.width;
            params[1] = (float)src_xvba_surface->info.normal.height;
            params[2] = 1.0f / src_xvba_surface->info.normal.width;
            params[3] = 1.0f / src_xvba_surface->info.normal.height;

            // program.local[1..4] = mix_params[0..3]
            for (i = 1; i <= XVBA_MAX_EVERGREEN_PARAMS; i++) {
                params = obj_glx_surface->evergreen_params[i];
                params[0] = 1.0f;
                params[1] = 1.0f;
                params[2] = 0.0f;
                params[3] = 0.0f;
            }

            // fill in X params
            params = &obj_glx_surface->evergreen_params[1][0];
            if (evergreen_workaround & EVERGREEN_WORKAROUND_SWAP8_X)
                fill_evergreen_params(&params, 8);
            if (evergreen_workaround & EVERGREEN_WORKAROUND_SWAP16_X)
                fill_evergreen_params(&params, 16);
            if (evergreen_workaround & EVERGREEN_WORKAROUND_SWAP32_X)
                fill_evergreen_params(&params, 32);
            if (evergreen_workaround & EVERGREEN_WORKAROUND_SWAP64_X)
                fill_evergreen_params(&params, 64);
            n_x_params = (params - &obj_glx_surface->evergreen_params[1][0])/4;

            // fill in Y params
            params = &obj_glx_surface->evergreen_params[1][1];
            if (evergreen_workaround & EVERGREEN_WORKAROUND_SWAP8_Y)
                fill_evergreen_params(&params, 8);
            if (evergreen_workaround & EVERGREEN_WORKAROUND_SWAP16_Y)
                fill_evergreen_params(&params, 16);
            if (evergreen_workaround & EVERGREEN_WORKAROUND_SWAP32_Y)
                fill_evergreen_params(&params, 32);
            if (evergreen_workaround & EVERGREEN_WORKAROUND_SWAP64_Y)
                fill_evergreen_params(&params, 64);
            n_y_params = (params - &obj_glx_surface->evergreen_params[1][1])/4;

            // load shader
            n_params = MAX(n_x_params, n_y_params);
            switch (n_params) {
            case 1:
                shader_fp = Evergreen_mix_1_fp;
                shader_fp_length = EVERGREEN_MIX_1_FP_SZ;
                break;
            case 2:
                shader_fp = Evergreen_mix_2_fp;
                shader_fp_length = EVERGREEN_MIX_2_FP_SZ;
                break;
            case 3:
                shader_fp = Evergreen_mix_3_fp;
                shader_fp_length = EVERGREEN_MIX_3_FP_SZ;
                break;
            case 4:
                shader_fp = Evergreen_mix_4_fp;
                shader_fp_length = EVERGREEN_MIX_4_FP_SZ;
                break;
            default:
                /* XXX: unsupported combination, disable Evergreen workaround */
                D(bug("ERROR: unsupported Evergreen workaround 0x%x, disabling\n",
                      evergreen_workaround));
                obj_glx_surface->evergreen_workaround = 0;
                break;
            }
            obj_glx_surface->evergreen_params_count = n_params;

            if (shader_fp && shader_fp_length) {
                obj_glx_surface->evergreen_shader = gl_create_shader_object(
                    shader_fp,
                    shader_fp_length
                );
                if (!obj_glx_surface->evergreen_shader)
                    return VA_STATUS_ERROR_OPERATION_FAILED;
            }
        }
    }

    /* Make sure GLX texture has the same dimensions as the surface */
    int needs_tx_texture = 0;
    if (needs_evergreen_texture ||
        obj_glx_surface->format == GL_RGBA || // XXX: XvBA bug, no RGBA support
        (/*!fglrx_check_version(8,76,7) &&*/  // XXX: #70011.64 supposedly fixed
         (obj_glx_surface->width  != src_xvba_surface->info.normal.width ||
          obj_glx_surface->height != src_xvba_surface->info.normal.height))) {
    again:
        needs_tx_texture = 1;

        if (!obj_glx_surface->tx_texture) {
            obj_glx_surface->tx_texture = gl_create_texture(
                GL_TEXTURE_2D,
                GL_BGRA,
                src_xvba_surface->info.normal.width,
                src_xvba_surface->info.normal.height
            );
            if (!obj_glx_surface->tx_texture)
                return VA_STATUS_ERROR_ALLOCATION_FAILED;
        }

        if (!obj_glx_surface->tx_xvba_surface) {
            obj_glx_surface->tx_xvba_surface = xvba_create_surface_gl(
                obj_context->xvba_decoder,
                obj_glx_surface->gl_context->context,
                obj_glx_surface->tx_texture
            );
            if (!obj_glx_surface->tx_xvba_surface)
                return VA_STATUS_ERROR_ALLOCATION_FAILED;
        }

        if (!fbo_ensure(obj_glx_surface))
            return VA_STATUS_ERROR_OPERATION_FAILED;

        dst_xvba_surface = obj_glx_surface->tx_xvba_surface;
    }

    /* Transfer XvBA surface */
    if (xvba_transfer_surface(obj_context->xvba_session,
                              dst_xvba_surface,
                              src_xvba_surface,
                              get_XVBA_SURFACE_FLAG(flags)) < 0) {
        /* XXX: the user texture is probably RGBA so, create the tx texture */
        if (!needs_tx_texture && obj_glx_surface->format == GL_NONE) {
            obj_glx_surface->format = GL_RGBA;
            goto again;
        }
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }
    if (!needs_tx_texture && obj_glx_surface->format == GL_NONE)
        obj_glx_surface->format = GL_BGRA;

    GLuint alternate_texture;
    int needs_alternate_texture = 0;
    if (needs_evergreen_texture) {
        needs_alternate_texture = 1;
        alternate_texture = obj_glx_surface->evergreen_texture;

        gl_bind_framebuffer_object(obj_glx_surface->evergreen_fbo);
        glBindTexture(GL_TEXTURE_2D, obj_glx_surface->tx_texture);
        if (obj_glx_surface->evergreen_shader) {
            gl_bind_shader_object(obj_glx_surface->evergreen_shader);

            int i;
            for (i = 0; i <= obj_glx_surface->evergreen_params_count; i++)
                gl_vtable->gl_program_local_parameter_4fv(
                    GL_FRAGMENT_PROGRAM,
                    i,
                    obj_glx_surface->evergreen_params[i]
                );
        }
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        {
            const unsigned int w = src_xvba_surface->info.normal.width;
            const unsigned int h = src_xvba_surface->info.normal.height;
            glTexCoord2f(0.0f, 0.0f); glVertex2i(0, 0);
            glTexCoord2f(1.0f, 0.0f); glVertex2i(w, 0);
            glTexCoord2f(1.0f, 1.0f); glVertex2i(w, h);
            glTexCoord2f(0.0f, 1.0f); glVertex2i(0, h);
        }
        glEnd();
        if (obj_glx_surface->evergreen_shader)
            gl_unbind_shader_object(obj_glx_surface->evergreen_shader);
        gl_unbind_framebuffer_object(obj_glx_surface->evergreen_fbo);
    }
    else if (needs_tx_texture) {
        needs_alternate_texture = 1;
        alternate_texture = obj_glx_surface->tx_texture;
    }

    if (needs_alternate_texture) {
        gl_bind_framebuffer_object(obj_glx_surface->fbo);
        glBindTexture(GL_TEXTURE_2D, alternate_texture);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        {
            /* Both TX and Evergreen textures are GL_TEXTURE_2D */
            const unsigned int w = obj_glx_surface->width;
            const unsigned int h = obj_glx_surface->height;
            const float tw = obj_surface->width / (float)src_xvba_surface->info.normal.width;
            const float th = obj_surface->height / (float)src_xvba_surface->info.normal.height;
            glTexCoord2f(0.0f, 0.0f); glVertex2i(0, 0);
            glTexCoord2f(tw,   0.0f); glVertex2i(w, 0);
            glTexCoord2f(tw,   th  ); glVertex2i(w, h);
            glTexCoord2f(0.0f, th  ); glVertex2i(0, h);
        }
        glEnd();
        gl_unbind_framebuffer_object(obj_glx_surface->fbo);
    }
    return VA_STATUS_SUCCESS;
}

// Transfer VA surface to GLX surface
static VAStatus
transfer_surface(
    xvba_driver_data_t  *driver_data,
    object_glx_surface_p obj_glx_surface,
    object_surface_p     obj_surface,
    unsigned int         flags
)
{
    PutImageHacks * const h = obj_surface->putimage_hacks;

    if (!h || h->type == PUTIMAGE_HACKS_SURFACE)
        return transfer_surface_native(driver_data,
                                       obj_glx_surface,
                                       obj_surface,
                                       flags);

    object_image_p obj_image = h->obj_image;
    if (!obj_image)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    VAStatus status;
    status = commit_hw_image(driver_data, obj_image, NULL, HWIMAGE_TYPE_GLX);
    if (status != VA_STATUS_SUCCESS)
        return status;

    object_image_glx_p const hwi = obj_image->hw.glx;
    if (!hwi)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    if (!fbo_ensure(obj_glx_surface))
        return VA_STATUS_ERROR_OPERATION_FAILED;

    object_buffer_p obj_buffer = XVBA_BUFFER(obj_image->image.buf);
    if (!obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

    GLVTable * const gl_vtable = gl_get_vtable();
    unsigned int i;
    for (i = 0; i < hwi->num_textures; i++) {
        if (hwi->shader)
            gl_vtable->gl_active_texture(GL_TEXTURE0 + i);
        glBindTexture(hwi->target, hwi->textures[i]);
    }

    gl_bind_framebuffer_object(obj_glx_surface->fbo);
    if (hwi->shader)
        gl_bind_shader_object(hwi->shader);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    {
        float tw, th;
        switch (hwi->target) {
        case GL_TEXTURE_2D:
            tw = obj_image->image.width == hwi->width ?
                1.0f :
                (obj_image->image.width - 0.5f) / (float)hwi->width;
            th = obj_image->image.height == hwi->height ?
                1.0f :
                (obj_image->image.height - 0.5f) / (float)hwi->height;
            break;
        case GL_TEXTURE_RECTANGLE_ARB:
            tw = (float)obj_image->image.width;
            th = (float)obj_image->image.height;
            break;
        default:
            tw = 0.0f;
            th = 0.0f;
            ASSERT(hwi->target == GL_TEXTURE_2D ||
                   hwi->target == GL_TEXTURE_RECTANGLE_ARB);
            break;
        }

        const unsigned int w = obj_glx_surface->width;
        const unsigned int h = obj_glx_surface->height;
        glTexCoord2f(0.0f, 0.0f); glVertex2i(0, 0);
        glTexCoord2f(0.0f, th  ); glVertex2i(0, h);
        glTexCoord2f(tw  , th  ); glVertex2i(w, h);
        glTexCoord2f(tw  , 0.0f); glVertex2i(w, 0);
    }
    glEnd();
    if (hwi->shader)
        gl_unbind_shader_object(hwi->shader);
    gl_unbind_framebuffer_object(obj_glx_surface->fbo);

    i = hwi->num_textures;
    do {
        --i;
        if (hwi->shader)
            gl_vtable->gl_active_texture(GL_TEXTURE0 + i);
        glBindTexture(hwi->target, 0);
    } while (i > 0);
    return VA_STATUS_SUCCESS;
}

static VAStatus
ensure_procamp_shader(
    xvba_driver_data_t  *driver_data,
    object_glx_surface_p obj_glx_surface
)
{
    uint64_t new_mtime = obj_glx_surface->procamp_mtime;
    unsigned int i, n_procamp_zeros = 0;

    for (i = 0; i < driver_data->va_display_attrs_count; i++) {
        VADisplayAttribute * const attr = &driver_data->va_display_attrs[i];

        switch (attr->type) {
        case VADisplayAttribBrightness:
        case VADisplayAttribContrast:
        case VADisplayAttribSaturation:
        case VADisplayAttribHue:
            if (attr->value == 0)
                ++n_procamp_zeros;
            if (new_mtime < driver_data->va_display_attrs_mtime[i])
                new_mtime = driver_data->va_display_attrs_mtime[i];
            break;
        default:
            break;
        }
    }

    /* Check that ProcAmp adjustments were made since the last call */
    if (new_mtime <= obj_glx_surface->procamp_mtime)
        return VA_STATUS_SUCCESS;

    /* Check that we really need a shader (ProcAmp with non-default values) */
    if (n_procamp_zeros == 4) {
        obj_glx_surface->use_procamp_shader = 0;
        obj_glx_surface->procamp_mtime      = new_mtime;
        return VA_STATUS_SUCCESS;
    }

    if (obj_glx_surface->procamp_shader) {
        gl_destroy_shader_object(obj_glx_surface->procamp_shader);
        obj_glx_surface->procamp_shader = NULL;
    }
    obj_glx_surface->procamp_shader = gl_create_shader_object(
        ProcAmp_fp,
        PROCAMP_FP_SZ
    );
    if (!obj_glx_surface->procamp_shader)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    obj_glx_surface->use_procamp_shader = 1;
    obj_glx_surface->procamp_mtime      = new_mtime;
    return VA_STATUS_SUCCESS;
}

static GLuint
ensure_hqscaler_texture(void)
{
    const int N = 128;
    float *data = NULL;
    GLuint tex = 0;
    unsigned int i;

    tex = gl_create_texture(GL_TEXTURE_1D, GL_RGBA32F_ARB, N, 0);
    if (!tex)
        goto error;

    data = malloc(N * 4 * sizeof(*data));
    if (!data)
        goto error;

    /* Generate weights and offsets */
    for (i = 0; i < N; i++) {
        const float x  = (1.0f*i) / N;
        const float x2 = x*x;
        const float x3 = x2*x;
        const float w0 = (1.0f/6.0f) * (     -x3 + 3.0f*x2 - 3.0f*x + 1.0f);
        const float w1 = (1.0f/6.0f) * ( 3.0f*x3 - 6.0f*x2          + 4.0f);
        const float w2 = (1.0f/6.0f) * (-3.0f*x3 + 3.0f*x2 + 3.0f*x + 1.0f);
        const float w3 = (1.0f/6.0f) * (      x3);
        const float g0 = w0 + w1;
        const float h0 = -1.0f + w1 / g0 + 0.5f;
        const float g1 = w2 + w3;
        const float h1 =  1.0f + w3 / g1 + 0.5f;

        /* float4 = (h0, h1, g0, g1) */
        data[i*4 + 0] = h0;
        data[i*4 + 1] = h1;
        data[i*4 + 2] = g0;
        data[i*4 + 3] = g1;
    }

    glBindTexture(GL_TEXTURE_1D, tex);
    gl_set_texture_scaling(GL_TEXTURE_1D, GL_NEAREST);
    gl_set_texture_wrapping(GL_TEXTURE_1D, GL_REPEAT);
    glTexSubImage1D(
        GL_TEXTURE_1D,
        0,
        0,
        N,
        GL_RGBA,
        GL_FLOAT,
        data
    );
    glBindTexture(GL_TEXTURE_1D, 0);
    free(data);
    return tex;

    /* ERRORS */
error:
    if (tex)
        glDeleteTextures(1, &tex);
    if (data)
        free(data);
    return 0;
}

static VAStatus
ensure_scaler(
    xvba_driver_data_t  *driver_data,
    object_glx_surface_p obj_glx_surface,
    unsigned int         flags
)
{
    const unsigned int va_scale = flags & VA_FILTER_SCALING_MASK;

    if (obj_glx_surface->va_scale == va_scale)
        return VA_STATUS_SUCCESS;

    if (obj_glx_surface->hqscaler) {
        gl_destroy_shader_object(obj_glx_surface->hqscaler);
        obj_glx_surface->hqscaler = NULL;
    }

    if (obj_glx_surface->hqscaler_texture) {
        glDeleteTextures(1, &obj_glx_surface->hqscaler_texture);
        obj_glx_surface->hqscaler_texture = 0;
    }

    const GLenum target = obj_glx_surface->target;
    switch (va_scale) {
    case VA_FILTER_SCALING_DEFAULT:
        glBindTexture(target, obj_glx_surface->texture);
        gl_set_texture_scaling(target, GL_LINEAR);
        glBindTexture(target, 0);
        break;
    case VA_FILTER_SCALING_FAST:
        glBindTexture(target, obj_glx_surface->texture);
        gl_set_texture_scaling(target, GL_NEAREST);
        glBindTexture(target, 0);
        break;
    case VA_FILTER_SCALING_HQ: {
        const char **shader_fp = NULL;
        unsigned int shader_fp_length = 0;

        glBindTexture(target, obj_glx_surface->texture);
        gl_set_texture_scaling(target, GL_LINEAR);
        glBindTexture(target, 0);

        obj_glx_surface->hqscaler_texture = ensure_hqscaler_texture();
        if (obj_glx_surface->hqscaler_texture) {
            shader_fp = Bicubic_FLOAT_fp;
            shader_fp_length = BICUBIC_FLOAT_FP_SZ;
        }
        else {
            shader_fp = Bicubic_fp;
            shader_fp_length = BICUBIC_FP_SZ;
        }

        obj_glx_surface->hqscaler = gl_create_shader_object(
            shader_fp,
            shader_fp_length
        );
        if (!obj_glx_surface->hqscaler)
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
        break;
    }
    }

    obj_glx_surface->va_scale = va_scale;
    return VA_STATUS_SUCCESS;
}

// vaCreateSurfaceGLX
VAStatus
xvba_CreateSurfaceGLX(
    VADriverContextP    ctx,
    unsigned int        target,
    unsigned int        texture,
    void              **gl_surface
)
{
    XVBA_DRIVER_DATA_INIT;

    xvba_set_display_type(driver_data, VA_DISPLAY_GLX);

    if (!gl_surface)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    /* Make sure it is a valid GL texture object */
    if (!glIsTexture(texture))
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    /* Make sure we have the necessary GLX extensions */
    if (!ensure_extensions())
        return VA_STATUS_ERROR_OPERATION_FAILED;

    /* Setup new GLX context */
    GLContextState old_cs, *new_cs;
    gl_get_current_context(&old_cs);
    old_cs.display = driver_data->x11_dpy;
    new_cs = gl_create_context(driver_data->x11_dpy, driver_data->x11_screen, &old_cs);
    if (!new_cs)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    if (!gl_set_current_context(new_cs, NULL))
        return VA_STATUS_ERROR_OPERATION_FAILED;

    gl_init_context(new_cs);

    object_glx_surface_p obj_glx_surface;
    obj_glx_surface = create_glx_surface_from_texture(
        driver_data,
        target,
        texture
    );
    if (!obj_glx_surface)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    *gl_surface = obj_glx_surface;
    obj_glx_surface->gl_context = new_cs;

    gl_set_current_context(&old_cs, NULL);
    return VA_STATUS_SUCCESS;
}

// vaDestroySurfaceGLX
VAStatus
xvba_DestroySurfaceGLX(
    VADriverContextP    ctx,
    void               *gl_surface
)
{
    XVBA_DRIVER_DATA_INIT;

    xvba_set_display_type(driver_data, VA_DISPLAY_GLX);

    /* Make sure we have the necessary GLX extensions */
    if (!ensure_extensions())
        return VA_STATUS_ERROR_OPERATION_FAILED;

    object_glx_surface_p obj_glx_surface = gl_surface;
    if (!obj_glx_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    GLContextState old_cs, *new_cs = obj_glx_surface->gl_context;
    if (!gl_set_current_context(new_cs, &old_cs))
        return VA_STATUS_ERROR_OPERATION_FAILED;

    destroy_glx_surface(driver_data, obj_glx_surface);

    gl_destroy_context(new_cs);
    gl_set_current_context(&old_cs, NULL);
    return VA_STATUS_SUCCESS;
}

// vaCopySurfaceGLX
static VAStatus
do_copy_surface_glx(
    xvba_driver_data_t  *driver_data,
    object_glx_surface_p obj_glx_surface,
    object_surface_p     obj_surface,
    unsigned int         flags
)
{
    VAStatus status;

    /* Transfer surface to texture */
    if (!is_empty_surface(obj_surface)) {
        status = transfer_surface(driver_data, obj_glx_surface, obj_surface, flags);
        if (status != VA_STATUS_SUCCESS)
            return status;
    }

    /* Make sure color matrix for ProcAmp adjustments is setup */
    status = ensure_procamp_shader(driver_data, obj_glx_surface);
    if (status != VA_STATUS_SUCCESS)
        return status;

    /* Check if FBO is needed. e.g. for subpictures */
    const int needs_fbo = (obj_surface->assocs_count > 0 ||
                           obj_glx_surface->use_procamp_shader);
    if (!needs_fbo)
        return VA_STATUS_SUCCESS;

    /* Create framebuffer surface */
    if (!fbo_ensure(obj_glx_surface))
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    gl_bind_framebuffer_object(obj_glx_surface->fbo);

    /* Re-render the video frame with ProcAmp adjustments */
    if (obj_glx_surface->use_procamp_shader) {
        GLVTable * const gl_vtable = gl_get_vtable();
        int i;

        gl_bind_shader_object(obj_glx_surface->procamp_shader);

        /* Commit the new ProcAmp color matrix */
        for (i = 0; i < 4; i++)
            gl_vtable->gl_program_local_parameter_4fv(
                GL_FRAGMENT_PROGRAM, i,
                driver_data->cm_composite[i]
            );

        /* Render the picture frame */
        glBindTexture(obj_glx_surface->target, obj_glx_surface->texture);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        {
            float tw, th;
            switch (obj_glx_surface->target) {
            case GL_TEXTURE_2D:
                tw = 1.0f;
                th = 1.0f;
                break;
            case GL_TEXTURE_RECTANGLE_ARB:
                tw = (float)obj_glx_surface->width;
                th = (float)obj_glx_surface->height;
                break;
            default:
                tw = 0.0f;
                th = 0.0f;
                ASSERT(obj_glx_surface->target == GL_TEXTURE_2D ||
                       obj_glx_surface->target == GL_TEXTURE_RECTANGLE_ARB);
                break;
            }

            const unsigned int w = obj_glx_surface->width;
            const unsigned int h = obj_glx_surface->height;
            glTexCoord2f(0.0f, 0.0f); glVertex2i(0, 0);
            glTexCoord2f(tw  , 0.0f); glVertex2i(w, 0);
            glTexCoord2f(tw  , th  ); glVertex2i(w, h);
            glTexCoord2f(0.0f, th  ); glVertex2i(0, h);
        }
        glEnd();
        glBindTexture(obj_glx_surface->target, 0);
        gl_unbind_shader_object(obj_glx_surface->procamp_shader);
    }

    /* Render subpictures to FBO */
    VARectangle surface_rect;
    surface_rect.x      = 0;
    surface_rect.y      = 0;
    surface_rect.width  = obj_surface->width;
    surface_rect.height = obj_surface->height;
    status = render_subpictures(driver_data, obj_surface, &surface_rect);

    gl_unbind_framebuffer_object(obj_glx_surface->fbo);
    return status;
}

VAStatus
xvba_CopySurfaceGLX(
    VADriverContextP    ctx,
    void               *gl_surface,
    VASurfaceID         surface,
    unsigned int        flags
)
{
    XVBA_DRIVER_DATA_INIT;

    xvba_set_display_type(driver_data, VA_DISPLAY_GLX);

    object_glx_surface_p obj_glx_surface = gl_surface;
    if (!obj_glx_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    object_surface_p obj_surface = XVBA_SURFACE(surface);
    if (!obj_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    /* Make sure we have the necessary GLX extensions */
    if (!ensure_extensions())
        return VA_STATUS_ERROR_OPERATION_FAILED;

    GLContextState old_cs, *new_cs = obj_glx_surface->gl_context;
    if (!gl_set_current_context(new_cs, &old_cs))
        return VA_STATUS_ERROR_OPERATION_FAILED;

    VAStatus status;
    status = do_copy_surface_glx(
        driver_data,
        obj_glx_surface,
        obj_surface,
        flags
    );

    gl_set_current_context(&old_cs, NULL);
    return status;
}

// Locks output surface
static void
glx_output_surface_lock(object_glx_output_p obj_output)
{
    if (!obj_output || !obj_output->render_thread_ok)
        return;
    pthread_mutex_lock(&obj_output->lock);
}

// Unlocks output surface
static void
glx_output_surface_unlock(object_glx_output_p obj_output)
{
    if (!obj_output || !obj_output->render_thread_ok)
        return;
    pthread_mutex_unlock(&obj_output->lock);
}

// Destroys output surface
void
glx_output_surface_destroy(
    xvba_driver_data_t *driver_data,
    object_glx_output_p obj_output
)
{
    if (!obj_output)
        return;

    if (1) {
        const uint64_t end   = get_ticks_usec();
        const uint64_t start = obj_output->render_start;
        const uint64_t ticks = obj_output->render_ticks;

        D(bug("%llu refreshes in %llu usec (%.1f fps)\n",
              ticks, end - start,
              ticks * 1000000.0 / (end - start)));
    }

    if (obj_output->render_thread_ok) {
        async_queue_push(obj_output->render_comm, MSG2PTR(MSG_TYPE_QUIT));
        pthread_join(obj_output->render_thread, NULL);
        obj_output->render_thread    = 0;
        obj_output->render_thread_ok = 0;
    }

    if (obj_output->render_comm) {
        async_queue_free(obj_output->render_comm);
        obj_output->render_comm = NULL;
    }

    if (obj_output->render_context) {
        gl_destroy_context(obj_output->render_context);
        obj_output->render_context = NULL;
    }

    if (obj_output->parent)
        --obj_output->parent->children_count;

    if (obj_output->gl_surface) {
        if (!obj_output->parent)
            destroy_glx_surface(driver_data, obj_output->gl_surface);
        obj_output->gl_surface = NULL;
    }

    if (obj_output->gl_context) {
        glFinish();
        GLContextState dummy_cs;
        dummy_cs.display = driver_data->x11_dpy;
        dummy_cs.window  = None;
        dummy_cs.context = NULL;
        gl_set_current_context(&dummy_cs, NULL);
        if (!obj_output->parent)
            gl_destroy_context(obj_output->gl_context);
        obj_output->gl_context = NULL;
    }

    if (obj_output->gl_window.xid != None) {
#if 0
        /* User's XDestroyWindow() on the parent window will destroy
           our child windows too */
        if (!obj_output->parent) {
            XUnmapWindow(driver_data->x11_dpy, obj_output->gl_window.xid);
            x11_wait_event(driver_data->x11_dpy, obj_output->gl_window.xid, UnmapNotify);
            XDestroyWindow(driver_data->x11_dpy, obj_output->gl_window.xid);
        }
#endif
        obj_output->gl_window.xid = None;
    }

    if (obj_output->gl_window.cmap != None) {
        if (!obj_output->parent)
            XFreeColormap(driver_data->x11_dpy, obj_output->gl_window.cmap);
        obj_output->gl_window.cmap = None;
    }

    if (obj_output->gl_window.vi) {
        if (!obj_output->parent)
            XFree(obj_output->gl_window.vi);
        obj_output->gl_window.vi = NULL;
    }
    free(obj_output);
}

// Creates output surface
static object_glx_output_p
glx_output_surface_create(
    xvba_driver_data_t *driver_data,
    Window              window,
    unsigned int        width,
    unsigned int        height
)
{
    object_glx_output_p obj_output = calloc(1, sizeof(*obj_output));
    if (!obj_output)
        return NULL;

    obj_output->va_surface_status = VASurfaceReady;
    obj_output->window.xid        = window;
    obj_output->window.width      = width;
    obj_output->window.height     = height;

    pthread_mutex_init(&obj_output->lock, NULL);

    /* XXX: recurse through parents until we find an output surface */
    Window root_window, parent_window, *child_windows = NULL;
    unsigned int n_child_windows = 0;
    XQueryTree(
        driver_data->x11_dpy,
        window,
        &root_window,
        &parent_window,
        &child_windows, &n_child_windows
    );
    if (child_windows)
        XFree(child_windows);

    GLContextState old_cs, *parent_cs = NULL;
    gl_get_current_context(&old_cs);
    old_cs.display = driver_data->x11_dpy;

    if (parent_window != None) {
        object_output_p parent_obj_output;
        parent_obj_output = output_surface_lookup(driver_data, parent_window);
        if (parent_obj_output && parent_obj_output->glx) {
            obj_output->parent = parent_obj_output->glx;
            parent_cs          = parent_obj_output->glx->gl_context;
        }
    }

    static GLint gl_visual_attr[] = {
        GLX_RGBA,
        GLX_RED_SIZE, 1,
        GLX_GREEN_SIZE, 1,
        GLX_BLUE_SIZE, 1,
        GLX_DOUBLEBUFFER,
        GL_NONE
    };

    XWindowAttributes wattr;
    XGetWindowAttributes(driver_data->x11_dpy, window, &wattr);
    int depth = wattr.depth;
    if (depth != 15 && depth != 16 && depth != 24 && depth != 32)
        depth = 24;

    obj_output->gl_window.vi = glXChooseVisual(
        driver_data->x11_dpy,
        driver_data->x11_screen,
        gl_visual_attr
    );
    if (!obj_output->gl_window.vi)
        goto error;

    obj_output->gl_window.cmap = XCreateColormap(
        driver_data->x11_dpy,
        RootWindow(driver_data->x11_dpy, driver_data->x11_screen),
        obj_output->gl_window.vi->visual,
        AllocNone
    );
    if (obj_output->gl_window.cmap == None)
        goto error;

    x11_get_window_colorkey(driver_data->x11_dpy, window, 0, 0, &obj_output->bgcolor);
    if (driver_data->va_background_color)
        obj_output->bgcolor = driver_data->va_background_color->value;

    XSetWindowAttributes xswa;
    unsigned long xswa_mask = CWBorderPixel | CWBackPixel | CWColormap;
    xswa.border_pixel       = BlackPixel(driver_data->x11_dpy, driver_data->x11_screen);
    xswa.background_pixel   = obj_output->bgcolor;
    xswa.colormap           = obj_output->gl_window.cmap;

    obj_output->gl_window.xid = XCreateWindow(
        driver_data->x11_dpy,
        window,
        0,
        0,
        width,
        height,
        0,
        depth,
        InputOutput,
        obj_output->gl_window.vi->visual,
        xswa_mask, &xswa
    );
    if (obj_output->gl_window.xid == None)
        goto error;

    XSelectInput(driver_data->x11_dpy, obj_output->gl_window.xid, StructureNotifyMask);
    XMapWindow(driver_data->x11_dpy, obj_output->gl_window.xid);
    XLowerWindow(driver_data->x11_dpy, obj_output->gl_window.xid);
    x11_wait_event(driver_data->x11_dpy, obj_output->gl_window.xid, MapNotify);

    /* XXX: assume the program will only be using vaPutSurface() and
       doesn't have any other GLX context managed itself */
    ASSERT(driver_data->va_display_type == VA_DISPLAY_X11);
    gl_set_current_context_cache(1);

    /* XXX: check that we don't already have a valid GLX context */
    obj_output->gl_context = gl_create_context(
        driver_data->x11_dpy,
        driver_data->x11_screen,
        parent_cs
    );
    if (!obj_output->gl_context)
        goto error;
    obj_output->gl_context->window = obj_output->gl_window.xid;
    if (!gl_set_current_context(obj_output->gl_context, NULL))
        goto error;
    if (!ensure_extensions()) {
        gl_set_current_context(&old_cs, NULL);
        goto error;
    }

    gl_init_context(obj_output->gl_context);
    gl_set_bgcolor(obj_output->bgcolor);
    glClear(GL_COLOR_BUFFER_BIT);
    gl_set_current_context(&old_cs, NULL);

    if (use_putsurface_fast()) {
        RenderThreadArgs *args = NULL;

        obj_output->render_comm = async_queue_new();
        if (!obj_output->render_comm)
            goto render_thread_init_end;

        args = malloc(sizeof(*args));
        if (!args)
            goto render_thread_init_end;
        args->driver_data = driver_data;
        args->obj_output  = obj_output;
        obj_output->render_thread_ok = !pthread_create(
            &obj_output->render_thread,
            NULL,
            render_thread,
            args
        );
    render_thread_init_end:
        if (!obj_output->render_thread_ok)
            free(args);
    }
    obj_output->render_ticks = 0;
    obj_output->render_start = get_ticks_usec();
    return obj_output;

error:
    glx_output_surface_destroy(driver_data, obj_output);
    return NULL;
}

// Returns thread-specific GL context
static inline GLContextState *
glx_output_surface_get_context(object_glx_output_p obj_output)
{
    if (obj_output->render_thread_ok &&
        obj_output->render_thread == pthread_self())
        return obj_output->render_context;
    return obj_output->gl_context;
}

// Ensures output surface exists
static object_glx_output_p
glx_output_surface_ensure(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface,
    Window              window
)
{
    object_output_p obj_output;
    obj_output = output_surface_ensure(driver_data, obj_surface, window);
    if (!obj_output)
        return NULL;

    object_glx_output_p glx_output = obj_output->glx;
    if (!glx_output) {
        unsigned int w, h;
        x11_get_geometry(driver_data->x11_dpy, window, NULL, NULL, &w, &h);
        glx_output = glx_output_surface_create(driver_data, window, w, h);
        if (!glx_output)
            return NULL;
        obj_output->glx = glx_output;
    }
    return glx_output;
}

// Ensures output surface size matches drawable size
static int
glx_output_surface_ensure_size(
    xvba_driver_data_t *driver_data,
    object_glx_output_p glx_output
)
{
    GLContextState * const gl_context = glx_output_surface_get_context(glx_output);
    Display * const dpy = gl_context->display;
    const Window win = glx_output->window.xid;
    unsigned int width, height;
    int size_changed = 0;

    x11_get_geometry(dpy, win, NULL, NULL, &width, &height);
    if (glx_output->window.width  != width ||
        glx_output->window.height != height) {
        /* If there is still a ConfigureNotify event in the queue,
           this means the user-application was not notified of the
           change yet. So, just don't assume any change in this case */
        XEvent e;
        if (XCheckTypedWindowEvent(dpy, win, ConfigureNotify, &e))
            XPutBackEvent(dpy, &e);
        else {
            glx_output->window.width  = width;
            glx_output->window.height = height;
            size_changed = 1;

            /* Resize GL rendering window to fit new window size */
            XMoveResizeWindow(
                dpy,
                glx_output->gl_window.xid,
                0, 0, width, height
            );
            XSync(dpy, False);
        }
    }

    /* Make sure the VA/GLX surface is created */
    if (size_changed) {
        destroy_glx_surface(driver_data, glx_output->gl_surface);
        glx_output->gl_surface = NULL;
    }
    if (!glx_output->gl_surface) {
        glx_output->gl_surface = create_glx_surface(
            driver_data,
            width,
            height
        );
        if (!glx_output->gl_surface)
            return -1;
        glx_output->gl_surface->gl_context = glx_output->gl_context;

        /* Make sure the FBO is created */
        if (!fbo_ensure(glx_output->gl_surface))
            return -1;
        gl_bind_framebuffer_object(glx_output->gl_surface->fbo);
        glClear(GL_COLOR_BUFFER_BIT);
        gl_unbind_framebuffer_object(glx_output->gl_surface->fbo);
    }
    return 0;
}

// Ensure rectangle is within specified bounds
static inline void
ensure_bounds(VARectangle *r, unsigned int width, unsigned int height)
{
    if (r->x < 0)
        r->x = 0;
    if (r->y < 0)
        r->y = 0;
    if (r->width > width - r->x)
        r->width = width - r->x;
    if (r->height > height - r->y)
        r->height = height - r->y;
}

// Query GLX surface status
int
query_surface_status_glx(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface
)
{
    unsigned int i;
    for (i = 0; i < obj_surface->output_surfaces_count; i++) {
        object_glx_output_p obj_output = obj_surface->output_surfaces[i]->glx;
        if (!obj_output)
            continue;
        if (obj_output->va_surface_status != VASurfaceDisplaying)
            continue;
        obj_output->va_surface_status = VASurfaceReady;

        /* Make sure all pending OpenGL commands have completed */
        GLContextState old_cs;
        if (gl_set_current_context(obj_output->gl_context, &old_cs)) {
            glFinish();
            gl_swap_buffers(obj_output->gl_context);
            gl_set_current_context(&old_cs, NULL);
        }
    }
    return XVBA_COMPLETED;
}

// Queue surface for display
VAStatus
flip_surface(
    xvba_driver_data_t  *driver_data,
    object_glx_output_p  obj_output
)
{
    object_glx_surface_p const obj_glx_surface = obj_output->gl_surface;

    /* Draw GL surface to screen */
    glBindTexture(obj_glx_surface->target, obj_glx_surface->texture);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    {
        float tw, th;
        switch (obj_glx_surface->target) {
        case GL_TEXTURE_2D:
            tw = 1.0f;
            th = 1.0f;
            break;
        case GL_TEXTURE_RECTANGLE_ARB:
            tw = (float)obj_glx_surface->width;
            th = (float)obj_glx_surface->height;
            break;
        default:
            tw = 0.0f;
            th = 0.0f;
            ASSERT(obj_glx_surface->target == GL_TEXTURE_2D ||
                   obj_glx_surface->target == GL_TEXTURE_RECTANGLE_ARB);
            break;
        }

        const unsigned int w = obj_glx_surface->width;
        const unsigned int h = obj_glx_surface->height;
        glTexCoord2f(0.0f, 0.0f); glVertex2i(0, 0);
        glTexCoord2f(0.0f, 1.0f); glVertex2i(0, h);
        glTexCoord2f(1.0f, 1.0f); glVertex2i(w, h);
        glTexCoord2f(1.0f, 0.0f); glVertex2i(w, 0);
    }
    glEnd();
    glBindTexture(obj_glx_surface->target, 0);

    gl_swap_buffers(glx_output_surface_get_context(obj_output));
    obj_output->render_ticks++;
    return VA_STATUS_SUCCESS;
}

static VAStatus
queue_surface(
    xvba_driver_data_t  *driver_data,
    object_glx_output_p  obj_output,
    object_surface_p     obj_surface
)
{
    /* Commit framebuffer to screen */
    obj_surface->va_surface_status = VASurfaceDisplaying;
    obj_output->va_surface_status  = VASurfaceDisplaying;

    if (obj_output->render_thread_ok)
        return VA_STATUS_SUCCESS;
    return flip_surface(driver_data, obj_output);
}

// Render video surface (and subpictures) into the specified drawable
static VAStatus
do_put_surface_glx(
    xvba_driver_data_t  *driver_data,
    object_glx_output_p  obj_output,
    object_surface_p     obj_surface,
    const VARectangle   *src_rect,
    const VARectangle   *dst_rect,
    const VARectangle   *cliprects,
    unsigned int         num_cliprects,
    unsigned int         flags
)
{
    /* Ensure VA/GLX surface exists with the specified dimensions */
    object_glx_surface_p obj_glx_surface;
    obj_glx_surface = glx_surface_ensure(driver_data, obj_surface, obj_output);
    if (!obj_glx_surface)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    if (glx_output_surface_ensure_size(driver_data, obj_output) < 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    /* Ensure visible rect is within parent window bounds */
    VARectangle vis_rect = *dst_rect;
    ensure_bounds(&vis_rect, obj_output->window.width, obj_output->window.height);

    /* Reset GLX viewport for active context */
    gl_resize(obj_output->window.width, obj_output->window.height);

    /* Transfer surface to texture */
    VAStatus status;
    if (!is_empty_surface(obj_surface)) {
        status = transfer_surface(driver_data, obj_glx_surface, obj_surface, flags);
        if (status != VA_STATUS_SUCCESS)
            return status;
    }

    /* Make sure color matrix for ProcAmp adjustments is setup */
    status = ensure_procamp_shader(driver_data, obj_glx_surface);
    if (status != VA_STATUS_SUCCESS)
        return status;

    /* Setup scaling algorithm */
    status = ensure_scaler(driver_data, obj_glx_surface, flags);
    if (status != VA_STATUS_SUCCESS)
        return status;

    /* Render picture */
    GLVTable * const gl_vtable = gl_get_vtable();
    float params[4];
    unsigned int i;

    gl_bind_framebuffer_object(obj_output->gl_surface->fbo);
    gl_vtable->gl_active_texture(GL_TEXTURE0);
    glBindTexture(obj_glx_surface->target, obj_glx_surface->texture);
    if (obj_glx_surface->hqscaler) {
        gl_bind_shader_object(obj_glx_surface->hqscaler);
        params[0] = (float)obj_glx_surface->width;
        params[1] = (float)obj_glx_surface->height;
        params[2] = 1.0f / obj_glx_surface->width;
        params[3] = 1.0f / obj_glx_surface->height;
        gl_vtable->gl_program_local_parameter_4fv(
            GL_FRAGMENT_PROGRAM,
            0,
            params
        );
        if (obj_glx_surface->hqscaler_texture) {
            gl_vtable->gl_active_texture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_1D, obj_glx_surface->hqscaler_texture);
        }
    }
    if (obj_glx_surface->use_procamp_shader) {
        gl_bind_shader_object(obj_glx_surface->procamp_shader);
        for (i = 0; i < 4; i++)
            gl_vtable->gl_program_local_parameter_4fv(
                GL_FRAGMENT_PROGRAM, i,
                driver_data->cm_composite[i]
            );
    }
    if (flags & VA_CLEAR_DRAWABLE) {
        if (driver_data->va_background_color &&
            driver_data->va_background_color->value != obj_output->bgcolor) {
            obj_output->bgcolor = driver_data->va_background_color->value;
            gl_set_bgcolor(obj_output->bgcolor);
        }
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glPushMatrix();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glTranslatef((float)vis_rect.x, (float)vis_rect.y, 0.0f);
    if (!is_empty_surface(obj_surface)) {
        const float surface_width  = obj_surface->xvba_surface_width;
        const float surface_height = obj_surface->xvba_surface_height;
        float tx1 = src_rect->x / surface_width;
        float ty1 = src_rect->y / surface_height;
        float tx2 = tx1 + src_rect->width / surface_width;
        float ty2 = ty1 + src_rect->height / surface_height;
        const int w = vis_rect.width;
        const int h = vis_rect.height;

        switch (obj_glx_surface->target) {
        case GL_TEXTURE_RECTANGLE_ARB:
            tx1 *= obj_glx_surface->width;
            tx2 *= obj_glx_surface->width;
            ty1 *= obj_glx_surface->height;
            ty2 *= obj_glx_surface->height;
            break;
        }

        glBegin(GL_QUADS);
        glTexCoord2f(tx1, ty1); glVertex2i(0, 0);
        glTexCoord2f(tx1, ty2); glVertex2i(0, h);
        glTexCoord2f(tx2, ty2); glVertex2i(w, h);
        glTexCoord2f(tx2, ty1); glVertex2i(w, 0);
        glEnd();
    }
    gl_vtable->gl_active_texture(GL_TEXTURE0);
    glBindTexture(obj_glx_surface->target, 0);

    /* Render subpictures */
    glScalef(
        (float)dst_rect->width / (float)obj_surface->width,
        (float)dst_rect->height / (float)obj_surface->height,
        1.0f
    );
    status = render_subpictures(driver_data, obj_surface, src_rect);
    glPopMatrix();
    if (obj_glx_surface->use_procamp_shader)
        gl_unbind_shader_object(obj_glx_surface->procamp_shader);
    if (obj_glx_surface->hqscaler) {
        if (obj_glx_surface->hqscaler_texture) {
            gl_vtable->gl_active_texture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_1D, 0);
        }
        gl_unbind_shader_object(obj_glx_surface->hqscaler);
    }
    gl_unbind_framebuffer_object(obj_output->gl_surface->fbo);

    /* Queue surface for display */
    return queue_surface(driver_data, obj_output, obj_surface);
}

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
)
{
    /* XXX: no clip rects supported */
    if (cliprects || num_cliprects > 0)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    /* Ensure output surface (child window) is set up */
    object_glx_output_p obj_output;
    obj_output = glx_output_surface_ensure(driver_data, obj_surface, drawable);
    if (!obj_output)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    ASSERT(obj_output->window.xid == drawable);
    ASSERT(obj_output->gl_window.xid != None);
    ASSERT(obj_output->gl_context);

    /* Ensure source rect is within surface bounds */
    VARectangle src_rect = *source_rect;
    ensure_bounds(&src_rect, obj_surface->width, obj_surface->height);

    /* Send args to render thread */
    if (obj_output->render_thread_ok) {
        uint64_t now = get_ticks_usec();
        PutSurfaceMsg *msg;

        if (now >= obj_output->render_timestamp + VIDEO_REFRESH) {
            if (!async_queue_push(obj_output->render_comm, MSG2PTR(MSG_TYPE_FLIP)))
                return VA_STATUS_ERROR_OPERATION_FAILED;
            obj_output->render_timestamp = now;
        }

        msg = malloc(sizeof(*msg));
        if (!msg)
            return VA_STATUS_ERROR_ALLOCATION_FAILED;

        msg->obj_surface = obj_surface;
        msg->src_rect    = src_rect;
        msg->dst_rect    = *target_rect;
        msg->flags       = flags;
        if (!async_queue_push(obj_output->render_comm, msg))
            return VA_STATUS_ERROR_OPERATION_FAILED;
        return VA_STATUS_SUCCESS;
    }

    GLContextState old_cs;
    if (!gl_set_current_context(obj_output->gl_context, &old_cs))
        return VA_STATUS_ERROR_OPERATION_FAILED;

    VAStatus status;
    status = do_put_surface_glx(
        driver_data,
        obj_output,
        obj_surface,
        &src_rect,
        target_rect,
        cliprects, num_cliprects,
        flags
    );

    gl_set_current_context(&old_cs, NULL);
    return status;
}
