/*
 *  xvba_buffer.c - XvBA backend for VA-API (VA buffers)
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
#include "xvba_buffer.h"
#include "xvba_driver.h"
#include "xvba_decode.h"
#include "xvba_video.h"
#include "xvba_dump.h"
#include "fglrxinfo.h"
#include <math.h>

#define DEBUG 1
#include "debug.h"


// Definitions for XVBAPictureDescriptor.picture_structure field
enum {
    PICT_TOP_FIELD      = 0,
    PICT_BOTTOM_FIELD   = 1,
    PICT_FRAME          = 3
};

// Reconstruct H.264 level_idc syntax element
static int
vaapi_h264_get_level(
    VAPictureParameterBufferH264 *pic_param,
    unsigned int                  picture_width,
    unsigned int                  picture_height
)
{
    const unsigned int PicWidthInMbs = pic_param->picture_width_in_mbs_minus1 + 1;
    const unsigned int FrameHeightInMbs = (picture_height + 15) / 16;

    /* Table A-1. Level limits */
    static const struct {
        int          level;
        unsigned int MaxMBPS;
        unsigned int MaxFS;
        unsigned int MaxDPBx2;
        unsigned int MaxBR;
        unsigned int MaxCPB;
    }
    map[] = {
        { 10,   1485,   99,     297,     64,    175 },
        {  9,   1485,   99,     297,    128,    350 },
        { 11,   3000,   396,    675,    192,    500 },
        { 12,   6000,   396,   1782,    384,   1000 },
        { 13,  11880,   396,   1782,    768,   2000 },
        { 20,  11880,   396,   1782,   2000,   2000 },
        { 21,  19800,   792,   3564,   4000,   4000 },
        { 22,  20250,  1620,   6075,   4000,   4000 },
        { 30,  40500,  1620,   6075,  10000,  10000 },
        { 31, 108000,  3600,  13500,  14000,  14000 },
        { 32, 216000,  5120,  15360,  20000,  20000 },
        { 40, 245760,  8192,  24576,  20000,  25000 },
        { 41, 245760,  8192,  24576,  50000,  62500 },
        { 42, 522240,  8704,  26112,  50000,  62500 },
        { 50, 589824, 22080,  82800, 135000, 135000 },
        { 51, 983040, 36864, 138240, 240000, 240000 },
        { -1, }
    };

    int i;
    for (i = 0; map[i].level >= 0; i++) {
        if (map[i].MaxFS < PicWidthInMbs * FrameHeightInMbs)    /* A.3.1 (e) */
            continue;
        const unsigned int sqrt_8xMaxFS = sqrt(8.0 * map[i].MaxFS);
        if (sqrt_8xMaxFS < PicWidthInMbs)                       /* A.3.1 (f) */
            continue;
        if (sqrt_8xMaxFS < FrameHeightInMbs)                    /* A.3.1 (g) */
            continue;
        const unsigned int max_ref_frames = map[i].MaxDPBx2 * 512 / (PicWidthInMbs * FrameHeightInMbs * 384);
        if (max_ref_frames < pic_param->num_ref_frames)         /* A.3.1 (h) */
            continue;
        return map[i].level;
    }
    D(bug("vaapi_h264_get_level(): could not reconstruct level\n"));
    return 0;
}

// Reconstruct picture_structure
static int
vaapi_h264_get_picture_structure(VAPictureParameterBufferH264 *pic_param)
{
    int picture_structure;
    switch (pic_param->CurrPic.flags & (VA_PICTURE_H264_TOP_FIELD |
                                        VA_PICTURE_H264_BOTTOM_FIELD)) {
    case VA_PICTURE_H264_TOP_FIELD:
        picture_structure = PICT_TOP_FIELD;
        break;
    case VA_PICTURE_H264_BOTTOM_FIELD:
        picture_structure = PICT_BOTTOM_FIELD;
        break;
    default: /* 0 || VA_PICTURE_H264_TOP_FIELD|VA_PICTURE_H264_BOTTOM_FIELD */
        picture_structure = PICT_FRAME;
        break;
    }
    return picture_structure;
}

