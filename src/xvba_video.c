/*
 *  xvba_video.c - XvBA backend for VA-API (VA context, config, surfaces)
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
#include "xvba_video.h"
#include "xvba_dump.h"
#include "xvba_buffer.h"
#include "xvba_decode.h"
#include "xvba_image.h"
#include "xvba_subpic.h"
#include "xvba_video_x11.h"
#if USE_GLX
#include "xvba_video_glx.h"
#endif
#include "utils.h"
#include "utils_x11.h"

#define DEBUG 1
#include "debug.h"


// Translates VAProfile to XVBACodec
static XVBACodec get_XVBACodec(VAProfile profile)
{
    switch (profile) {
    case VAProfileMPEG2Simple:
    case VAProfileMPEG2Main:
        return XVBA_CODEC_MPEG2;
    case VAProfileMPEG4Simple:
    case VAProfileMPEG4AdvancedSimple:
    case VAProfileMPEG4Main:
        break;
#if VA_CHECK_VERSION(0,30,0)
    case VAProfileH263Baseline:
        break;
#endif
    case VAProfileH264Baseline:
    case VAProfileH264Main:
    case VAProfileH264High:
        return XVBA_CODEC_H264;
    case VAProfileVC1Simple:
    case VAProfileVC1Main:
    case VAProfileVC1Advanced:
        return XVBA_CODEC_VC1;
#if VA_CHECK_VERSION(0,31,0)
    case VAProfileJPEGBaseline:
        break;
#endif
    }
    return 0;
}

// Destroys XvBA subpictures bound to a VA surface
static void
destroy_subpictures(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface
)
{
    unsigned int i;
    for (i = 0; i < obj_surface->assocs_count; i++) {
        SubpictureAssociationP const assoc = obj_surface->assocs[i];
        destroy_subpicture_surface(
            driver_data,
            XVBA_SUBPICTURE(assoc->subpicture)
        );
    }
}

// Creates XvBA surface
static VAStatus
ensure_surface_size(
    xvba_driver_data_t *driver_data,
    object_surface_p    obj_surface,
    unsigned int        width,
    unsigned int        height
)
{
    if (obj_surface->xvba_surface &&
        obj_surface->xvba_surface_width  == width &&
        obj_surface->xvba_surface_height == height)
        return VA_STATUS_SUCCESS;

    if (obj_surface->xvba_surface) {
        xvba_destroy_surface(obj_surface->xvba_surface);
        obj_surface->xvba_surface = NULL;
    }

    object_context_p obj_context = XVBA_CONTEXT(obj_surface->va_context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    obj_surface->xvba_surface = xvba_create_surface(
        obj_context->xvba_session,
        width,
        height,
        XVBA_NV12
    );
    if (!obj_surface->xvba_surface)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    obj_surface->xvba_surface_width  = width;
    obj_surface->xvba_surface_height = height;
    return VA_STATUS_SUCCESS;
}

// Destroys XvBA surface and associated buffers
static void
destroy_surface(xvba_driver_data_t *driver_data, object_surface_p obj_surface)
{
    destroy_subpictures(driver_data, obj_surface);
    destroy_surface_buffers(driver_data, obj_surface);

    if (obj_surface->xvba_surface) {
        xvba_destroy_surface(obj_surface->xvba_surface);
        obj_surface->xvba_surface = NULL;
    }
}

// Query surface status
int
query_surface_status(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_surface_p    obj_surface,
    VASurfaceStatus    *surface_status
)
{
    int status;

    if (surface_status)
        *surface_status = VASurfaceReady;

    if (!obj_surface)
        return 0;

    switch (obj_surface->va_surface_status) {
    case VASurfaceRendering: /* Rendering (XvBA level) */
        ASSERT(obj_surface->used_for_decoding);
        if (!obj_context || !obj_context->xvba_decoder)
            return 0;
        if (!obj_surface->xvba_surface)
            return 0;
        status = xvba_sync_surface(
            obj_context->xvba_decoder,
            obj_surface->xvba_surface,
            XVBA_GET_SURFACE_STATUS
        );
        if (status < 0)
            return -1;
        if (status == XVBA_COMPLETED)
            obj_surface->va_surface_status = VASurfaceReady;
        break;
    case VASurfaceDisplaying:
        status = query_surface_status_glx(driver_data, obj_surface);
        if (status < 0)
            return -1;
        if (status == XVBA_COMPLETED)
            obj_surface->va_surface_status = VASurfaceReady;
        break;
    }

    if (surface_status)
        *surface_status = obj_surface->va_surface_status;
    return 0;
}

