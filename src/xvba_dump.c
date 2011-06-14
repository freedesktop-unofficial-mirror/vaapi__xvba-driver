/*
 *  xvba_dump.c - Dump utilities
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
#include "xvba_dump.h"

#define DEBUG 1
#include "debug.h"


// Returns string representation of FOURCC
const char *string_of_FOURCC(uint32_t fourcc)
{
    static char str[5];
    str[0] = fourcc;
    str[1] = fourcc >> 8;
    str[2] = fourcc >> 16;
    str[3] = fourcc >> 24;
    str[4] = '\0';
    return str;
}

// Returns string representation of VA surface format
const char *string_of_VAConfigAttribRTFormat(unsigned int format)
{
    switch (format) {
#define FORMAT(FORMAT) \
        case VA_RT_FORMAT_##FORMAT: return #FORMAT
        FORMAT(YUV420);
        FORMAT(YUV422);
        FORMAT(YUV444);
    }
    return "<unknown>";
}

// Returns string representation of XVBA_SURFACE_FLAG
const char *string_of_XVBA_SURFACE_FLAG(XVBA_SURFACE_FLAG flag)
{
    switch (flag) {
#define FLAG(flag) \
        case XVBA_##flag: return "XVBA_" #flag
        FLAG(FRAME);
        FLAG(TOP_FIELD);
        FLAG(BOTTOM_FIELD);
#undef FLAG
    }
    return "<unknown>";
}

// Returns string representation of XVBA_BUFFER
const char *string_of_XVBA_BUFFER(XVBA_BUFFER buffer_type)
{
    switch (buffer_type) {
#define BUFFER_TYPE(t) \
        case XVBA_##t##_BUFFER: return "XVBA_" #t "_BUFFER"
        BUFFER_TYPE(PICTURE_DESCRIPTION);
        BUFFER_TYPE(QM);
        BUFFER_TYPE(DATA);
        BUFFER_TYPE(DATA_CTRL);
#undef BUFFER_TYPE
    }
    return "<unknown>";
}

// Returns string representation of XVBA_CAPABILITY_ID
const char *string_of_XVBA_CAPABILITY_ID(XVBA_CAPABILITY_ID cap_id)
{
    switch (cap_id) {
#define CAP_ID(cap_id) \
        case XVBA_##cap_id: return "XVBA_" #cap_id
        CAP_ID(H264);
        CAP_ID(VC1);
        CAP_ID(MPEG2_IDCT);
        CAP_ID(MPEG2_VLD);
#undef CAP_ID
    }
    return "<unknown>";
}

// Returns string representation of XVBA_DECODE_FLAGS
const char *string_of_XVBA_DECODE_FLAGS(XVBA_DECODE_FLAGS flag)
{
    switch (flag) {
#define FLAG(flag) \
        case XVBA_##flag: return "XVBA_" #flag
        FLAG(NOFLAG);
        FLAG(H264_BASELINE);
        FLAG(H264_MAIN);
        FLAG(H264_HIGH);
        FLAG(VC1_SIMPLE);
        FLAG(VC1_MAIN);
        FLAG(VC1_ADVANCED);
#undef FLAG
    }
    return "<unknown>";
}

// Returns string representation of XVBACodec
const char *string_of_XVBACodec(XVBACodec codec)
{
    const char *str = NULL;
    switch (codec) {
#define _(X) case XVBA_CODEC_##X: str = #X; break
        _(MPEG1);
        _(MPEG2);
        _(MPEG4);
        _(H264);
        _(VC1);
#undef _
    }
    return str;
}

// Returns string representation of VABufferType
const char *string_of_VABufferType(VABufferType type)
{
    const char *str = NULL;
    switch (type) {
#define _(X) case X: str = #X; break
        _(VAPictureParameterBufferType);
        _(VAIQMatrixBufferType);
        _(VABitPlaneBufferType);
        _(VASliceGroupMapBufferType);
        _(VASliceParameterBufferType);
        _(VASliceDataBufferType);
        _(VAMacroblockParameterBufferType);
        _(VAResidualDataBufferType);
        _(VADeblockingParameterBufferType);
        _(VAImageBufferType);
#if VA_CHECK_VERSION(0,30,0)
        _(VAProtectedSliceDataBufferType);
        _(VAEncCodedBufferType);
        _(VAEncSequenceParameterBufferType);
        _(VAEncPictureParameterBufferType);
        _(VAEncSliceParameterBufferType);
        _(VAEncH264VUIBufferType);
        _(VAEncH264SEIBufferType);
#endif
#undef _
    }
    return str;
}

#if USE_TRACER
#define TRACE               trace_print
#define INDENT(INC)         trace_indent(INC)
#define DUMPi(S, M)         TRACE("." #M " = %d,\n", S->M)
#define DUMPx(S, M)         TRACE("." #M " = 0x%08x,\n", S->M)
#define DUMPp(S, M)         TRACE("." #M " = %p,\n", S->M)
#define DUMPm(S, M, I, J)   dump_matrix_NxM(#M, (uint8_t *)S->M, I, J, I * J)
#else
#define trace_enabled()     (0)
#define do_nothing()        do { } while (0)
#define TRACE(FORMAT,...)   do_nothing()
#define INDENT(INC)         do_nothing()
#define DUMPi(S, M)         do_nothing()
#define DUMPx(S, M)         do_nothing()
#define DUMPp(S, M)         do_nothing()
#define DUMPm(S, M, I, J)   do_nothing()
#endif

// Dumps matrix[N][M] = N rows x M columns (uint8_t)
static void
dump_matrix_NxM(const char *label, uint8_t *matrix, int N, int M, int L)
{
    int i, j, n = 0;

    TRACE(".%s = {\n", label);
    INDENT(1);
    for (j = 0; j < N; j++) {
        for (i = 0; i < M; i++, n++) {
            if (n >= L)
                break;
            if (i > 0)
                TRACE(", ");
            TRACE("0x%02x", matrix[n]);
        }
        if (j < (N - 1))
            TRACE(",");
        TRACE("\n");
        if (n >= L)
            break;
    }
    INDENT(-1);
    TRACE("}\n");
}

// Dump XVBAPictureDescriptor buffer
static void
dump_XVBABufferDescriptor__XVBA_PICTURE_DESCRIPTION_BUFFER(
    XVBABufferDescriptor *buffer
)
{
    if (!buffer)
        return;

    object_context_p obj_context = buffer->appPrivate;
    ASSERT(obj_context);
    if (!obj_context)
        return;

    XVBAPictureDescriptor *pic_desc = buffer->bufferXVBA;
    if (!pic_desc)
        return;

    DUMPp(pic_desc, past_surface);
    DUMPp(pic_desc, future_surface);
    DUMPi(pic_desc, profile);
    DUMPi(pic_desc, level);
    DUMPi(pic_desc, width_in_mb);
    DUMPi(pic_desc, height_in_mb);
    DUMPi(pic_desc, picture_structure);

    switch (obj_context->xvba_codec) {
    case XVBA_CODEC_H264:
        TRACE(".sps_info.flags = 0x%08x, /* %d:%d:%d:%d:%d:%d */\n",
              pic_desc->sps_info.flags,
              pic_desc->sps_info.avc.residual_colour_transform_flag,
              pic_desc->sps_info.avc.delta_pic_always_zero_flag,
              pic_desc->sps_info.avc.gaps_in_frame_num_value_allowed_flag,
              pic_desc->sps_info.avc.frame_mbs_only_flag,
              pic_desc->sps_info.avc.mb_adaptive_frame_field_flag,
              pic_desc->sps_info.avc.direct_8x8_inference_flag);
        break;
    case XVBA_CODEC_VC1:
        TRACE(".sps_info.flags = 0x%08x, /* %d:%d:%d:%d:%d:%d:%d */\n",
              pic_desc->sps_info.flags,
              pic_desc->sps_info.vc1.postprocflag,
              pic_desc->sps_info.vc1.pulldown,
              pic_desc->sps_info.vc1.interlace,
              pic_desc->sps_info.vc1.tfcntrflag,
              pic_desc->sps_info.vc1.finterpflag,
              pic_desc->sps_info.vc1.psf,
              pic_desc->sps_info.vc1.second_field);
        break;
    default:
        break;
    }

    DUMPi(pic_desc, chroma_format);

    if (obj_context->xvba_codec == XVBA_CODEC_H264) {
        DUMPi(pic_desc, avc_bit_depth_luma_minus8);
        DUMPi(pic_desc, avc_bit_depth_chroma_minus8);
        DUMPi(pic_desc, avc_log2_max_frame_num_minus4);
        DUMPi(pic_desc, avc_pic_order_cnt_type);
        DUMPi(pic_desc, avc_log2_max_pic_order_cnt_lsb_minus4);
        DUMPi(pic_desc, avc_num_ref_frames);
    }

    switch (obj_context->xvba_codec) {
    case XVBA_CODEC_H264:
        TRACE(".pps_info.flags = 0x%08x, /* %d:%d:%d:%d:%d:%d:%d:%d */\n",
              pic_desc->pps_info.flags,
              pic_desc->pps_info.avc.entropy_coding_mode_flag,
              pic_desc->pps_info.avc.pic_order_present_flag,
              pic_desc->pps_info.avc.weighted_pred_flag,
              pic_desc->pps_info.avc.weighted_bipred_idc,
              pic_desc->pps_info.avc.deblocking_filter_control_present_flag,
              pic_desc->pps_info.avc.constrained_intra_pred_flag,
              pic_desc->pps_info.avc.redundant_pic_cnt_present_flag,
              pic_desc->pps_info.avc.transform_8x8_mode_flag);
        break;
    case XVBA_CODEC_VC1:
        TRACE(".pps_info.flags = 0x%08x, /* %d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d */\n",
              pic_desc->pps_info.flags,
              pic_desc->pps_info.vc1.panscan_flag,
              pic_desc->pps_info.vc1.refdist_flag,
              pic_desc->pps_info.vc1.loopfilter,
              pic_desc->pps_info.vc1.fastuvmc,
              pic_desc->pps_info.vc1.extended_mv,
              pic_desc->pps_info.vc1.dquant,
              pic_desc->pps_info.vc1.vstransform,
              pic_desc->pps_info.vc1.overlap,
              pic_desc->pps_info.vc1.quantizer,
              pic_desc->pps_info.vc1.extended_dmv,
              pic_desc->pps_info.vc1.maxbframes,
              pic_desc->pps_info.vc1.rangered,
              pic_desc->pps_info.vc1.syncmarker,
              pic_desc->pps_info.vc1.multires,
              pic_desc->pps_info.vc1.range_mapy_flag,
              pic_desc->pps_info.vc1.range_mapy,
              pic_desc->pps_info.vc1.range_mapuv_flag,
              pic_desc->pps_info.vc1.range_mapuv);
        break;
    default:
        break;
    }

    if (obj_context->xvba_codec == XVBA_CODEC_H264) {
        DUMPi(pic_desc, avc_num_slice_groups_minus1);
        DUMPi(pic_desc, avc_slice_group_map_type);
        DUMPi(pic_desc, avc_num_ref_idx_l0_active_minus1);
        DUMPi(pic_desc, avc_num_ref_idx_l1_active_minus1);

        DUMPi(pic_desc, avc_pic_init_qp_minus26);
        DUMPi(pic_desc, avc_pic_init_qs_minus26);
        DUMPi(pic_desc, avc_chroma_qp_index_offset);
        DUMPi(pic_desc, avc_second_chroma_qp_index_offset);

        DUMPi(pic_desc, avc_slice_group_change_rate_minus1);

        DUMPi(pic_desc, avc_curr_field_order_cnt_list[0]);
        DUMPi(pic_desc, avc_curr_field_order_cnt_list[1]);
    }

    DUMPi(pic_desc, avc_frame_num);
    DUMPi(pic_desc, avc_intra_flag);
    DUMPi(pic_desc, avc_reference);
}

