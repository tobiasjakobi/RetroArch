/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
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

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#include "image.h"
#include "../../file.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "../../general.h"
#include "../rpng/rpng.h"

static bool rpng_image_load_tga_shift(const char *path, struct texture_image *out_img,
      unsigned a_shift, unsigned r_shift, unsigned g_shift, unsigned b_shift)
{
   unsigned i;
   void *raw_buf = NULL;
   ssize_t len = read_file(path, &raw_buf);
   if (len < 0)
   {
      RARCH_ERR("Failed to read image: %s.\n", path);
      return false;
   }

   uint8_t *buf = (uint8_t*)raw_buf;

   if (buf[2] != 2) // Uncompressed RGB
   {
      RARCH_ERR("TGA image is not uncompressed RGB.\n");
      free(buf);
      return false;
   }

   unsigned width = 0;
   unsigned height = 0;

   uint8_t info[6];
   memcpy(info, buf + 12, 6);

   width = info[0] + ((unsigned)info[1] * 256);
   height = info[2] + ((unsigned)info[3] * 256);
   unsigned bits = info[4];

   RARCH_LOG("Loaded TGA: (%ux%u @ %u bpp)\n", width, height, bits);

   unsigned size = width * height * sizeof(uint32_t);
   out_img->pixels = malloc(size);
   out_img->width = width;
   out_img->height = height;
   if (!out_img->pixels)
   {
      RARCH_ERR("Failed to allocate TGA pixels.\n");
      free(buf);
      return false;
   }

   const uint8_t *tmp = buf + 18;
   if (bits == 32)
   {
      for (i = 0; i < width * height; i++)
      {
         uint32_t b = tmp[i * 4 + 0];
         uint32_t g = tmp[i * 4 + 1];
         uint32_t r = tmp[i * 4 + 2];
         uint32_t a = tmp[i * 4 + 3];

         out_img->pixels[i] = (a << a_shift) | (r << r_shift) | (g << g_shift) | (b << b_shift);
      }
   }
   else if (bits == 24)
   {
      for (i = 0; i < width * height; i++)
      {
         uint32_t b = tmp[i * 3 + 0];
         uint32_t g = tmp[i * 3 + 1];
         uint32_t r = tmp[i * 3 + 2];
         uint32_t a = 0xff;

         out_img->pixels[i] = (a << a_shift) | (r << r_shift) | (g << g_shift) | (b << b_shift);
      }
   }
   else
   {
      RARCH_ERR("Bit depth of TGA image is wrong. Only 32-bit and 24-bit supported.\n");
      free(buf);
      free(out_img->pixels);
      out_img->pixels = NULL;
      return false;
   }

   free(buf);
   return true;
}

static bool rpng_image_load_argb_shift(const char *path, struct texture_image *out_img,
      unsigned a_shift, unsigned r_shift, unsigned g_shift, unsigned b_shift)
{
   if (strstr(path, ".tga"))
      return rpng_image_load_tga_shift(path, out_img, a_shift, r_shift, g_shift, b_shift);
#ifdef HAVE_ZLIB
   else if (strstr(path, ".png"))
   {
      bool ret = rpng_load_image_argb(path, &out_img->pixels, &out_img->width, &out_img->height);
      if (!ret)
         return false;

      // This is quite uncommon ...
      if (a_shift != 24 || r_shift != 16 || g_shift != 8 || b_shift != 0)
      {
         uint32_t i, num_pixels, *pixels;
         num_pixels = out_img->width * out_img->height;
         pixels = (uint32_t*)out_img->pixels;

         for (i = 0; i < num_pixels; i++)
         {
            uint32_t col = pixels[i];
            uint8_t a = (uint8_t)(col >> 24);
            uint8_t r = (uint8_t)(col >> 16);
            uint8_t g = (uint8_t)(col >>  8);
            uint8_t b = (uint8_t)(col >>  0);
            pixels[i] = (a << a_shift) | (r << r_shift) | (g << g_shift) | (b << b_shift);
         }
      }

      return true;
   }
#endif

   return false;
}

void texture_image_free(struct texture_image *img)
{
   free(img->pixels);
   memset(img, 0, sizeof(*img));
}

bool texture_image_load(struct texture_image *out_img, const char *path)
{
   bool ret;

   // This interface "leak" is very ugly. FIXME: Fix this properly ...
   if (driver.gfx_use_rgba)
      ret = rpng_image_load_argb_shift(path, out_img, 24, 0, 8, 16);
   else
      ret = rpng_image_load_argb_shift(path, out_img, 24, 16, 8, 0);

   return ret;
}
