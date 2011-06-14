/*
 *  xvba_video.h - XvBA backend for VA-API (VA context, config, surfaces)
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

#ifndef XVBA_VIDEO_H
#define XVBA_VIDEO_H

#include "xvba_driver.h"

/* Define wait delay (in microseconds) between two XVBASyncSurface() calls */
#define XVBA_SYNC_DELAY 10

typedef enum {
    XVBA_CODEC_MPEG1 = 1,
    XVBA_CODEC_MPEG2,
    XVBA_CODEC_MPEG4,
    XVBA_CODEC_H264,
    XVBA_CODEC_VC1
} XVBACodec;

typedef struct SubpictureAssociation *SubpictureAssociationP;
struct SubpictureAssociation {
    VASubpictureID              subpicture;
    VASurfaceID                 surface;
    VARectangle                 src_rect;
    VARectangle                 dst_rect;
    unsigned int                flags;
};

typedef struct object_config object_config_t;
struct object_config {
    struct object_base          base;
    VAProfile                   profile;
    VAEntrypoint                entrypoint;
    VAConfigAttrib              attrib_list[XVBA_MAX_CONFIG_ATTRIBUTES];
    unsigned int                attrib_count;
};

typedef struct object_context object_context_t;
struct object_context {
    struct object_base          base;
    VAConfigID                  va_config;
    unsigned int                picture_width;
    unsigned int                picture_height;
    unsigned int                flags;
    unsigned int                num_render_targets;
    VASurfaceID                *render_targets;
    VASurfaceID                 current_render_target;
    XVBACodec                   xvba_codec;
    XVBASession                *xvba_session;
    XVBASession                *xvba_decoder;
    VABufferID                 *va_buffers;
    unsigned int                va_buffers_count;
    unsigned int                va_buffers_count_max;

    /* Temporary data */
    void                       *data_buffer;    /* commit_picture() */
    unsigned int                slice_count;    /* commit_picture() */
};

typedef struct object_surface object_surface_t;
struct object_surface {
    struct object_base          base;
    VAContextID                 va_context;
    VASurfaceID                 va_surface_status;
    XVBASurface                *xvba_surface;
    unsigned int                xvba_surface_width;
    unsigned int                xvba_surface_height;
    object_output_p            *output_surfaces;
    unsigned int                output_surfaces_count;
    unsigned int                output_surfaces_count_max;
    unsigned int                width;
    unsigned int                height;
    struct object_glx_surface  *gl_surface;
    XVBABufferDescriptor       *pic_desc_buffer;
    XVBABufferDescriptor       *iq_matrix_buffer;
    XVBABufferDescriptor       *data_buffer;
    XVBABufferDescriptor      **data_ctrl_buffers;
    unsigned int                data_ctrl_buffers_count;
    unsigned int                data_ctrl_buffers_count_max;
    SubpictureAssociationP     *assocs;
    unsigned int                assocs_count;
    unsigned int                assocs_count_max;
    struct PutImageHacks       *putimage_hacks; /* vaPutImage() hacks */
    unsigned int                used_for_decoding : 1;
};

// Add subpicture association to surface
// NOTE: the subpicture owns the SubpictureAssociation object
int surface_add_association(
    object_surface_p            obj_surface,
    SubpictureAssociationP      assoc
) attribute_hidden;

// Remove subpicture association from surface
// NOTE: the subpicture owns the SubpictureAssociation object
int surface_remove_association(
    object_surface_p            obj_surface,
    SubpictureAssociationP      assoc
) attribute_hidden;

// Query surface status
int
query_surface_status(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_surface_p    obj_surface,
    VASurfaceStatus    *surface_status
) attribute_hidden;

// Synchronize surface
int
sync_surface(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_surface_p    obj_surface
) attribute_hidden;

// vaGetConfigAttributes
VAStatus
xvba_GetConfigAttributes(
    VADriverContextP    ctx,
    VAProfile           profile,
    VAEntrypoint        entrypoint,
    VAConfigAttrib     *attrib_list,
    int                 num_attribs
) attribute_hidden;

// vaCreateConfig
VAStatus
xvba_CreateConfig(
    VADriverContextP    ctx,
    VAProfile           profile,
    VAEntrypoint        entrypoint,
    VAConfigAttrib     *attrib_list,
    int                 num_attribs,
    VAConfigID         *config_id
) attribute_hidden;

// vaDestroyConfig
VAStatus
xvba_DestroyConfig(
    VADriverContextP    ctx,
    VAConfigID          config_id
) attribute_hidden;

