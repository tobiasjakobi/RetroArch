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

#include "thread_wrapper.h"
#include "../thread.h"
#include "../general.h"
#include "../performance.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

enum thread_cmd
{
   CMD_NONE = 0,
   CMD_INIT,
   CMD_SET_SHADER,
   CMD_FREE,
   CMD_ALIVE, // Blocking alive check. Used when paused.
   CMD_SET_ROTATION,
   CMD_READ_VIEWPORT,

   CMD_POKE_SET_FILTERING,
#ifdef HAVE_FBO
   CMD_POKE_SET_FBO_STATE,
   CMD_POKE_GET_FBO_STATE,
#endif
   CMD_POKE_SET_ASPECT_RATIO,

   CMD_DUMMY = INT_MAX
};

typedef struct thread_video
{
   slock_t *lock;
   scond_t *cond_cmd;
   scond_t *cond_thread;
   sthread_t *thread;

   video_info_t info;
   const video_driver_t *driver;

   const video_poke_interface_t *poke;

   void *driver_data;
   const input_driver_t **input;
   void **input_data;

#if defined(HAVE_MENU)
   struct
   {
      void *frame;
      size_t frame_cap;
      unsigned width;
      unsigned height;
      float alpha;
      bool frame_updated;
      bool rgb32;
      bool enable;
      bool full_screen;
   } texture;
#endif
   bool apply_state_changes;

   bool alive;
   bool focus;
   bool nonblock;

   retro_time_t last_time;
   unsigned hit_count;
   unsigned miss_count;

   float *alpha_mod;
   unsigned alpha_mods;
   bool alpha_update;
   slock_t *alpha_lock;

   enum thread_cmd send_cmd;
   enum thread_cmd reply_cmd;
   union
   {
      bool b;
      int i;
      float f;
      const char *str;
      void *v;

      struct
      {
         enum rarch_shader_type type;
         const char *path;
      } set_shader;

      struct
      {
         unsigned index;
         float x, y, w, h;
      } rect;

      struct
      {
         const struct texture_image *data;
         unsigned num;
      } image;

      struct
      {
         unsigned index;
         bool smooth;
      } filtering;
   } cmd_data;

   struct rarch_viewport vp;
   struct rarch_viewport read_vp; // Last viewport reported to caller.

   struct
   {
      slock_t *lock;
      uint8_t *buffer;
      unsigned width;
      unsigned height;
      unsigned pitch;
      bool updated;
      bool within_thread;
      char msg[1024];
   } frame;

   video_driver_t video_thread;

} thread_video_t;

static void *thread_init_never_call(const video_info_t *video, const input_driver_t **input, void **input_data)
{
   (void)video;
   (void)input;
   (void)input_data;
   RARCH_ERR("Sanity check fail! Threaded mustn't be reinit.\n");
   abort();
   return NULL;
}

static void thread_reply(thread_video_t *thr, enum thread_cmd cmd)
{
   slock_lock(thr->lock);
   thr->reply_cmd = cmd;
   thr->send_cmd = CMD_NONE;
   scond_signal(thr->cond_cmd);
   slock_unlock(thr->lock);
}

static void thread_update_driver_state(thread_video_t *thr)
{
#if defined(HAVE_MENU)
   if (thr->texture.frame_updated)
   {
      thr->poke->set_texture_frame(thr->driver_data,
            thr->texture.frame, thr->texture.rgb32,
            thr->texture.width, thr->texture.height,
            thr->texture.alpha);
      thr->texture.frame_updated = false;
   }

   if (thr->poke && thr->poke->set_texture_enable)
      thr->poke->set_texture_enable(thr->driver_data, thr->texture.enable, thr->texture.full_screen);
#endif

   if (thr->apply_state_changes)
   {
      thr->poke->apply_state_changes(thr->driver_data);
      thr->apply_state_changes = false;
   }
}

