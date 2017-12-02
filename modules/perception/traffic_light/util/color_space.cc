/*********************************************************************
 *
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2014, Robert Bosch LLC.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Robert Bosch nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *********************************************************************/

#include "modules/perception/traffic_light/util/color_space.h"

#include <cassert>
#include <cstdint>

namespace apollo {
namespace perception {
namespace traffic_light {

template <bool align>
SIMD_INLINE void YuvSeperateAvx2(uint8_t *y, __m256i &y0,     // NOLINT
                                 __m256i &y1,                 // NOLINT
                                 __m256i &u0, __m256i &v0) {  // NOLINT
  __m256i yuv_m256[4];

  if (align) {
    yuv_m256[0] = Load<true>(reinterpret_cast<__m256i *>(y));
    yuv_m256[1] = Load<true>(reinterpret_cast<__m256i *>(y) + 1);
    yuv_m256[2] = Load<true>(reinterpret_cast<__m256i *>(y) + 2);
    yuv_m256[3] = Load<true>(reinterpret_cast<__m256i *>(y) + 3);
  } else {
    yuv_m256[0] = Load<false>(reinterpret_cast<__m256i *>(y));
    yuv_m256[1] = Load<false>(reinterpret_cast<__m256i *>(y) + 1);
    yuv_m256[2] = Load<false>(reinterpret_cast<__m256i *>(y) + 2);
    yuv_m256[3] = Load<false>(reinterpret_cast<__m256i *>(y) + 3);
  }

  y0 = _mm256_or_si256(_mm256_permute4x64_epi64(
                           _mm256_shuffle_epi8(yuv_m256[0], Y_SHUFFLE0), 0xD8),
                       _mm256_permute4x64_epi64(
                           _mm256_shuffle_epi8(yuv_m256[1], Y_SHUFFLE1), 0xD8));
  y1 = _mm256_or_si256(_mm256_permute4x64_epi64(
                           _mm256_shuffle_epi8(yuv_m256[2], Y_SHUFFLE0), 0xD8),
                       _mm256_permute4x64_epi64(
                           _mm256_shuffle_epi8(yuv_m256[3], Y_SHUFFLE1), 0xD8));

  u0 = _mm256_permutevar8x32_epi32(
      _mm256_or_si256(
          _mm256_or_si256(_mm256_shuffle_epi8(yuv_m256[0], U_SHUFFLE0),
                          _mm256_shuffle_epi8(yuv_m256[1], U_SHUFFLE1)),
          _mm256_or_si256(_mm256_shuffle_epi8(yuv_m256[2], U_SHUFFLE2),
                          _mm256_shuffle_epi8(yuv_m256[3], U_SHUFFLE3))),
      U_SHUFFLE4);
  v0 = _mm256_permutevar8x32_epi32(
      _mm256_or_si256(
          _mm256_or_si256(_mm256_shuffle_epi8(yuv_m256[0], V_SHUFFLE0),
                          _mm256_shuffle_epi8(yuv_m256[1], V_SHUFFLE1)),
          _mm256_or_si256(_mm256_shuffle_epi8(yuv_m256[2], V_SHUFFLE2),
                          _mm256_shuffle_epi8(yuv_m256[3], V_SHUFFLE3))),
      U_SHUFFLE4);
}

template <bool align>
void Yuv2rgbAvx2(__m256i y0, __m256i u0, __m256i v0, uint8_t *rgb) {
  __m256i r0 = YuvToRed(y0, v0);
  __m256i g0 = YuvToGreen(y0, u0, v0);
  __m256i b0 = YuvToBlue(y0, u0);

  Store<align>((__m256i *)rgb + 0, InterleaveBgr<0>(r0, g0, b0));  // NOLINT
  Store<align>((__m256i *)rgb + 1, InterleaveBgr<1>(r0, g0, b0));  // NOLINT
  Store<align>((__m256i *)rgb + 2, InterleaveBgr<2>(r0, g0, b0));  // NOLINT
}

template <bool align>
void Yuv2rgbAvx2(uint8_t *yuv, uint8_t *rgb) {
  __m256i y0, y1, u0, v0;

  YuvSeperateAvx2<align>(yuv, y0, y1, u0, v0);
  __m256i u0_u0 = _mm256_permute4x64_epi64(u0, 0xD8);
  __m256i v0_v0 = _mm256_permute4x64_epi64(v0, 0xD8);
  Yuv2rgbAvx2<align>(y0, _mm256_unpacklo_epi8(u0_u0, u0_u0),
                     _mm256_unpacklo_epi8(v0_v0, v0_v0), rgb);
  Yuv2rgbAvx2<align>(y1, _mm256_unpackhi_epi8(u0_u0, u0_u0),
                     _mm256_unpackhi_epi8(v0_v0, v0_v0),
                     rgb + 3 * sizeof(__m256i));
}

void Yuyv2rgbAvx(unsigned char *YUV, unsigned char *RGB, int NumPixels) {
  assert(NumPixels == (1920 * 1080));
  bool align = Aligned(YUV) & Aligned(RGB);
  uint8_t *yuv_offset = YUV;
  uint8_t *rgb_offset = RGB;
  if (align) {
    for (int i = 0; i < NumPixels; i = i + (2 * sizeof(__m256i)),
             yuv_offset += 4 * sizeof(__m256i),
             rgb_offset += 6 * sizeof(__m256i)) {
      Yuv2rgbAvx2<true>(yuv_offset, rgb_offset);
    }
  } else {
    for (int i = 0; i < NumPixels; i = i + (2 * sizeof(__m256i)),
             yuv_offset += 4 * sizeof(__m256i),
             rgb_offset += 6 * sizeof(__m256i)) {
      Yuv2rgbAvx2<false>(yuv_offset, rgb_offset);
    }
  }
}

}  // namespace traffic_light
}  // namespace perception
}  // namespace apollo
