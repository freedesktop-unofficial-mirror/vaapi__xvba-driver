/*
 *  xvba_decode.h - XvBA backend for VA-API (decoding)
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

#ifndef XVBA_DECODE_H
#define XVBA_DECODE_H

#include "xvba_driver.h"

// Checks decoder for profile/entrypoint is available
int
has_decoder(
    xvba_driver_data_t *driver_data,
    VAProfile           profile,
    VAEntrypoint        entrypoint
) attribute_hidden;

// Create XvBA decode session
VAStatus
create_decoder(xvba_driver_data_t *driver_data, object_context_p obj_context)
    attribute_hidden;

// Destroy XvBA decode session
void
destroy_decoder(xvba_driver_data_t *driver_data, object_context_p obj_context)
    attribute_hidden;

// Create XvBA buffer
int
create_buffer(
    object_context_p       obj_context,
    XVBABufferDescriptor **buffer_p,
    XVBA_BUFFER            type
) attribute_hidden;

// Destroy XvBA buffer
void
destroy_buffer(
    object_context_p       obj_context,
    XVBABufferDescriptor **buffer_p
) attribute_hidden;

// Append data to the XvBA buffer
void
append_buffer(
    XVBABufferDescriptor *xvba_buffer,
    const uint8_t        *buf,
    unsigned int          buf_size
) attribute_hidden;

// Pad XvBA buffer to 128-byte boundaries
void pad_buffer(XVBABufferDescriptor *xvba_buffer)
    attribute_hidden;

// Clear XvBA buffer
void clear_buffer(XVBABufferDescriptor *xvba_buffer)
    attribute_hidden;

// Create surface XvBA buffers
VAStatus
create_surface_buffers(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface
) attribute_hidden;

// Destroy surface XvBA buffers
void
destroy_surface_buffers(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface
) attribute_hidden;

// vaQueryConfigProfiles
VAStatus
xvba_QueryConfigProfiles(
    VADriverContextP    ctx,
    VAProfile          *profile_list,
    int                *num_profiles
) attribute_hidden;

// vaQueryConfigEntrypoints
VAStatus
xvba_QueryConfigEntrypoints(
    VADriverContextP    ctx,
    VAProfile           profile,
    VAEntrypoint       *entrypoint_list,
    int                *num_entrypoints
) attribute_hidden;

// vaBeginPicture
VAStatus
xvba_BeginPicture(
    VADriverContextP    ctx,
    VAContextID         context,
    VASurfaceID         render_target
) attribute_hidden;

// vaRenderPicture
VAStatus
xvba_RenderPicture(
    VADriverContextP    ctx,
    VAContextID         context,
    VABufferID         *buffers,
    int                 num_buffers
) attribute_hidden;

// vaEndPicture
VAStatus
xvba_EndPicture(
    VADriverContextP    ctx,
    VAContextID         context
) attribute_hidden;

#endif /* XVBA_DECODE_H */