// Dump data buffer
static void
dump_XVBABufferDescriptor__XVBA_DATA_BUFFER(XVBABufferDescriptor *buffer)
{
    if (!buffer)
        return;

    uint8_t *data = buffer->bufferXVBA;
    if (!data)
        return;

    dump_matrix_NxM("data", data + buffer->data_offset, 4, 12, buffer->data_size_in_buffer);
}

// Dump XVBADataCtrl buffer
static void
dump_XVBABufferDescriptor__XVBA_DATA_CTRL_BUFFER(XVBABufferDescriptor *buffer)
{
    if (!buffer)
        return;

    XVBADataCtrl *data_ctrl = buffer->bufferXVBA;
    if (!data_ctrl)
        return;

    DUMPi(data_ctrl, SliceBitsInBuffer);
    DUMPi(data_ctrl, SliceDataLocation);
    DUMPi(data_ctrl, SliceBytesInBuffer);
}

// Dump quantization matrix buffer (H.264 only for now)
static void
dump_XVBABufferDescriptor__XVBA_QM_BUFFER(XVBABufferDescriptor *buffer)
{
    if (!buffer)
        return;

    object_context_p obj_context = buffer->appPrivate;
    ASSERT(obj_context);
    if (!obj_context)
        return;

    if (obj_context->xvba_codec != XVBA_CODEC_H264)
        return;

    XVBAQuantMatrixAvc *qm = buffer->bufferXVBA;
    if (!qm)
        return;

    DUMPm(qm, bScalingLists4x4, 6, 16);
    DUMPm(qm, bScalingLists8x8[0], 8, 8);
    DUMPm(qm, bScalingLists8x8[1], 8, 8);
}