// Synchronize surface
int
sync_surface(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_surface_p    obj_surface
)
{
    VASurfaceStatus surface_status;
    int status;

    while ((status = query_surface_status(driver_data, obj_context, obj_surface, &surface_status)) == 0 &&
           surface_status != VASurfaceReady)
        delay_usec(XVBA_SYNC_DELAY);
    return status;
}

// Add subpicture association to surface
// NOTE: the subpicture owns the SubpictureAssociation object
int surface_add_association(
    object_surface_p            obj_surface,
    SubpictureAssociationP      assoc
)
{
    /* Check that we don't already have this association */
    if (obj_surface->assocs) {
        unsigned int i;
        for (i = 0; i < obj_surface->assocs_count; i++) {
            if (obj_surface->assocs[i] == assoc)
                return 0;
            if (obj_surface->assocs[i]->subpicture == assoc->subpicture) {
                /* XXX: this should not happen, but replace it in the interim */
                ASSERT(obj_surface->assocs[i]->surface == assoc->surface);
                obj_surface->assocs[i] = assoc;
                return 0;
            }
        }
    }

    /* Check that we have not reached the maximum subpictures capacity yet */
    if (obj_surface->assocs_count >= XVBA_MAX_SUBPICTURES)
        return -1;

    /* Append this subpicture association */
    SubpictureAssociationP *assocs;
    assocs = realloc_buffer(
        &obj_surface->assocs,
        &obj_surface->assocs_count_max,
        1 + obj_surface->assocs_count,
        sizeof(obj_surface->assocs[0])
    );
    if (!assocs)
        return -1;

    assocs[obj_surface->assocs_count++] = assoc;
    return 0;
}

// Remove subpicture association from surface
// NOTE: the subpicture owns the SubpictureAssociation object
int surface_remove_association(
    object_surface_p            obj_surface,
    SubpictureAssociationP      assoc
)
{
    if (!obj_surface->assocs || obj_surface->assocs_count == 0)
        return -1;

    unsigned int i;
    const unsigned int last = obj_surface->assocs_count - 1;
    for (i = 0; i <= last; i++) {
        if (obj_surface->assocs[i] == assoc) {
            /* Swap with the last subpicture */
            obj_surface->assocs[i] = obj_surface->assocs[last];
            obj_surface->assocs[last] = NULL;
            obj_surface->assocs_count--;
            return 0;
        }
    }
    return -1;
}

// vaGetConfigAttributes
VAStatus
xvba_GetConfigAttributes(
    VADriverContextP    ctx,
    VAProfile           profile,
    VAEntrypoint        entrypoint,
    VAConfigAttrib     *attrib_list,
    int                 num_attribs
)
{
    XVBA_DRIVER_DATA_INIT;

    if (!has_decoder(driver_data, profile, entrypoint))
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;

    int i;
    for (i = 0; i < num_attribs; i++) {
        switch (attrib_list[i].type) {
        case VAConfigAttribRTFormat:
            attrib_list[i].value = VA_RT_FORMAT_YUV420;
            break;
        default:
            attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
            break;
        }
    }
    return VA_STATUS_SUCCESS;
}

