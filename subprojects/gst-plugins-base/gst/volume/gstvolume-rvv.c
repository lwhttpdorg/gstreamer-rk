/* GStreamer
 * Copyright (C) 2026 Felix-Gong <gongxiaofei24@iscas.ac.cn>
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

#ifdef HAVE_CONFIG_H
#include config.h
#endif
#include gstvolume-rvv.h
#include <riscv_vector.h>
#include <gst/gstcpuid.h>

void
volume_orc_process_int16_rvv (gint16 * data, gint32 vol, gint n)
{
  gint i = 0;
  vint16m1_t vec;
  vint32m2_t wide;
  size_t avl;
  while (i < n) {
    avl = __riscv_vsetvl_e16m1 (n - i);
    vec = __riscv_vle16_v_i16m1 (data + i, avl);
    wide = __riscv_vwmul_vx_i32m2 (vec, vol, avl);
    /* Shift right and narrow (truncate/wrap - matches ORC convlw) */
    vec = __riscv_vnsra_wx_i16m1 (wide, 11, avl);
    __riscv_vse16_v_i16m1 (data + i, vec, avl);
    i += avl;
  }
}

void
volume_orc_process_int16_clamp_rvv (gint16 * data, gint32 vol, gint n)
{
  gint i = 0;
  vint16m1_t vec;
  vint32m2_t wide;
  size_t avl;
  while (i < n) {
    avl = __riscv_vsetvl_e16m1 (n - i);
    vec = __riscv_vle16_v_i16m1 (data + i, avl);
    wide = __riscv_vwmul_vx_i32m2 (vec, vol, avl);
    /* Shift in 32-bit (no narrowing) */
    wide = __riscv_vsra_vx_i32m2 (wide, 11, avl);
    /* Clamp to int16 range in 32-bit BEFORE narrowing */
    wide = __riscv_vmax_vx_i32m2 (wide, G_MININT16, avl);
    wide = __riscv_vmin_vx_i32m2 (wide, G_MAXINT16, avl);
    /* Narrow (truncation is safe - values already in int16 range) */
    vec = __riscv_vnsra_wx_i16m1 (wide, 0, avl);
    __riscv_vse16_v_i16m1 (data + i, vec, avl);
    i += avl;
  }
}

void
volume_orc_process_int32_rvv (gint32 * data, gint32 vol, gint n)
{
  gint i = 0;
  vint32m1_t vec;
  vint64m2_t wide;
  size_t avl;
  while (i < n) {
    avl = __riscv_vsetvl_e32m1 (n - i);
    vec = __riscv_vle32_v_i32m1 (data + i, avl);
    wide = __riscv_vwmul_vx_i64m2 (vec, vol, avl);
    /* Shift right and narrow (truncate/wrap - matches ORC convql) */
    vec = __riscv_vnsra_wx_i32m1 (wide, 27, avl);
    __riscv_vse32_v_i32m1 (data + i, vec, avl);
    i += avl;
  }
}

void
volume_orc_process_int32_clamp_rvv (gint32 * data, gint32 vol, gint n)
{
  gint i = 0;
  vint32m1_t vec;
  vint64m2_t wide;
  size_t avl;
  while (i < n) {
    avl = __riscv_vsetvl_e32m1 (n - i);
    vec = __riscv_vle32_v_i32m1 (data + i, avl);
    wide = __riscv_vwmul_vx_i64m2 (vec, vol, avl);
    /* Shift in 64-bit (no narrowing) */
    wide = __riscv_vsra_vx_i64m2 (wide, 27, avl);
    /* Clamp to int32 range in 64-bit BEFORE narrowing */
    wide = __riscv_vmax_vx_i64m2 (wide, G_MININT32, avl);
    wide = __riscv_vmin_vx_i64m2 (wide, G_MAXINT32, avl);
    /* Narrow (truncation is safe - values already in int32 range) */
    vec = __riscv_vnsra_wx_i32m1 (wide, 0, avl);
    __riscv_vse32_v_i32m1 (data + i, vec, avl);
    i += avl;
  }
}

void
volume_orc_scalarmultiply_f32_ns_rvv (gfloat * data, gfloat vol, gint n)
{
  gint i = 0;
  vfloat32m1_t vec;
  size_t avl;
  while (i < n) {
    avl = __riscv_vsetvl_e32m1 (n - i);
    vec = __riscv_vle32_v_f32m1 (data + i, avl);
    vec = __riscv_vfmul_vf_f32m1 (vec, vol, avl);
    __riscv_vse32_v_f32m1 (data + i, vec, avl);
    i += avl;
  }
}

void
volume_orc_scalarmultiply_f64_ns_rvv (gdouble * data, gdouble vol, gint n)
{
  gint i = 0;
  vfloat64m1_t vec;
  size_t avl;
  while (i < n) {
    avl = __riscv_vsetvl_e64m1 (n - i);
    vec = __riscv_vle64_v_f64m1 (data + i, avl);
    vec = __riscv_vfmul_vf_f64m1 (vec, vol, avl);
    __riscv_vse64_v_f64m1 (data + i, vec, avl);
    i += avl;
  }
}