// Dump XVBABufferDescriptor
static void dump_XVBABufferDescriptor(XVBABufferDescriptor *buffer)
{
    if (!buffer)
        return;

    TRACE("%s = {\n", string_of_XVBA_BUFFER(buffer->buffer_type));
    INDENT(1);
    DUMPi(buffer, buffer_size);
    DUMPp(buffer, bufferXVBA);
    DUMPi(buffer, data_size_in_buffer);
    DUMPi(buffer, data_offset);
    switch (buffer->buffer_type) {
    case XVBA_PICTURE_DESCRIPTION_BUFFER:
        dump_XVBABufferDescriptor__XVBA_PICTURE_DESCRIPTION_BUFFER(buffer);
        break;
    case XVBA_DATA_BUFFER:
        dump_XVBABufferDescriptor__XVBA_DATA_BUFFER(buffer);
        break;
    case XVBA_DATA_CTRL_BUFFER:
        dump_XVBABufferDescriptor__XVBA_DATA_CTRL_BUFFER(buffer);
        break;
    case XVBA_QM_BUFFER:
        dump_XVBABufferDescriptor__XVBA_QM_BUFFER(buffer);
        break;
    default:
        break;
    }
    INDENT(-1);
    TRACE("};\n");
}

// Dumps XVBACreateContext()
void dump_XVBACreateContext(void *input_arg)
{
    XVBA_Create_Context_Input * const input = input_arg;

    if (trace_enabled())
        TRACE("XVBACreateContext(): display %p, drawable 0x%08x\n",
              input->display,
              input->draw);
}

