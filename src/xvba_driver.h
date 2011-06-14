/*
 *  xvba_driver.h - XvBA driver
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

#ifndef XVBA_DRIVER_H
#define XVBA_DRIVER_H

#include <va/va_backend.h>
#include "vaapi_compat.h"
#include "xvba_gate.h"
#include "object_heap.h"
#include "color_matrix.h"

#define XVBA_DRIVER_DATA_INIT                           \
        struct xvba_driver_data *driver_data =          \
            (struct xvba_driver_data *)ctx->pDriverData

#define XVBA_OBJECT(id, type) \
    ((object_##type##_p)object_heap_lookup(&driver_data->type##_heap, (id)))

#define XVBA_CONFIG(id)                 XVBA_OBJECT(id, config)
#define XVBA_CONTEXT(id)                XVBA_OBJECT(id, context)
#define XVBA_SURFACE(id)                XVBA_OBJECT(id, surface)
#define XVBA_BUFFER(id)                 XVBA_OBJECT(id, buffer)
#define XVBA_OUTPUT(id)                 XVBA_OBJECT(id, output)
#define XVBA_IMAGE(id)                  XVBA_OBJECT(id, image)
#define XVBA_SUBPICTURE(id)             XVBA_OBJECT(id, subpicture)

#define XVBA_CONFIG_ID_OFFSET           0x01000000
#define XVBA_CONTEXT_ID_OFFSET          0x02000000
#define XVBA_SURFACE_ID_OFFSET          0x03000000
#define XVBA_BUFFER_ID_OFFSET           0x04000000
#define XVBA_OUTPUT_ID_OFFSET           0x05000000
#define XVBA_IMAGE_ID_OFFSET            0x06000000
#define XVBA_SUBPICTURE_ID_OFFSET       0x07000000

#define XVBA_MAX_DELAYED_PICTURES       32
#define XVBA_MAX_PROFILES               12
#define XVBA_MAX_ENTRYPOINTS            5
#define XVBA_MAX_CONFIG_ATTRIBUTES      10
#define XVBA_MAX_IMAGE_FORMATS          10
#define XVBA_MAX_DISPLAY_ATTRIBUTES     6
#define XVBA_STR_DRIVER_VENDOR          "Splitted-Desktop Systems"
#define XVBA_STR_DRIVER_NAME            "XvBA backend for VA-API"

typedef struct xvba_driver_data xvba_driver_data_t;
struct xvba_driver_data {
    XVBAContext                *xvba_context;
    struct object_heap          config_heap;
    struct object_heap          context_heap;
    struct object_heap          surface_heap;
    struct object_heap          buffer_heap;
    struct object_heap          output_heap;
    struct object_heap          image_heap;
    struct object_heap          subpicture_heap;
    Display                    *x11_dpy;
    const char                 *x11_dpy_name;
    int                         x11_screen;
    Display                    *x11_dpy_local;
    XVBADecodeCap              *xvba_decode_caps;
    unsigned int                xvba_decode_caps_count;
    XVBASurfaceCap             *xvba_surface_caps;
    unsigned int                xvba_surface_caps_count;
    VADisplayAttribute         *va_background_color;
    VADisplayAttribute          va_display_attrs[XVBA_MAX_DISPLAY_ATTRIBUTES];
    uint64_t                    va_display_attrs_mtime[XVBA_MAX_DISPLAY_ATTRIBUTES];
    unsigned int                va_display_attrs_count;
    unsigned int                va_display_type;
    ColorMatrix                 cm_brightness;
    ColorMatrix                 cm_contrast;
    ColorMatrix                 cm_saturation;
    ColorMatrix                 cm_hue;
    ColorMatrix                 cm_composite;
    unsigned int                cm_composite_ok;
    char                        va_vendor[256];
    unsigned int                device_id;
    unsigned int                is_evergreen_gpu      : 1;
    unsigned int                is_fusion_igp         : 1;
    unsigned int                warn_h264_over_hp_l41 : 1;
    unsigned int                warn_vc1_over_ap_l3   : 1;
};

typedef struct object_config   *object_config_p;
typedef struct object_context  *object_context_p;
typedef struct object_surface  *object_surface_p;
typedef struct object_buffer   *object_buffer_p;
typedef struct object_output   *object_output_p;
typedef struct object_image    *object_image_p;

// Set display type
int xvba_set_display_type(xvba_driver_data_t *driver_data, unsigned int type)
    attribute_hidden;

#endif /* XVBA_DRIVER_H */
