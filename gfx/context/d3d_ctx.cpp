/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2014 - Daniel De Matteis
 *  Copyright (C) 2012-2014 - OV2
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

#include "../d3d9/d3d.hpp"
#include "win32_common.h"

#include "../gfx_common.h"

#ifdef _MSC_VER
#pragma comment( lib, "d3d9" )
#pragma comment( lib, "d3dx9" )
#pragma comment( lib, "cgd3d9" )
#pragma comment( lib, "dxguid" )
#endif

static d3d_video_t *curD3D = NULL;
static bool d3d_quit = false;
static void *dinput;

extern bool d3d_restore(d3d_video_t *data);

static void d3d_resize(void *data, unsigned new_width, unsigned new_height)
{
   (void)data;
   d3d_video_t *d3d = (d3d_video_t*)curD3D;
   LPDIRECT3DDEVICE d3dr = (LPDIRECT3DDEVICE)d3d->dev;
   if (!d3dr)
      return;

   RARCH_LOG("[D3D]: Resize %ux%u.\n", new_width, new_height);

   if (new_width != d3d->video_info.width || new_height != d3d->video_info.height)
   {
      d3d->video_info.width = d3d->screen_width = new_width;
      d3d->video_info.height = d3d->screen_height = new_height;
      d3d_restore(d3d);
   }
}

#ifdef HAVE_WINDOW
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message,
        WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
			LPCREATESTRUCT p_cs;
			p_cs = (LPCREATESTRUCT)lParam;
			curD3D = (d3d_video_t*)p_cs->lpCreateParams;
			break;

        case WM_CHAR:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
        case WM_SYSKEYDOWN:
			return win32_handle_keyboard_event(hWnd, message, wParam, lParam);

        case WM_DESTROY:
			d3d_quit = true;
			return 0;
        case WM_SIZE:
			unsigned new_width, new_height;
			new_width = LOWORD(lParam);
			new_height = HIWORD(lParam);

			if (new_width && new_height)
				d3d_resize(driver.video_data, new_width, new_height);
			return 0;
    }
    if (dinput_handle_message(dinput, message, wParam, lParam))
        return 0;
    return DefWindowProc(hWnd, message, wParam, lParam);
}
#endif

static void gfx_ctx_d3d_swap_buffers(void *data)
{
   (void)data;
   d3d_video_t *d3d = (d3d_video_t*)data;
   LPDIRECT3DDEVICE d3dr = (LPDIRECT3DDEVICE)d3d->dev;

   D3DDevice_Presents(d3d, d3dr);
}

static void gfx_ctx_d3d_update_title(void *data)
{
   d3d_video_t *d3d = (d3d_video_t*)data;
   char buf[128], buffer_fps[128];
   bool fps_draw = g_settings.fps_show;

   if (gfx_get_fps(buf, sizeof(buf), fps_draw ? buffer_fps : NULL, sizeof(buffer_fps)))
   {
      SetWindowText(d3d->hWnd, buf);
   }

   if (fps_draw)
   {
      msg_queue_push(g_extern.msg_queue, buffer_fps, 1, 1);
   }

   g_extern.frame_count++;
}

static void gfx_ctx_d3d_show_mouse(void *data, bool state)
{
   (void)data;
#ifdef HAVE_WINDOW
   if (state)
      while (ShowCursor(TRUE) < 0);
   else
      while (ShowCursor(FALSE) >= 0);
#endif
}

void d3d_make_d3dpp(void *data, const video_info_t *info, D3DPRESENT_PARAMETERS *d3dpp)
{
   d3d_video_t *d3d = (d3d_video_t*)data;
   memset(d3dpp, 0, sizeof(*d3dpp));

   d3dpp->Windowed = g_settings.video.windowed_fullscreen || !info->fullscreen;

   if (info->vsync)
   {
      switch (g_settings.video.swap_interval)
      {
         default:
         case 1: d3dpp->PresentationInterval = D3DPRESENT_INTERVAL_ONE; break;
         case 2: d3dpp->PresentationInterval = D3DPRESENT_INTERVAL_TWO; break;
         case 3: d3dpp->PresentationInterval = D3DPRESENT_INTERVAL_THREE; break;
         case 4: d3dpp->PresentationInterval = D3DPRESENT_INTERVAL_FOUR; break;
      }
   }
   else
      d3dpp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

   d3dpp->SwapEffect = D3DSWAPEFFECT_DISCARD;
   d3dpp->BackBufferCount = 2;
   d3dpp->hDeviceWindow = d3d->hWnd;
   d3dpp->BackBufferFormat = !d3dpp->Windowed ? D3DFMT_X8R8G8B8 : D3DFMT_UNKNOWN;

   if (!d3dpp->Windowed)
   {
      d3dpp->BackBufferWidth = d3d->screen_width;
      d3dpp->BackBufferHeight = d3d->screen_height;
   }
}

static void gfx_ctx_d3d_check_window(void *data, bool *quit,
   bool *resize, unsigned *width, unsigned *height, unsigned frame_count)
{
   (void)data;
   d3d_video_t *d3d = (d3d_video_t*)data;
   *quit = false;
   *resize = false;

   if (d3d_quit)
      *quit = true;
   if (d3d->should_resize)
      *resize = true;
   MSG msg;

   while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
   {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
   }
}

static bool gfx_ctx_d3d_has_focus(void *data)
{
   d3d_video_t *d3d = (d3d_video_t*)data;
   return GetFocus() == d3d->hWnd;
}

static bool gfx_ctx_d3d_bind_api(void *data, enum gfx_ctx_api api, unsigned major, unsigned minor)
{
   (void)data;
   (void)major;
   (void)minor;
   (void)api;
   /* As long as we don't have a D3D11 implementation, we default to this */
   return api == GFX_CTX_DIRECT3D9_API;
}

static bool gfx_ctx_d3d_init(void *data)
{
   (void)data;
   return true;
}

static void gfx_ctx_d3d_destroy(void *data)
{
   (void)data;
}

static void gfx_ctx_d3d_input_driver(void *data, const input_driver_t **input, void **input_data)
{
   (void)data;
   dinput = input_dinput.init();
   *input = dinput ? &input_dinput : NULL;
   *input_data = dinput;
}

static void gfx_ctx_d3d_get_video_size(void *data, unsigned *width, unsigned *height)
{
   (void)data;
}

static void gfx_ctx_d3d_swap_interval(void *data, unsigned interval)
{
   d3d_video_t *d3d = (d3d_video_t*)data;
   d3d_restore(d3d);
}

const gfx_ctx_driver_t gfx_ctx_d3d9 = {
   gfx_ctx_d3d_init,
   gfx_ctx_d3d_destroy,
   gfx_ctx_d3d_bind_api,
   gfx_ctx_d3d_swap_interval,
   NULL,
   gfx_ctx_d3d_get_video_size,
   NULL,							
   gfx_ctx_d3d_update_title,
   gfx_ctx_d3d_check_window,
   d3d_resize,
   gfx_ctx_d3d_has_focus,
   gfx_ctx_d3d_swap_buffers,
   gfx_ctx_d3d_input_driver,
   NULL,
   gfx_ctx_d3d_show_mouse,
   "d3d",
};
