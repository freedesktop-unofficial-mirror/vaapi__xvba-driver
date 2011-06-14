/*
 *  xvba_decode.c - XvBA backend for VA-API (decoding)
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
#include "xvba_decode.h"
#include "xvba_driver.h"
#include "xvba_video.h"
#include "xvba_buffer.h"
#include "xvba_dump.h"
#include "xvba_image.h"
#include "utils.h"

#define DEBUG 1
#include "debug.h"


// NAL units in XVBA_DATA_BUFFER must be 128-byte aligned
#define XVBA_BUFFER_ALIGN 128

// Determines XVBA_CAPABILITY_ID from VAProfile/VAEntrypoint
static XVBA_CAPABILITY_ID
get_XVBA_CAPABILITY_ID(VAProfile profile, VAEntrypoint entrypoint)
{
    switch (profile) {
    case VAProfileMPEG2Simple:
    case VAProfileMPEG2Main:
        switch (entrypoint) {
        case VAEntrypointIDCT: return XVBA_MPEG2_IDCT;
        case VAEntrypointVLD:  return XVBA_MPEG2_VLD;
        default:               return 0;
        }
        return 0;
    case VAProfileMPEG4Simple:
    case VAProfileMPEG4AdvancedSimple:
    case VAProfileMPEG4Main:
        return 0;
#if VA_CHECK_VERSION(0,30,0)
    case VAProfileH263Baseline:
        return 0;
#endif
    case VAProfileH264Baseline:
    case VAProfileH264Main:
    case VAProfileH264High:
        switch (entrypoint) {
        case VAEntrypointVLD:  return XVBA_H264;
        default:               return 0;
        }
        return 0;
    case VAProfileVC1Simple:
    case VAProfileVC1Main:
    case VAProfileVC1Advanced:
        switch (entrypoint) {
        case VAEntrypointVLD:  return XVBA_VC1;
        default:               return 0;
        }
        return 0;
#if VA_CHECK_VERSION(0,31,0)
    case VAProfileJPEGBaseline:
        return 0;
#endif
    }
    return 0;
}

// Translates VAProfile to XVBA_DECODE_FLAGS
static XVBA_DECODE_FLAGS get_XVBA_DECODE_FLAGS(VAProfile profile)
{
    switch (profile) {
    case VAProfileMPEG2Simple:
    case VAProfileMPEG2Main:
        return 0;
    case VAProfileMPEG4Simple:
    case VAProfileMPEG4AdvancedSimple:
    case VAProfileMPEG4Main:
        return 0;
#if VA_CHECK_VERSION(0,30,0)
    case VAProfileH263Baseline:
        return 0;
#endif
    case VAProfileH264Baseline:         return XVBA_H264_BASELINE;
    case VAProfileH264Main:             return XVBA_H264_MAIN;
    case VAProfileH264High:             return XVBA_H264_HIGH;
    case VAProfileVC1Simple:            return XVBA_VC1_SIMPLE;
    case VAProfileVC1Main:              return XVBA_VC1_MAIN;
    case VAProfileVC1Advanced:          return XVBA_VC1_ADVANCED;
#if VA_CHECK_VERSION(0,31,0)
    case VAProfileJPEGBaseline:
        return 0;
#endif
    }
    return 0;
}

// Initializes XvBA decode caps
static int ensure_decode_caps(xvba_driver_data_t *driver_data)
{
    int status;

    if (driver_data->xvba_decode_caps && driver_data->xvba_decode_caps_count)
        return 1;

    if (driver_data->xvba_decode_caps) {
        free(driver_data->xvba_decode_caps);
        driver_data->xvba_decode_caps = NULL;
    }
    driver_data->xvba_decode_caps_count = 0;

    status = xvba_get_decode_caps(
        driver_data->xvba_context,
        &driver_data->xvba_decode_caps_count,
        &driver_data->xvba_decode_caps
    );
    if (status < 0)
        return 0;
    return 1;
}

// Translates VAProfile/VAEntrypoint to XVBADecodeCap
static XVBADecodeCap *
get_XVBADecodeCap(
    xvba_driver_data_t *driver_data,
    VAProfile           profile,
    VAEntrypoint        entrypoint
)
{
    unsigned int i;

    if (!ensure_decode_caps(driver_data))
        return NULL;

    XVBA_CAPABILITY_ID cap_id = get_XVBA_CAPABILITY_ID(profile, entrypoint);
    XVBA_DECODE_FLAGS decode_flags = get_XVBA_DECODE_FLAGS(profile);
    for (i = 0; i < driver_data->xvba_decode_caps_count; i++) {
        XVBADecodeCap * const decode_cap = &driver_data->xvba_decode_caps[i];
        if (decode_cap->capability_id == cap_id &&
            decode_cap->flags == decode_flags)
            return decode_cap;
    }
    return NULL;
}

// Translates VABufferType to XVBA_BUFFER
static XVBA_DECODE_FLAGS get_XVBA_BUFFER(VABufferType buffer_type)
{
    switch (buffer_type) {
    case VAPictureParameterBufferType:  return XVBA_PICTURE_DESCRIPTION_BUFFER;
    case VASliceDataBufferType:         return XVBA_DATA_BUFFER;
    case VASliceParameterBufferType:    return XVBA_DATA_CTRL_BUFFER;
    case VAIQMatrixBufferType:          return XVBA_QM_BUFFER;
    default:                            break;
    }
    return XVBA_NONE;
}

// Checks decoder for profile/entrypoint is available
int
has_decoder(
    xvba_driver_data_t *driver_data,
    VAProfile           profile,
    VAEntrypoint        entrypoint
)
{
    return get_XVBADecodeCap(driver_data, profile, entrypoint) != NULL;
}

// Checks whether VA profile is implemented
static int
is_supported_profile(VAProfile profile)
{
    switch (profile) {
    case VAProfileH264Baseline:
    case VAProfileH264Main:
    case VAProfileH264High:
    case VAProfileVC1Simple:
    case VAProfileVC1Main:
    case VAProfileVC1Advanced:
        return 1;
    default:
        break;
    }
    return 0;
}

// Creates current XvBA decode session
VAStatus
create_decoder(xvba_driver_data_t *driver_data, object_context_p obj_context)
{
    object_config_p obj_config = XVBA_CONFIG(obj_context->va_config);
    if (!obj_config)
        return VA_STATUS_ERROR_INVALID_CONFIG;

    XVBADecodeCap *decode_cap;
    decode_cap = get_XVBADecodeCap(
        driver_data,
        obj_config->profile,
        obj_config->entrypoint
    );
    if (!decode_cap)
        return VA_STATUS_ERROR_UNKNOWN;

    XVBASession *decode_session;
    decode_session = xvba_create_decode_session(
        driver_data->xvba_context,
        obj_context->picture_width,
        obj_context->picture_height,
        decode_cap
    );
    if (!decode_session)
        return VA_STATUS_ERROR_UNKNOWN;

    obj_context->xvba_decoder = decode_session;
    obj_context->xvba_session = decode_session;
    return VA_STATUS_SUCCESS;
}

// Destroys current XvBA decode session
void
destroy_decoder(xvba_driver_data_t *driver_data, object_context_p obj_context)
{
    if (!obj_context->xvba_decoder)
        return;
    xvba_destroy_decode_session(obj_context->xvba_decoder);
    if (obj_context->xvba_session == obj_context->xvba_decoder)
        obj_context->xvba_session = NULL;
    obj_context->xvba_decoder = NULL;
}

// Allocates a new XvBA buffer
int
create_buffer(
    object_context_p       obj_context,
    XVBABufferDescriptor **buffer_p,
    XVBA_BUFFER            type
)
{
    if (buffer_p)
        *buffer_p = NULL;

    XVBABufferDescriptor *buffer;
    buffer = xvba_create_decode_buffers(obj_context->xvba_decoder, type, 1);
    if (!buffer)
        return 0;

    if (buffer_p)
        *buffer_p = buffer;

    buffer->appPrivate = obj_context;
    buffer->size = sizeof(*buffer);
    clear_buffer(buffer);
    return 1;
}

// Destroys XvBA buffer
void
destroy_buffer(
    object_context_p       obj_context,
    XVBABufferDescriptor **buffer_p
)
{
    if (!buffer_p || !*buffer_p)
        return;

    /* Something went wrong otherwise */
    ASSERT((*buffer_p)->appPrivate == obj_context);
    if (!obj_context)
        return;

    xvba_destroy_decode_buffers(obj_context->xvba_decoder, *buffer_p, 1);
    *buffer_p = NULL;
}