void dump_XVBACreateContext_output(void *output_arg)
{
    XVBA_Create_Context_Output * const output = output_arg;

    if (trace_enabled())
        TRACE("XVBACreateContext(): -> context %p\n", output->context);
}

// Dumps XVBADestroyContext()
void dump_XVBADestroyContext(void *context)
{
    if (trace_enabled())
        TRACE("XVBADestroyContext(): context %p\n", context);
}

// Dumps XVBACreateSurface()
void dump_XVBACreateSurface(void *input_arg)
{
    XVBA_Create_Surface_Input * const input = input_arg;

    if (trace_enabled())
        TRACE("XVBACreateSurface(): session %p, size %ux%u, format %s\n",
              input->session,
              input->width,
              input->height,
              string_of_FOURCC(input->surface_type));
}

void dump_XVBACreateSurface_output(void *output_arg)
{
    XVBA_Create_Surface_Output * const output = output_arg;

    if (trace_enabled())
        TRACE("XVBACreateSurface(): -> surface %p\n", output->surface);
}

// Dumps XVBACreateGLSharedSurface()
void dump_XVBACreateGLSharedSurface(void *input_arg)
{
    XVBA_Create_GLShared_Surface_Input * const input = input_arg;

    if (trace_enabled())
        TRACE("XVBACreateGLSharedSurface(): session %p, GLXContext %p, texture %d\n",
              input->session,
              input->glcontext,
              input->gltexture);
}