// Reconstruct VC-1 LEVEL syntax element
static int
vaapi_vc1_get_level_advanced(
    VAPictureParameterBufferVC1 *pic_param,
    unsigned int                 picture_width,
    unsigned int                 picture_height
)
{
    /* XXX: use CODED_WIDTH and CODED_HEIGHT syntax elements instead? */
    const unsigned int mb_width  = (picture_width + 15) / 16;
    const unsigned int mb_height = (picture_height + 15) / 16;
    const unsigned int MB_f      = mb_width * mb_height;
    const unsigned int MB_s      = MB_f * 30; /* default to 30 Hz */

    /* Table 253. Limitations of profiles and levels */
    static const struct {
        int          level;
        unsigned int MB_s;
        unsigned int MB_f;
        int          interlace;
        unsigned int Rmax;
        unsigned int Bmax;
    }
    map[] = {
        { 0,  11880,   396, 0,   2000,   250 },
        { 1,  48600,  1620, 1,  10000,  1250 },
        { 2, 110400,  3680, 1,  20000,  2500 },
        { 3, 245760,  8192, 1,  45000,  5500 },
        { 4, 491520, 16384, 1, 135000, 16500 },
        { -1, }
    };

    int i;
    for (i = 0; map[i].level >= 0; i++) {
        if (pic_param->sequence_fields.bits.interlace && !map[i].interlace)
            continue;
        if (map[i].MB_f < MB_f)
            continue;
        if (map[i].MB_s < MB_s)
            continue;
        return map[i].level;
    }
    D(bug("vaapi_vc1_get_level_advanced(): could not reconstruct level\n"));
    return 0;
}

// Reconstruct picture_structure
static int
vaapi_vc1_get_picture_structure(VAPictureParameterBufferVC1 *pic_param)
{
    int picture_structure;
    if (pic_param->sequence_fields.bits.interlace &&
        pic_param->picture_fields.bits.frame_coding_mode == 3) {
        /* Field-Interlace mode */
        if (pic_param->picture_fields.bits.is_first_field &&
            pic_param->picture_fields.bits.top_field_first)
            picture_structure = PICT_TOP_FIELD;
        else
            picture_structure = PICT_BOTTOM_FIELD;
    }
    else
        picture_structure = PICT_FRAME;
    return picture_structure;
}

// Create VA buffer object
object_buffer_p
create_va_buffer(
    xvba_driver_data_t *driver_data,
    VAContextID         context,
    VABufferType        buffer_type,
    unsigned int        num_elements,
    unsigned int        size
)
{
    VABufferID buffer_id;
    object_buffer_p obj_buffer;

    buffer_id = object_heap_allocate(&driver_data->buffer_heap);
    if (buffer_id == VA_INVALID_BUFFER)
        return NULL;

    obj_buffer = XVBA_BUFFER(buffer_id);
    if (!obj_buffer)
        return NULL;

    obj_buffer->va_context       = context;
    obj_buffer->type             = buffer_type;
    obj_buffer->max_num_elements = num_elements;
    obj_buffer->num_elements     = num_elements;
    obj_buffer->buffer_size      = size * num_elements;
    obj_buffer->buffer_data      = malloc(obj_buffer->buffer_size);
    obj_buffer->mtime            = 0;

    if (!obj_buffer->buffer_data) {
        destroy_va_buffer(driver_data, obj_buffer);
        return NULL;
    }
    return obj_buffer;
}

// Destroy VA buffer object
void
destroy_va_buffer(
    xvba_driver_data_t *driver_data,
    object_buffer_p     obj_buffer
)
{
    if (!obj_buffer)
        return;

    if (obj_buffer->buffer_data) {
        free(obj_buffer->buffer_data);
        obj_buffer->buffer_data = NULL;
    }
    object_heap_free(&driver_data->buffer_heap, &obj_buffer->base);
}

// Destroy VA buffer objects stored in VA context
void
destroy_va_buffers(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context
)
{
    unsigned int i;

    for (i = 0; i < obj_context->va_buffers_count; i++) {
        object_buffer_p obj_buffer = XVBA_BUFFER(obj_context->va_buffers[i]);
        if (!obj_buffer)
            continue;
        destroy_va_buffer(driver_data, obj_buffer);
    }
    obj_context->va_buffers_count = 0;
}

// Determines whether BUFFER is queued for decoding
int
is_queued_buffer(
    xvba_driver_data_t *driver_data,
    object_buffer_p     obj_buffer
)
{
    object_context_p obj_context = XVBA_CONTEXT(obj_buffer->va_context);
    if (!obj_context)
        return 0;

    unsigned int i;
    for (i = 0; i < obj_context->va_buffers_count; i++) {
        if (obj_context->va_buffers[i] == obj_buffer->base.id)
            return 1;
    }
    return 0;
}

// Translate no buffer
static int
translate_nothing(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
)
{
    return 1;
}

// Translate VASliceDataBuffer
static int
translate_VASliceDataBuffer(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
)
{
    /* Slice data buffers are copied in VASliceParameterBuffer
       translation routines */
    return 1;
}

