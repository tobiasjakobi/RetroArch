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

#include "SDL.h"
#include "../driver.h"
#include <stdlib.h>
#include <string.h>
#include "../general.h"
#include "scaler/scaler.h"
#include "gfx_common.h"
#include "gfx_context.h"
#include "fonts/fonts.h"

#ifdef HAVE_X11
#include "context/x11_common.h"
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "SDL/SDL_syswm.h"

typedef struct sdl_menu_frame
{
   bool active;
   SDL_Surface *frame;
   struct scaler_ctx scaler;

} sdl_menu_frame_t;

typedef struct sdl_video
{
   SDL_Surface *screen;
   bool quitting;

   void *font;
   const font_renderer_driver_t *font_driver;
   uint8_t font_r;
   uint8_t font_g;
   uint8_t font_b;

   struct scaler_ctx scaler;
   unsigned last_width;
   unsigned last_height;

   sdl_menu_frame_t menu;
} sdl_video_t;

static void sdl_gfx_free(void *data)
{
   sdl_video_t *vid = data;
   if (!vid)
      return;

   if (vid->menu.frame)
      SDL_FreeSurface(vid->menu.frame);

   SDL_QuitSubSystem(SDL_INIT_VIDEO);

   if (vid->font)
      vid->font_driver->free(vid->font);

   scaler_ctx_gen_reset(&vid->scaler);

   free(vid);
}

static void sdl_init_font(sdl_video_t *vid, const char *font_path, unsigned font_size)
{
   if (!g_settings.video.font_enable)
      return;

   if (font_renderer_create_default(&vid->font_driver, &vid->font,
            *g_settings.video.font_path ? g_settings.video.font_path : NULL,
            g_settings.video.font_size))
   {
         int r = g_settings.video.msg_color_r * 255;
         int g = g_settings.video.msg_color_g * 255;
         int b = g_settings.video.msg_color_b * 255;

         r = r < 0 ? 0 : (r > 255 ? 255 : r);
         g = g < 0 ? 0 : (g > 255 ? 255 : g);
         b = b < 0 ? 0 : (b > 255 ? 255 : b);

         vid->font_r = r;
         vid->font_g = g;
         vid->font_b = b;
   }
   else
      RARCH_LOG("Could not initialize fonts.\n");
}

static void sdl_render_msg(sdl_video_t *vid, SDL_Surface *buffer,
      const char *msg, unsigned width, unsigned height, const SDL_PixelFormat *fmt)
{
   int x, y, msg_base_x, msg_base_y, delta_x, delta_y;
   unsigned rshift, gshift, bshift;

   if (!vid->font)
      return;

   const struct font_atlas *atlas = vid->font_driver->get_atlas(vid->font);

   msg_base_x = g_settings.video.msg_pos_x * width;
   msg_base_y = (1.0f - g_settings.video.msg_pos_y) * height;

   rshift = fmt->Rshift;
   gshift = fmt->Gshift;
   bshift = fmt->Bshift;

   for (; *msg; msg++)
   {
      const struct font_glyph *glyph = vid->font_driver->get_glyph(vid->font, (uint8_t)*msg);
      if (!glyph)
         continue;

      int glyph_width  = glyph->width;
      int glyph_height = glyph->height;

      int base_x = msg_base_x + glyph->draw_offset_x;
      int base_y = msg_base_y + glyph->draw_offset_y;

      const uint8_t *src = atlas->buffer + glyph->atlas_offset_x + glyph->atlas_offset_y * atlas->width;

      if (base_x < 0)
      {
         src -= base_x;
         glyph_width += base_x;
         base_x = 0;
      }

      if (base_y < 0)
      {
         src -= base_y * (int)atlas->width;
         glyph_height += base_y;
         base_y = 0;
      }

      int max_width  = width - base_x;
      int max_height = height - base_y;

      if (max_width <= 0 || max_height <= 0)
         continue;

      if (glyph_width > max_width)
         glyph_width = max_width;
      if (glyph_height > max_height)
         glyph_height = max_height;

      uint32_t *out = (uint32_t*)buffer->pixels + base_y * (buffer->pitch >> 2) + base_x;

      for (y = 0; y < glyph_height; y++, src += atlas->width, out += buffer->pitch >> 2)
      {
         for (x = 0; x < glyph_width; x++)
         {
            unsigned blend = src[x];
            unsigned out_pix = out[x];
            unsigned r = (out_pix >> rshift) & 0xff;
            unsigned g = (out_pix >> gshift) & 0xff;
            unsigned b = (out_pix >> bshift) & 0xff;

            unsigned out_r = (r * (256 - blend) + vid->font_r * blend) >> 8;
            unsigned out_g = (g * (256 - blend) + vid->font_g * blend) >> 8;
            unsigned out_b = (b * (256 - blend) + vid->font_b * blend) >> 8;
            out[x] = (out_r << rshift) | (out_g << gshift) | (out_b << bshift);
         }
      }

      msg_base_x += glyph->advance_x;
      msg_base_y += glyph->advance_y;
   }
}

