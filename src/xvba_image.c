/*
 *  xvba_image.c - XvBA backend for VA-API (VAImage)
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
#include "xvba_image.h"
#include "xvba_video.h"
#include "xvba_buffer.h"
#include "xvba_decode.h"
#include "xvba_dump.h"

#define DEBUG 1
#include "debug.h"


// List of supported image formats
typedef struct {
    uint32_t format;
    VAImageFormat va_format;
} xvba_image_format_map_t;

static const xvba_image_format_map_t xvba_image_formats_map[] = {
    { XVBA_NV12, { VA_FOURCC('N','V','1','2'), VA_LSB_FIRST, 12 } },
    { XVBA_YV12, { VA_FOURCC('Y','V','1','2'), VA_LSB_FIRST, 12 } },
    { XVBA_YV12, { VA_FOURCC('I','4','2','0'), VA_LSB_FIRST, 12 } },
    { XVBA_ARGB, { VA_FOURCC('B','G','R','A'), VA_LSB_FIRST, 32,
                   32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000 } },
    { XVBA_FAKE, { VA_FOURCC('R','G','B','A'), VA_LSB_FIRST, 32,
                   32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000 } },
    { 0, }
};

// Initializes XvBA decode caps
static int ensure_surface_caps(xvba_driver_data_t *driver_data)
{
    int status;

    if (driver_data->xvba_surface_caps && driver_data->xvba_surface_caps_count)
        return 1;

    if (driver_data->xvba_surface_caps) {
        free(driver_data->xvba_surface_caps);
        driver_data->xvba_surface_caps = NULL;
    }
    driver_data->xvba_surface_caps_count = 0;

    status = xvba_get_surface_caps(
        driver_data->xvba_context,
        &driver_data->xvba_surface_caps_count,
        &driver_data->xvba_surface_caps
    );
    if (status < 0)
        return 0;
    return 1;
}

// Check VA image format represents RGB
int is_rgb_format(const VAImageFormat *image_format)
{
    switch (image_format->fourcc) {
    case VA_FOURCC('B','G','R','A'):
    case VA_FOURCC('R','G','B','A'):
    case VA_FOURCC('A','R','G','B'):
    case VA_FOURCC('A','B','G','R'):
        return 1;
    }
    return 0;
}

// Check VA image formats are the same
int compare_image_formats(const VAImageFormat *a, const VAImageFormat *b)
{
    return (a->fourcc         == b->fourcc         &&
            a->byte_order     == b->byte_order     &&
            a->bits_per_pixel == b->bits_per_pixel &&
            (is_rgb_format(a)
             ? (a->depth      == b->depth          &&
                a->red_mask   == b->red_mask       &&
                a->green_mask == b->green_mask     &&
                a->blue_mask  == b->blue_mask      &&
                a->alpha_mask == b->alpha_mask)
             : 1));
}

// Checks whether the XvBA implementation supports the specified image format
static inline int
is_supported_format(xvba_driver_data_t *driver_data, uint32_t format)
{
    unsigned int i;

    if (!ensure_surface_caps(driver_data))
        return 0;

    for (i = 0; i < driver_data->xvba_surface_caps_count; i++) {
        XVBASurfaceCap * const surface_cap = &driver_data->xvba_surface_caps[i];
        if (surface_cap->format == format && surface_cap->flag == XVBA_FRAME)
            return 1;
    }
    return 0;
}

// Enable "PutImage hacks", this reinitializes the XvBA state
static VAStatus
putimage_hacks_enable(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface,
    object_image_p      obj_image,
    PutImageHacksType   type
)
{
    ASSERT(type == PUTIMAGE_HACKS_SURFACE ||
           type == PUTIMAGE_HACKS_IMAGE);

    PutImageHacks *h = obj_surface->putimage_hacks;
    if (!h) {
        h = malloc(sizeof(*h));
        if (!h)
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
        h->type         = type;
        h->xvba_surface = NULL;
        h->obj_image    = NULL;
        obj_surface->putimage_hacks = h;
    }

    if (type == PUTIMAGE_HACKS_IMAGE) {
        if (h->obj_image &&
            !compare_image_formats(&h->obj_image->image.format, &obj_image->image.format)) {
            destroy_image(driver_data, h->obj_image);
            h->obj_image = NULL;
        }

        if (!h->obj_image) {
            h->obj_image = create_image(
                driver_data,
                obj_surface->width,
                obj_surface->height,
                &obj_image->image.format
            );
            if (!h->obj_image)
                return VA_STATUS_ERROR_ALLOCATION_FAILED;
        }
        ASSERT(h->obj_image->image.width == obj_surface->width);
        ASSERT(h->obj_image->image.height == obj_surface->height);

        h->type = PUTIMAGE_HACKS_IMAGE;
        return VA_STATUS_SUCCESS;
    }

    if (h->xvba_surface) {
        if (h->xvba_surface->info.normal.format == obj_image->xvba_format)
            return VA_STATUS_SUCCESS;
        xvba_destroy_surface(h->xvba_surface);
        h->xvba_surface = NULL;
    }

    object_context_p obj_context = XVBA_CONTEXT(obj_surface->va_context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    h->xvba_surface = xvba_create_surface(
        obj_context->xvba_session,
        obj_surface->width,
        obj_surface->height,
        obj_image->xvba_format
    );
    if (!h->xvba_surface)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    return VA_STATUS_SUCCESS;
}

// Disable "PutImage hacks", flush pending pictures
void
putimage_hacks_disable(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface
)
{
    if (!obj_surface->putimage_hacks)
        return;

    if (obj_surface->putimage_hacks->obj_image) {
        destroy_image(driver_data, obj_surface->putimage_hacks->obj_image);
        obj_surface->putimage_hacks->obj_image = NULL;
    }

    if (obj_surface->putimage_hacks->xvba_surface) {
        xvba_destroy_surface(obj_surface->putimage_hacks->xvba_surface);
        obj_surface->putimage_hacks->xvba_surface = NULL;
    }

    free(obj_surface->putimage_hacks);
    obj_surface->putimage_hacks = NULL;
}

// Check we need the "PutImage hacks"
static VAStatus
putimage_hacks_check(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface,
    object_image_p      obj_image
)
{
    object_context_p obj_context = XVBA_CONTEXT(obj_surface->va_context);

    PutImageHacksType type;
    /* If the surface was never used for decoding, it's safe to be
       aliased to plain pixels data (represented via a VAImage) */
    if (!obj_surface->used_for_decoding)
        type = PUTIMAGE_HACKS_IMAGE;
    else
        type = PUTIMAGE_HACKS_SURFACE;

    if (obj_surface->putimage_hacks &&
        obj_surface->putimage_hacks->type != type)
        putimage_hacks_disable(driver_data, obj_surface);

    VAStatus status;
    switch (type) {
    case PUTIMAGE_HACKS_NONE:
        status = VA_STATUS_SUCCESS;
        break;
    case PUTIMAGE_HACKS_SURFACE:
        if (!obj_context)
            return VA_STATUS_ERROR_INVALID_CONTEXT;
        // fall-through
    default:
        status = putimage_hacks_enable(
            driver_data,
            obj_surface,
            obj_image,
            type
        );
        break;
    }
    return status;
}