// Translate VAPictureParameterBufferMPEG2
static int
translate_VAPictureParameterBufferMPEG2(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
)
{
    return 0;
}

// Translate VAIQMatrixBufferMPEG2
static int
translate_VAIQMatrixBufferMPEG2(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
)
{
    return 0;
}

// Translate VASliceParameterBufferMPEG2
static int
translate_VASliceParameterBufferMPEG2(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
    )
{
    return 0;
}

// Translate VAPictureParameterBufferH264
static int
translate_VAPictureParameterBufferH264(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
)
{
    VAPictureParameterBufferH264 * const pic_param = obj_buffer->buffer_data;

    object_surface_p obj_surface = XVBA_SURFACE(obj_context->current_render_target);
    if (!obj_surface)
        return 0;

    object_config_p obj_config = XVBA_CONFIG(obj_context->va_config);
    if (!obj_config)
        return 0;

    int profile, level;
    switch (obj_config->profile) {
    case VAProfileH264Baseline:
        profile = XVBA_H264_BASELINE;
        break;
    case VAProfileH264Main:
        profile = XVBA_H264_MAIN;
        break;
    case VAProfileH264High:
        profile = XVBA_H264_HIGH;
        break;
    default:
        D(bug("translate_VAPictureParameterBufferH264(): invalid profile\n"));
        return 0;
    }

    level = vaapi_h264_get_level(
        pic_param,
        obj_context->picture_width,
        obj_context->picture_height
    );

    /* Check for H.264 content over HP@L4.1 */
    unsigned int num_ref_frames = pic_param->num_ref_frames;
    if (profile == XVBA_H264_HIGH && level > 41) {
        if (!driver_data->warn_h264_over_hp_l41) {
            driver_data->warn_h264_over_hp_l41 = 1;
            xvba_information_message(
                "driver does not support H.264 content over HP@L4.1. "
                "Please upgrade.\n"
            );
        }

        /* Use fail-safe values (lower ref frames) */
        const unsigned int mbw = pic_param->picture_width_in_mbs_minus1 + 1;
        const unsigned int mbh = (obj_context->picture_height + 15) / 16;
        const unsigned int max_ref_frames = 12288 * 1024 / (mbw * mbh * 384);
        if (max_ref_frames < num_ref_frames)
            num_ref_frames = max_ref_frames;
    }

    XVBABufferDescriptor * const xvba_buffer = obj_surface->pic_desc_buffer;
    ASSERT(xvba_buffer);
    if (!xvba_buffer)
        return 0;

    XVBAPictureDescriptor * const pic_desc = xvba_buffer->bufferXVBA;
    memset(pic_desc, 0, sizeof(*pic_desc));

    pic_desc->past_surface                                      = NULL;
    pic_desc->future_surface                                    = NULL;
    pic_desc->profile                                           = profile;
    pic_desc->level                                             = level;
    pic_desc->width_in_mb                                       = 1 + pic_param->picture_width_in_mbs_minus1;
    pic_desc->height_in_mb                                      = 1 + pic_param->picture_height_in_mbs_minus1;
    pic_desc->picture_structure                                 = vaapi_h264_get_picture_structure(pic_param);
    pic_desc->sps_info.flags                                    = 0; /* reset all bits */
    pic_desc->sps_info.avc.residual_colour_transform_flag       = pic_param->seq_fields.bits.residual_colour_transform_flag;
    pic_desc->sps_info.avc.delta_pic_always_zero_flag           = pic_param->seq_fields.bits.delta_pic_order_always_zero_flag;
    pic_desc->sps_info.avc.gaps_in_frame_num_value_allowed_flag = pic_param->seq_fields.bits.gaps_in_frame_num_value_allowed_flag;
    pic_desc->sps_info.avc.frame_mbs_only_flag                  = pic_param->seq_fields.bits.frame_mbs_only_flag;
    pic_desc->sps_info.avc.mb_adaptive_frame_field_flag         = pic_param->seq_fields.bits.mb_adaptive_frame_field_flag;
    pic_desc->sps_info.avc.direct_8x8_inference_flag            = pic_param->seq_fields.bits.direct_8x8_inference_flag;
    pic_desc->chroma_format                                     = pic_param->seq_fields.bits.chroma_format_idc;
    pic_desc->avc_bit_depth_luma_minus8                         = pic_param->bit_depth_luma_minus8;
    pic_desc->avc_bit_depth_chroma_minus8                       = pic_param->bit_depth_chroma_minus8;
    pic_desc->avc_log2_max_frame_num_minus4                     = pic_param->seq_fields.bits.log2_max_frame_num_minus4;
    pic_desc->avc_pic_order_cnt_type                            = pic_param->seq_fields.bits.pic_order_cnt_type;
    pic_desc->avc_log2_max_pic_order_cnt_lsb_minus4             = pic_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4;
    pic_desc->avc_num_ref_frames                                = num_ref_frames;
    pic_desc->pps_info.flags                                    = 0; /* reset all bits */
    pic_desc->pps_info.avc.entropy_coding_mode_flag             = pic_param->pic_fields.bits.entropy_coding_mode_flag;
    pic_desc->pps_info.avc.pic_order_present_flag               = pic_param->pic_fields.bits.pic_order_present_flag;
    pic_desc->pps_info.avc.weighted_pred_flag                   = pic_param->pic_fields.bits.weighted_pred_flag;
    pic_desc->pps_info.avc.weighted_bipred_idc                  = pic_param->pic_fields.bits.weighted_bipred_idc;
    pic_desc->pps_info.avc.deblocking_filter_control_present_flag = pic_param->pic_fields.bits.deblocking_filter_control_present_flag;
    pic_desc->pps_info.avc.constrained_intra_pred_flag          = pic_param->pic_fields.bits.constrained_intra_pred_flag;
    pic_desc->pps_info.avc.redundant_pic_cnt_present_flag       = pic_param->pic_fields.bits.redundant_pic_cnt_present_flag;
    pic_desc->pps_info.avc.transform_8x8_mode_flag              = pic_param->pic_fields.bits.transform_8x8_mode_flag;
    pic_desc->avc_num_slice_groups_minus1                       = pic_param->num_slice_groups_minus1;
    pic_desc->avc_slice_group_map_type                          = pic_param->slice_group_map_type;
    pic_desc->avc_pic_init_qp_minus26                           = pic_param->pic_init_qp_minus26;
    pic_desc->avc_pic_init_qs_minus26                           = pic_param->pic_init_qs_minus26;
    pic_desc->avc_chroma_qp_index_offset                        = pic_param->chroma_qp_index_offset;
    pic_desc->avc_second_chroma_qp_index_offset                 = pic_param->second_chroma_qp_index_offset;
    pic_desc->avc_slice_group_change_rate_minus1                = pic_param->slice_group_change_rate_minus1;
    pic_desc->avc_frame_num                                     = pic_param->frame_num;
    pic_desc->avc_reference                                     = pic_param->pic_fields.bits.reference_pic_flag;

    pic_desc->avc_curr_field_order_cnt_list[0]                  = pic_param->CurrPic.TopFieldOrderCnt;
    pic_desc->avc_curr_field_order_cnt_list[1]                  = pic_param->CurrPic.BottomFieldOrderCnt;

    xvba_buffer->data_size_in_buffer = sizeof(*pic_desc);
    return 1;
}

