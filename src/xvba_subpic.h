/*
 *  xvba_subpic.h - XvBA backend for VA-API (VA subpictures)
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

#ifndef XVBA_SUBPIC_H
#define XVBA_SUBPIC_H

#include "xvba_video.h"

typedef struct object_subpicture  object_subpicture_t;
typedef struct object_subpicture *object_subpicture_p;

struct object_subpicture {
    struct object_base  base;
    VAImageID           image_id;
    SubpictureAssociationP *assocs;
    unsigned int        assocs_count;
    unsigned int        assocs_count_max;
    unsigned int        chromakey_min;
    unsigned int        chromakey_max;
    unsigned int        chromakey_mask;
    float               alpha;
};

// Destroy XvBA surface bound to VA subpictures
void
destroy_subpicture_surface(
    xvba_driver_data_t *driver_data,
    object_subpicture_p obj_subpicture
) attribute_hidden;

// Associate one surface to the subpicture
VAStatus
subpicture_associate_1(
    object_subpicture_p obj_subpicture,
    object_surface_p    obj_surface,
    const VARectangle  *src_rect,
    const VARectangle  *dst_rect,
    unsigned int        flags
) attribute_hidden;

// Deassociate one surface from the subpicture
VAStatus
subpicture_deassociate_1(
    object_subpicture_p obj_subpicture,
    object_surface_p    obj_surface
) attribute_hidden;

// Commit subpicture to video surface
VAStatus
commit_subpicture(
    xvba_driver_data_t *driver_data,
    object_subpicture_p obj_subpicture,
    void               *session,
    unsigned int        flags
) attribute_hidden;

// vaQuerySubpictureFormats
VAStatus
xvba_QuerySubpictureFormats(
    VADriverContextP    ctx,
    VAImageFormat      *format_list,
    unsigned int       *flags,
    unsigned int       *num_formats
) attribute_hidden;

// vaCreateSubpicture
VAStatus
xvba_CreateSubpicture(
    VADriverContextP    ctx,
    VAImageID           image,
    VASubpictureID     *subpicture
) attribute_hidden;

// vaDestroySubpicture
VAStatus
xvba_DestroySubpicture(
    VADriverContextP    ctx,
    VASubpictureID      subpicture
) attribute_hidden;

// vaSetSubpictureImage
VAStatus
xvba_SetSubpictureImage(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    VAImageID           image
) attribute_hidden;

// vaSetSubpicturePalette (not a PUBLIC interface)
VAStatus
xvba_SetSubpicturePalette(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    unsigned char      *palette
) attribute_hidden;

// vaSetSubpictureChromaKey
VAStatus
xvba_SetSubpictureChromakey(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    unsigned int        chromakey_min,
    unsigned int        chromakey_max,
    unsigned int        chromakey_mask
) attribute_hidden;

// vaSetSubpictureGlobalAlpha
VAStatus
xvba_SetSubpictureGlobalAlpha(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    float               global_alpha
) attribute_hidden;

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
) attribute_hidden;

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
) attribute_hidden;

// vaDeassociateSubpicture
VAStatus
xvba_DeassociateSubpicture(
    VADriverContextP    ctx,
    VASubpictureID      subpicture,
    VASurfaceID        *target_surfaces,
    int                 num_surfaces
) attribute_hidden;

#endif /* XVBA_SUBPIC_H */
