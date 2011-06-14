/*
 *  xvba_driver.c - XvBA driver
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
#include "xvba_driver.h"
#include "xvba_buffer.h"
#include "xvba_decode.h"
#include "xvba_image.h"
#include "xvba_subpic.h"
#include "xvba_video.h"
#include "xvba_video_x11.h"
#if USE_GLX
#include "xvba_video_glx.h"
#include <va/va_backend_glx.h>
#endif
#include "fglrxinfo.h"

#define DEBUG 1
#include "debug.h"


// Set display type
int xvba_set_display_type(xvba_driver_data_t *driver_data, unsigned int type)
{
    if (driver_data->va_display_type == 0) {
        driver_data->va_display_type = type;
        return 1;
    }
    return driver_data->va_display_type == type;
}

// Destroy BUFFER objects
static void destroy_buffer_cb(object_base_p obj, void *user_data)
{
    object_buffer_p const obj_buffer = (object_buffer_p)obj;
    xvba_driver_data_t * const driver_data = user_data;

    destroy_va_buffer(driver_data, obj_buffer);
}

// Destroy object heap
typedef void (*destroy_heap_func_t)(object_base_p obj, void *user_data);

static void
destroy_heap(
    const char         *name,
    object_heap_p       heap,
    destroy_heap_func_t destroy_func,
    void               *user_data
)
{
    object_base_p obj;
    object_heap_iterator iter;

    if (!heap)
        return;

    obj = object_heap_first(heap, &iter);
    while (obj) {
        xvba_information_message("vaTerminate(): %s ID 0x%08x is still allocated, destroying\n", name, obj->id);
        if (destroy_func)
            destroy_func(obj, user_data);
        else
            object_heap_free(heap, obj);
        obj = object_heap_next(heap, &iter);
    }
    object_heap_destroy(heap);
}

#define DESTROY_HEAP(heap, func) \
        destroy_heap(#heap, &driver_data->heap##_heap, func, driver_data)

#define CREATE_HEAP(type, id) do {                                  \
        int result = object_heap_init(&driver_data->type##_heap,    \
                                      sizeof(struct object_##type), \
                                      XVBA_##id##_ID_OFFSET);       \
        ASSERT(result == 0);                                        \
        if (result != 0)                                            \
            return VA_STATUS_ERROR_ALLOCATION_FAILED;               \
    } while (0)

// vaTerminate
static void xvba_common_Terminate(xvba_driver_data_t *driver_data)
{
    if (driver_data->xvba_decode_caps) {
        free(driver_data->xvba_decode_caps);
        driver_data->xvba_decode_caps = NULL;
        driver_data->xvba_decode_caps_count = 0;
    }

    if (driver_data->xvba_surface_caps) {
        free(driver_data->xvba_surface_caps);
        driver_data->xvba_surface_caps = NULL;
        driver_data->xvba_surface_caps_count = 0;
    }

    DESTROY_HEAP(buffer,        destroy_buffer_cb);
    DESTROY_HEAP(output,        NULL);
    DESTROY_HEAP(image,         NULL);
    DESTROY_HEAP(subpicture,    NULL);
    DESTROY_HEAP(surface,       NULL);
    DESTROY_HEAP(context,       NULL);
    DESTROY_HEAP(config,        NULL);

    if (driver_data->xvba_context) {
        xvba_destroy_context(driver_data->xvba_context);
        driver_data->xvba_context = NULL;
    }

    xvba_gate_exit();
}

// vaInitialize
static VAStatus xvba_common_Initialize(xvba_driver_data_t *driver_data)
{
    int xvba_version;
    int fglrx_major_version, fglrx_minor_version, fglrx_micro_version;
    unsigned int device_id;

    driver_data->x11_dpy_local = XOpenDisplay(driver_data->x11_dpy_name);
    if (!driver_data->x11_dpy_local)
        return VA_STATUS_ERROR_UNKNOWN;

    if (!fglrx_is_dri_capable(driver_data->x11_dpy, driver_data->x11_screen))
        return VA_STATUS_ERROR_UNKNOWN;

    if (!fglrx_get_version(driver_data->x11_dpy, driver_data->x11_screen,
                           &fglrx_major_version,
                           &fglrx_minor_version,
                           &fglrx_micro_version))
        return VA_STATUS_ERROR_UNKNOWN;
    D(bug("FGLRX driver version %d.%d.%d detected\n",
          fglrx_major_version, fglrx_minor_version, fglrx_micro_version));

    if (!fglrx_check_version(8,80,5)) {
        xvba_error_message("FGLRX driver version 8.80.5 (Catalyst 10.12) or later is required\n");
        return VA_STATUS_ERROR_UNKNOWN;
    }

    if (!fglrx_get_device_id(driver_data->x11_dpy, driver_data->x11_screen,
                             &device_id))
        return VA_STATUS_ERROR_UNKNOWN;
    D(bug("FGLRX device ID 0x%04x\n", device_id));
    driver_data->device_id = device_id;
    switch (device_id & 0xff00) {
    case 0x6700: // Radeon HD 6000 series
    case 0x6800: // Radeon HD 5000 series
        D(bug("Evergreen GPU detected\n"));
        driver_data->is_evergreen_gpu = 1;
        break;
    case 0x9800: // Fusion series
        D(bug("Fusion IGP detected\n"));
        driver_data->is_evergreen_gpu = 1;
        driver_data->is_fusion_igp    = 1;
        break;
    }

    if (xvba_gate_init() < 0)
        return VA_STATUS_ERROR_UNKNOWN;

    if (xvba_query_extension(driver_data->x11_dpy, &xvba_version) < 0)
        return VA_STATUS_ERROR_UNKNOWN;
    D(bug("XvBA version %d.%d detected\n",
          (xvba_version >> 16) & 0xffff, xvba_version & 0xffff));

    if (!xvba_check_version(0,74)) {
        xvba_information_message("Please upgrade to XvBA >= 0.74\n");
        return VA_STATUS_ERROR_UNIMPLEMENTED;
    }

    driver_data->xvba_context = xvba_create_context(driver_data->x11_dpy, None);
    if (!driver_data->xvba_context)
        return VA_STATUS_ERROR_UNKNOWN;

    sprintf(driver_data->va_vendor, "%s %s - %d.%d.%d",
            XVBA_STR_DRIVER_VENDOR,
            XVBA_STR_DRIVER_NAME,
            XVBA_VIDEO_MAJOR_VERSION,
            XVBA_VIDEO_MINOR_VERSION,
            XVBA_VIDEO_MICRO_VERSION);

    if (XVBA_VIDEO_PRE_VERSION > 0) {
        const int len = strlen(driver_data->va_vendor);
        sprintf(&driver_data->va_vendor[len], ".pre%d", XVBA_VIDEO_PRE_VERSION);
    }

    CREATE_HEAP(config,         CONFIG);
    CREATE_HEAP(context,        CONTEXT);
    CREATE_HEAP(surface,        SURFACE);
    CREATE_HEAP(buffer,         BUFFER);
    CREATE_HEAP(output,         OUTPUT);
    CREATE_HEAP(image,          IMAGE);
    CREATE_HEAP(subpicture,     SUBPICTURE);

    return VA_STATUS_SUCCESS;
}

#if VA_MAJOR_VERSION == 0 && VA_MINOR_VERSION >= 31
#define VA_INIT_VERSION_MAJOR   0
#define VA_INIT_VERSION_MINOR   31
#define VA_INIT_VERSION_MICRO   0
#define VA_INIT_SUFFIX          0_31_0
#include "xvba_driver_template.h"

#define VA_INIT_VERSION_MAJOR   0
#define VA_INIT_VERSION_MINOR   31
#define VA_INIT_VERSION_MICRO   1
#define VA_INIT_SUFFIX          0_31_1
#define VA_INIT_GLX             USE_GLX
#include "xvba_driver_template.h"

#define VA_INIT_VERSION_MAJOR   0
#define VA_INIT_VERSION_MINOR   31
#define VA_INIT_VERSION_MICRO   2
#define VA_INIT_SUFFIX          0_31_2
#define VA_INIT_GLX             USE_GLX
#include "xvba_driver_template.h"

VAStatus __vaDriverInit_0_31(void *ctx)
{
    VADriverContextP_0_31_0 const ctx0 = ctx;
    VADriverContextP_0_31_1 const ctx1 = ctx;
    VADriverContextP_0_31_2 const ctx2 = ctx;

    /* Assume a NULL display implies VA-API 0.31.1 struct with the
       vtable_tpi field placed just after the vtable, thus replacing
       original native_dpy field */
    if (ctx0->native_dpy)
        return xvba_Initialize_0_31_0(ctx);
    if (ctx1->native_dpy)
        return xvba_Initialize_0_31_1(ctx);
    if (ctx2->native_dpy)
        return xvba_Initialize_0_31_2(ctx);
    return VA_STATUS_ERROR_INVALID_DISPLAY;
}
#endif

#if VA_MAJOR_VERSION == 0 && VA_MINOR_VERSION >= 32
#define VA_INIT_VERSION_MAJOR   0
#define VA_INIT_VERSION_MINOR   32
#define VA_INIT_VERSION_MICRO   0
#define VA_INIT_SUFFIX          0_32_0
#define VA_INIT_GLX             USE_GLX
#include "xvba_driver_template.h"

VAStatus __vaDriverInit_0_32(void *ctx)
{
    return xvba_Initialize_0_32_0(ctx);
}
#endif

#define VA_INIT_VERSION_MAJOR   VA_MAJOR_VERSION
#define VA_INIT_VERSION_MINOR   VA_MINOR_VERSION
#define VA_INIT_VERSION_MICRO   VA_MICRO_VERSION
#define VA_INIT_VERSION_SDS     VA_SDS_VERSION
#define VA_INIT_GLX             USE_GLX
#include "xvba_driver_template.h"

VAStatus VA_DRIVER_INIT_FUNC(void *ctx)
{
#if VA_MAJOR_VERSION == 0 && VA_MINOR_VERSION == 31
    return __vaDriverInit_0_31(ctx);
#endif
    return xvba_Initialize_Current(ctx);
}