// Translate VAIQMatrixBufferH264
static int
translate_VAIQMatrixBufferH264(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
)
{
    VAIQMatrixBufferH264 * const iq_matrix = obj_buffer->buffer_data;

    object_surface_p obj_surface = XVBA_SURFACE(obj_context->current_render_target);
    if (!obj_surface)
        return 0;

    XVBABufferDescriptor * const xvba_buffer = obj_surface->iq_matrix_buffer;
    if (!xvba_buffer)
        return 0;

    XVBAQuantMatrixAvc * const qm = xvba_buffer->bufferXVBA;
    int i, j;

    if (sizeof(qm->bScalingLists4x4) == sizeof(iq_matrix->ScalingList4x4))
        memcpy(qm->bScalingLists4x4, iq_matrix->ScalingList4x4,
               sizeof(qm->bScalingLists4x4));
    else {
        for (j = 0; j < 6; j++) {
            for (i = 0; i < 16; i++)
                qm->bScalingLists4x4[j][i] = iq_matrix->ScalingList4x4[j][i];
        }
    }

    if (sizeof(qm->bScalingLists8x8) == sizeof(iq_matrix->ScalingList8x8))
        memcpy(qm->bScalingLists8x8, iq_matrix->ScalingList8x8,
               sizeof(qm->bScalingLists8x8));
    else {
        for (j = 0; j < 2; j++) {
            for (i = 0; i < 64; i++)
                qm->bScalingLists8x8[j][i] = iq_matrix->ScalingList8x8[j][i];
        }
    }

    xvba_buffer->data_size_in_buffer = sizeof(*qm);
    return 1;
}