// Appends data to the specified buffer
void
append_buffer(
    XVBABufferDescriptor *xvba_buffer,
    const uint8_t        *buf,
    unsigned int          buf_size
)
{
    memcpy(
        ((uint8_t *)xvba_buffer->bufferXVBA + xvba_buffer->data_size_in_buffer),
        buf,
        buf_size
    );
    xvba_buffer->data_size_in_buffer += buf_size;
}

// Pads buffer to 128-byte boundaries
void pad_buffer(XVBABufferDescriptor *xvba_buffer)
{
    unsigned int r, align;

    if ((r = xvba_buffer->data_size_in_buffer % XVBA_BUFFER_ALIGN) != 0) {
        align = XVBA_BUFFER_ALIGN - r;
        ASSERT(xvba_buffer->data_size_in_buffer + align <= xvba_buffer->buffer_size);
        memset(((uint8_t *)xvba_buffer->bufferXVBA + xvba_buffer->data_size_in_buffer), 0, align);
        xvba_buffer->data_size_in_buffer += align;
    }
}

// Clears an XvBA buffer
void clear_buffer(XVBABufferDescriptor *xvba_buffer)
{
    if (!xvba_buffer)
        return;
    xvba_buffer->data_offset         = 0;
    xvba_buffer->data_size_in_buffer = 0;
}

