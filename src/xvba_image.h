/*
 *  xvba_image.h - XvBA backend for VA-API (VAImage)
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

#ifndef XVBA_IMAGE_H
#define XVBA_IMAGE_H

#include "xvba_driver.h"

typedef struct object_image_glx  *object_image_glx_p;
typedef struct object_image_xvba *object_image_xvba_p;
struct object_image_xvba {
    XVBASurface        *surface;
    unsigned int        width;
    unsigned int        height;
};

enum {
    HWIMAGE_TYPE_XVBA = 1,
    HWIMAGE_TYPE_GLX  = 2
};

typedef struct _HWImageHooks HWImageHooks;
struct _HWImageHooks {
    VAStatus (*create)(
        xvba_driver_data_t      *driver_data,
        object_image_p           obj_image,
        XVBASession             *session
    );

    void (*destroy)(
        xvba_driver_data_t     *driver_data,
        object_image_p          obj_image
    );

    VAStatus (*commit)(
        xvba_driver_data_t      *driver_data,
        object_image_p           obj_image,
        object_buffer_p          obj_buffer,
        XVBASession             *session
    );
};

typedef struct _HWImage HWImage;
struct _HWImage {
    uint64_t            mtime;
    object_image_xvba_p xvba;
    object_image_glx_p  glx;
};

typedef struct object_image object_image_t;
struct object_image {
    struct object_base  base;
    VAImage             image;
    XVBA_SURFACE_FORMAT xvba_format;
    unsigned int        xvba_width;
    unsigned int        xvba_height;
    HWImage             hw;
};

typedef struct GetImageHacks {
    unsigned int        picnum; /* Current index into delayed_pictures[] */
} GetImageHacks;

typedef enum {
    PUTIMAGE_HACKS_NONE,
    PUTIMAGE_HACKS_SURFACE,
    PUTIMAGE_HACKS_IMAGE
} PutImageHacksType;

typedef struct PutImageHacks {
    PutImageHacksType   type;
    XVBASurface        *xvba_surface;
    object_image_p      obj_image;
} PutImageHacks;

void
getimage_hacks_disable(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context
) attribute_hidden;

void
putimage_hacks_disable(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface
) attribute_hidden;

// Check VA image format represents RGB
int is_rgb_format(const VAImageFormat *image_format)
    attribute_hidden;

// Check VA image format represents YUV
static inline int is_yuv_format(const VAImageFormat *image_format)
{
    return !is_rgb_format(image_format);
}

// Check VA image formats are the same
int compare_image_formats(const VAImageFormat *a, const VAImageFormat *b)
    attribute_hidden;

// Create image
object_image_p
create_image(
    xvba_driver_data_t *driver_data,
    unsigned int        width,
    unsigned int        height,
    VAImageFormat      *format
) attribute_hidden;

// Destroy image
void
destroy_image(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image
) attribute_hidden;

// Destroy HW image 
void
destroy_hw_image(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image
) attribute_hidden;

// Commit image to the HW
VAStatus
commit_hw_image(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image,
    XVBASession        *session,
    unsigned int        flags
) attribute_hidden;

// vaQueryImageFormats
VAStatus
xvba_QueryImageFormats(
    VADriverContextP    ctx,
    VAImageFormat      *format_list,
    int                *num_formats
) attribute_hidden;

// vaCreateImage
VAStatus
xvba_CreateImage(
    VADriverContextP    ctx,
    VAImageFormat      *format,
    int                 width,
    int                 height,
    VAImage            *image
) attribute_hidden;

// vaDestroyImage
VAStatus
xvba_DestroyImage(
    VADriverContextP    ctx,
    VAImageID           image_id
) attribute_hidden;

// vaDeriveImage
VAStatus
xvba_DeriveImage(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    VAImage            *image
) attribute_hidden;

// vaSetImagePalette
VAStatus
xvba_SetImagePalette(
    VADriverContextP    ctx,
    VAImageID           image,
    unsigned char      *palette
) attribute_hidden;

// vaGetImage
VAStatus
xvba_GetImage(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    int                 x,
    int                 y,
    unsigned int        width,
    unsigned int        height,
    VAImageID           image_id
) attribute_hidden;

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
) attribute_hidden;

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
) attribute_hidden;

#endif /* XVBA_IMAGE_H */