// Translate VASliceParameterBufferH264
static int
translate_VASliceParameterBufferH264(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
)
{
    VASliceParameterBufferH264 * const slice_param = obj_buffer->buffer_data;

    /* XXX: we don't handle partial slice data buffers yet */
    if (slice_param->slice_data_flag != VA_SLICE_DATA_FLAG_ALL) {
        D(bug("partial slice data buffers are not handled\n"));
        return 0;
    }

    object_surface_p obj_surface = XVBA_SURFACE(obj_context->current_render_target);
    if (!obj_surface)
        return 0;

    XVBABufferDescriptor * const xvba_pic_desc_buffer = obj_surface->pic_desc_buffer;
    if (!xvba_pic_desc_buffer)
        return 0;

    XVBAPictureDescriptor * const pic_desc = xvba_pic_desc_buffer->bufferXVBA;
    pic_desc->avc_intra_flag                   = slice_param->slice_type == 2; /* I-type */
    pic_desc->avc_num_ref_idx_l0_active_minus1 = slice_param->num_ref_idx_l0_active_minus1;
    pic_desc->avc_num_ref_idx_l1_active_minus1 = slice_param->num_ref_idx_l1_active_minus1;

    object_buffer_p data_buffer = obj_context->data_buffer;
    ASSERT(data_buffer);
    if (!data_buffer)
        return 0;
    ASSERT(slice_param->slice_data_offset + slice_param->slice_data_size <= data_buffer->buffer_size);
    if (slice_param->slice_data_offset + slice_param->slice_data_size > data_buffer->buffer_size)
        return 0;

    XVBABufferDescriptor * const xvba_data_buffer = obj_surface->data_buffer;
    ASSERT(xvba_data_buffer);
    if (!xvba_data_buffer)
        return 0;

    XVBABufferDescriptor * const xvba_data_ctrl_buffer = obj_surface->data_ctrl_buffers[obj_context->slice_count++];
    ASSERT(xvba_data_ctrl_buffer);
    if (!xvba_data_ctrl_buffer)
        return 0;

    XVBADataCtrl * const data_ctrl = xvba_data_ctrl_buffer->bufferXVBA;
    const unsigned int data_offset = xvba_data_buffer->data_size_in_buffer;

    const uint8_t * const va_slice_data = ((uint8_t *)data_buffer->buffer_data +
                                           slice_param->slice_data_offset);

    static const uint8_t start_code_prefix_one_3byte[3] = { 0x00, 0x00, 0x01 };
    if (memcmp(va_slice_data, start_code_prefix_one_3byte, 3) != 0)
        append_buffer(xvba_data_buffer, start_code_prefix_one_3byte, 3);
    append_buffer(xvba_data_buffer, va_slice_data, slice_param->slice_data_size);

    /* XXX: should XVBA_DATA_CTRL_BUFFER.SliceBytesInBuffer and
       SliceBitsInBuffer be required to account for padding bytes too? */
    data_ctrl->SliceDataLocation   = data_offset;
    data_ctrl->SliceBytesInBuffer  = xvba_data_buffer->data_size_in_buffer - data_offset;
    data_ctrl->SliceBitsInBuffer   = 8 * data_ctrl->SliceBytesInBuffer;
    pad_buffer(xvba_data_buffer);

    xvba_data_ctrl_buffer->data_size_in_buffer = sizeof(*data_ctrl);
    return 1;
}