// Creates XvBA buffers associated to a surface
static VAStatus
ensure_buffer(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface,
    object_buffer_p     obj_buffer
)
{
    object_context_p obj_context = XVBA_CONTEXT(obj_surface->va_context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    XVBABufferDescriptor **buffer_p = NULL;
    XVBA_BUFFER xvba_buffer_type = get_XVBA_BUFFER(obj_buffer->type);
    switch (obj_buffer->type) {
    case VAPictureParameterBufferType:
        buffer_p = &obj_surface->pic_desc_buffer;
        break;
    case VAIQMatrixBufferType:
        buffer_p = &obj_surface->iq_matrix_buffer;
        break;
    case VASliceDataBufferType:
        buffer_p = &obj_surface->data_buffer;
        break;
    case VASliceParameterBufferType:
        if (realloc_buffer(&obj_surface->data_ctrl_buffers,
                           &obj_surface->data_ctrl_buffers_count_max,
                           1 + obj_surface->data_ctrl_buffers_count,
                           sizeof(*obj_surface->data_ctrl_buffers)) == NULL)
            return VA_STATUS_ERROR_ALLOCATION_FAILED;

        buffer_p = &obj_surface->data_ctrl_buffers[obj_surface->data_ctrl_buffers_count++];
        break;
    default:
        break;
    }
    if (buffer_p && !*buffer_p &&
        !create_buffer(obj_context, buffer_p, xvba_buffer_type))
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    return VA_STATUS_SUCCESS;
}

static VAStatus
ensure_buffers(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_surface_p    obj_surface
)
{
    unsigned int i;
    for (i = 0; i < obj_context->va_buffers_count; i++) {
        object_buffer_p obj_buffer = XVBA_BUFFER(obj_context->va_buffers[i]);
        if (!obj_buffer)
            continue;
        VAStatus status = ensure_buffer(driver_data, obj_surface, obj_buffer);
        if (status != VA_STATUS_SUCCESS)
            return status;
    }
    return VA_STATUS_SUCCESS;
}

// Destroys XvBA buffers associated to a surface
void
destroy_surface_buffers(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface
)
{
    object_context_p obj_context = XVBA_CONTEXT(obj_surface->va_context);
    if (!obj_context)
        return;

    destroy_buffer(obj_context, &obj_surface->pic_desc_buffer);
    destroy_buffer(obj_context, &obj_surface->iq_matrix_buffer);
    destroy_buffer(obj_context, &obj_surface->data_buffer);

    unsigned int i;
    for (i = 0; i < obj_surface->data_ctrl_buffers_count_max; i++)
        destroy_buffer(obj_context, &obj_surface->data_ctrl_buffers[i]);
    free(obj_surface->data_ctrl_buffers);
    obj_surface->data_ctrl_buffers           = NULL;
    obj_surface->data_ctrl_buffers_count     = 0;
    obj_surface->data_ctrl_buffers_count_max = 0;
}

// Send picture to the HW for decoding
static VAStatus
decode_picture(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_surface_p    obj_surface
)
{
    XVBASession * const xvba_decoder = obj_context->xvba_decoder;
    XVBABufferDescriptor *xvba_buffers[2];
    unsigned int i, n_buffers;

    if (xvba_decode_picture_start(xvba_decoder, obj_surface->xvba_surface) < 0)
        return VA_STATUS_ERROR_UNKNOWN;

    n_buffers = 0;
    xvba_buffers[n_buffers++] = obj_surface->pic_desc_buffer;
    if (obj_surface->iq_matrix_buffer)
        xvba_buffers[n_buffers++] = obj_surface->iq_matrix_buffer;
    if (xvba_decode_picture(xvba_decoder, xvba_buffers, n_buffers) < 0)
        return VA_STATUS_ERROR_UNKNOWN;

    for (i = 0; i < obj_surface->data_ctrl_buffers_count; i++) {
        n_buffers                 = 0;
        xvba_buffers[n_buffers++] = obj_surface->data_buffer;
        xvba_buffers[n_buffers++] = obj_surface->data_ctrl_buffers[i];
        if (xvba_decode_picture(xvba_decoder, xvba_buffers, n_buffers) < 0)
            return VA_STATUS_ERROR_UNKNOWN;
    }

    if (xvba_decode_picture_end(xvba_decoder) < 0)
        return VA_STATUS_ERROR_UNKNOWN;

    obj_surface->va_surface_status = VASurfaceRendering;
    return 0;
}

// Translate picture buffers and send it to the HW for decoding
static VAStatus
commit_picture(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_surface_p    obj_surface
)
{
    VAStatus va_status = ensure_buffers(driver_data, obj_context, obj_surface);
    if (va_status != VA_STATUS_SUCCESS)
        return va_status;

    obj_context->data_buffer = NULL;
    obj_context->slice_count = 0;

    int i, j, slice_data_is_first = -1;
    for (i = 0; i < obj_context->va_buffers_count; i++) {
        object_buffer_p obj_buffer = XVBA_BUFFER(obj_context->va_buffers[i]);
        if (!obj_buffer)
            return VA_STATUS_ERROR_INVALID_BUFFER;

        switch (obj_buffer->type) {
        case VASliceDataBufferType:
            if (slice_data_is_first < 0)
                slice_data_is_first = 1;
            break;
        case VASliceParameterBufferType:
            if (slice_data_is_first < 0)
                slice_data_is_first = 0;
            if (slice_data_is_first) {
                for (j = i - 1; j >= 0; j--) {
                    object_buffer_p data_buffer = XVBA_BUFFER(obj_context->va_buffers[j]);
                    ASSERT(data_buffer);
                    if (data_buffer->type == VASliceDataBufferType) {
                        obj_context->data_buffer = data_buffer;
                        break;
                    }
                }
            }
            else {
                for (j = i + 1; j < obj_context->va_buffers_count; j++) {
                    object_buffer_p data_buffer = XVBA_BUFFER(obj_context->va_buffers[j]);
                    ASSERT(data_buffer);
                    if (data_buffer->type == VASliceDataBufferType) {
                        obj_context->data_buffer = data_buffer;
                        break;
                    }
                }
            }
            ASSERT(obj_context->data_buffer);
            break;
        default:
            break;
        }

        if (!translate_buffer(driver_data, obj_context, obj_buffer))
            return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
    }

    /* Wait for the surface to be free for decoding */
    if (sync_surface(driver_data, obj_context, obj_surface) < 0)
        return VA_STATUS_ERROR_UNKNOWN;

    /* Send picture to the HW */
    return decode_picture(driver_data, obj_context, obj_surface);
}

// vaQueryConfigProfiles
VAStatus
xvba_QueryConfigProfiles(
    VADriverContextP    ctx,
    VAProfile          *profile_list,
    int                *num_profiles
)
{
    XVBA_DRIVER_DATA_INIT;

    static const VAProfile va_profiles[] = {
        VAProfileMPEG2Simple,
        VAProfileMPEG2Main,
        VAProfileH264Baseline,
        VAProfileH264Main,
        VAProfileH264High,
        VAProfileVC1Simple,
        VAProfileVC1Main,
        VAProfileVC1Advanced
    };

    static const VAEntrypoint va_entrypoints[] = {
        VAEntrypointVLD,
        VAEntrypointIDCT
    };

    int i, j, n = 0;
    for (i = 0; i < ARRAY_ELEMS(va_profiles); i++) {
        VAProfile profile = va_profiles[i];
        if (!is_supported_profile(profile))
            continue;
        for (j = 0; j < ARRAY_ELEMS(va_entrypoints); j++) {
            VAEntrypoint entrypoint = va_entrypoints[j];
            if (has_decoder(driver_data, profile, entrypoint)) {
                profile_list[n++] = profile;
                break;
            }
        }
    }

    /* If the assert fails then XVBA_MAX_PROFILES needs to be bigger */
    ASSERT(n <= XVBA_MAX_PROFILES);
    if (num_profiles)
        *num_profiles = n;

    return VA_STATUS_SUCCESS;
}

// vaQueryConfigEntrypoints
VAStatus
xvba_QueryConfigEntrypoints(
    VADriverContextP    ctx,
    VAProfile           profile,
    VAEntrypoint       *entrypoint_list,
    int                *num_entrypoints
)
{
    XVBA_DRIVER_DATA_INIT;

    static const VAEntrypoint va_entrypoints[] = {
        VAEntrypointVLD,
        VAEntrypointIDCT
    };

    if (!is_supported_profile(profile))
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;

    int i, n = 0;
    for (i = 0; i < ARRAY_ELEMS(va_entrypoints); i++) {
        const VAEntrypoint entrypoint = va_entrypoints[i];
        if (has_decoder(driver_data, profile, entrypoint))
            entrypoint_list[n++] = entrypoint;
    }

    /* If the assert fails then XVBA_MAX_ENTRUYPOINTS needs to be bigger */
    ASSERT(n <= XVBA_MAX_ENTRYPOINTS);
    if (num_entrypoints)
        *num_entrypoints = n;

    return VA_STATUS_SUCCESS;
}

// vaBeginPicture
VAStatus
xvba_BeginPicture(
    VADriverContextP    ctx,
    VAContextID         context,
    VASurfaceID         render_target
)
{
    XVBA_DRIVER_DATA_INIT;

    D(bug("vaBeginPicture(): context 0x%08x, surface 0x%08x\n",
          context, render_target));

    object_context_p obj_context = XVBA_CONTEXT(context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    object_surface_p obj_surface = XVBA_SURFACE(render_target);
    if (!obj_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    /* Destroy any previous surface override from vaPutImage() */
    putimage_hacks_disable(driver_data, obj_surface);

    obj_context->current_render_target = obj_surface->base.id;
    obj_surface->va_surface_status     = VASurfaceRendering;
    obj_surface->used_for_decoding     = 1;

    ASSERT(!obj_context->va_buffers_count);
    destroy_va_buffers(driver_data, obj_context);

    unsigned int i;
    clear_buffer(obj_surface->pic_desc_buffer);
    clear_buffer(obj_surface->iq_matrix_buffer);
    clear_buffer(obj_surface->data_buffer);
    for (i = 0; i < obj_surface->data_ctrl_buffers_count; i++)
        clear_buffer(obj_surface->data_ctrl_buffers[i]);
    obj_surface->data_ctrl_buffers_count = 0;
    return VA_STATUS_SUCCESS;
}

// vaRenderPicture
VAStatus
xvba_RenderPicture(
    VADriverContextP    ctx,
    VAContextID         context,
    VABufferID         *buffers,
    int                 num_buffers
)
{
    XVBA_DRIVER_DATA_INIT;
    int i;

    D(bug("vaRenderPicture(): context 0x%08x, %d buffers\n",
          context, num_buffers));

    object_context_p obj_context = XVBA_CONTEXT(context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    object_surface_p obj_surface = XVBA_SURFACE(obj_context->current_render_target);
    if (!obj_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    /* Verify that we got valid buffer references */
    for (i = 0; i < num_buffers; i++) {
        object_buffer_p obj_buffer = XVBA_BUFFER(buffers[i]);
        if (!obj_buffer)
            return VA_STATUS_ERROR_INVALID_BUFFER;
    }

    /* Record buffers. They will be processed in EndPicture() */
    for (i = 0; i < num_buffers; i++) {
        object_buffer_p obj_buffer = XVBA_BUFFER(buffers[i]);

        D(bug("  buffer 0x%08x\n", buffers[i]));

        VABufferID *va_buffers;
        va_buffers = realloc_buffer(
            &obj_context->va_buffers,
            &obj_context->va_buffers_count_max,
            1 + obj_context->va_buffers_count,
            sizeof(*va_buffers)
        );
        if (!va_buffers)
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
        va_buffers[obj_context->va_buffers_count++] = obj_buffer->base.id;
    }
    return VA_STATUS_SUCCESS;
}

// vaEndPicture
VAStatus
xvba_EndPicture(
    VADriverContextP    ctx,
    VAContextID         context
)
{
    XVBA_DRIVER_DATA_INIT;

    D(bug("vaEndPicture(): context 0x%08x\n", context));

    object_context_p obj_context = XVBA_CONTEXT(context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    object_surface_p obj_surface = XVBA_SURFACE(obj_context->current_render_target);
    if (!obj_surface || !obj_surface->xvba_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    /* Send picture bits to the HW and free VA resources (buffers) */
    VAStatus va_status = commit_picture(driver_data, obj_context, obj_surface);

    /* XXX: assume we are done with rendering right away */
    obj_context->current_render_target = VA_INVALID_SURFACE;

    destroy_va_buffers(driver_data, obj_context);
    return va_status;
}
