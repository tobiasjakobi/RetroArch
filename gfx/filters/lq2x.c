/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2014 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

// Compile: gcc -o lq2x.so -shared lq2x.c -std=c99 -O3 -Wall -pedantic -fPIC

#include "softfilter.h"
#include <stdlib.h>

#define LQ2X_SCALE 2

struct softfilter_thread_data
{
   void *out_data;
   const void *in_data;
   size_t out_pitch;
   size_t in_pitch;
   unsigned colfmt;
   unsigned width;
   unsigned height;
   int first;
   int last;
};

struct filter_data
{
   unsigned threads;
   struct softfilter_thread_data *workers;
   unsigned in_fmt;
};

static unsigned lq2x_generic_input_fmts()
{
   return SOFTFILTER_FMT_RGB565 | SOFTFILTER_FMT_XRGB8888;
}

static unsigned lq2x_generic_output_fmts(unsigned input_fmts)
{
   return input_fmts;
}

static unsigned lq2x_generic_threads(void *data)
{
   struct filter_data *filt = data;
   return filt->threads;
}

static void *lq2x_generic_create(unsigned in_fmt, unsigned out_fmt,
      unsigned max_width, unsigned max_height,
      unsigned threads, softfilter_simd_mask_t simd)
{
   (void)simd;

   struct filter_data *filt = calloc(1, sizeof(*filt));
   if (!filt)
      return NULL;
   filt->workers = calloc(threads, sizeof(struct softfilter_thread_data));
   filt->threads = threads;
   filt->in_fmt  = in_fmt;
   if (!filt->workers)
   {
      free(filt);
      return NULL;
   }
   return filt;
}

static void lq2x_generic_output(void *data, unsigned *out_width, unsigned *out_height,
      unsigned width, unsigned height)
{
   *out_width = width * LQ2X_SCALE;
   *out_height = height * LQ2X_SCALE;
}

static void lq2x_generic_destroy(void *data)
{
   struct filter_data *filt = data;
   free(filt->workers);
   free(filt);
}

static void lq2x_generic_rgb565(unsigned width, unsigned height,
      int first, int last, uint16_t *src, 
      unsigned src_stride, uint16_t *dst, unsigned dst_stride)
{
   uint16_t *out0, *out1;
   out0 = dst;
   out1 = dst + dst_stride;

   for (unsigned y = 0; y < height; y++)
   {
      int prevline, nextline;
      prevline = (y == 0 ? 0 : src_stride);
      nextline = (y == height - 1 || last) ? 0 : src_stride;

      for (unsigned x = 0; x < width; x++)
      {
         uint16_t A, B, C, D, E, c;
         A = *(src - prevline);
         B = (x > 0) ? *(src - 1) : *src;
         C = *src;
         D = (x < width - 1) ? *(src + 1) : *src;
         E = *(src++ + nextline);
         c = C;

         if(A != E && B != D)
         {
            *out0++ = (A == B ? ((C + A - ((C ^ A) & 0x0821)) >> 1) : c);
            *out0++ = (A == D ? ((C + A - ((C ^ A) & 0x0821)) >> 1) : c);
            *out1++ = (E == B ? ((C + E - ((C ^ E) & 0x0821)) >> 1) : c);
            *out1++ = (E == D ? ((C + E - ((C ^ E) & 0x0821)) >> 1) : c);
         }
         else
         {
            *out0++ = c;
            *out0++ = c;
            *out1++ = c;
            *out1++ = c;
         }
      }

      src += src_stride - width;
      out0 += dst_stride + dst_stride - (width << 1);
      out1 += dst_stride + dst_stride - (width << 1);
   }
}