// Translate VAPictureParameterBufferVC1
static int
translate_VAPictureParameterBufferVC1(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
)
{
    VAPictureParameterBufferVC1 * const pic_param = obj_buffer->buffer_data;

    object_surface_p obj_surface = XVBA_SURFACE(obj_context->current_render_target);
    if (!obj_surface)
        return 0;

    object_config_p obj_config = XVBA_CONFIG(obj_context->va_config);
    if (!obj_config)
        return 0;

    int profile, level = 0;
    switch (obj_config->profile) {
    case VAProfileVC1Simple:
        profile = XVBA_VC1_SIMPLE;
        break;
    case VAProfileVC1Main:
        profile = XVBA_VC1_MAIN;
        break;
    case VAProfileVC1Advanced:
        profile = XVBA_VC1_ADVANCED;
        level   = vaapi_vc1_get_level_advanced(
            pic_param,
            obj_context->picture_width,
            obj_context->picture_height
        );
        break;
    default:
        D(bug("translate_VAPictureParameterBufferVC1(): invalid profile\n"));
        return 0;
    }

    /* Check for VC-1 content over AP@L3 */
    if (profile == XVBA_VC1_ADVANCED && level > 3) {
        if (!driver_data->warn_vc1_over_ap_l3) {
            driver_data->warn_vc1_over_ap_l3 = 1;
            xvba_information_message(
                "driver does not support VC-1 content over AP@L3. "
                "Please upgrade.\n"
            );
        }
    }

    XVBAPictureDescriptor * const pic_desc = obj_surface->pic_desc_buffer->bufferXVBA;
    memset(pic_desc, 0, sizeof(*pic_desc));

    pic_desc->past_surface                      = NULL;
    pic_desc->future_surface                    = NULL;
    pic_desc->profile                           = profile;
    pic_desc->level                             = level;
    pic_desc->width_in_mb                       = pic_param->coded_width;
    pic_desc->height_in_mb                      = pic_param->coded_height;
    pic_desc->picture_structure                 = vaapi_vc1_get_picture_structure(pic_param);
    pic_desc->sps_info.flags                    = 0; /* reset all bits */
    pic_desc->sps_info.vc1.postprocflag         = pic_param->post_processing != 0;
    pic_desc->sps_info.vc1.pulldown             = pic_param->sequence_fields.bits.pulldown;
    pic_desc->sps_info.vc1.interlace            = pic_param->sequence_fields.bits.interlace;
    pic_desc->sps_info.vc1.tfcntrflag           = pic_param->sequence_fields.bits.tfcntrflag;
    pic_desc->sps_info.vc1.finterpflag          = pic_param->sequence_fields.bits.finterpflag;
    pic_desc->sps_info.vc1.psf                  = pic_param->sequence_fields.bits.psf;
    pic_desc->sps_info.vc1.second_field         = !pic_param->picture_fields.bits.is_first_field;
    pic_desc->chroma_format                     = 1; /* XXX: 4:2:0 */
    pic_desc->pps_info.flags                    = 0; /* reset all bits */
    pic_desc->pps_info.vc1.panscan_flag         = pic_param->entrypoint_fields.bits.panscan_flag;
    pic_desc->pps_info.vc1.refdist_flag         = pic_param->reference_fields.bits.reference_distance_flag;
    pic_desc->pps_info.vc1.loopfilter           = pic_param->entrypoint_fields.bits.loopfilter;
    pic_desc->pps_info.vc1.fastuvmc             = pic_param->fast_uvmc_flag;
    pic_desc->pps_info.vc1.extended_mv          = pic_param->mv_fields.bits.extended_mv_flag;
    pic_desc->pps_info.vc1.dquant               = pic_param->pic_quantizer_fields.bits.dquant;
    pic_desc->pps_info.vc1.vstransform          = pic_param->transform_fields.bits.variable_sized_transform_flag;
    pic_desc->pps_info.vc1.overlap              = pic_param->conditional_overlap_flag;
    pic_desc->pps_info.vc1.quantizer            = pic_param->pic_quantizer_fields.bits.quantizer;
    pic_desc->pps_info.vc1.extended_dmv         = pic_param->mv_fields.bits.extended_dmv_flag;
    pic_desc->pps_info.vc1.maxbframes           = pic_param->sequence_fields.bits.max_b_frames;
    pic_desc->pps_info.vc1.rangered             = pic_param->sequence_fields.bits.rangered;
    pic_desc->pps_info.vc1.syncmarker           = pic_param->sequence_fields.bits.syncmarker;
    pic_desc->pps_info.vc1.multires             = pic_param->sequence_fields.bits.multires;
    pic_desc->pps_info.vc1.range_mapy_flag      = pic_param->range_mapping_fields.bits.luma_flag;
    pic_desc->pps_info.vc1.range_mapy           = pic_param->range_mapping_fields.bits.luma;
    pic_desc->pps_info.vc1.range_mapuv_flag     = pic_param->range_mapping_fields.bits.chroma_flag;
    pic_desc->pps_info.vc1.range_mapuv          = pic_param->range_mapping_fields.bits.chroma;

    if (pic_param->backward_reference_picture != VA_INVALID_SURFACE) {
        object_surface_p s = XVBA_SURFACE(pic_param->backward_reference_picture);
        ASSERT(s);
        if (!s)
            return 0;
        pic_desc->past_surface = s->xvba_surface;
    }

    if (pic_param->forward_reference_picture != VA_INVALID_SURFACE) {
        object_surface_p s = XVBA_SURFACE(pic_param->forward_reference_picture);
        ASSERT(s);
        if (!s)
            return 0;
        pic_desc->future_surface = s->xvba_surface;
    }

    obj_surface->pic_desc_buffer->data_size_in_buffer = sizeof(*pic_desc);
    return 1;
}

