/* GStreamer
 * Copyright © 2025 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Author: Sreerenj Balachandran <sreerenj@amazon.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gstv4l2codech264_debug.h"

void
gst_v4l2_codec_h264_debug_dump_sequence (GstCodecDecoder * self,
    const struct v4l2_ctrl_h264_sps *sps)
{
  if (!sps)
    return;

  GString *offsets = g_string_new (NULL);
  for (gint i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
    g_string_append_printf (offsets, "%d,", sps->offset_for_ref_frame[i]);

  /* Trim trailing comma */
  if (offsets->len > 0)
    g_string_truncate (offsets, offsets->len - 1);

  GST_TRACE_OBJECT (self,
      "SPS(v4l2_ctrl_h264_sps): profile_idc=%u, level_idc=%u, sps_id=%u, chroma_format_idc=%u, "
      "bit_depth_luma_minus8=%u, bit_depth_chroma_minus8=%u, "
      "log2_max_frame_num_minus4=%u, pic_order_cnt_type=%u, "
      "log2_max_pic_order_cnt_lsb_minus4=%u, max_num_ref_frames=%u, "
      "num_ref_frames_in_pic_order_cnt_cycle=%u, "
      "offset_for_non_ref_pic=%d, offset_for_top_to_bottom_field=%d, "
      "offset_for_ref_frame=[%s], "
      "pic_width_in_mbs_minus1=%u, pic_height_in_map_units_minus1=%u, flags=0x%x",
      sps->profile_idc,
      sps->level_idc,
      sps->seq_parameter_set_id,
      sps->chroma_format_idc,
      sps->bit_depth_luma_minus8,
      sps->bit_depth_chroma_minus8,
      sps->log2_max_frame_num_minus4,
      sps->pic_order_cnt_type,
      sps->log2_max_pic_order_cnt_lsb_minus4,
      sps->max_num_ref_frames,
      sps->num_ref_frames_in_pic_order_cnt_cycle,
      sps->offset_for_non_ref_pic,
      sps->offset_for_top_to_bottom_field,
      offsets->str,
      sps->pic_width_in_mbs_minus1,
      sps->pic_height_in_map_units_minus1, sps->flags);

  g_string_free (offsets, TRUE);

}

void
gst_v4l2_codec_h264_debug_dump_pps (GstCodecDecoder * self,
    const struct v4l2_ctrl_h264_pps *pps)
{

  GST_TRACE_OBJECT (self,
      "PPS(v4l2_ctrl_h264_pps): id=%u, sps_id=%u, num_slice_groups_minus1=%u, "
      "ref_idx_l0=%u, ref_idx_l1=%u, weighted_bipred_idc=%u, "
      "pic_init_qp_minus26=%d, pic_init_qs_minus26=%d, "
      "chroma_qp_index_offset=%d, second_chroma_qp_index_offset=%d, flags=0x%x",
      pps->pic_parameter_set_id, pps->seq_parameter_set_id,
      pps->num_slice_groups_minus1, pps->num_ref_idx_l0_default_active_minus1,
      pps->num_ref_idx_l1_default_active_minus1, pps->weighted_bipred_idc,
      pps->pic_init_qp_minus26, pps->pic_init_qs_minus26,
      pps->chroma_qp_index_offset, pps->second_chroma_qp_index_offset,
      pps->flags);
}

void
gst_v4l2_codec_h264_debug_dump_scaling_matrix (GstCodecDecoder * self,
    const struct v4l2_ctrl_h264_scaling_matrix *scaling)
{
  static const char *names_4x4[6] = {
    "Intra Y", "Intra Cb", "Intra Cr",
    "Inter Y", "Inter Cb", "Inter Cr"
  };

  static const char *names_8x8[6] = {
    "Intra Y", "Inter Y",
    "Intra Cb", "Inter Cb",
    "Intra Cr", "Inter Cr"
  };

  for (int i = 0; i < 6; i++) {
    GST_TRACE ("Scaling Matrix 4x4 [%s]:", names_4x4[i]);
    for (int row = 0; row < 4; row++)
      GST_TRACE ("  %3u %3u %3u %3u",
          scaling->scaling_list_4x4[i][row * 4 + 0],
          scaling->scaling_list_4x4[i][row * 4 + 1],
          scaling->scaling_list_4x4[i][row * 4 + 2],
          scaling->scaling_list_4x4[i][row * 4 + 3]);
  }

  for (int i = 0; i < 6; i++) {
    GST_TRACE ("Scaling Matrix 8x8 [%s]:", names_8x8[i]);
    for (int row = 0; row < 8; row++)
      GST_TRACE ("  %3u %3u %3u %3u %3u %3u %3u %3u",
          scaling->scaling_list_8x8[i][row * 8 + 0],
          scaling->scaling_list_8x8[i][row * 8 + 1],
          scaling->scaling_list_8x8[i][row * 8 + 2],
          scaling->scaling_list_8x8[i][row * 8 + 3],
          scaling->scaling_list_8x8[i][row * 8 + 4],
          scaling->scaling_list_8x8[i][row * 8 + 5],
          scaling->scaling_list_8x8[i][row * 8 + 6],
          scaling->scaling_list_8x8[i][row * 8 + 7]);
  }
}