static void thread_loop(void *data)
{
   thread_video_t *thr = data;
   unsigned i = 0;
   (void)i;

   for (;;)
   {
      bool updated = false;
      slock_lock(thr->lock);
      while (thr->send_cmd == CMD_NONE && !thr->frame.updated)
         scond_wait(thr->cond_thread, thr->lock);
      if (thr->frame.updated)
         updated = true;
      enum thread_cmd send_cmd = thr->send_cmd; // To avoid race condition where send_cmd is updated right after the switch is checked.
      slock_unlock(thr->lock);

      switch (send_cmd)
      {
         case CMD_INIT:
            thr->driver_data = thr->driver->init(&thr->info, thr->input, thr->input_data);
            thr->cmd_data.b = thr->driver_data;
            thr->driver->viewport_info(thr->driver_data, &thr->vp);
            thread_reply(thr, CMD_INIT);
            break;

         case CMD_FREE:
            if (thr->driver_data)
               thr->driver->free(thr->driver_data);
            thr->driver_data = NULL;
            thread_reply(thr, CMD_FREE);
            return;

         case CMD_SET_ROTATION:
            thr->driver->set_rotation(thr->driver_data, thr->cmd_data.i);
            thread_reply(thr, CMD_SET_ROTATION);
            break;

         case CMD_READ_VIEWPORT:
         {
            struct rarch_viewport vp = {0};
            thr->driver->viewport_info(thr->driver_data, &vp);
            if (memcmp(&vp, &thr->read_vp, sizeof(vp)) == 0) // We can read safely
            {
               // read_viewport() in GL driver calls rarch_render_cached_frame() to be able to read from back buffer.
               // This means frame() callback in threaded wrapper will be called from this thread, causing a timeout, and no frame to be rendered.
               // To avoid this, set a flag so wrapper can see if it's called in this "special" way.
               thr->frame.within_thread = true;
               thr->cmd_data.b = thr->driver->read_viewport(thr->driver_data, thr->cmd_data.v);
               thr->frame.within_thread = false;
               thread_reply(thr, CMD_READ_VIEWPORT);
            }
            else // Viewport dimensions changed right after main thread read the async value. Cannot read safely.
            {
               thr->cmd_data.b = false;
               thread_reply(thr, CMD_READ_VIEWPORT);
            }
            break;
         }
            
         case CMD_SET_SHADER:
         {
            bool ret = thr->driver->set_shader(thr->driver_data,
                  thr->cmd_data.set_shader.type,
                  thr->cmd_data.set_shader.path);
            thr->cmd_data.b = ret;
            thread_reply(thr, CMD_SET_SHADER);
            break;
         }

         case CMD_ALIVE:
            thr->cmd_data.b = thr->driver->alive(thr->driver_data);
            thread_reply(thr, CMD_ALIVE);
            break;

         case CMD_POKE_SET_FILTERING:
            thr->poke->set_filtering(thr->driver_data,
                  thr->cmd_data.filtering.index,
                  thr->cmd_data.filtering.smooth);
            thread_reply(thr, CMD_POKE_SET_FILTERING);
            break;

         case CMD_POKE_SET_ASPECT_RATIO:
            thr->poke->set_aspect_ratio(thr->driver_data,
                  thr->cmd_data.i);
            thread_reply(thr, CMD_POKE_SET_ASPECT_RATIO);
            break;
            
         case CMD_NONE:
            // Never reply on no command. Possible deadlock if thread sends command right after frame update.
            break;

         default:
            thread_reply(thr, send_cmd);
            break;
      }

      if (updated)
      {
         slock_lock(thr->frame.lock);

         thread_update_driver_state(thr);
         bool ret = thr->driver->frame(thr->driver_data,
               thr->frame.buffer, thr->frame.width, thr->frame.height,
               thr->frame.pitch, *thr->frame.msg ? thr->frame.msg : NULL);

         slock_unlock(thr->frame.lock);

         bool alive = ret && thr->driver->alive(thr->driver_data);
         bool focus = ret && thr->driver->focus(thr->driver_data);

         struct rarch_viewport vp = {0};
         thr->driver->viewport_info(thr->driver_data, &vp);

         slock_lock(thr->lock);
         thr->alive = alive;
         thr->focus = focus;
         thr->frame.updated = false;
         thr->vp = vp;
         scond_signal(thr->cond_cmd);
         slock_unlock(thr->lock);
      }
   }
}

