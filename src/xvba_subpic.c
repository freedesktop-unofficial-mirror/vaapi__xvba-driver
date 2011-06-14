/*
 *  xvba_subpic.c - XvBA backend for VA-API (VA subpictures)
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
#include "xvba_subpic.h"
#include "xvba_video.h"
#include "xvba_image.h"
#include "xvba_buffer.h"
#include "utils.h"

#define DEBUG 1
#include "debug.h"


// List of supported subpicture formats
typedef struct {
    uint32_t      format;
    VAImageFormat va_format;
    unsigned int  va_flags;
} xvba_subpic_format_map_t;

static const xvba_subpic_format_map_t xvba_subpic_formats_map[] = {
    { XVBA_ARGB, { VA_FOURCC('B','G','R','A'), VA_LSB_FIRST, 32,
                   32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000 },
      VA_SUBPICTURE_GLOBAL_ALPHA },
    { XVBA_FAKE, { VA_FOURCC('R','G','B','A'), VA_LSB_FIRST, 32,
                   32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000 },
      VA_SUBPICTURE_GLOBAL_ALPHA },
    { 0, }
};

// Append association to the subpicture
static int
subpicture_add_association(
    object_subpicture_p    obj_subpicture,
    SubpictureAssociationP assoc
)
{
    SubpictureAssociationP *assocs;
    assocs = realloc_buffer(&obj_subpicture->assocs,
                            &obj_subpicture->assocs_count_max,
                            1 + obj_subpicture->assocs_count,
                            sizeof(obj_subpicture->assocs[0]));
    if (!assocs)
        return -1;

    assocs[obj_subpicture->assocs_count++] = assoc;
    return 0;
}

// Remove association at the specified index from the subpicture
static inline int
subpicture_remove_association_at(object_subpicture_p obj_subpicture, int index)
{
    ASSERT(obj_subpicture->assocs && obj_subpicture->assocs_count > 0);
    if (!obj_subpicture->assocs || obj_subpicture->assocs_count == 0)
        return -1;

    /* Replace with the last association */
    const unsigned int last = obj_subpicture->assocs_count - 1;
    obj_subpicture->assocs[index] = obj_subpicture->assocs[last];
    obj_subpicture->assocs[last] = NULL;
    --obj_subpicture->assocs_count;
    return 0;
}

// Remove association from the subpicture
static int
subpicture_remove_association(
    object_subpicture_p    obj_subpicture,
    SubpictureAssociationP assoc
)
{
    ASSERT(obj_subpicture->assocs && obj_subpicture->assocs_count > 0);
    if (!obj_subpicture->assocs || obj_subpicture->assocs_count == 0)
        return -1;

    unsigned int i;
    for (i = 0; i < obj_subpicture->assocs_count; i++) {
        if (obj_subpicture->assocs[i] == assoc)
            return subpicture_remove_association_at(obj_subpicture, i);
    }
    return -1;
}

// Associate one surface to the subpicture
VAStatus
subpicture_associate_1(
    object_subpicture_p obj_subpicture,
    object_surface_p    obj_surface,
    const VARectangle  *src_rect,
    const VARectangle  *dst_rect,
    unsigned int        flags
)
{
    /* XXX: some flags are not supported */
    static const unsigned int supported_flags = VA_SUBPICTURE_GLOBAL_ALPHA;
    if (flags & ~supported_flags)
        return VA_STATUS_ERROR_FLAG_NOT_SUPPORTED;

    SubpictureAssociationP assoc = malloc(sizeof(*assoc));
    if (!assoc)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    assoc->subpicture = obj_subpicture->base.id;
    assoc->surface    = obj_surface->base.id;
    assoc->src_rect   = *src_rect;
    assoc->dst_rect   = *dst_rect;
    assoc->flags      = flags;

    if (surface_add_association(obj_surface, assoc) < 0) {
        free(assoc);
        return VA_STATUS_ERROR_UNKNOWN;
    }

    if (subpicture_add_association(obj_subpicture, assoc) < 0) {
        surface_remove_association(obj_surface, assoc);
        free(assoc);
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }
    return VA_STATUS_SUCCESS;
}