static void sdl_gfx_set_handles(void)
{
#if defined(HAVE_X11)
   SDL_SysWMinfo info;
   SDL_VERSION(&info.version);

   if (SDL_GetWMInfo(&info) == 1)
   {
      driver.display_type  = RARCH_DISPLAY_X11;
      driver.video_display = (uintptr_t)info.info.x11.display;
      driver.video_window  = (uintptr_t)info.info.x11.window;
   }
#endif
}

static void *sdl_gfx_init(const video_info_t *video, const input_driver_t **input, void **input_data)
{
#ifdef HAVE_X11
   XInitThreads();
#endif

   if (SDL_WasInit(0) == 0 && SDL_Init(SDL_INIT_VIDEO) < 0)
      return NULL;
   else if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
      return NULL;

   sdl_video_t *vid = calloc(1, sizeof(*vid));
   if (!vid)
      return NULL;

   const SDL_VideoInfo *video_info = SDL_GetVideoInfo();
   rarch_assert(video_info);
   unsigned full_x = video_info->current_w;
   unsigned full_y = video_info->current_h;
   RARCH_LOG("Detecting desktop resolution %ux%u.\n", full_x, full_y);

   void *sdl_input = NULL;

   if (!video->fullscreen)
      RARCH_LOG("Creating window @ %ux%u\n", video->width, video->height);

   vid->screen = SDL_SetVideoMode(video->width, video->height, 32,
         SDL_HWSURFACE | SDL_HWACCEL | SDL_DOUBLEBUF | (video->fullscreen ? SDL_FULLSCREEN : 0));

   // We assume that SDL chooses ARGB8888.
   // Assuming this simplifies the driver *a ton*.

   if (!vid->screen)
   {
      RARCH_ERR("Failed to init SDL surface: %s\n", SDL_GetError());
      goto error;
   }

   if (video->fullscreen)
      SDL_ShowCursor(SDL_DISABLE);

   sdl_gfx_set_handles();

   if (input && input_data)
   {
      sdl_input = input_sdl.init();
      if (sdl_input)
      {
         *input = &input_sdl;
         *input_data = sdl_input;
      }
      else
      {
         *input = NULL;
         *input_data = NULL;
      }
   }

   sdl_init_font(vid, g_settings.video.font_path, g_settings.video.font_size);

   vid->scaler.scaler_type = video->smooth ? SCALER_TYPE_BILINEAR : SCALER_TYPE_POINT;
   vid->scaler.in_fmt  = video->rgb32 ? SCALER_FMT_ARGB8888 : SCALER_FMT_RGB565;
   vid->scaler.out_fmt = SCALER_FMT_ARGB8888;

   vid->menu.scaler = vid->scaler;
   vid->menu.scaler.scaler_type = SCALER_TYPE_BILINEAR;

   vid->menu.frame = SDL_ConvertSurface(vid->screen, vid->screen->format, vid->screen->flags | SDL_SRCALPHA);

   if (!vid->menu.frame)
   {
      RARCH_ERR("Failed to init menu surface: %s\n", SDL_GetError());
      goto error;
   }

   return vid;

error:
   sdl_gfx_free(vid);
   return NULL;
}

static void check_window(sdl_video_t *vid)
{
   SDL_Event event;
   while (SDL_PollEvent(&event))
   {
      switch (event.type)
      {
         case SDL_QUIT:
            vid->quitting = true;
            break;

         default:
            break;
      }
   }
}

static bool sdl_gfx_frame(void *data, const void *frame, unsigned width, unsigned height, unsigned pitch, const char *msg)
{
   if (!frame)
      return true;

   sdl_video_t *vid = data;

   vid->scaler.in_stride = pitch;
   if (width != vid->last_width || height != vid->last_height)
   {
      vid->scaler.in_width  = width;
      vid->scaler.in_height = height;

      vid->scaler.out_width  = vid->screen->w;
      vid->scaler.out_height = vid->screen->h;
      vid->scaler.out_stride = vid->screen->pitch;

      scaler_ctx_gen_filter(&vid->scaler);

      vid->last_width  = width;
      vid->last_height = height;
   }

   if (SDL_MUSTLOCK(vid->screen))
      SDL_LockSurface(vid->screen);

   RARCH_PERFORMANCE_INIT(sdl_scale);
   RARCH_PERFORMANCE_START(sdl_scale);
   scaler_ctx_scale(&vid->scaler, vid->screen->pixels, frame);
   RARCH_PERFORMANCE_STOP(sdl_scale);

   if (vid->menu.active)
      SDL_BlitSurface(vid->menu.frame, NULL, vid->screen, NULL);

   if (msg)
      sdl_render_msg(vid, vid->screen, msg, vid->screen->w, vid->screen->h, vid->screen->format);

   if (SDL_MUSTLOCK(vid->screen))
      SDL_UnlockSurface(vid->screen);

   char buf[128];
   if (gfx_get_fps(buf, sizeof(buf), NULL, 0))
      SDL_WM_SetCaption(buf, NULL);

   SDL_Flip(vid->screen);
   g_extern.frame_count++;

   return true;
}