static void thread_send_cmd(thread_video_t *thr, enum thread_cmd cmd)
{
   slock_lock(thr->lock);
   thr->send_cmd = cmd;
   thr->reply_cmd = CMD_NONE;
   scond_signal(thr->cond_thread);
   slock_unlock(thr->lock);
}

static void thread_wait_reply(thread_video_t *thr, enum thread_cmd cmd)
{
   slock_lock(thr->lock);
   while (cmd != thr->reply_cmd)
      scond_wait(thr->cond_cmd, thr->lock);
   slock_unlock(thr->lock);
}

static bool thread_alive(void *data)
{
   thread_video_t *thr = data;
   if (g_extern.is_paused)
   {
      thread_send_cmd(thr, CMD_ALIVE);
      thread_wait_reply(thr, CMD_ALIVE);
      return thr->cmd_data.b;
   }
   else
   {
      slock_lock(thr->lock);
      bool ret = thr->alive;
      slock_unlock(thr->lock);
      return ret;
   }
}

static bool thread_focus(void *data)
{
   thread_video_t *thr = data;
   slock_lock(thr->lock);
   bool ret = thr->focus;
   slock_unlock(thr->lock);
   return ret;
}

static bool thread_frame(void *data, const void *frame_,
      unsigned width, unsigned height, unsigned pitch, const char *msg)
{
   thread_video_t *thr = data;

   // If called from within read_viewport, we're actually in the driver thread, so just render directly.
   if (thr->frame.within_thread)
   {
      thread_update_driver_state(thr);
      return thr->driver->frame(thr->driver_data, frame_, width, height, pitch, msg);
   }

   RARCH_PERFORMANCE_INIT(thread_frame);
   RARCH_PERFORMANCE_START(thread_frame);

   unsigned copy_stride = width * (thr->info.rgb32 ? sizeof(uint32_t) : sizeof(uint16_t));

   const uint8_t *src = frame_;
   uint8_t *dst = thr->frame.buffer;

   slock_lock(thr->lock);

   // scond_wait_timeout cannot be implemented on consoles.
#ifndef RARCH_CONSOLE
   if (!thr->nonblock)
   {
      retro_time_t target_frame_time = (retro_time_t)roundf(1000000LL / g_settings.video.refresh_rate);
      retro_time_t target = thr->last_time + target_frame_time;
      // Ideally, use absolute time, but that is only a good idea on POSIX.
      while (thr->frame.updated)
      {
         retro_time_t current = rarch_get_time_usec();
         retro_time_t delta = target - current;

         if (delta <= 0)
            break;

         if (!scond_wait_timeout(thr->cond_cmd, thr->lock, delta))
            break;
      }
   }
#endif

   // Drop frame if updated flag is still set, as thread is still working on last frame.
   if (!thr->frame.updated)
   {
      if (src)
      {
         unsigned h;
         for (h = 0; h < height; h++, src += pitch, dst += copy_stride)
            memcpy(dst, src, copy_stride);
      }

      thr->frame.updated = true;
      thr->frame.width  = width;
      thr->frame.height = height;
      thr->frame.pitch  = copy_stride;

      if (msg)
         strlcpy(thr->frame.msg, msg, sizeof(thr->frame.msg));
      else
         *thr->frame.msg = '\0';

      scond_signal(thr->cond_thread);

#if defined(HAVE_MENU)
      if (thr->texture.enable)
      {
         while (thr->frame.updated)
            scond_wait(thr->cond_cmd, thr->lock);
      }
#endif
      thr->hit_count++;
   }
   else
      thr->miss_count++;

   slock_unlock(thr->lock);

   RARCH_PERFORMANCE_STOP(thread_frame);

   thr->last_time = rarch_get_time_usec();
   return true;
}

static void thread_set_nonblock_state(void *data, bool state)
{
   thread_video_t *thr = data;
   thr->nonblock = state;
}