void dump_XVBACreateGLSharedSurface_output(void *output_arg)
{
    XVBA_Create_GLShared_Surface_Output * const output = output_arg;

    if (trace_enabled())
        TRACE("XVBACreateGLSharedSurface(): -> surface %p\n", output->surface);
}

// Dumps XVBADestroySurface()
void dump_XVBADestroySurface(void *surface)
{
    if (trace_enabled())
        TRACE("XVBADestroySurface(): surface %p\n", surface);
}

// Dumps XVBAGetCapDecode()
void dump_XVBAGetCapDecode(void *input_arg)
{
    XVBA_GetCapDecode_Input * const input = input_arg;

    if (trace_enabled())
        TRACE("XVBAGetCapDecode(): context %p\n", input->context);
}

void
dump_XVBAGetCapDecode_output_decode_caps(
    unsigned int    num_decode_caps,
    XVBADecodeCap  *decode_caps
)
{
    unsigned int i;

    if (!trace_enabled())
        return;

    INDENT(1);
    for (i = 0; i < num_decode_caps; i++) {
        XVBADecodeCap * const decode_cap = &decode_caps[i];
        TRACE("capability %d = {\n", i + 1);
        INDENT(1);
        TRACE("capability_id = %s\n",
              string_of_XVBA_CAPABILITY_ID(decode_cap->capability_id));
        TRACE("flags         = %s\n",
              string_of_XVBA_DECODE_FLAGS(decode_cap->flags));
        TRACE("surface_type  = %s\n",
              string_of_FOURCC(decode_cap->surface_type));
        INDENT(-1);
        TRACE("}\n");
    }
    INDENT(-1);
}

void
dump_XVBAGetCapDecode_output_surface_caps(
    unsigned int    num_surface_caps,
    XVBASurfaceCap *surface_caps
)
{
    unsigned int i;

    if (!trace_enabled())
        return;

    INDENT(1);
    for (i = 0; i < num_surface_caps; i++) {
        XVBASurfaceCap * const surface_cap = &surface_caps[i];
        TRACE("getsurface target %d = {\n", i + 1);
        INDENT(1);
        TRACE("    format        = %s\n",
              string_of_FOURCC(surface_cap->format));
        TRACE("    flag          = %s\n",
              string_of_XVBA_SURFACE_FLAG(surface_cap->flag));
        INDENT(-1);
        TRACE("}\n");
    }
    INDENT(-1);
}

// Dumps XVBACreateDecode()
void dump_XVBACreateDecode(void *input_arg)
{
    XVBA_Create_Decode_Session_Input * const input = input_arg;

    if (!trace_enabled())
        return;

    TRACE("XVBACreateDecode()");
    TRACE(": context %p", input->context);
    TRACE(", size %ux%u", input->width, input->height);

    if (input->decode_cap) {
        XVBADecodeCap * const decode_cap = input->decode_cap;
        TRACE(", capability_id %s, flags %s, surface_type %s",
              string_of_XVBA_CAPABILITY_ID(decode_cap->capability_id),
              string_of_XVBA_DECODE_FLAGS(decode_cap->flags),
              string_of_FOURCC(decode_cap->surface_type));
    }
    TRACE("\n");
}

void dump_XVBACreateDecode_output(void *output_arg)
{
    XVBA_Create_Decode_Session_Output * const output = output_arg;

    if (trace_enabled())
        TRACE("XVBACreateDecode(): -> session %p\n", output->session);
}