// vaQueryConfigAttributes
VAStatus
xvba_QueryConfigAttributes(
    VADriverContextP    ctx,
    VAConfigID          config_id,
    VAProfile          *profile,
    VAEntrypoint       *entrypoint,
    VAConfigAttrib     *attrib_list,
    int                *num_attribs
) attribute_hidden;

// vaCreateSurfaces
VAStatus
xvba_CreateSurfaces(
    VADriverContextP    ctx,
    int                 width,
    int                 height,
    int                 format,
    int                 num_surfaces,
    VASurfaceID        *surfaces
) attribute_hidden;

// vaDestroySurfaces
VAStatus
xvba_DestroySurfaces(
    VADriverContextP    ctx,
    VASurfaceID        *surface_list,
    int                 num_surfaces
) attribute_hidden;

// vaCreateContext
VAStatus
xvba_CreateContext(
    VADriverContextP    ctx,
    VAConfigID          config_id,
    int                 picture_width,
    int                 picture_height,
    int                 flag,
    VASurfaceID        *render_targets,
    int                 num_render_targets,
    VAContextID        *context
) attribute_hidden;

// vaDestroyContext
VAStatus
xvba_DestroyContext(
    VADriverContextP    ctx,
    VAContextID         context
) attribute_hidden;

// vaQuerySurfaceStatus
VAStatus
xvba_QuerySurfaceStatus(
    VADriverContextP    ctx,
    VASurfaceID         render_target,
    VASurfaceStatus    *status
) attribute_hidden;

// vaSyncSurface 2-args variant (>= 0.31)
VAStatus
xvba_SyncSurface2(
    VADriverContextP    ctx,
    VASurfaceID         render_target
) attribute_hidden;

// vaSyncSurface 3-args variant (<= 0.30)
VAStatus
xvba_SyncSurface3(
    VADriverContextP    ctx,
    VAContextID         context,
    VASurfaceID         render_target
) attribute_hidden;

// vaQueryDisplayAttributes
VAStatus
xvba_QueryDisplayAttributes(
    VADriverContextP    ctx,
    VADisplayAttribute *attr_list,
    int                *num_attributes
) attribute_hidden;

// vaGetDisplayAttributes
VAStatus
xvba_GetDisplayAttributes(
    VADriverContextP    ctx,
    VADisplayAttribute *attr_list,
    int                 num_attributes
) attribute_hidden;

// vaSetDisplayAttributes
VAStatus
xvba_SetDisplayAttributes(
    VADriverContextP    ctx,
    VADisplayAttribute *attr_list,
    int                 num_attributes
) attribute_hidden;

// vaDbgCopySurfaceToBuffer (not a PUBLIC interface)
VAStatus
xvba_DbgCopySurfaceToBuffer(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    void              **buffer,
    unsigned int       *stride
) attribute_hidden;

#if VA_CHECK_VERSION(0,30,0)
// vaCreateSurfaceFromCIFrame
VAStatus
xvba_CreateSurfaceFromCIFrame(
    VADriverContextP    ctx,
    unsigned long       frame_id,
    VASurfaceID        *surface
) attribute_hidden;

// vaCreateSurfaceFromV4L2Buf
VAStatus
xvba_CreateSurfaceFromV4L2Buf(
    VADriverContextP    ctx,
    int                 v4l2_fd,
    struct v4l2_format *v4l2_fmt,
    struct v4l2_buffer *v4l2_buf,
    VASurfaceID        *surface
) attribute_hidden;

// vaCopySurfaceToBuffer
VAStatus
xvba_CopySurfaceToBuffer(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    unsigned int       *fourcc,
    unsigned int       *luma_stride,
    unsigned int       *chroma_u_stride,
    unsigned int       *chroma_v_stride,
    unsigned int       *luma_offset,
    unsigned int       *chroma_u_offset,
    unsigned int       *chroma_v_offset,
    void              **buffer
) attribute_hidden;
#endif

#if VA_CHECK_VERSION(0,31,1)
// vaLockSurface
VAStatus
xvba_LockSurface(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    unsigned int       *fourcc,
    unsigned int       *luma_stride,
    unsigned int       *chroma_u_stride,
    unsigned int       *chroma_v_stride,
    unsigned int       *luma_offset,
    unsigned int       *chroma_u_offset,
    unsigned int       *chroma_v_offset,
    unsigned int       *buffer_name,
    void              **buffer
) attribute_hidden;

// vaUnlockSurface
VAStatus
xvba_UnlockSurface(
    VADriverContextP    ctx,
    VASurfaceID         surface
) attribute_hidden;
#endif

#endif /* XVBA_VIDEO_H */
