/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2014 - Daniel De Matteis
 *  Copyright (C) 2012-2014 - Michael Lelli
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

#ifndef DYNAMIC_DUMMY_H__
#define DYNAMIC_DUMMY_H__

#include "libretro.h"

void libretro_dummy_retro_init();
void libretro_dummy_retro_deinit();
unsigned libretro_dummy_retro_api_version();
void libretro_dummy_retro_get_system_info(struct retro_system_info *info);
void libretro_dummy_retro_get_system_av_info(struct retro_system_av_info *info);

void libretro_dummy_retro_set_environment(retro_environment_t cb);
void libretro_dummy_retro_set_video_refresh(retro_video_refresh_t cb);
void libretro_dummy_retro_set_audio_sample(retro_audio_sample_t cb);
void libretro_dummy_retro_set_audio_sample_batch(retro_audio_sample_batch_t cb);
void libretro_dummy_retro_set_input_poll(retro_input_poll_t cb);
void libretro_dummy_retro_set_input_state(retro_input_state_t cb);

void libretro_dummy_retro_set_controller_port_device(unsigned port, unsigned device);

void libretro_dummy_retro_reset();
void libretro_dummy_retro_run();

size_t libretro_dummy_retro_serialize_size();
bool libretro_dummy_retro_serialize(void *data, size_t size);
bool libretro_dummy_retro_unserialize(const void *data, size_t size);

void libretro_dummy_retro_cheat_reset();
void libretro_dummy_retro_cheat_set(unsigned index, bool enabled, const char *code);

bool libretro_dummy_retro_load_game(const struct retro_game_info *game);
bool libretro_dummy_retro_load_game_special(unsigned game_type,
      const struct retro_game_info *info, size_t num_info);

void libretro_dummy_retro_unload_game();
unsigned libretro_dummy_retro_get_region();
void *libretro_dummy_retro_get_memory_data(unsigned id);
size_t libretro_dummy_retro_get_memory_size(unsigned id);

#endif

