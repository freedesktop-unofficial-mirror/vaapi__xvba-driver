/*
 *  xvba_buffer.h - XvBA backend for VA-API (VA buffers)
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

#ifndef XVBA_BUFFER_H
#define XVBA_BUFFER_H

#include "xvba_driver.h"

typedef struct object_buffer object_buffer_t;
struct object_buffer {
    struct object_base  base;
    VAContextID         va_context;
    VABufferType        type;
    void               *buffer_data;
    unsigned int        buffer_size;
    unsigned int        max_num_elements;
    unsigned int        num_elements;
    uint64_t            mtime;
};

// Create VA buffer object
object_buffer_p
create_va_buffer(
    xvba_driver_data_t *driver_data,
    VAContextID         context,
    VABufferType        buffer_type,
    unsigned int        num_elements,
    unsigned int        size
) attribute_hidden;

// Destroy VA buffer object
void
destroy_va_buffer(
    xvba_driver_data_t *driver_data,
    object_buffer_p     obj_buffer
) attribute_hidden;

// Destroy VA buffer objects stored in VA context
void
destroy_va_buffers(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context
) attribute_hidden;

// Determines whether BUFFER is queued for decoding
int
is_queued_buffer(
    xvba_driver_data_t *driver_data,
    object_buffer_p     obj_buffer
) attribute_hidden;

// Translate VA buffer
int translate_buffer(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
) attribute_hidden;

// vaCreateBuffer
VAStatus
xvba_CreateBuffer(
    VADriverContextP    ctx,
    VAContextID         context,
    VABufferType        type,
    unsigned int        size,
    unsigned int        num_elements,
    void               *data,
    VABufferID         *buf_id
) attribute_hidden;

// vaDestroyBuffer
VAStatus
xvba_DestroyBuffer(
    VADriverContextP    ctx,
    VABufferID          buffer_id
) attribute_hidden;

// vaBufferSetNumElements
VAStatus
xvba_BufferSetNumElements(
    VADriverContextP    ctx,
    VABufferID          buf_id,
    unsigned int        num_elements
) attribute_hidden;

// vaMapBuffer
VAStatus
xvba_MapBuffer(
    VADriverContextP    ctx,
    VABufferID          buf_id,
    void              **pbuf
) attribute_hidden;

// vaUnmapBuffer
VAStatus
xvba_UnmapBuffer(
    VADriverContextP    ctx,
    VABufferID          buf_id
) attribute_hidden;

// vaBufferInfo
VAStatus
xvba_BufferInfo(
    VADriverContextP    ctx,
    VAContextID         context,
    VABufferID          buf_id,
    VABufferType       *type,
    unsigned int       *size,
    unsigned int       *num_elements
) attribute_hidden;

#endif /* XVBA_BUFFER_H */
