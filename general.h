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

#ifndef __RARCH_GENERAL_H
#define __RARCH_GENERAL_H

#include <stdio.h>
#include <limits.h>
#include <setjmp.h>
#include "driver.h"
#include "record/ffemu.h"
#include "message_queue.h"
#include "rewind.h"
#include "movie.h"
#include "autosave.h"
#include "dynamic.h"
#include "cheats.h"
#include "audio/dsp_filter.h"
#include "compat/strl.h"
#include "core_options.h"
#include "miscellaneous.h"
#include "gfx/filter.h"

#include "history.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "1.0.0.2"
#endif

#ifdef HAVE_NETPLAY
#include "netplay.h"
#endif

#include "command.h"
#include "audio/resampler.h"

#define MAX_PLAYERS 8

enum basic_event
{
   RARCH_CMD_RESET = 1,
   RARCH_CMD_LOAD_CONTENT,
   RARCH_CMD_LOAD_CORE,
   RARCH_CMD_LOAD_STATE,
   RARCH_CMD_SAVE_STATE,
   RARCH_CMD_TAKE_SCREENSHOT,
   RARCH_CMD_PREPARE_DUMMY,
   RARCH_CMD_QUIT,
   RARCH_CMD_REINIT,
   RARCH_CMD_REWIND,
   RARCH_CMD_AUTOSAVE,
   RARCH_CMD_AUDIO_STOP,
   RARCH_CMD_AUDIO_START,
   RARCH_CMD_DSP_FILTER_INIT,
   RARCH_CMD_DSP_FILTER_DEINIT,
   RARCH_CMD_RECORD_INIT,
   RARCH_CMD_RECORD_DEINIT,
   RARCH_CMD_HISTORY_DEINIT,
   RARCH_CMD_HISTORY_INIT,
};

enum menu_enums
{
   MODE_GAME = 0,
   MODE_LOAD_GAME,
   MODE_MENU,
   MODE_MENU_PREINIT,
   MODE_CLEAR_INPUT,
};

enum sound_mode_enums
{
   SOUND_MODE_NORMAL = 0,
   SOUND_MODE_LAST
};

struct defaults
{
   char menu_config_dir[PATH_MAX];
   char config_path[PATH_MAX];
   char core_path[PATH_MAX];
   char autoconfig_dir[PATH_MAX];
   char audio_filter_dir[PATH_MAX];
   char assets_dir[PATH_MAX];
   char core_dir[PATH_MAX];
   char core_info_dir[PATH_MAX];
   char port_dir[PATH_MAX];
   char shader_dir[PATH_MAX];
   char savestate_dir[PATH_MAX];
   char sram_dir[PATH_MAX];
   char screenshot_dir[PATH_MAX];
   char system_dir[PATH_MAX];

   struct
   {
      int out_latency;
      float video_refresh_rate;
      bool video_threaded_enable;
   } settings; 
};

// All config related settings go here.
struct settings
{
   struct 
   {
      char driver[32];
      char gl_context[32];
      float scale;
      bool fullscreen;
      bool windowed_fullscreen;
      unsigned monitor_index;
      unsigned fullscreen_x;
      unsigned fullscreen_y;
      bool vsync;
      bool hard_sync;
      bool black_frame_insertion;
      unsigned swap_interval;
      unsigned hard_sync_frames;
      bool smooth;
      bool force_aspect;
      bool crop_overscan;
      float aspect_ratio;
      bool aspect_ratio_auto;
      bool scale_integer;
      unsigned aspect_ratio_idx;
      unsigned rotation;

      char shader_path[PATH_MAX];
      bool shader_enable;

      char filter_path[PATH_MAX];
      float refresh_rate;
      bool threaded;

      char filter_dir[PATH_MAX];
      char shader_dir[PATH_MAX];

      char font_path[PATH_MAX];
      float font_size;
      bool font_enable;
      float msg_pos_x;
      float msg_pos_y;
      float msg_color_r;
      float msg_color_g;
      float msg_color_b;

      bool post_filter_record;
      bool gpu_record;
      bool gpu_screenshot;

      bool allow_rotate;
      bool shared_context;
   } video;

#ifdef HAVE_MENU
   struct 
   {
      char driver[32];
   } menu;
#endif