void
gst_v4l2_codec_h264_debug_dump_decode_params (GstCodecDecoder * self,
    const struct v4l2_ctrl_h264_decode_params *p)
{
  GString *s =
      g_string_new ("H264DecodeParams (v4l2_ctrl_h264_decode_params): ");

  g_string_append_printf (s,
      "nal_ref_idc=%u frame_num=%u top_fld_cnt=%d bot_fld_cnt=%d "
      "idr_pic_id=%u poc_lsb=%u d_poc_bot=%d d_poc0=%d d_poc1=%d "
      "dec_ref_bits=%u poc_bits=%u sgcc=%u flags=0x%08x | ",
      p->nal_ref_idc, p->frame_num, p->top_field_order_cnt,
      p->bottom_field_order_cnt, p->idr_pic_id, p->pic_order_cnt_lsb,
      p->delta_pic_order_cnt_bottom, p->delta_pic_order_cnt0,
      p->delta_pic_order_cnt1, p->dec_ref_pic_marking_bit_size,
      p->pic_order_cnt_bit_size, p->slice_group_change_cycle, p->flags);

  for (int i = 0; i < V4L2_H264_NUM_DPB_ENTRIES; i++) {
    const struct v4l2_h264_dpb_entry *d = &p->dpb[i];
    g_string_append_printf (s,
        "DPB[%d]={ts=%llu fn=%u pn=%u t_fld_cnt=%d b_fld_cnt=%d flags=0x%08x} ",
        i, (unsigned long long) d->reference_ts, d->frame_num, d->pic_num,
        d->top_field_order_cnt, d->bottom_field_order_cnt, d->flags);
  }

  GST_TRACE_OBJECT (self, "%s", s->str);
  g_string_free (s, TRUE);
}

void
gst_v4l2_codec_h264_debug_dump_pred_weights (GstCodecDecoder * self,
    const struct v4l2_ctrl_h264_pred_weights *w)
{
  GString *s = g_string_new ("H264PredWeights: ");

  g_string_append_printf (s,
      "luma_log2_weight_denom=%u chroma_log2_weight_denom=%u | ",
      w->luma_log2_weight_denom, w->chroma_log2_weight_denom);

  for (int list = 0; list < 2; list++) {
    g_string_append_printf (s, "List%d: ", list);
    for (int i = 0; i < 32; i++)
      g_string_append_printf (s,
          "[%d] lw=%d lo=%d cw={%d,%d} co={%d,%d} ",
          i,
          w->weight_factors[list].luma_weight[i],
          w->weight_factors[list].luma_offset[i],
          w->weight_factors[list].chroma_weight[i][0],
          w->weight_factors[list].chroma_weight[i][1],
          w->weight_factors[list].chroma_offset[i][0],
          w->weight_factors[list].chroma_offset[i][1]);
    g_string_append (s, "| ");
  }

  GST_TRACE_OBJECT (self, "%s", s->str);
  g_string_free (s, TRUE);
}

void
gst_v4l2_codec_h264_debug_dump_slice_params (GstCodecDecoder * self,
    const struct v4l2_ctrl_h264_slice_params *p)
{
  GString *s = g_string_new ("H264SliceParams: ");

  g_string_append_printf (s,
      "hdr_bits=%u mb_start=%u slice_type=%u color_plane=%u redun_cnt=%u cabac_idc=%u "
      "qp_delta=%d qs_delta=%d dis_dbf=%u alpha_div2=%d beta_div2=%d "
      "ref_l0=%u ref_l1=%u flags=0x%08x | ",
      p->header_bit_size, p->first_mb_in_slice, p->slice_type,
      p->colour_plane_id, p->redundant_pic_cnt, p->cabac_init_idc,
      p->slice_qp_delta, p->slice_qs_delta, p->disable_deblocking_filter_idc,
      p->slice_alpha_c0_offset_div2, p->slice_beta_offset_div2,
      p->num_ref_idx_l0_active_minus1, p->num_ref_idx_l1_active_minus1,
      p->flags);

  g_string_append (s, "ref_pic_list0=[");
  for (int i = 0; i < V4L2_H264_REF_LIST_LEN; i++)
    g_string_append_printf (s, "{f=%u i=%u} ",
        p->ref_pic_list0[i].fields, p->ref_pic_list0[i].index);

  g_string_append (s, "] | ref_pic_list1=[");
  for (int i = 0; i < V4L2_H264_REF_LIST_LEN; i++)
    g_string_append_printf (s, "{f=%u i=%u} ",
        p->ref_pic_list1[i].fields, p->ref_pic_list1[i].index);

  g_string_append (s, "]");

  GST_TRACE_OBJECT (self, "%s", s->str);
  g_string_free (s, TRUE);
}
