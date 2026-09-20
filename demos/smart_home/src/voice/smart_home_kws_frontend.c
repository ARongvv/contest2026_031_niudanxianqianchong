/****************************************************************************
 * demos/smart_home/src/voice/smart_home_kws_frontend.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2026 The NuttX Contributors
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "smart_home_kws_frontend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ── 以下常数待与训练管线对拍（见头文件警告）────────────────── */
#define KWS_FFT_SIZE          512                    /* FFT 点数 */
#define KWS_FFT_BINS          (KWS_FFT_SIZE / 2 + 1) /* 257 */
#define KWS_MEL_FMIN_HZ       20.0f
#define KWS_MEL_FMAX_HZ       7990.0f
#define KWS_LOG_EPSILON       1e-6f                  /* ln(x + eps) */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static float g_hann[KWS_FRONTEND_FRAME_SAMPLES];
static float g_mel_weights[KWS_FRONTEND_MEL_BINS][KWS_FFT_BINS];
static int g_bit_reverse[KWS_FFT_SIZE];
static int g_frontend_ready;

/* 每帧复数 FFT 的临时缓冲（compute 调用方串行化）。 */
static float g_fft_re[KWS_FFT_SIZE];
static float g_fft_im[KWS_FFT_SIZE];
static float g_power[KWS_FFT_BINS];
static float g_frame_mel[KWS_FRONTEND_FRAMES][KWS_FRONTEND_MEL_BINS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static float hz_to_mel(float hz)
{
  return 1127.0f * logf(1.0f + hz / 700.0f);
}

static float mel_to_hz(float mel)
{
  return 700.0f * (expf(mel / 1127.0f) - 1.0f);
}

/* 迭代基-2 复数 FFT（就地）。n 为 KWS_FFT_SIZE。 */
static void fft_512(float *re, float *im)
{
  int i, j, len, half;
  const int n = KWS_FFT_SIZE;

  for (i = 1; i < n; i++)
    {
      j = g_bit_reverse[i];
      if (j > i)
        {
          float t;

          t = re[i]; re[i] = re[j]; re[j] = t;
          t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

  for (len = 2; len <= n; len <<= 1)
    {
      half = len >> 1;
      for (i = 0; i < n; i += len)
        {
          for (j = 0; j < half; j++)
            {
              float angle = -2.0f * (float)M_PI * j / len;
              float w_re = cosf(angle);
              float w_im = sinf(angle);
              float u_re = re[i + j];
              float u_im = im[i + j];
              float v_re = re[i + j + half] * w_re - im[i + j + half] * w_im;
              float v_im = re[i + j + half] * w_im + im[i + j + half] * w_re;

              re[i + j] = u_re + v_re;
              im[i + j] = u_im + v_im;
              re[i + j + half] = u_re - v_re;
              im[i + j + half] = u_im - v_im;
            }
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int kws_frontend_init(void)
{
  int i, bin, mel;
  float mel_min, mel_max, mel_step;

  if (g_frontend_ready)
    {
      return 0;
    }

  /* Hann 窗。 */
  for (i = 0; i < KWS_FRONTEND_FRAME_SAMPLES; i++)
    {
      g_hann[i] = 0.5f *
          (1.0f - cosf(2.0f * (float)M_PI * i /
                       (KWS_FRONTEND_FRAME_SAMPLES - 1)));
    }

  /* 位反转表。 */
  {
    int bits = 0;

    while ((1 << bits) < KWS_FFT_SIZE)
      {
        bits++;
      }

    for (i = 0; i < KWS_FFT_SIZE; i++)
      {
        int x = i;
        int r = 0;
        int b;

        for (b = 0; b < bits; b++)
          {
            r = (r << 1) | (x & 1);
            x >>= 1;
          }

        g_bit_reverse[i] = r;
      }
  }

  /* 三角 mel 滤波器组。 */
  mel_min = hz_to_mel(KWS_MEL_FMIN_HZ);
  mel_max = hz_to_mel(KWS_MEL_FMAX_HZ);
  mel_step = (mel_max - mel_min) / (KWS_FRONTEND_MEL_BINS + 1);

  for (mel = 0; mel < KWS_FRONTEND_MEL_BINS; mel++)
    {
      float left = mel_to_hz(mel_min + mel_step * mel);
      float center = mel_to_hz(mel_min + mel_step * (mel + 1));
      float right = mel_to_hz(mel_min + mel_step * (mel + 2));

      memset(g_mel_weights[mel], 0, sizeof(g_mel_weights[mel]));
      for (bin = 0; bin < KWS_FFT_BINS; bin++)
        {
          float freq = (float)bin * 16000.0f / KWS_FFT_SIZE;

          if (freq <= left || freq >= right)
            {
              continue;
            }

          if (freq <= center)
            {
              g_mel_weights[mel][bin] = (freq - left) / (center - left);
            }
          else
            {
              g_mel_weights[mel][bin] = (right - freq) / (right - center);
            }
        }
    }

  g_frontend_ready = 1;
  return 0;
}

int kws_frontend_compute(const int16_t *pcm, float quant_scale,
                         int quant_zero_point, int8_t *out)
{
  int frame, mel, ch, t;
  float frame_energy[KWS_FRONTEND_MEL_BINS];

  if (pcm == NULL || out == NULL || quant_scale <= 0.0f)
    {
      return -1;
    }

  if (kws_frontend_init() != 0)
    {
      return -1;
    }

  /* 逐帧：加窗 → FFT → 功率谱 → mel 能量 → ln。 */
  for (frame = 0; frame < KWS_FRONTEND_FRAMES; frame++)
    {
      const int16_t *src = pcm + frame * KWS_FRONTEND_HOP_SAMPLES;
      int i;

      memset(g_fft_im, 0, sizeof(g_fft_im));
      for (i = 0; i < KWS_FRONTEND_FRAME_SAMPLES; i++)
        {
          g_fft_re[i] = (float)src[i] * g_hann[i];
        }

      memset(g_fft_re + KWS_FRONTEND_FRAME_SAMPLES, 0,
             sizeof(g_fft_re) -
                 KWS_FRONTEND_FRAME_SAMPLES * sizeof(float));

      fft_512(g_fft_re, g_fft_im);

      for (i = 0; i < KWS_FFT_BINS; i++)
        {
          g_power[i] = g_fft_re[i] * g_fft_re[i] +
                       g_fft_im[i] * g_fft_im[i];
        }

      for (mel = 0; mel < KWS_FRONTEND_MEL_BINS; mel++)
        {
          float energy = 0.0f;

          for (i = 0; i < KWS_FFT_BINS; i++)
            {
              energy += g_mel_weights[mel][i] * g_power[i];
            }

          frame_energy[mel] = logf(energy + KWS_LOG_EPSILON);
        }

      memcpy(g_frame_mel[frame], frame_energy,
             sizeof(frame_energy));
    }

  /* 堆叠与量化：out[t][m][c] = q(frame[t+c][m])，NHWC 布局。 */
  for (t = 0; t < KWS_FRONTEND_TIME_STEPS; t++)
    {
      for (mel = 0; mel < KWS_FRONTEND_MEL_BINS; mel++)
        {
          for (ch = 0; ch < KWS_FRONTEND_STACK; ch++)
            {
              float value = g_frame_mel[t + ch][mel];
              float q = value / quant_scale + (float)quant_zero_point;
              int iq = (int)lroundf(q);

              if (iq > 127)
                {
                  iq = 127;
                }

              if (iq < -128)
                {
                  iq = -128;
                }

              out[(t * KWS_FRONTEND_MEL_BINS + mel) *
                      KWS_FRONTEND_STACK + ch] = (int8_t)iq;
            }
        }
    }

  return 0;
}