// vaQueryImageFormats
VAStatus
xvba_QueryImageFormats(
    VADriverContextP    ctx,
    VAImageFormat      *format_list,
    int                *num_formats
)
{
    XVBA_DRIVER_DATA_INIT;

    if (!format_list)
	return VA_STATUS_SUCCESS;

    int i, n = 0;
    for (i = 0; xvba_image_formats_map[i].format != 0; i++) {
        const xvba_image_format_map_t * const f = &xvba_image_formats_map[i];
        /* XXX: XvBA is really too stupid: the set of supported formats for
           XVBAGetSurface() and for XVBAUploadSurface() do NOT intersect... */
        if (1 || is_supported_format(driver_data, f->format))
            format_list[n++] = f->va_format;
    }

    /* If the assert fails then XVBA_MAX_IMAGE_FORMATS needs to be bigger */
    ASSERT(n <= XVBA_MAX_IMAGE_FORMATS);
    if (num_formats)
	*num_formats = n;

    return VA_STATUS_SUCCESS;
}

// Create image
object_image_p
create_image(
    xvba_driver_data_t *driver_data,
    unsigned int        width,
    unsigned int        height,
    VAImageFormat      *format
)
{
    VAImageID image_id = object_heap_allocate(&driver_data->image_heap);
    if (image_id == VA_INVALID_ID)
        return NULL;

    object_image_p obj_image = XVBA_IMAGE(image_id);
    if (!obj_image)
        return NULL;

    VAImage * const image = &obj_image->image;
    image->image_id       = image_id;
    image->buf            = VA_INVALID_ID;

    /* XXX: we align size to 16-pixel boundaries because the image may
       be used to retrieve XvBA surface pixels and this requires exact
       match of the dimensions. And since yet another stupid AMD bug
       requires 16-pixel alignment for surfaces... */
    unsigned int width2, height2, size2, awidth, aheight, size;
    awidth  = (width  + 15) & -16U;
    aheight = (height + 15) & -16U;
    size    = awidth * aheight;
    width2  = (awidth  + 1) / 2;
    height2 = (aheight + 1) / 2;
    size2   = width2 * height2;

    XVBA_SURFACE_FORMAT xvba_format;
    switch (format->fourcc) {
    case VA_FOURCC('N','V','1','2'):
        xvba_format       = XVBA_NV12;
        image->num_planes = 2;
        image->pitches[0] = awidth;
        image->offsets[0] = 0;
        image->pitches[1] = awidth;
        image->offsets[1] = size;
        image->data_size  = size + 2 * size2;
	break;
    case VA_FOURCC('I','4','2','0'):
        xvba_format       = XVBA_YV12;
        image->num_planes = 3;
        image->pitches[0] = awidth;
        image->offsets[0] = 0;
        image->pitches[1] = width2;
        image->offsets[1] = size + size2;
        image->pitches[2] = width2;
        image->offsets[2] = size;
        image->data_size  = size + 2 * size2;
        break;
    case VA_FOURCC('Y','V','1','2'):
        xvba_format       = XVBA_YV12;
        image->num_planes = 3;
        image->pitches[0] = awidth;
        image->offsets[0] = 0;
        image->pitches[1] = width2;
        image->offsets[1] = size;
        image->pitches[2] = width2;
        image->offsets[2] = size + size2;
        image->data_size  = size + 2 * size2;
        break;
    case VA_FOURCC('Y','U','Y','2'):
        xvba_format       = XVBA_YUY2;
        image->num_planes = 1;
        image->pitches[0] = awidth * 2;
        image->offsets[0] = 0;
        image->data_size  = image->pitches[0] * aheight;
        break;
    case VA_FOURCC('B','G','R','A'):
        if (format->bits_per_pixel != 32)
            goto error;
        xvba_format       = XVBA_ARGB;
        image->num_planes = 1;
        image->pitches[0] = awidth * 4;
        image->offsets[0] = 0;
        image->data_size  = image->pitches[0] * aheight;
        break;
    case VA_FOURCC('R','G','B','A'):
        if (format->bits_per_pixel != 32)
            goto error;
        xvba_format       = XVBA_NONE;
        image->num_planes = 1;
        image->pitches[0] = awidth * 4;
        image->offsets[0] = 0;
        image->data_size  = image->pitches[0] * aheight;
        break;
    default:
        goto error;
    }
    obj_image->xvba_format = xvba_format;
    obj_image->xvba_width  = awidth;
    obj_image->xvba_height = aheight;
    obj_image->hw.mtime    = 0;
    obj_image->hw.xvba     = NULL;
    obj_image->hw.glx      = NULL;

    object_buffer_p obj_buffer;
    obj_buffer = create_va_buffer(
        driver_data,
        VA_INVALID_ID,
        VAImageBufferType,
        1, image->data_size
    );
    if (!obj_buffer)
        goto error;

    image->image_id		= image_id;
    image->buf                  = obj_buffer->base.id;
    image->format		= *format;
    image->width		= width;
    image->height		= height;

    /* XXX: no paletted formats supported yet */
    image->num_palette_entries	= 0;
    image->entry_bytes		= 0;
    return obj_image;

error:
    destroy_image(driver_data, obj_image);
    return NULL;
}