   struct
   {
      char driver[32];
      bool enable;
      unsigned out_rate;
      unsigned block_frames;
      char device[PATH_MAX];
      unsigned latency;
      bool sync;

      char dsp_plugin[PATH_MAX];
      char filter_dir[PATH_MAX];

      bool rate_control;
      float rate_control_delta;
      float volume; // dB scale
      char resampler[32];
   } audio;

   struct
   {
      char driver[32];
      char joypad_driver[32];
      char keyboard_layout[64];
      struct retro_keybind binds[MAX_PLAYERS][RARCH_BIND_LIST_END];

      // Set by autoconfiguration in joypad_autoconfig_dir. Does not override main binds.
      struct retro_keybind autoconf_binds[MAX_PLAYERS][RARCH_BIND_LIST_END];
      bool autoconfigured[MAX_PLAYERS];

      unsigned libretro_device[MAX_PLAYERS];
      unsigned analog_dpad_mode[MAX_PLAYERS];

      float axis_threshold;
      int joypad_map[MAX_PLAYERS];
      unsigned device[MAX_PLAYERS];
      char device_names[MAX_PLAYERS][64];
      bool autodetect_enable;
      bool netplay_client_swap_input;

      unsigned turbo_period;
      unsigned turbo_duty_cycle;

      char autoconfig_dir[PATH_MAX];
   } input;

   int state_slot;

   char core_options_path[PATH_MAX];
   char content_history_path[PATH_MAX];
   unsigned content_history_size;

   char libretro[PATH_MAX];
   char libretro_directory[PATH_MAX];
   unsigned libretro_log_level;
   char libretro_info_path[PATH_MAX];
   char cheat_database[PATH_MAX];
   char cheat_settings_path[PATH_MAX];

   char screenshot_directory[PATH_MAX];
   char system_directory[PATH_MAX];

   char extraction_directory[PATH_MAX];

   bool rewind_enable;
   size_t rewind_buffer_size;
   unsigned rewind_granularity;

   float slowmotion_ratio;
   float fastforward_ratio;

   bool pause_nonactive;
   unsigned autosave_interval;

   bool block_sram_overwrite;
   bool savestate_auto_index;
   bool savestate_auto_save;
   bool savestate_auto_load;

   bool network_cmd_enable;
   uint16_t network_cmd_port;
   bool pipe_cmd_enable;
   char pipe_cmd_name[PATH_MAX];

   char content_directory[PATH_MAX];
   char assets_directory[PATH_MAX];
#if defined(HAVE_MENU)
   char menu_content_directory[PATH_MAX];
   char menu_config_directory[PATH_MAX];
   bool menu_show_start_screen;
#endif
   bool fps_show;
   bool load_dummy_on_core_shutdown;

   bool core_specific_config;

   char username[32];
   unsigned int user_language;

   bool config_save_on_exit;
};

typedef struct rarch_resolution
{
   unsigned idx;
   unsigned id;
} rarch_resolution_t;

typedef struct rarch_viewport
{
   int x;
   int y;
   unsigned width;
   unsigned height;
   unsigned full_width;
   unsigned full_height;
} rarch_viewport_t;

// All run-time- / command line flag-related globals go here.
struct global
{
   bool verbosity;
   bool perfcnt_enable;
   bool audio_active;
   bool video_active;
   bool force_fullscreen;

   struct string_list *temporary_content;

   content_history_t *history;

   uint32_t content_crc;

   char gb_rom_path[PATH_MAX];
   char bsx_rom_path[PATH_MAX];
   char sufami_rom_path[2][PATH_MAX];
   bool has_set_save_path;
   bool has_set_state_path;
   bool has_set_libretro_device[MAX_PLAYERS];
   bool has_set_libretro;
   bool has_set_libretro_directory;
   bool has_set_verbosity;

   bool has_set_netplay_mode;
   bool has_set_username;
   bool has_set_netplay_ip_address;
   bool has_set_netplay_delay_frames;
   bool has_set_netplay_ip_port;

#ifdef HAVE_RMENU
   char menu_texture_path[PATH_MAX];
#endif