static void lq2x_generic_xrgb8888(unsigned width, unsigned height,
      int first, int last, uint32_t *src, 
      unsigned src_stride, uint32_t *dst, unsigned dst_stride)
{
   unsigned x, y;
   uint32_t *out0, *out1;
   out0 = dst;
   out1 = dst + dst_stride;

   for(y = 0; y < height; y++)
   {
      int prevline = (y == 0 ? 0 : src_stride);
      int nextline = (y == height - 1 || last) ? 0 : src_stride;

      for(x = 0; x < width; x++)
      {
         uint32_t A = *(src - prevline);
         uint32_t B = (x > 0) ? *(src - 1) : *src;
         uint32_t C = *src;
         uint32_t D = (x < width - 1) ? *(src + 1) : *src;
         uint32_t E = *(src++ + nextline);
         uint32_t c = C;

         if(A != E && B != D)
         {
            *out0++ = (A == B ? (C + A - ((C ^ A) & 0x0421)) >> 1 : c);
            *out0++ = (A == D ? (C + A - ((C ^ A) & 0x0421)) >> 1 : c);
            *out1++ = (E == B ? (C + E - ((C ^ E) & 0x0421)) >> 1 : c);
            *out1++ = (E == D ? (C + E - ((C ^ E) & 0x0421)) >> 1 : c);
         }
         else
         {
            *out0++ = c;
            *out0++ = c;
            *out1++ = c;
            *out1++ = c;
         }
      }

      src += src_stride - width;
      out0 += dst_stride + dst_stride - (width << 1);
      out1 += dst_stride + dst_stride - (width << 1);
   }
}

static void lq2x_work_cb_rgb565(void *data, void *thread_data)
{
   struct softfilter_thread_data *thr = thread_data;
   uint16_t *input = thr->in_data;
   uint16_t *output = thr->out_data;
   unsigned width = thr->width;
   unsigned height = thr->height;

   lq2x_generic_rgb565(width, height,
         thr->first, thr->last, input, thr->in_pitch / SOFTFILTER_BPP_RGB565, output, thr->out_pitch / SOFTFILTER_BPP_RGB565);
}

static void lq2x_work_cb_xrgb8888(void *data, void *thread_data)
{
   struct softfilter_thread_data *thr = thread_data;
   uint32_t *input = thr->in_data;
   uint32_t *output = thr->out_data;
   unsigned width = thr->width;
   unsigned height = thr->height;

   (void)data;

   lq2x_generic_xrgb8888(width, height,
         thr->first, thr->last, input, thr->in_pitch / SOFTFILTER_BPP_XRGB8888, output, thr->out_pitch / SOFTFILTER_BPP_XRGB8888);
}

static void lq2x_generic_packets(void *data,
      struct softfilter_work_packet *packets,
      void *output, size_t output_stride,
      const void *input, unsigned width, unsigned height, size_t input_stride)
{
   struct filter_data *filt = data;
   unsigned i;
   for (i = 0; i < filt->threads; i++)
   {
      struct softfilter_thread_data *thr = &filt->workers[i];

      unsigned y_start = (height * i) / filt->threads;
      unsigned y_end = (height * (i + 1)) / filt->threads;
      thr->out_data = (uint8_t*)output + y_start * LQ2X_SCALE * output_stride;
      thr->in_data = (const uint8_t*)input + y_start * input_stride;
      thr->out_pitch = output_stride;
      thr->in_pitch = input_stride;
      thr->width = width;
      thr->height = y_end - y_start;

      // Workers need to know if they can access pixels outside their given buffer.
      thr->first = y_start;
      thr->last = y_end == height;

      if (filt->in_fmt == SOFTFILTER_FMT_RGB565)
         packets[i].work = lq2x_work_cb_rgb565;
      //else if (filt->in_fmt == SOFTFILTER_FMT_RGB4444)
         //packets[i].work = lq2x_work_cb_rgb4444;
      else if (filt->in_fmt == SOFTFILTER_FMT_XRGB8888)
         packets[i].work = lq2x_work_cb_xrgb8888;
      packets[i].thread_data = thr;
   }
}

static const struct softfilter_implementation lq2x_generic = {
   lq2x_generic_input_fmts,
   lq2x_generic_output_fmts,

   lq2x_generic_create,
   lq2x_generic_destroy,

   lq2x_generic_threads,
   lq2x_generic_output,
   lq2x_generic_packets,
   "LQ2x",
   SOFTFILTER_API_VERSION,
};

const struct softfilter_implementation *softfilter_get_implementation(softfilter_simd_mask_t simd)
{
   (void)simd;
   return &lq2x_generic;
}