// Associate surfaces to the subpicture
static VAStatus
associate_subpicture(
    xvba_driver_data_t *driver_data,
    object_subpicture_p obj_subpicture,
    VASurfaceID        *surfaces,
    unsigned int        num_surfaces,
    const VARectangle  *src_rect,
    const VARectangle  *dst_rect,
    unsigned int        flags
)
{
    VAStatus status;
    unsigned int i;

    object_image_p obj_image = XVBA_IMAGE(obj_subpicture->image_id);
    if (!obj_image)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    /* Check subpicture association bounds */
    if (src_rect->x < 0 ||
        src_rect->y < 0 ||
        src_rect->x + src_rect->width > obj_image->image.width ||
        src_rect->y + src_rect->height > obj_image->image.height)
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    if (dst_rect->x < 0 ||
        dst_rect->y < 0)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    for (i = 0; i < num_surfaces; i++) {
        object_surface_p const obj_surface = XVBA_SURFACE(surfaces[i]);
        if (!obj_surface)
            return VA_STATUS_ERROR_INVALID_SURFACE;

        if (dst_rect->x + dst_rect->width > obj_surface->width ||
            dst_rect->y + dst_rect->height > obj_surface->height)
            return VA_STATUS_ERROR_INVALID_PARAMETER;

        status = subpicture_associate_1(
            obj_subpicture,
            obj_surface,
            src_rect,
            dst_rect,
            flags
        );
        if (status != VA_STATUS_SUCCESS)
            return status;
    }
    return VA_STATUS_SUCCESS;
}