   // Config associated with global "default" config.
   char config_path[PATH_MAX];
   char append_config_path[PATH_MAX];
   char input_config_path[PATH_MAX];

#ifdef HAVE_FILE_LOGGER
   char default_log_file[PATH_MAX];
#endif
   
   char basename[PATH_MAX];
   char fullpath[PATH_MAX];

   // A list of save types and associated paths for all content.
   struct string_list *savefiles;

   // For --subsystem content.
   char subsystem[256];
   struct string_list *subsystem_fullpaths;

   char savefile_name[PATH_MAX];
   char savestate_name[PATH_MAX];

   // Used on reentrancy to use a savestate dir.
   char savefile_dir[PATH_MAX];
   char savestate_dir[PATH_MAX];

   bool block_patch;
   bool ups_pref;
   bool bps_pref;
   bool ips_pref;
   char ups_name[PATH_MAX];
   char bps_name[PATH_MAX];
   char ips_name[PATH_MAX];


   struct
   {
      retro_time_t minimum_frame_time;
      retro_time_t last_frame_time;
   } frame_limit;

   struct
   {
      struct retro_system_info info;
      struct retro_system_av_info av_info;
      float aspect_ratio;

      unsigned rotation;
      bool shutdown;
      unsigned performance_level;
      enum retro_pixel_format pix_fmt;

      bool block_extract;
      bool force_nonblock;
      bool no_content;

      const char *input_desc_btn[MAX_PLAYERS][RARCH_FIRST_CUSTOM_BIND];
      char valid_extensions[PATH_MAX];
      
      retro_keyboard_event_t key_event;

      struct retro_audio_callback audio_callback;

      struct retro_disk_control_callback disk_control; 
      struct retro_hw_render_callback hw_render_callback;

      struct retro_frame_time_callback frame_time;
      retro_usec_t frame_time_last;

      core_option_manager_t *core_options;

      struct retro_subsystem_info *special;
      unsigned num_special;

      struct retro_controller_info *ports;
      unsigned num_ports;
   } system;

   struct
   {
      void *resampler_data;
      const rarch_resampler_t *resampler;

      float *data;

      size_t data_ptr;
      size_t chunk_size;
      size_t nonblock_chunk_size;
      size_t block_chunk_size;

      double src_ratio;
      float in_rate;

      bool use_float;
      bool mute;

      float *outsamples;
      int16_t *conv_outsamples;

      int16_t *rewind_buf;
      size_t rewind_ptr;
      size_t rewind_size;

      rarch_dsp_filter_t *dsp;

      bool rate_control; 
      double orig_src_ratio;
      size_t driver_buffer_size;

      float volume_db;
      float volume_gain;
   } audio_data;

   struct
   {
#define AUDIO_BUFFER_FREE_SAMPLES_COUNT (8 * 1024)
      unsigned buffer_free_samples[AUDIO_BUFFER_FREE_SAMPLES_COUNT];
      uint64_t buffer_free_samples_count;

#define MEASURE_FRAME_TIME_SAMPLES_COUNT (2 * 1024)
      retro_time_t frame_time_samples[MEASURE_FRAME_TIME_SAMPLES_COUNT];
      uint64_t frame_time_samples_count;
   } measure_data;

   struct
   {
      rarch_softfilter_t *filter;

      void *buffer;
      unsigned scale;
      unsigned out_bpp;
      bool out_rgb32;
   } filter;

   msg_queue_t *msg_queue;

   bool exec;

   // Rewind support.
   state_manager_t *state_manager;
   size_t state_size;
   bool frame_is_reverse;

   // Movie playback/recording support.
   struct
   {
      bsv_movie_t *movie;
      char movie_path[PATH_MAX];
      bool movie_playback;

      // Immediate playback/recording.
      char movie_start_path[PATH_MAX];
      bool movie_start_recording;
      bool movie_start_playback;
      bool movie_end;
   } bsv;

   bool sram_load_disable;
   bool sram_save_disable;
   bool use_sram;

   // Pausing support
   bool is_paused;
   bool is_oneshot;
   bool is_slowmotion;

   // Turbo support
   bool turbo_frame_enable[MAX_PLAYERS];
   uint16_t turbo_enable[MAX_PLAYERS];
   unsigned turbo_count;