// Dumps XVBADestroyDecode()
void dump_XVBADestroyDecode(void *session)
{
    if (trace_enabled())
        TRACE("XVBADestroyDecode(): session %p\n", session);
}

// Dumps XVBACreateDecodeBuffers()
void dump_XVBACreateDecodeBuffers(void *input_arg)
{
    XVBA_Create_DecodeBuff_Input * const input = input_arg;

    if (trace_enabled())
        TRACE("XVBACreateDecodeBuffers(): session %p, %s x %d\n",
              input->session,
              string_of_XVBA_BUFFER(input->buffer_type),
              input->num_of_buffers);
}

void dump_XVBACreateDecodeBuffers_output(void *output_arg)
{
    XVBA_Create_DecodeBuff_Output * const output = output_arg;

    if (trace_enabled())
        TRACE("XVBACreateDecodeBuffers(): -> buffers %p\n",
              output->buffer_list);
}

// Dumps XVBADestroyDecodeBuffers()
void dump_XVBADestroyDecodeBuffers(void *input_arg)
{
    XVBA_Destroy_Decode_Buffers_Input const * input = input_arg;

    if (trace_enabled())
        TRACE("XVBADestroyDecodeBuffers(): session %p, buffers %p x %d\n",
              input->session,
              input->buffer_list,
              input->num_of_buffers_in_list);
}

// Dumps XVBAStartDecodePicture()
void dump_XVBAStartDecodePicture(void *input_arg)
{
    XVBA_Decode_Picture_Start_Input * const input = input_arg;

    if (trace_enabled())
        TRACE("XVBAStartDecodePicture(): session %p, surface %p\n",
              input->session,
              input->target_surface);
}

// Dumps XVBADecodePicture()
void dump_XVBADecodePicture(void *input_arg)
{
    XVBA_Decode_Picture_Input * const input = input_arg;

    if (!trace_enabled())
        return;

    TRACE("XVBADecodePicture(): session %p\n", input->session);

    unsigned int i;
    for (i = 0; i < input->num_of_buffers_in_list; i++)
        dump_XVBABufferDescriptor(input->buffer_list[i]);
}

// Dumps XVBAEndDecodePicture()
void dump_XVBAEndDecodePicture(void *input_arg)
{
    XVBA_Decode_Picture_End_Input * const input = input_arg;

    if (trace_enabled())
        TRACE("XVBAEndDecodePicture(): session %p\n", input->session);
}

// Dumps XVBAGetSurface()
void dump_XVBAGetSurface(void *input_arg)
{
    if (!trace_enabled())
        return;

#if XVBA_CHECK_VERSION(0,74)
    if (!xvba_check_version(0,74))
        return;

    XVBA_Get_Surface_Input * const input = input_arg;

    TRACE("XVBAGetSurface()");
    TRACE(": session %p", input->session);
    TRACE(", surface %p", input->src_surface);
    TRACE(", dest pixels %p size %ux%u pitch %d format %s",
          input->target_buffer,
          input->target_width,
          input->target_height,
          input->target_pitch,
          string_of_FOURCC(input->target_parameter.surfaceType));
#if 0
    /* XXX: always default to XVBA_FRAME here */
    TRACE(" flag %s",string_of_XVBA_SURFACE_FLAG(input->target_parameter.flag));
#endif
    TRACE("\n");
#endif
}

// Dumps XVBATransferSurface()
void dump_XVBATransferSurface(void *input_arg)
{
    if (!trace_enabled())
        return;

#if XVBA_CHECK_VERSION(0,74)
    if (!xvba_check_version(0,74))
        return;

    XVBA_Transfer_Surface_Input * const input = input_arg;

    TRACE("XVBATransferSurface()");
    TRACE(": session %p", input->session);
    TRACE(", from surface %p", input->src_surface);
    TRACE(", to GL surface %p", input->target_surface);
    TRACE(", flag %s", string_of_XVBA_SURFACE_FLAG(input->flag));
    TRACE("\n");
#endif
}
