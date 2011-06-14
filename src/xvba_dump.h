/*
 *  xvba_dump.h - Dump utilities
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

#ifndef XVBA_DUMP_H
#define XVBA_DUMP_H

#include "xvba_driver.h"
#include "xvba_video.h"

// Returns string representation of FOURCC
const char *string_of_FOURCC(uint32_t fourcc)
    attribute_hidden;

// Returns string representation of VA surface format
const char *string_of_VAConfigAttribRTFormat(unsigned int format)
    attribute_hidden;

// Returns string representation of XVBA_SURFACE_FLAG
const char *string_of_XVBA_SURFACE_FLAG(XVBA_SURFACE_FLAG flag)
    attribute_hidden;

// Returns string representation of XVBA_BUFFER
const char *string_of_XVBA_BUFFER(XVBA_BUFFER buffer_type)
    attribute_hidden;

// Returns string representation of XVBA_CAPABILITY_ID
const char *string_of_XVBA_CAPABILITY_ID(XVBA_CAPABILITY_ID cap_id)
    attribute_hidden;

// Returns string representation of XVBA_DECODE_FLAGS
const char *string_of_XVBA_DECODE_FLAGS(XVBA_DECODE_FLAGS flag)
    attribute_hidden;

// Returns string representation of XVBACodec
const char *string_of_XVBACodec(XVBACodec codec)
    attribute_hidden;

// Returns string representation of VABufferType
const char *string_of_VABufferType(VABufferType type)
    attribute_hidden;

// Dumps XVBACreateContext()
void dump_XVBACreateContext(void *input)
    attribute_hidden;
void dump_XVBACreateContext_output(void *output)
    attribute_hidden;

// Dumps XVBADestroyContext()
void dump_XVBADestroyContext(void *context)
    attribute_hidden;

// Dumps XVBACreateSurface()
void dump_XVBACreateSurface(void *input)
    attribute_hidden;
void dump_XVBACreateSurface_output(void *output)
    attribute_hidden;

// Dumps XVBACreateGLSharedSurface()
void dump_XVBACreateGLSharedSurface(void *input)
    attribute_hidden;
void dump_XVBACreateGLSharedSurface_output(void *output)
    attribute_hidden;

// Dumps XVBADestroySurface()
void dump_XVBADestroySurface(void *surface)
    attribute_hidden;

// Dumps XVBAGetCapDecode()
void dump_XVBAGetCapDecode(void *input)
    attribute_hidden;

void
dump_XVBAGetCapDecode_output_decode_caps(
    unsigned int    num_decode_caps,
    XVBADecodeCap  *decode_caps
) attribute_hidden;

void
dump_XVBAGetCapDecode_output_surface_caps(
    unsigned int    num_surface_caps,
    XVBASurfaceCap *surface_caps
) attribute_hidden;

// Dumps XVBACreateDecode()
void dump_XVBACreateDecode(void *input)
    attribute_hidden;
void dump_XVBACreateDecode_output(void *output)
    attribute_hidden;

// Dumps XVBADestroyDecode()
void dump_XVBADestroyDecode(void *session)
    attribute_hidden;

// Dumps XVBACreateDecodeBuffers()
void dump_XVBACreateDecodeBuffers(void *input)
    attribute_hidden;
void dump_XVBACreateDecodeBuffers_output(void *output)
    attribute_hidden;

// Dumps XVBADestroyDecodeBuffers()
void dump_XVBADestroyDecodeBuffers(void *input)
    attribute_hidden;

// Dumps XVBAStartDecodePicture()
void dump_XVBAStartDecodePicture(void *input)
    attribute_hidden;

// Dumps XVBADecodePicture()
void dump_XVBADecodePicture(void *input)
    attribute_hidden;

// Dumps XVBAEndDecodePicture()
void dump_XVBAEndDecodePicture(void *input)
    attribute_hidden;

// Dumps XVBAUpdateSurface()
void dump_XVBAUpdateSurface(void *input)
    attribute_hidden;

// Dumps XVBAGetSurface()
void dump_XVBAGetSurface(void *input)
    attribute_hidden;

// Dumps XVBATransferSurface()
void dump_XVBATransferSurface(void *input)
    attribute_hidden;

#endif /* XVBA_DUMP_H */