// Destroy image
void
destroy_image(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image
)
{
    obj_image->image.image_id = VA_INVALID_ID;
    destroy_hw_image(driver_data, obj_image);
    destroy_va_buffer(driver_data, XVBA_BUFFER(obj_image->image.buf));
    object_heap_free(&driver_data->image_heap, (object_base_p)obj_image);
}

#if USE_GLX
const HWImageHooks hw_image_hooks_glx attribute_hidden;
#endif

// Commit image to the HW
VAStatus
commit_hw_image(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image,
    XVBASession        *session,
    unsigned int        flags
)
{
    VAStatus status;

    object_buffer_p const obj_buffer = XVBA_BUFFER(obj_image->image.buf);
    if (!obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

    /* Update video surface only if the image (hence its buffer) was
       updated since our last synchronisation.

       NOTE: this assumes the user really unmaps the buffer when he is
       done with it, as it is actually required */
    if (obj_image->hw.mtime < obj_buffer->mtime) {
#if USE_GLX
        if (flags & HWIMAGE_TYPE_GLX) {
            if (!obj_image->hw.glx) {
                ASSERT(hw_image_hooks_glx.create);
                status = hw_image_hooks_glx.create(
                    driver_data,
                    obj_image,
                    session
                );
                if (status != VA_STATUS_SUCCESS)
                    return status;
            }
            ASSERT(hw_image_hooks_glx.commit);
            status = hw_image_hooks_glx.commit(
                driver_data,
                obj_image,
                obj_buffer,
                session
            );
            if (status != VA_STATUS_SUCCESS)
                return status;
        }
#endif
        obj_image->hw.mtime = obj_buffer->mtime;
    }
    return VA_STATUS_SUCCESS;
}

// Destroy HW image 
void
destroy_hw_image(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image
)
{
    if (!obj_image)
        return;

#if USE_GLX
    if (obj_image->hw.glx) {
        if (hw_image_hooks_glx.destroy)
            hw_image_hooks_glx.destroy(driver_data, obj_image);
        obj_image->hw.glx = NULL;
    }
#endif
}

// Copy image
static VAStatus
copy_image(
    xvba_driver_data_t *driver_data,
    object_image_p      dst_obj_image,
    object_image_p      src_obj_image
)
{
    if (!compare_image_formats(&dst_obj_image->image.format,
                               &src_obj_image->image.format))
        return VA_STATUS_ERROR_UNKNOWN;

    object_buffer_p dst_obj_buffer = XVBA_BUFFER(dst_obj_image->image.buf);
    if (!dst_obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

    object_buffer_p src_obj_buffer = XVBA_BUFFER(src_obj_image->image.buf);
    if (!src_obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

    /* XXX: use map/unmap functions */
    ASSERT(dst_obj_image->image.data_size == src_obj_image->image.data_size);
    memcpy(
        dst_obj_buffer->buffer_data,
        src_obj_buffer->buffer_data,
        dst_obj_image->image.data_size
    );
    dst_obj_buffer->mtime = src_obj_buffer->mtime;
    return VA_STATUS_SUCCESS;
}

// vaCreateImage
VAStatus
xvba_CreateImage(
    VADriverContextP    ctx,
    VAImageFormat      *format,
    int                 width,
    int                 height,
    VAImage            *out_image
)
{
    XVBA_DRIVER_DATA_INIT;

    if (!format || !out_image)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    out_image->image_id = VA_INVALID_ID;
    out_image->buf      = VA_INVALID_ID;

    object_image_p obj_image;
    obj_image = create_image(driver_data, width, height, format);
    if (!obj_image)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    *out_image = obj_image->image;
    return VA_STATUS_SUCCESS;
}

// vaDestroyImage
VAStatus
xvba_DestroyImage(
    VADriverContextP    ctx,
    VAImageID           image_id
)
{
    XVBA_DRIVER_DATA_INIT;

    object_image_p obj_image = XVBA_IMAGE(image_id);
    if (!obj_image)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    destroy_image(driver_data, obj_image);
    return VA_STATUS_SUCCESS;
}

// vaDeriveImage
VAStatus
xvba_DeriveImage(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    VAImage             *image
)
{
    /* TODO */
    return VA_STATUS_ERROR_OPERATION_FAILED;
}

// vaSetImagePalette
VAStatus
xvba_SetImagePalette(
    VADriverContextP    ctx,
    VAImageID           image,
    unsigned char      *palette
)
{
    /* TODO */
    return VA_STATUS_ERROR_OPERATION_FAILED;
}

// Get image from surface
static VAStatus
get_image(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_surface_p    obj_surface,
    object_image_p      obj_image,
    const VARectangle  *rect
)
{
    /* XVBAGetSurface() API appeared in XvBA 0.74 */
    if (!xvba_check_version(0,74))
        return VA_STATUS_ERROR_OPERATION_FAILED;

    object_buffer_p obj_buffer = XVBA_BUFFER(obj_image->image.buf);
    if (!obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

    /* XXX: only support full surface readback for now */
    if (rect->x != 0 ||
        rect->y != 0 ||
        rect->width != obj_surface->width ||
        rect->height != obj_surface->height)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    /* Make sure the surface is decoded prior to extracting it */
    /* XXX: API doc mentions this as implicit in XVBAGetSurface() though... */
    if (sync_surface(driver_data, obj_context, obj_surface) < 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    /* XXX: stupid driver requires 16-pixels alignment */
    ASSERT(obj_surface->xvba_surface->type == XVBA_SURFACETYPE_NORMAL);
    if (obj_image->xvba_width != obj_surface->xvba_surface->info.normal.width ||
        obj_image->xvba_height != obj_surface->xvba_surface->info.normal.height)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    if (xvba_get_surface(obj_context->xvba_decoder,
                         obj_surface->xvba_surface,
                         obj_image->xvba_format,
                         obj_buffer->buffer_data,
                         obj_image->image.pitches[0],
                         obj_image->xvba_width,
                         obj_image->xvba_height) < 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    return VA_STATUS_SUCCESS;
}

// vaGetImage
VAStatus
xvba_GetImage(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    int                 x,
    int                 y,
    unsigned int        width,
    unsigned int        height,
    VAImageID           image
)
{
    XVBA_DRIVER_DATA_INIT;

    object_surface_p obj_surface = XVBA_SURFACE(surface);
    if (!obj_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    object_context_p obj_context = XVBA_CONTEXT(obj_surface->va_context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    object_image_p obj_image = XVBA_IMAGE(image);
    if (!obj_image)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    VARectangle rect;
    rect.x      = x;
    rect.y      = y;
    rect.width  = width;
    rect.height = height;
    return get_image(driver_data, obj_context, obj_surface, obj_image, &rect);
}

// Put image to surface
static VAStatus
put_image(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image,
    object_surface_p    obj_surface,
    const VARectangle  *src_rect,
    const VARectangle  *dst_rect
)
{
#if 0
    /* Don't do anything if the surface is used for rendering for example */
    if (obj_surface->va_surface_status != VASurfaceReady)
        return VA_STATUS_ERROR_SURFACE_BUSY;
#endif

    /* Only support upload to normal video surfaces */
    if (obj_surface->xvba_surface &&
        obj_surface->xvba_surface->type != XVBA_SURFACETYPE_NORMAL)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    /* Check we are not overriding the whole surface */
    if (dst_rect->x == 0 &&
        dst_rect->y == 0 &&
        dst_rect->width == obj_surface->width &&
        dst_rect->height == obj_surface->height) {
        obj_surface->used_for_decoding = 0;
        obj_surface->va_surface_status = VASurfaceReady;
    }

    /* XXX: XvBA does not have a real PutImage API. It requires
       image and surface to have the same dimensions... */
    if (obj_image->image.width != obj_surface->width  ||
        obj_image->image.height != obj_surface->height)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    /* XXX: XvBA implementation cannot do partial surface uploads, though
       the API permits it... Neither can it scale the data for us */
    if (src_rect->x != 0 ||
        src_rect->y != 0 ||
        src_rect->width  != obj_image->image.width ||
        src_rect->height != obj_image->image.height)
         return VA_STATUS_ERROR_OPERATION_FAILED;
    if (dst_rect->x != 0 ||
        dst_rect->y != 0 ||
        dst_rect->width  != obj_surface->width ||
        dst_rect->height != obj_surface->height)
        return VA_STATUS_ERROR_OPERATION_FAILED;
    if (src_rect->width != dst_rect->width ||
        src_rect->height != dst_rect->height)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    VAStatus status = putimage_hacks_check(driver_data, obj_surface, obj_image);
    if (status != VA_STATUS_SUCCESS)
        return status;

    PutImageHacks * const h = obj_surface->putimage_hacks;
    if (h && h->type == PUTIMAGE_HACKS_IMAGE) {
        status = copy_image(
            driver_data,
            obj_surface->putimage_hacks->obj_image,
            obj_image
        );
        return status;
    }
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

// vaPutImage
VAStatus
xvba_PutImage(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    VAImageID           image,
    int                 src_x,
    int                 src_y,
    unsigned int        width,
    unsigned int        height,
    int                 dest_x,
    int                 dest_y
)
{
    XVBA_DRIVER_DATA_INIT;

    object_surface_p obj_surface = XVBA_SURFACE(surface);
    if (!obj_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    object_context_p obj_context = XVBA_CONTEXT(obj_surface->va_context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    object_image_p obj_image = XVBA_IMAGE(image);
    if (!obj_image)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    VARectangle src_rect, dst_rect;
    src_rect.x      = src_x;
    src_rect.y      = src_y;
    src_rect.width  = width;
    src_rect.height = height;
    dst_rect.x      = dest_x;
    dst_rect.y      = dest_y;
    dst_rect.width  = width;
    dst_rect.height = height;
    return put_image(driver_data, obj_image, obj_surface, &src_rect, &dst_rect);
}

// vaPutImage2
VAStatus
xvba_PutImage_full(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    VAImageID           image,
    int                 src_x,
    int                 src_y,
    unsigned int        src_width,
    unsigned int        src_height,
    int                 dest_x,
    int                 dest_y,
    unsigned int        dest_width,
    unsigned int        dest_height
)
{
    XVBA_DRIVER_DATA_INIT;

    object_surface_p obj_surface = XVBA_SURFACE(surface);
    if (!obj_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    object_image_p obj_image = XVBA_IMAGE(image);
    if (!obj_image)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    VARectangle src_rect, dst_rect;
    src_rect.x      = src_x;
    src_rect.y      = src_y;
    src_rect.width  = src_width;
    src_rect.height = src_height;
    dst_rect.x      = dest_x;
    dst_rect.y      = dest_y;
    dst_rect.width  = dest_width;
    dst_rect.height = dest_height;
    return put_image(driver_data, obj_image, obj_surface, &src_rect, &dst_rect);
}