static void sdl_gfx_set_nonblock_state(void *data, bool state)
{
   (void)data; // Can SDL even do this?
   (void)state;
}

static bool sdl_gfx_alive(void *data)
{
   sdl_video_t *vid = data;
   check_window(vid);
   return !vid->quitting;
}

static bool sdl_gfx_focus(void *data)
{
   (void)data;
   return (SDL_GetAppState() & (SDL_APPINPUTFOCUS | SDL_APPACTIVE)) == (SDL_APPINPUTFOCUS | SDL_APPACTIVE);
}

static void sdl_gfx_viewport_info(void *data, struct rarch_viewport *vp)
{
   sdl_video_t *vid = data;
   vp->x = vp->y = 0;
   vp->width  = vp->full_width  = vid->screen->w;
   vp->height = vp->full_height = vid->screen->h;
}

static void sdl_set_filtering(void *data, unsigned index, bool smooth)
{
   sdl_video_t *vid = data;
   vid->scaler.scaler_type = smooth ? SCALER_TYPE_BILINEAR : SCALER_TYPE_POINT;
}

static void sdl_set_aspect_ratio(void *data, unsigned aspectratio_index)
{
   sdl_video_t *vid = data;

   switch (aspectratio_index)
   {
      case ASPECT_RATIO_SQUARE:
         gfx_set_square_pixel_viewport(g_extern.system.av_info.geometry.base_width, g_extern.system.av_info.geometry.base_height);
         break;

      case ASPECT_RATIO_CORE:
         gfx_set_core_viewport();
         break;

      case ASPECT_RATIO_CONFIG:
         gfx_set_config_viewport();
         break;

      default:
         break;
   }

   g_extern.system.aspect_ratio = aspectratio_lut[aspectratio_index].value;
}

static void sdl_apply_state_changes(void *data)
{
   (void)data;
}

static void sdl_set_texture_frame(void *data, const void *frame, bool rgb32,
                               unsigned width, unsigned height, float alpha)
{
   (void) alpha;
   sdl_video_t *vid = data;

   enum scaler_pix_fmt format = rgb32 ? SCALER_FMT_ARGB8888 : SCALER_FMT_RGBA4444;

   vid->menu.scaler.in_stride = width * (rgb32 ? sizeof(uint32_t) : sizeof(uint16_t));

   if (
          width != vid->menu.scaler.in_width
       || height != vid->menu.scaler.in_height
       || format != vid->menu.scaler.in_fmt
      )
   {
      vid->menu.scaler.in_fmt    = format;
      vid->menu.scaler.in_width  = width;
      vid->menu.scaler.in_height = height;

      vid->menu.scaler.out_width  = vid->screen->w;
      vid->menu.scaler.out_height = vid->screen->h;
      vid->menu.scaler.out_stride = vid->screen->pitch;

      scaler_ctx_gen_filter(&vid->menu.scaler);
   }

   scaler_ctx_scale(&vid->menu.scaler, vid->menu.frame->pixels, frame);
   SDL_SetAlpha(vid->menu.frame, SDL_SRCALPHA, 255.0 * alpha);
}

static void sdl_set_texture_enable(void *data, bool state, bool full_screen)
{
   (void) full_screen;

   sdl_video_t *vid = data;
   vid->menu.active = state;
}

static void sdl_show_mouse(void *data, bool state)
{
   (void)data;
   SDL_ShowCursor(state);
}

static void sdl_grab_mouse_toggle(void *data)
{
   (void)data;
   const SDL_GrabMode mode = SDL_WM_GrabInput(SDL_GRAB_QUERY);
   SDL_WM_GrabInput(mode == SDL_GRAB_ON ? SDL_GRAB_OFF : SDL_GRAB_ON);
}

static const video_poke_interface_t sdl_poke_interface = {
   sdl_set_filtering,
#ifdef HAVE_FBO
   NULL,
   NULL,
#endif
   sdl_set_aspect_ratio,
   sdl_apply_state_changes,
#ifdef HAVE_MENU
   sdl_set_texture_frame,
   sdl_set_texture_enable,
#endif
   NULL,
   sdl_show_mouse,
   sdl_grab_mouse_toggle,
   NULL
};

static void sdl_get_poke_interface(void *data, const video_poke_interface_t **iface)
{
   (void)data;
   *iface = &sdl_poke_interface;
}

const video_driver_t video_sdl = {
   .init = sdl_gfx_init,
   .frame = sdl_gfx_frame,
   .set_nonblock_state = sdl_gfx_set_nonblock_state,
   .alive = sdl_gfx_alive,
   .focus = sdl_gfx_focus,
   .free = sdl_gfx_free,
   .ident = "sdl",
   .viewport_info = sdl_gfx_viewport_info,
   .poke_interface = sdl_get_poke_interface
};