// Deassociate one surface from the subpicture
VAStatus
subpicture_deassociate_1(
    object_subpicture_p obj_subpicture,
    object_surface_p    obj_surface
)
{
    ASSERT(obj_subpicture->assocs && obj_subpicture->assocs_count > 0);
    if (!obj_subpicture->assocs || obj_subpicture->assocs_count == 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    unsigned int i;
    for (i = 0; i < obj_subpicture->assocs_count; i++) {
        SubpictureAssociationP const assoc = obj_subpicture->assocs[i];
        ASSERT(assoc);
        if (assoc && assoc->surface == obj_surface->base.id) {
            surface_remove_association(obj_surface, assoc);
            subpicture_remove_association_at(obj_subpicture, i);
            free(assoc);
            return VA_STATUS_SUCCESS;
        }
    }
    return VA_STATUS_ERROR_OPERATION_FAILED;
}

// Deassociate surfaces from the subpicture
static VAStatus
deassociate_subpicture(
    xvba_driver_data_t *driver_data,
    object_subpicture_p obj_subpicture,
    VASurfaceID        *surfaces,
    unsigned int        num_surfaces
)
{
    VAStatus status, error = VA_STATUS_SUCCESS;
    unsigned int i;

    for (i = 0; i < num_surfaces; i++) {
        object_surface_p const obj_surface = XVBA_SURFACE(surfaces[i]);
        if (!obj_surface)
            return VA_STATUS_ERROR_INVALID_SURFACE;
        status = subpicture_deassociate_1(obj_subpicture, obj_surface);
        if (status != VA_STATUS_SUCCESS) {
            /* Simply report the first error to the user */
            if (error == VA_STATUS_SUCCESS)
                error = status;
        }
    }
    return error;
}

// Commit subpicture to video surface
VAStatus
commit_subpicture(
    xvba_driver_data_t *driver_data,
    object_subpicture_p obj_subpicture,
    void               *session,
    unsigned int        flags
)
{
    object_image_p obj_image = XVBA_IMAGE(obj_subpicture->image_id);
    if (!obj_image)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    return commit_hw_image(driver_data, obj_image, session, flags);
}

// Create subpicture with image
static object_subpicture_p
create_subpicture(
    xvba_driver_data_t *driver_data,
    object_image_p      obj_image
)
{
    VASubpictureID subpic_id;
    subpic_id = object_heap_allocate(&driver_data->subpicture_heap);
    if (subpic_id == VA_INVALID_ID)
        return NULL;

    object_subpicture_p obj_subpicture = XVBA_SUBPICTURE(subpic_id);
    ASSERT(obj_subpicture);
    if (!obj_subpicture)
        return NULL;

    obj_subpicture->image_id            = obj_image->base.id;
    obj_subpicture->assocs              = NULL;
    obj_subpicture->assocs_count        = 0;
    obj_subpicture->assocs_count_max    = 0;
    obj_subpicture->chromakey_min       = 0;
    obj_subpicture->chromakey_max       = 0;
    obj_subpicture->chromakey_mask      = 0;
    obj_subpicture->alpha               = 1.0f;
    return obj_subpicture;
}

// Destroy XvBA surface bound to VA subpicture
void
destroy_subpicture_surface(
    xvba_driver_data_t *driver_data,
    object_subpicture_p obj_subpicture
)
{
    if (!obj_subpicture)
        return;

    object_image_p const obj_image = XVBA_IMAGE(obj_subpicture->image_id);
    if (obj_image)
        destroy_hw_image(driver_data, obj_image);
}

// Destroy subpicture
static void
destroy_subpicture(
    xvba_driver_data_t *driver_data,
    object_subpicture_p obj_subpicture
)
{
    object_surface_p obj_surface;
    VAStatus status;
    unsigned int i, n;

    destroy_subpicture_surface(driver_data, obj_subpicture);

    if (obj_subpicture->assocs) {
        const unsigned int n_assocs = obj_subpicture->assocs_count;
        for (i = 0, n = 0; i < n_assocs; i++) {
            SubpictureAssociationP const assoc = obj_subpicture->assocs[0];
            if (!assoc)
                continue;
            obj_surface = XVBA_SURFACE(assoc->surface);
            ASSERT(obj_surface);
            if (!obj_surface)
                continue;
            status = subpicture_deassociate_1(obj_subpicture, obj_surface);
            if (status == VA_STATUS_SUCCESS)
                ++n;
        }
        if (n != n_assocs)
            xvba_error_message("vaDestroySubpicture(): subpicture 0x%08x still "
                               "has %d surfaces associated to it\n",
                               obj_subpicture->base.id, n_assocs - n);
        free(obj_subpicture->assocs);
        obj_subpicture->assocs = NULL;
    }
    obj_subpicture->assocs_count = 0;
    obj_subpicture->assocs_count_max = 0;

    obj_subpicture->image_id = VA_INVALID_ID;
    object_heap_free(&driver_data->subpicture_heap,
                     (object_base_p)obj_subpicture);
}

// vaQuerySubpictureFormats
VAStatus
xvba_QuerySubpictureFormats(
    VADriverContextP    ctx,
    VAImageFormat      *format_list,
    unsigned int       *flags,
    unsigned int       *num_formats
)
{
    int n;

    for (n = 0; xvba_subpic_formats_map[n].format != 0; n++) {
        if (format_list)
            format_list[n] = xvba_subpic_formats_map[n].va_format;
        if (flags)
            flags[n] = xvba_subpic_formats_map[n].va_flags;
    }

    if (num_formats)
        *num_formats = n;

    return VA_STATUS_SUCCESS;
}

// vaCreateSubpicture
VAStatus
xvba_CreateSubpicture(
    VADriverContextP    ctx,
    VAImageID           image,
    VASubpictureID     *subpicture
)
{
    XVBA_DRIVER_DATA_INIT;

    if (!subpicture)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    object_image_p obj_image = XVBA_IMAGE(image);
    if (!obj_image)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    object_subpicture_p obj_subpicture;
    obj_subpicture = create_subpicture(driver_data, obj_image);
    if (!obj_subpicture)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    *subpicture = obj_subpicture->base.id;
    return VA_STATUS_SUCCESS;
}

// vaDestroySubpicture
VAStatus
xvba_DestroySubpicture(
    VADriverContextP    ctx,
    VASubpictureID      subpicture
)
{
    XVBA_DRIVER_DATA_INIT;

    object_subpicture_p obj_subpicture = XVBA_SUBPICTURE(subpicture);
    if (!obj_subpicture)
        return VA_STATUS_ERROR_INVALID_SUBPICTURE;

    destroy_subpicture(driver_data, obj_subpicture);
    return VA_STATUS_SUCCESS;
}

// vaSetSubpictureImage
VAStatus
xvba_SetSubpictureImage(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    VAImageID           image
)
{
    XVBA_DRIVER_DATA_INIT;

    object_subpicture_p obj_subpicture = XVBA_SUBPICTURE(subpicture);
    if (!obj_subpicture)
        return VA_STATUS_ERROR_INVALID_SUBPICTURE;

    object_image_p obj_image = XVBA_IMAGE(image);
    if (!obj_image)
        return VA_STATUS_ERROR_INVALID_IMAGE;

    obj_subpicture->image_id = obj_image->base.id;
    return VA_STATUS_SUCCESS;
}

// vaSetSubpicturePalette (not a PUBLIC interface)
VAStatus
xvba_SetSubpicturePalette(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    unsigned char      *palette
)
{
    /* TODO */
    return VA_STATUS_ERROR_OPERATION_FAILED;
}

// vaSetSubpictureChromaKey
VAStatus
xvba_SetSubpictureChromakey(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    unsigned int        chromakey_min,
    unsigned int        chromakey_max,
    unsigned int        chromakey_mask
)
{
    XVBA_DRIVER_DATA_INIT;

    object_subpicture_p obj_subpicture = XVBA_SUBPICTURE(subpicture);
    if (!obj_subpicture)
        return VA_STATUS_ERROR_INVALID_SUBPICTURE;

    obj_subpicture->chromakey_min  = chromakey_min;
    obj_subpicture->chromakey_max  = chromakey_max;
    obj_subpicture->chromakey_mask = chromakey_mask;
    return VA_STATUS_SUCCESS;
}

// vaSetSubpictureGlobalAlpha
VAStatus
xvba_SetSubpictureGlobalAlpha(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    float               global_alpha
)
{
    XVBA_DRIVER_DATA_INIT;

    object_subpicture_p obj_subpicture = XVBA_SUBPICTURE(subpicture);
    if (!obj_subpicture)
        return VA_STATUS_ERROR_INVALID_SUBPICTURE;

    obj_subpicture->alpha = global_alpha;
    return VA_STATUS_SUCCESS;
}

// vaAssociateSubpicture
VAStatus
xvba_AssociateSubpicture(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    VASurfaceID        *target_surfaces,
    int                 num_surfaces,
    short               src_x,
    short               src_y,
    short               dest_x,
    short               dest_y,
    unsigned short      width,
    unsigned short      height,
    unsigned int        flags
)
{
    XVBA_DRIVER_DATA_INIT;

    if (!target_surfaces || num_surfaces == 0)
        return VA_STATUS_SUCCESS;

    object_subpicture_p obj_subpicture = XVBA_SUBPICTURE(subpicture);
    if (!obj_subpicture)
        return VA_STATUS_ERROR_INVALID_SUBPICTURE;

    VARectangle src_rect, dst_rect;
    src_rect.x      = src_x;
    src_rect.y      = src_y;
    src_rect.width  = width;
    src_rect.height = height;
    dst_rect.x      = dest_x;
    dst_rect.y      = dest_y;
    dst_rect.width  = width;
    dst_rect.height = height;
    return associate_subpicture(driver_data, obj_subpicture,
                                target_surfaces, num_surfaces,
                                &src_rect, &dst_rect, flags);
}

// vaAssociateSubpicture2
VAStatus
xvba_AssociateSubpicture_full(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    VASurfaceID        *target_surfaces,
    int                 num_surfaces,
    short               src_x,
    short               src_y,
    unsigned short      src_width,
    unsigned short      src_height,
    short               dest_x,
    short               dest_y,
    unsigned short      dest_width,
    unsigned short      dest_height,
    unsigned int        flags
)
{
    XVBA_DRIVER_DATA_INIT;

    if (!target_surfaces || num_surfaces == 0)
        return VA_STATUS_SUCCESS;

    object_subpicture_p obj_subpicture = XVBA_SUBPICTURE(subpicture);
    if (!obj_subpicture)
        return VA_STATUS_ERROR_INVALID_SUBPICTURE;

    VARectangle src_rect, dst_rect;
    src_rect.x      = src_x;
    src_rect.y      = src_y;
    src_rect.width  = src_width;
    src_rect.height = src_height;
    dst_rect.x      = dest_x;
    dst_rect.y      = dest_y;
    dst_rect.width  = dest_width;
    dst_rect.height = dest_height;
    return associate_subpicture(driver_data, obj_subpicture,
                                target_surfaces, num_surfaces,
                                &src_rect, &dst_rect, flags);
}

// vaDeassociateSubpicture
VAStatus
xvba_DeassociateSubpicture(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    VASurfaceID        *target_surfaces,
    int                 num_surfaces
)
{
    XVBA_DRIVER_DATA_INIT;

    if (!target_surfaces || num_surfaces == 0)
        return VA_STATUS_SUCCESS;

    object_subpicture_p obj_subpicture = XVBA_SUBPICTURE(subpicture);
    if (!obj_subpicture)
        return VA_STATUS_ERROR_INVALID_SUBPICTURE;

    return deassociate_subpicture(driver_data, obj_subpicture,
                                  target_surfaces, num_surfaces);
}