   // Autosave support.
   autosave_t **autosave;
   unsigned num_autosave;

   // Netplay.
#ifdef HAVE_NETPLAY
   netplay_t *netplay;
   char netplay_server[PATH_MAX];
   bool netplay_enable;
   bool netplay_is_client;
   bool netplay_is_spectate;
   unsigned netplay_sync_frames;
   uint16_t netplay_port;
#endif

   // Recording.
   const ffemu_backend_t *rec_driver;
   void *rec;

   char record_path[PATH_MAX];
   char record_config[PATH_MAX];
   bool recording_enable;
   unsigned record_width;
   unsigned record_height;

   uint8_t *record_gpu_buffer;
   size_t record_gpu_width;
   size_t record_gpu_height;

   struct
   {
      const void *data;
      unsigned width;
      unsigned height;
      size_t pitch;
   } frame_cache;

   unsigned frame_count;
   char title_buf[64];

   struct
   {
      struct string_list *list;
      size_t ptr;
   } shader_dir;

   struct
   {
      struct string_list *list;
      size_t ptr;
   } filter_dir;

   char sha256[64 + 1];

   cheat_manager_t *cheat;

   bool block_config_read;

   // Settings and/or global state that is specific to a console-style implementation.
   struct
   {
      struct
      {
         struct
         {
            rarch_resolution_t current;
            rarch_resolution_t initial;
            uint32_t *list;
            unsigned count;
            bool check;
         } resolutions;

         struct
         {
            rarch_viewport_t custom_vp;
         } viewports;
      } screen;

      struct
      {
         unsigned mode;
      } sound;
   } console;

   uint64_t lifecycle_state;

   // If this is non-NULL. RARCH_LOG and friends will write to this file.
   FILE *log_file;

   bool main_is_init;
   bool error_in_init;
   char error_string[1024];
   jmp_buf error_sjlj_context;

   bool libretro_no_content;
   bool libretro_dummy;

   // Config file associated with per-core configs.
   char core_specific_config_path[PATH_MAX];
};

struct rarch_main_wrap
{
   const char *content_path;
   const char *sram_path;
   const char *state_path;
   const char *config_path;
   const char *libretro_path;
   bool verbose;
   bool no_content;

   bool touched;
};

// Public data structures
extern struct settings g_settings;
extern struct global g_extern;
extern struct defaults g_defaults;
/////////

// Public functions
void config_load();
void config_set_defaults();
const char *config_get_default_video();
const char *config_get_default_audio();
const char *config_get_default_audio_resampler();
const char *config_get_default_input();

#include "conf/config_file.h"
bool config_load_file(const char *path, bool set_defaults);
bool config_save_file(const char *path);
bool config_read_keybinds(const char *path);

void rarch_main_clear_state();
void rarch_init_system_info();
int rarch_main(int argc, char *argv[]);

#ifndef MAX_ARGS
#define MAX_ARGS 32
#endif

void rarch_main_init_wrap(const struct rarch_main_wrap *args, int *argc, char **argv);

int rarch_main_init(int argc, char *argv[]);
void rarch_main_command(unsigned action);
bool rarch_main_iterate();
void rarch_main_deinit();
void rarch_render_cached_frame();
void rarch_deinit_msg_queue();
void rarch_input_poll();
void rarch_check_block_hotkey();
bool rarch_check_fullscreen();
void rarch_disk_control_set_eject(bool state, bool log);
void rarch_disk_control_set_index(unsigned index);
void rarch_disk_control_append_image(const char *path);
bool rarch_set_rumble_state(unsigned port, enum retro_rumble_effect effect, bool enable);

/////////

static inline float db_to_gain(float db)
{
   return powf(10.0f, db / 20.0f);
}

static inline void rarch_fail(int error_code, const char *error)
{
   // We cannot longjmp unless we're in rarch_main_init().
   // If not, something went very wrong, and we should just exit right away.
   rarch_assert(g_extern.error_in_init);

   strlcpy(g_extern.error_string, error, sizeof(g_extern.error_string));
   longjmp(g_extern.error_sjlj_context, error_code);
}

#endif