// Translate VASliceParameterBufferVC1
static int
translate_VASliceParameterBufferVC1(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
)
{
    VASliceParameterBufferVC1 * const slice_param = obj_buffer->buffer_data;

    /* XXX: we don't handle partial slice data buffers yet */
    if (slice_param->slice_data_flag != VA_SLICE_DATA_FLAG_ALL) {
        D(bug("partial slice data buffers are not handled\n"));
        return 0;
    }

    object_surface_p obj_surface = XVBA_SURFACE(obj_context->current_render_target);
    if (!obj_surface)
        return 0;

    XVBABufferDescriptor * const xvba_pic_desc_buffer = obj_surface->pic_desc_buffer;
    if (!xvba_pic_desc_buffer)
        return 0;

    XVBAPictureDescriptor * const pic_desc = xvba_pic_desc_buffer->bufferXVBA;

    object_buffer_p data_buffer = obj_context->data_buffer;
    ASSERT(data_buffer);
    if (!data_buffer)
        return 0;
    ASSERT(slice_param->slice_data_offset + slice_param->slice_data_size <= data_buffer->buffer_size);
    if (slice_param->slice_data_offset + slice_param->slice_data_size > data_buffer->buffer_size)
        return 0;

    XVBABufferDescriptor * const xvba_data_buffer = obj_surface->data_buffer;
    ASSERT(xvba_data_buffer);
    if (!xvba_data_buffer)
        return 0;

    XVBABufferDescriptor * const xvba_data_ctrl_buffer = obj_surface->data_ctrl_buffers[obj_context->slice_count++];
    ASSERT(xvba_data_ctrl_buffer);
    if (!xvba_data_ctrl_buffer)
        return 0;

    XVBADataCtrl * const data_ctrl = xvba_data_ctrl_buffer->bufferXVBA;
    const unsigned int data_offset = xvba_data_buffer->data_size_in_buffer;

    const uint8_t * const va_slice_data = ((uint8_t *)data_buffer->buffer_data +
                                           slice_param->slice_data_offset);

    static const uint8_t start_code_prefix[3] = { 0x00, 0x00, 0x01 };
    if (memcmp(va_slice_data, start_code_prefix, sizeof(start_code_prefix)) != 0) {
        append_buffer(xvba_data_buffer, start_code_prefix, sizeof(start_code_prefix));

        uint8_t start_code_ext = 0;
        if (pic_desc->picture_structure == PICT_FRAME) {
            /* XXX: we only support Progressive mode at this time */
            start_code_ext = 0x0d;
        }
        ASSERT(start_code_ext);
        append_buffer(xvba_data_buffer, &start_code_ext, 1);
    }

    append_buffer(xvba_data_buffer, va_slice_data, slice_param->slice_data_size);

    /* XXX: should XVBA_DATA_CTRL_BUFFER.SliceBytesInBuffer and
       SliceBitsInBuffer be required to account for padding bytes too? */
    data_ctrl->SliceDataLocation   = data_offset;
    data_ctrl->SliceBytesInBuffer  = xvba_data_buffer->data_size_in_buffer - data_offset;
    data_ctrl->SliceBitsInBuffer   = 8 * data_ctrl->SliceBytesInBuffer;
    pad_buffer(xvba_data_buffer);

    xvba_data_ctrl_buffer->data_size_in_buffer = sizeof(*data_ctrl);
    return 1;
}

// Translate VA buffer
typedef int
(*translate_buffer_func_t)(xvba_driver_data_t *driver_data,
                           object_context_p    obj_context,
                           object_buffer_p     obj_buffer);

typedef struct translate_buffer_info translate_buffer_info_t;
struct translate_buffer_info {
    XVBACodec codec;
    VABufferType type;
    translate_buffer_func_t func;
};