static bool thread_init(thread_video_t *thr, const video_info_t *info, const input_driver_t **input,
      void **input_data)
{
   thr->lock = slock_new();
   thr->alpha_lock = slock_new();
   thr->frame.lock = slock_new();
   thr->cond_cmd = scond_new();
   thr->cond_thread = scond_new();
   thr->input = input;
   thr->input_data = input_data;
   thr->info = *info;
   thr->alive = true;
   thr->focus = true;

   size_t max_size = info->input_scale * RARCH_SCALE_BASE;
   max_size *= max_size;
   max_size *= info->rgb32 ? sizeof(uint32_t) : sizeof(uint16_t);
   thr->frame.buffer = malloc(max_size);
   if (!thr->frame.buffer)
      return false;

   memset(thr->frame.buffer, 0x80, max_size);

   thr->last_time = rarch_get_time_usec();

   thr->thread = sthread_create(thread_loop, thr);
   if (!thr->thread)
      return false;
   thread_send_cmd(thr, CMD_INIT);
   thread_wait_reply(thr, CMD_INIT);

   return thr->cmd_data.b;
}

static bool thread_set_shader(void *data, enum rarch_shader_type type, const char *path)
{
   thread_video_t *thr = data;
   thr->cmd_data.set_shader.type = type;
   thr->cmd_data.set_shader.path = path;
   thread_send_cmd(thr, CMD_SET_SHADER);
   thread_wait_reply(thr, CMD_SET_SHADER);
   return thr->cmd_data.b;
}

static void thread_set_rotation(void *data, unsigned rotation)
{
   thread_video_t *thr = data;
   thr->cmd_data.i = rotation;
   thread_send_cmd(thr, CMD_SET_ROTATION);
   thread_wait_reply(thr, CMD_SET_ROTATION);
}

// This value is set async as stalling on the video driver for every query is too slow.
// This means this value might not be correct, so viewport reads are not supported for now.
static void thread_viewport_info(void *data, struct rarch_viewport *vp)
{
   thread_video_t *thr = data;
   slock_lock(thr->lock);
   *vp = thr->vp;

   // Explicitly mem-copied so we can use memcmp correctly later.
   memcpy(&thr->read_vp, &thr->vp, sizeof(thr->vp));
   slock_unlock(thr->lock);
}

static bool thread_read_viewport(void *data, uint8_t *buffer)
{
   thread_video_t *thr = data;
   thr->cmd_data.v = buffer;
   thread_send_cmd(thr, CMD_READ_VIEWPORT);
   thread_wait_reply(thr, CMD_READ_VIEWPORT);
   return thr->cmd_data.b;
}

static void thread_free(void *data)
{
   thread_video_t *thr = data;
   if (!thr)
      return;

   thread_send_cmd(thr, CMD_FREE);
   thread_wait_reply(thr, CMD_FREE);
   sthread_join(thr->thread);

#if defined(HAVE_MENU)
   free(thr->texture.frame);
#endif
   free(thr->frame.buffer);
   slock_free(thr->frame.lock);
   slock_free(thr->lock);
   scond_free(thr->cond_cmd);
   scond_free(thr->cond_thread);

   free(thr->alpha_mod);
   slock_free(thr->alpha_lock);

   RARCH_LOG("Threaded video stats: Frames pushed: %u, Frames dropped: %u.\n",
         thr->hit_count, thr->miss_count);

   free(thr);
}

static void thread_set_filtering(void *data, unsigned index, bool smooth)
{
   thread_video_t *thr = data;
   thr->cmd_data.filtering.index = index;
   thr->cmd_data.filtering.smooth = smooth;
   thread_send_cmd(thr, CMD_POKE_SET_FILTERING);
   thread_wait_reply(thr, CMD_POKE_SET_FILTERING);
}

static void thread_set_aspect_ratio(void *data, unsigned aspectratio_index)
{
   thread_video_t *thr = data;
   thr->cmd_data.i = aspectratio_index;
   thread_send_cmd(thr, CMD_POKE_SET_ASPECT_RATIO);
   thread_wait_reply(thr, CMD_POKE_SET_ASPECT_RATIO);
}