static VAStatus
xvba_update_attribute(object_config_p obj_config, VAConfigAttrib *attrib)
{
    int i;

    /* Check existing attributes */
    for (i = 0; obj_config->attrib_count < i; i++) {
        if (obj_config->attrib_list[i].type == attrib->type) {
            /* Update existing attribute */
            obj_config->attrib_list[i].value = attrib->value;
            return VA_STATUS_SUCCESS;
        }
    }
    if (obj_config->attrib_count < XVBA_MAX_CONFIG_ATTRIBUTES) {
        i = obj_config->attrib_count;
        obj_config->attrib_list[i].type = attrib->type;
        obj_config->attrib_list[i].value = attrib->value;
        obj_config->attrib_count++;
        return VA_STATUS_SUCCESS;
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

// vaCreateConfig
VAStatus
xvba_CreateConfig(
    VADriverContextP    ctx,
    VAProfile           profile,
    VAEntrypoint        entrypoint,
    VAConfigAttrib     *attrib_list,
    int                 num_attribs,
    VAConfigID         *config_id
)
{
    XVBA_DRIVER_DATA_INIT;

    VAStatus va_status;
    int configID;
    object_config_p obj_config;
    int i;

    /* Validate profile and entrypoint */
    switch (profile) {
    case VAProfileMPEG2Simple:
    case VAProfileMPEG2Main:
        if (entrypoint == VAEntrypointIDCT || entrypoint == VAEntrypointVLD)
            va_status = VA_STATUS_SUCCESS;
        else
            va_status = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
        break;
    case VAProfileH264Baseline:
    case VAProfileH264Main:
    case VAProfileH264High:
        if (entrypoint == VAEntrypointVLD)
            va_status = VA_STATUS_SUCCESS;
        else
            va_status = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
        break;
    case VAProfileVC1Simple:
    case VAProfileVC1Main:
    case VAProfileVC1Advanced:
        if (entrypoint == VAEntrypointVLD)
            va_status = VA_STATUS_SUCCESS;
        else
            va_status = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
        break;
    default:
        va_status = VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
        break;
    }

    if (va_status != VA_STATUS_SUCCESS)
        return va_status;

    if (!has_decoder(driver_data, profile, entrypoint))
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;

    configID = object_heap_allocate(&driver_data->config_heap);
    obj_config = XVBA_CONFIG(configID);
    if (!obj_config)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    obj_config->profile                 = profile;
    obj_config->entrypoint              = entrypoint;
    obj_config->attrib_list[0].type     = VAConfigAttribRTFormat;
    obj_config->attrib_list[0].value    = VA_RT_FORMAT_YUV420;
    obj_config->attrib_count            = 1;

    for (i = 0; i < num_attribs; i++) {
        va_status = xvba_update_attribute(obj_config, &attrib_list[i]);
        if (va_status != VA_STATUS_SUCCESS) {
            xvba_DestroyConfig(ctx, configID);
            return va_status;
        }
    }

    if (config_id)
        *config_id = configID;

    return va_status;
}

// vaDestroyConfig
VAStatus
xvba_DestroyConfig(
    VADriverContextP    ctx,
    VAConfigID          config_id
)
{
    XVBA_DRIVER_DATA_INIT;

    object_config_p obj_config = XVBA_CONFIG(config_id);
    if (!obj_config)
        return VA_STATUS_ERROR_INVALID_CONFIG;

    object_heap_free(&driver_data->config_heap, (object_base_p)obj_config);
    return VA_STATUS_SUCCESS;
}

// vaQueryConfigAttributes
VAStatus
xvba_QueryConfigAttributes(
    VADriverContextP    ctx,
    VAConfigID          config_id,
    VAProfile          *profile,
    VAEntrypoint       *entrypoint,
    VAConfigAttrib     *attrib_list,
    int                *num_attribs
)
{
    XVBA_DRIVER_DATA_INIT;

    VAStatus va_status = VA_STATUS_SUCCESS;
    object_config_p obj_config;
    int i;

    obj_config = XVBA_CONFIG(config_id);
    if (!obj_config)
        return VA_STATUS_ERROR_INVALID_CONFIG;

    if (profile)
        *profile = obj_config->profile;

    if (entrypoint)
        *entrypoint = obj_config->entrypoint;

    if (num_attribs)
        *num_attribs =  obj_config->attrib_count;

    if (attrib_list) {
        for (i = 0; i < obj_config->attrib_count; i++)
            attrib_list[i] = obj_config->attrib_list[i];
    }

    return va_status;
}

// vaCreateSurfaces
VAStatus
xvba_CreateSurfaces(
    VADriverContextP    ctx,
    int                 width,
    int                 height,
    int                 format,
    int                 num_surfaces,
    VASurfaceID        *surfaces
)
{
    XVBA_DRIVER_DATA_INIT;

    D(bug("vaCreateSurfaces(): size %dx%d, format %s\n", width, height,
          string_of_VAConfigAttribRTFormat(format)));

    VAStatus va_status = VA_STATUS_SUCCESS;
    int i;

    /* We only support one format */
    if (format != VA_RT_FORMAT_YUV420)
        return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;

    for (i = 0; i < num_surfaces; i++) {
        int va_surface = object_heap_allocate(&driver_data->surface_heap);
        object_surface_p obj_surface = XVBA_SURFACE(va_surface);
        if (!obj_surface) {
            va_status = VA_STATUS_ERROR_ALLOCATION_FAILED;
            break;
        }
        D(bug("  surface 0x%08x\n", va_surface));
        obj_surface->va_context                  = VA_INVALID_ID;
        obj_surface->va_surface_status           = VASurfaceReady;
        obj_surface->xvba_surface                = NULL;
        obj_surface->xvba_surface_width          = width;
        obj_surface->xvba_surface_height         = height;
        obj_surface->output_surfaces             = NULL;
        obj_surface->output_surfaces_count       = 0;
        obj_surface->output_surfaces_count_max   = 0;
        obj_surface->width                       = width;
        obj_surface->height                      = height;
        obj_surface->gl_surface                  = NULL;
        obj_surface->pic_desc_buffer             = NULL;
        obj_surface->iq_matrix_buffer            = NULL;
        obj_surface->data_buffer                 = NULL;
        obj_surface->data_ctrl_buffers           = NULL;
        obj_surface->data_ctrl_buffers_count     = 0;
        obj_surface->data_ctrl_buffers_count_max = 0;
        obj_surface->assocs                      = NULL;
        obj_surface->assocs_count                = 0;
        obj_surface->assocs_count_max            = 0;
        obj_surface->putimage_hacks              = NULL;
        surfaces[i] = va_surface;
    }

    /* Error recovery */
    if (va_status != VA_STATUS_SUCCESS)
        xvba_DestroySurfaces(ctx, surfaces, i);

    return va_status;
}

// vaDestroySurfaces
VAStatus
xvba_DestroySurfaces(
    VADriverContextP    ctx,
    VASurfaceID        *surface_list,
    int                 num_surfaces
)
{
    XVBA_DRIVER_DATA_INIT;

    D(bug("vaDestroySurfaces()\n"));

    int i, j, n;
    for (i = num_surfaces - 1; i >= 0; i--) {
        object_surface_p obj_surface = XVBA_SURFACE(surface_list[i]);
        if (!obj_surface)
            continue;

        D(bug("  surface 0x%08x\n", obj_surface->base.id));
        destroy_surface(driver_data, obj_surface);

#if USE_GLX
        if (obj_surface->gl_surface) {
            glx_surface_unref(driver_data, obj_surface->gl_surface);
            obj_surface->gl_surface = NULL;
        }
#endif

        for (j = 0; j < obj_surface->output_surfaces_count; j++) {
            output_surface_unref(driver_data, obj_surface->output_surfaces[j]);
            obj_surface->output_surfaces[j] = NULL;
        }
        free(obj_surface->output_surfaces);
        obj_surface->output_surfaces_count = 0;
        obj_surface->output_surfaces_count_max = 0;

        if (obj_surface->assocs) {
            object_subpicture_p obj_subpicture;
            VAStatus status;
            const unsigned int n_assocs = obj_surface->assocs_count;

            for (j = 0, n = 0; j < n_assocs; j++) {
                SubpictureAssociationP const assoc = obj_surface->assocs[0];
                if (!assoc)
                    continue;
                obj_subpicture = XVBA_SUBPICTURE(assoc->subpicture);
                if (!obj_subpicture)
                    continue;
                status = subpicture_deassociate_1(obj_subpicture, obj_surface);
                if (status == VA_STATUS_SUCCESS)
                    ++n;
            }
            if (n != n_assocs)
                xvba_error_message("vaDestroySurfaces(): surface 0x%08x still "
                                   "has %d subpictures associated to it\n",
                                   obj_surface->base.id, n_assocs - n);
            free(obj_surface->assocs);
            obj_surface->assocs = NULL;
        }
        obj_surface->assocs_count = 0;
        obj_surface->assocs_count_max = 0;

        putimage_hacks_disable(driver_data, obj_surface);

        object_heap_free(&driver_data->surface_heap, (object_base_p)obj_surface);
    }
    return VA_STATUS_SUCCESS;
}

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
)
{
    XVBA_DRIVER_DATA_INIT;

    if (context)
        *context = VA_INVALID_ID;

    D(bug("vaCreateContext(): config 0x%08x, size %dx%d\n", config_id,
          picture_width, picture_height));

    object_config_p const obj_config = XVBA_CONFIG(config_id);
    if (!obj_config)
        return VA_STATUS_ERROR_INVALID_CONFIG;

    /* XXX: validate flag */

    int i;
    for (i = 0; i < num_render_targets; i++) {
        object_surface_p const obj_surface = XVBA_SURFACE(render_targets[i]);
        if (!obj_surface)
            return VA_STATUS_ERROR_INVALID_SURFACE;
        if (obj_surface->va_context != VA_INVALID_ID) {
            xvba_error_message("vaCreateContext(): surface 0x%08x is already "
                               "bound to context 0x%08x\n",
                               obj_surface->base.id, obj_surface->va_context);
            return VA_STATUS_ERROR_INVALID_SURFACE;
        }
        if (obj_surface->width  != picture_width ||
            obj_surface->height != picture_height)
            return VA_STATUS_ERROR_INVALID_SURFACE;
    }

    VAContextID context_id = object_heap_allocate(&driver_data->context_heap);
    object_context_p const obj_context = XVBA_CONTEXT(context_id);
    if (!obj_context)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    /* XXX: workaround XvBA internal bugs. Round up so that to create
       surfaces of the "expected" size */
    picture_width  = (picture_width  + 15) & -16;
    picture_height = (picture_height + 15) & -16;

    obj_context->va_config              = config_id;
    obj_context->picture_width          = picture_width;
    obj_context->picture_height         = picture_height;
    obj_context->flags                  = flag;
    obj_context->num_render_targets     = num_render_targets;
    obj_context->render_targets         = (VASurfaceID *)
        calloc(num_render_targets, sizeof(VASurfaceID));
    obj_context->current_render_target  = VA_INVALID_SURFACE;
    obj_context->xvba_codec             = get_XVBACodec(obj_config->profile);
    obj_context->xvba_session           = NULL;
    obj_context->xvba_decoder           = NULL;
    obj_context->va_buffers             = NULL;
    obj_context->va_buffers_count       = 0;
    obj_context->va_buffers_count_max   = 0;
    obj_context->data_buffer            = NULL;
    obj_context->slice_count            = 0;

    if (!obj_context->render_targets) {
        xvba_DestroyContext(ctx, context_id);
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    VAStatus va_status = create_decoder(driver_data, obj_context);
    if (va_status != VA_STATUS_SUCCESS) {
        xvba_DestroyContext(ctx, context_id);
        return va_status;
    }

    for (i = 0; i < num_render_targets; i++) {
        object_surface_t * const obj_surface = XVBA_SURFACE(render_targets[i]);
        obj_context->render_targets[i] = render_targets[i];
        obj_surface->va_context        = context_id;

        D(bug("  surface 0x%08x\n", render_targets[i]));
        va_status = ensure_surface_size(
            driver_data,
            obj_surface,
            picture_width,
            picture_height
        );
        if (va_status != VA_STATUS_SUCCESS) {
            xvba_DestroyContext(ctx, context_id);
            return va_status;
        }
    }

    D(bug("  context 0x%08x\n", context_id));
    if (context)
        *context = context_id;
    return VA_STATUS_SUCCESS;
}

// vaDestroyContext
VAStatus
xvba_DestroyContext(
    VADriverContextP    ctx,
    VAContextID         context
)
{
    XVBA_DRIVER_DATA_INIT;

    D(bug("vaDestroyContext(): context 0x%08x\n", context));

    object_context_p obj_context = XVBA_CONTEXT(context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    destroy_decoder(driver_data, obj_context);

    if (obj_context->va_buffers) {
        destroy_va_buffers(driver_data, obj_context);
        free(obj_context->va_buffers);
        obj_context->va_buffers = NULL;
    }

    if (obj_context->render_targets) {
        int i;
        for (i = 0; i < obj_context->num_render_targets; i++) {
            object_surface_p obj_surface;
            obj_surface = XVBA_SURFACE(obj_context->render_targets[i]);
            if (obj_surface)
                obj_surface->va_context = VA_INVALID_ID;
        }
        free(obj_context->render_targets);
        obj_context->render_targets = NULL;
    }

    obj_context->va_config              = VA_INVALID_ID;
    obj_context->current_render_target  = VA_INVALID_SURFACE;
    obj_context->picture_width          = 0;
    obj_context->picture_height         = 0;
    obj_context->num_render_targets     = 0;
    obj_context->flags                  = 0;

    object_heap_free(&driver_data->context_heap, (object_base_p)obj_context);
    return VA_STATUS_SUCCESS;
}

// vaQuerySurfaceStatus
VAStatus
xvba_QuerySurfaceStatus(
    VADriverContextP    ctx,
    VASurfaceID         render_target,
    VASurfaceStatus    *status
)
{
    XVBA_DRIVER_DATA_INIT;

    object_surface_p obj_surface = XVBA_SURFACE(render_target);
    if (!obj_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    object_context_p obj_context = XVBA_CONTEXT(obj_surface->va_context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    if (query_surface_status(driver_data, obj_context, obj_surface, status) < 0)
        return VA_STATUS_ERROR_UNKNOWN;

    return VA_STATUS_SUCCESS;
}

// vaSyncSurface
VAStatus
xvba_SyncSurface2(
    VADriverContextP    ctx,
    VASurfaceID         render_target
)
{
    XVBA_DRIVER_DATA_INIT;

    object_surface_p obj_surface = XVBA_SURFACE(render_target);
    if (!obj_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    if (sync_surface(driver_data, NULL, obj_surface) < 0)
        return VA_STATUS_ERROR_UNKNOWN;

    return VA_STATUS_SUCCESS;
}

VAStatus
xvba_SyncSurface3(
    VADriverContextP    ctx,
    VAContextID         context,
    VASurfaceID         render_target
)
{
    XVBA_DRIVER_DATA_INIT;

    object_context_p obj_context = XVBA_CONTEXT(context);
    if (!obj_context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    object_surface_p obj_surface = XVBA_SURFACE(render_target);
    if (!obj_surface)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    if (sync_surface(driver_data, obj_context, obj_surface) < 0)
        return VA_STATUS_ERROR_UNKNOWN;

    return VA_STATUS_SUCCESS;
}


// Ensure VA Display Attributes are initialized
static int ensure_display_attributes(xvba_driver_data_t *driver_data)
{
    VADisplayAttribute *attr;

    if (driver_data->va_display_attrs_count > 0)
        return 0;

    memset(driver_data->va_display_attrs_mtime, 0,
           sizeof(driver_data->va_display_attrs_mtime));

    cm_set_identity(driver_data->cm_brightness);
    cm_set_identity(driver_data->cm_contrast);
    cm_set_identity(driver_data->cm_saturation);
    cm_set_identity(driver_data->cm_hue);
    driver_data->cm_composite_ok = 0;

    attr = &driver_data->va_display_attrs[0];

    attr->type      = VADisplayAttribDirectSurface;
    attr->value     = 1; /* GLX: async transfer */
    attr->min_value = attr->value;
    attr->max_value = attr->value;
    attr->flags     = VA_DISPLAY_ATTRIB_GETTABLE;
    attr++;

    attr->type      = VADisplayAttribBackgroundColor;
    attr->value     = WhitePixel(driver_data->x11_dpy, driver_data->x11_screen);
    attr->min_value = 0;
    attr->max_value = 0xffffff;
    attr->flags     = VA_DISPLAY_ATTRIB_GETTABLE|VA_DISPLAY_ATTRIB_SETTABLE;
    attr++;

    attr->type      = VADisplayAttribBrightness;
    attr->value     = 0;
    attr->min_value = -100;
    attr->max_value = 100;
    attr->flags     = VA_DISPLAY_ATTRIB_GETTABLE|VA_DISPLAY_ATTRIB_SETTABLE;
    attr++;

    attr->type      = VADisplayAttribContrast;
    attr->value     = 0;
    attr->min_value = -100;
    attr->max_value = 100;
    attr->flags     = VA_DISPLAY_ATTRIB_GETTABLE|VA_DISPLAY_ATTRIB_SETTABLE;
    attr++;

    attr->type      = VADisplayAttribHue;
    attr->value     = 0;
    attr->min_value = -100;
    attr->max_value = 100;
    attr->flags     = VA_DISPLAY_ATTRIB_GETTABLE|VA_DISPLAY_ATTRIB_SETTABLE;
    attr++;

    attr->type      = VADisplayAttribSaturation;
    attr->value     = 0;
    attr->min_value = -100;
    attr->max_value = 100;
    attr->flags     = VA_DISPLAY_ATTRIB_GETTABLE|VA_DISPLAY_ATTRIB_SETTABLE;
    attr++;

    driver_data->va_display_attrs_count = attr - driver_data->va_display_attrs;
    ASSERT(driver_data->va_display_attrs_count <= XVBA_MAX_DISPLAY_ATTRIBUTES);
    return 0;
}

// Look up for the specified VA display attribute
static VADisplayAttribute *
get_display_attribute(
    xvba_driver_data_t *driver_data,
    VADisplayAttribType type
)
{
    if (ensure_display_attributes(driver_data) < 0)
        return NULL;

    unsigned int i;
    for (i = 0; i < driver_data->va_display_attrs_count; i++) {
        if (driver_data->va_display_attrs[i].type == type)
            return &driver_data->va_display_attrs[i];
    }
    return NULL;
}

// vaQueryDisplayAttributes
VAStatus
xvba_QueryDisplayAttributes(
    VADriverContextP    ctx,
    VADisplayAttribute *attr_list,
    int                *num_attributes
)
{
    XVBA_DRIVER_DATA_INIT;

    if (ensure_display_attributes(driver_data) < 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    if (attr_list)
        memcpy(attr_list, driver_data->va_display_attrs,
               driver_data->va_display_attrs_count * sizeof(attr_list[0]));

    if (num_attributes)
        *num_attributes = driver_data->va_display_attrs_count;

    return VA_STATUS_SUCCESS;
}

// vaGetDisplayAttributes
VAStatus
xvba_GetDisplayAttributes(
    VADriverContextP    ctx,
    VADisplayAttribute *attr_list,
    int                 num_attributes
)
{
    XVBA_DRIVER_DATA_INIT;

    unsigned int i;
    for (i = 0; i < num_attributes; i++) {
        VADisplayAttribute * const dst_attr = &attr_list[i];
        VADisplayAttribute *src_attr;

        src_attr = get_display_attribute(driver_data, dst_attr->type);
        if (src_attr && (src_attr->flags & VA_DISPLAY_ATTRIB_GETTABLE) != 0) {
            dst_attr->min_value = src_attr->min_value;
            dst_attr->max_value = src_attr->max_value;
            dst_attr->value     = src_attr->value;
        }
        else
            dst_attr->flags    &= ~VA_DISPLAY_ATTRIB_GETTABLE;
    }
    return VA_STATUS_SUCCESS;
}

// Normalize -100..100 value into the specified range
static float get_normalized_value(
    const VADisplayAttribute *attr,
    float                     vstart,
    float                     vend,
    float                     vdefault
)
{
    return (vdefault + (attr->value / 100.0f) *
            (attr->value >= 0 ? (vend - vdefault) : (vdefault - vstart)));
}

// vaSetDisplayAttributes
VAStatus
xvba_SetDisplayAttributes(
    VADriverContextP    ctx,
    VADisplayAttribute *attr_list,
    int                 num_attributes
)
{
    XVBA_DRIVER_DATA_INIT;

    unsigned int i;
    for (i = 0; i < num_attributes; i++) {
        VADisplayAttribute * const src_attr = &attr_list[i];
        VADisplayAttribute *dst_attr;

        dst_attr = get_display_attribute(driver_data, src_attr->type);
        if (!dst_attr)
            return VA_STATUS_ERROR_ATTR_NOT_SUPPORTED;

        if ((dst_attr->flags & VA_DISPLAY_ATTRIB_SETTABLE) != 0) {
            int value_changed = dst_attr->value != src_attr->value;
            dst_attr->value = src_attr->value;

            int procamp_changed = 0;
            float value, start, end, def;
            switch (dst_attr->type) {
            case VADisplayAttribBackgroundColor:
                driver_data->va_background_color = dst_attr;
                break;
            case VADisplayAttribBrightness:
                if (value_changed) {
                    cm_get_brightness_range(&start, &end, &def);
                    value = get_normalized_value(dst_attr, start, end, def);
                    cm_set_brightness(driver_data->cm_brightness, value);
                    procamp_changed = 1;
                }
                break;
            case VADisplayAttribContrast:
                if (value_changed) {
                    cm_get_contrast_range(&start, &end, &def);
                    value = get_normalized_value(dst_attr, start, end, def);
                    cm_set_contrast(driver_data->cm_contrast, value);
                    procamp_changed = 1;
                }
                break;
            case VADisplayAttribSaturation:
                if (value_changed) {
                    cm_get_saturation_range(&start, &end, &def);
                    value = get_normalized_value(dst_attr, start, end, def);
                    cm_set_saturation(driver_data->cm_saturation, value);
                    procamp_changed = 1;
                }
                break;
            case VADisplayAttribHue:
                if (value_changed) {
                    cm_get_hue_range(&start, &end, &def);
                    value = get_normalized_value(dst_attr, start, end, def);
                    cm_set_hue(driver_data->cm_hue, value);
                    procamp_changed = 1;
                }
                break;
            default:
                break;
            }

            if (procamp_changed) {
                cm_composite(
                    driver_data->cm_composite,
                    driver_data->cm_brightness,
                    driver_data->cm_contrast,
                    driver_data->cm_saturation,
                    driver_data->cm_hue
                );
                driver_data->cm_composite_ok = 1;
            }

            static uint64_t mtime;
            const int dst_attr_index = dst_attr - driver_data->va_display_attrs;
            ASSERT(dst_attr_index < XVBA_MAX_DISPLAY_ATTRIBUTES);
            driver_data->va_display_attrs_mtime[dst_attr_index] = ++mtime;
        }
    }
    return VA_STATUS_SUCCESS;
}

// vaDbgCopySurfaceToBuffer (not a PUBLIC interface)
VAStatus
xvba_DbgCopySurfaceToBuffer(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    void              **buffer,
    unsigned int       *stride
)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}

#if VA_CHECK_VERSION(0,30,0)
// vaCreateSurfaceFromCIFrame
VAStatus
xvba_CreateSurfaceFromCIFrame(
    VADriverContextP    ctx,
    unsigned long       frame_id,
    VASurfaceID        *surface
)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}

// vaCreateSurfaceFromV4L2Buf
VAStatus
xvba_CreateSurfaceFromV4L2Buf(
    VADriverContextP    ctx,
    int                 v4l2_fd,
    struct v4l2_format *v4l2_fmt,
    struct v4l2_buffer *v4l2_buf,
    VASurfaceID        *surface
)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}

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
)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}
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
)
{
    if (fourcc)          *fourcc          = VA_FOURCC('N','V','1','2');
    if (luma_stride)     *luma_stride     = 0;
    if (chroma_u_stride) *chroma_u_stride = 0;
    if (chroma_v_stride) *chroma_v_stride = 0;
    if (luma_offset)     *luma_offset     = 0;
    if (chroma_u_offset) *chroma_u_offset = 0;
    if (chroma_v_offset) *chroma_v_offset = 0;
    if (buffer_name)     *buffer_name     = 0;
    if (buffer)          *buffer          = NULL;
    return VA_STATUS_SUCCESS;
}

// vaUnlockSurface
VAStatus
xvba_UnlockSurface(
    VADriverContextP    ctx,
    VASurfaceID         surface
)
{
    return VA_STATUS_SUCCESS;
}
#endif