int
translate_buffer(
    xvba_driver_data_t *driver_data,
    object_context_p    obj_context,
    object_buffer_p     obj_buffer
)
{
    static const translate_buffer_info_t translate_info[] = {
#define _(CODEC, TYPE)                                  \
        { XVBA_CODEC_##CODEC, VA##TYPE##BufferType,     \
          translate_VA##TYPE##Buffer##CODEC }
        _(MPEG2, PictureParameter),
        _(MPEG2, IQMatrix),
        _(MPEG2, SliceParameter),
        _(H264, PictureParameter),
        _(H264, IQMatrix),
        _(H264, SliceParameter),
        _(VC1, PictureParameter),
        _(VC1, SliceParameter),
#undef _
        { XVBA_CODEC_VC1, VABitPlaneBufferType, translate_nothing },
        { 0, VASliceDataBufferType, translate_VASliceDataBuffer },
        { 0, 0, NULL }
    };
    const translate_buffer_info_t *tbip;
    for (tbip = translate_info; tbip->func != NULL; tbip++) {
        if (tbip->codec && tbip->codec != obj_context->xvba_codec)
            continue;
        if (tbip->type != obj_buffer->type)
            continue;
        return tbip->func(driver_data, obj_context, obj_buffer);
    }
    D(bug("ERROR: no translate function found for %s%s\n",
          string_of_VABufferType(obj_buffer->type),
          obj_context->xvba_codec ? string_of_XVBACodec(obj_context->xvba_codec) : NULL));
    return 0;
}

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
)
{
    XVBA_DRIVER_DATA_INIT;

    if (buf_id)
        *buf_id = VA_INVALID_BUFFER;

    /* Validate type */
    switch (type) {
    case VAPictureParameterBufferType:
    case VAIQMatrixBufferType:
    case VASliceParameterBufferType:
    case VASliceDataBufferType:
    case VABitPlaneBufferType:
    case VAImageBufferType:
        /* Ok */
        break;
    default:
        D(bug("ERROR: unsupported buffer type %d\n", type));
        return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
    }

    object_buffer_p obj_buffer;
    obj_buffer = create_va_buffer(driver_data, context, type, num_elements, size);
    if (!obj_buffer)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    if (data)
        memcpy(obj_buffer->buffer_data, data, obj_buffer->buffer_size);

    if (buf_id)
        *buf_id = obj_buffer->base.id;

    return VA_STATUS_SUCCESS;
}

// vaDestroyBuffer
VAStatus
xvba_DestroyBuffer(
    VADriverContextP    ctx,
    VABufferID          buffer_id
)
{
    XVBA_DRIVER_DATA_INIT;

    object_buffer_p obj_buffer = XVBA_BUFFER(buffer_id);

    if (obj_buffer) {
        if (!is_queued_buffer(driver_data, obj_buffer))
            destroy_va_buffer(driver_data, obj_buffer);
        /* else, it will be destroyed in the next commit_picture() call */
    }
    return VA_STATUS_SUCCESS;
}

// vaBufferSetNumElements
VAStatus
xvba_BufferSetNumElements(
    VADriverContextP    ctx,
    VABufferID          buf_id,
    unsigned int        num_elements
)
{
    XVBA_DRIVER_DATA_INIT;

    object_buffer_p obj_buffer = XVBA_BUFFER(buf_id);
    if (!obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

    if (num_elements < 0 || num_elements > obj_buffer->max_num_elements)
        return VA_STATUS_ERROR_UNKNOWN;

    obj_buffer->num_elements = num_elements;
    return VA_STATUS_SUCCESS;
}

// vaMapBuffer
VAStatus
xvba_MapBuffer(
    VADriverContextP    ctx,
    VABufferID          buf_id,
    void              **pbuf
)
{
    XVBA_DRIVER_DATA_INIT;

    object_buffer_p obj_buffer = XVBA_BUFFER(buf_id);
    if (!obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

    if (pbuf)
        *pbuf = obj_buffer->buffer_data;

    if (!obj_buffer->buffer_data)
        return VA_STATUS_ERROR_UNKNOWN;

    ++obj_buffer->mtime;
    return VA_STATUS_SUCCESS;
}

// vaUnmapBuffer
VAStatus
xvba_UnmapBuffer(
    VADriverContextP    ctx,
    VABufferID          buf_id
)
{
    XVBA_DRIVER_DATA_INIT;

    object_buffer_p obj_buffer = XVBA_BUFFER(buf_id);
    if (!obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

    ++obj_buffer->mtime;
    return VA_STATUS_SUCCESS;
}

// vaBufferInfo
VAStatus
xvba_BufferInfo(
    VADriverContextP    ctx,
    VAContextID         context,
    VABufferID          buf_id,
    VABufferType       *type,
    unsigned int       *size,
    unsigned int       *num_elements
)
{
    XVBA_DRIVER_DATA_INIT;

    object_buffer_p obj_buffer = XVBA_BUFFER(buf_id);
    if (!obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

    if (type)
        *type = obj_buffer->type;
    if (size)
        *size = obj_buffer->buffer_size / obj_buffer->num_elements;
    if (num_elements)
        *num_elements = obj_buffer->num_elements;
    return VA_STATUS_SUCCESS;
}