#if defined(HAVE_MENU)
static void thread_set_texture_frame(void *data, const void *frame,
      bool rgb32, unsigned width, unsigned height, float alpha)
{
   thread_video_t *thr = data;

   slock_lock(thr->frame.lock);
   size_t required = width * height * (rgb32 ? sizeof(uint32_t) : sizeof(uint16_t));
   if (required > thr->texture.frame_cap)
   {
      thr->texture.frame = realloc(thr->texture.frame, required);
      thr->texture.frame_cap = required;
   }

   if (thr->texture.frame)
   {
      memcpy(thr->texture.frame, frame, required);
      thr->texture.frame_updated = true;
      thr->texture.rgb32  = rgb32;
      thr->texture.width  = width;
      thr->texture.height = height;
      thr->texture.alpha  = alpha;
   }
   slock_unlock(thr->frame.lock);
}

static void thread_set_texture_enable(void *data, bool state, bool full_screen)
{
   thread_video_t *thr = data;

   slock_lock(thr->frame.lock);
   thr->texture.enable = state;
   thr->texture.full_screen = full_screen;
   slock_unlock(thr->frame.lock);
}
#endif

static void thread_apply_state_changes(void *data)
{
   thread_video_t *thr = data;
   slock_lock(thr->frame.lock);
   thr->apply_state_changes = true;
   slock_unlock(thr->frame.lock);
}

// This is read-only state which should not have any kind of race condition.
static struct gfx_shader *thread_get_current_shader(void *data)
{
   thread_video_t *thr = data;
   return thr->poke ? thr->poke->get_current_shader(thr->driver_data) : NULL;
}

static const video_poke_interface_t thread_poke = {
  .set_filtering = thread_set_filtering,

  .set_aspect_ratio = thread_set_aspect_ratio,
  .apply_state_changes = thread_apply_state_changes,
#if defined(HAVE_MENU)
  .set_texture_frame = thread_set_texture_frame,
  .set_texture_enable = thread_set_texture_enable,
#endif

  .get_current_shader = thread_get_current_shader,
};

static void thread_get_poke_interface(void *data, const video_poke_interface_t **iface)
{
   thread_video_t *thr = data;

   if (thr->driver->poke_interface)
   {
      *iface = &thread_poke;
      thr->driver->poke_interface(thr->driver_data, &thr->poke);
   }
   else
      *iface = NULL;
}

static const video_driver_t video_thread = {
   .init = thread_init_never_call, // Should never be called directly.
   .frame = thread_frame,
   .set_nonblock_state = thread_set_nonblock_state,
   .alive = thread_alive,
   .focus = thread_focus,
   .set_shader = thread_set_shader,
   .free = thread_free,
   .ident = "Thread wrapper",
   .set_rotation = thread_set_rotation,
   .viewport_info = thread_viewport_info,
   .read_viewport = thread_read_viewport,
   .poke_interface = thread_get_poke_interface,
};

static void thread_set_callbacks(thread_video_t *thr, const video_driver_t *driver)
{
   thr->video_thread = video_thread;
   // Disable optional features if not present.
   if (!driver->read_viewport)
      thr->video_thread.read_viewport = NULL;
   if (!driver->set_rotation)
      thr->video_thread.set_rotation = NULL;
   if (!driver->set_shader)
      thr->video_thread.set_shader = NULL;

   // Might have to optionally disable poke_interface features as well.
   if (!thr->video_thread.poke_interface)
      thr->video_thread.poke_interface = NULL;
}

bool rarch_threaded_video_init(const video_driver_t **out_driver, void **out_data,
      const input_driver_t **input, void **input_data,
      const video_driver_t *driver, const video_info_t *info)
{
   thread_video_t *thr = calloc(1, sizeof(*thr));
   if (!thr)
      return false;

   thread_set_callbacks(thr, driver);

   thr->driver = driver;
   *out_driver = &thr->video_thread;
   *out_data   = thr;
   return thread_init(thr, info, input, input_data);
}
