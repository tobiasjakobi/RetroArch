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


#ifndef __DRIVER__H
#define __DRIVER__H


#include <stdbool.h>
#include <sys/types.h>
#include "libretro_private.h"
#include <stdlib.h>
#include <stdint.h>
#include "gfx/scaler/scaler.h"
#include "gfx/image/image.h"
#include "gfx/filters/softfilter.h"
#include "gfx/shader_parse.h"
#include "audio/dsp_filter.h"
#include "miscellaneous.h"

#include "driver_menu.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "command.h"

#define AUDIO_CHUNK_SIZE_BLOCKING 512
#define AUDIO_CHUNK_SIZE_NONBLOCKING 2048 // So we don't get complete line-noise when fast-forwarding audio.
#define AUDIO_MAX_RATIO 16

// Specialized _POINTER that targets the full screen regardless of viewport.
// Should not be used by a libretro implementation as coordinates returned make no sense.
// It is only used internally for overlays.
#define RARCH_DEVICE_POINTER_SCREEN (RETRO_DEVICE_POINTER | 0x10000)

// libretro has 16 buttons from 0-15 (libretro.h)
// Analog binds use RETRO_DEVICE_ANALOG, but we follow the same scheme internally
// in RetroArch for simplicity,
// so they are mapped into [16, 23].
#define RARCH_FIRST_CUSTOM_BIND 16
#define RARCH_FIRST_META_KEY RARCH_CUSTOM_BIND_LIST_END
enum // RetroArch specific bind IDs.
{
   // Custom binds that extend the scope of RETRO_DEVICE_JOYPAD for RetroArch specifically.
   // Analogs (RETRO_DEVICE_ANALOG)
   RARCH_ANALOG_LEFT_X_PLUS = RARCH_FIRST_CUSTOM_BIND,
   RARCH_ANALOG_LEFT_X_MINUS,
   RARCH_ANALOG_LEFT_Y_PLUS,
   RARCH_ANALOG_LEFT_Y_MINUS,
   RARCH_ANALOG_RIGHT_X_PLUS,
   RARCH_ANALOG_RIGHT_X_MINUS,
   RARCH_ANALOG_RIGHT_Y_PLUS,
   RARCH_ANALOG_RIGHT_Y_MINUS,

   // Turbo
   RARCH_TURBO_ENABLE,

   RARCH_CUSTOM_BIND_LIST_END,

   // Command binds. Not related to game input, only usable for port 0.
   RARCH_FAST_FORWARD_KEY = RARCH_FIRST_META_KEY,
   RARCH_FAST_FORWARD_HOLD_KEY,
   RARCH_LOAD_STATE_KEY,
   RARCH_SAVE_STATE_KEY,
   RARCH_FULLSCREEN_TOGGLE_KEY,
   RARCH_QUIT_KEY,
   RARCH_STATE_SLOT_PLUS,
   RARCH_STATE_SLOT_MINUS,
   RARCH_REWIND,
   RARCH_MOVIE_RECORD_TOGGLE,
   RARCH_PAUSE_TOGGLE,
   RARCH_FRAMEADVANCE,
   RARCH_RESET,
   RARCH_SHADER_NEXT,
   RARCH_SHADER_PREV,
   RARCH_CHEAT_INDEX_PLUS,
   RARCH_CHEAT_INDEX_MINUS,
   RARCH_CHEAT_TOGGLE,
   RARCH_SCREENSHOT,
   RARCH_MUTE,
   RARCH_NETPLAY_FLIP,
   RARCH_SLOWMOTION,
   RARCH_ENABLE_HOTKEY,
   RARCH_VOLUME_UP,
   RARCH_VOLUME_DOWN,
   RARCH_DISK_EJECT_TOGGLE,
   RARCH_DISK_NEXT,
   RARCH_GRAB_MOUSE_TOGGLE,

   RARCH_MENU_TOGGLE,

   RARCH_BIND_LIST_END,
   RARCH_BIND_LIST_END_NULL
};

struct retro_keybind
{
   bool valid;
   unsigned id;
   const char *desc;
   enum retro_key key;

   // PC only uses lower 16-bits.
   // Full 64-bit can be used for port-specific purposes, like simplifying multiple binds, etc.
   uint64_t joykey;

   // Default key binding value - for resetting bind to default
   uint64_t def_joykey;

   uint32_t joyaxis;
   uint32_t def_joyaxis;

   uint32_t orig_joyaxis; // Used by input_{push,pop}_analog_dpad().
};

typedef struct retro_keybind* retro_keybind_ptr;

struct platform_bind
{
   uint64_t joykey;
   char desc[64];
};

#if defined(HAVE_OPENGLES2)
#define DEFAULT_SHADER_TYPE RARCH_SHADER_GLSL
#else
#define DEFAULT_SHADER_TYPE RARCH_SHADER_NONE
#endif

typedef struct video_info
{
   unsigned width;
   unsigned height;
   bool fullscreen;
   bool vsync;
   bool force_aspect;
   bool smooth;
   unsigned input_scale; // Maximum input size: RARCH_SCALE_BASE * input_scale
   bool rgb32; // Use 32-bit RGBA rather than native XBGR1555.
} video_info_t;

typedef struct audio_driver
{
   void *(*init)(const char *device, unsigned rate, unsigned latency);
   ssize_t (*write)(void *data, const void *buf, size_t size);
   bool (*stop)(void *data);
   bool (*start)(void *data);
   void (*set_nonblock_state)(void *data, bool toggle); // Should we care about blocking in audio thread? Fast forwarding.
   void (*free)(void *data);
   bool (*use_float)(void *data); // Defines if driver will take standard floating point samples, or int16_t samples.
   const char *ident;

   size_t (*write_avail)(void *data); // Optional
   size_t (*buffer_size)(void *data); // Optional
} audio_driver_t;

#define AXIS_NEG(x) (((uint32_t)(x) << 16) | UINT16_C(0xFFFF))
#define AXIS_POS(x) ((uint32_t)(x) | UINT32_C(0xFFFF0000))
#define AXIS_NONE UINT32_C(0xFFFFFFFF)
#define AXIS_DIR_NONE UINT16_C(0xFFFF)

#define AXIS_NEG_GET(x) (((uint32_t)(x) >> 16) & UINT16_C(0xFFFF))
#define AXIS_POS_GET(x) ((uint32_t)(x) & UINT16_C(0xFFFF))

#define NO_BTN UINT16_C(0xFFFF) // I hope no joypad will ever have this many buttons ... ;)

#define HAT_UP_SHIFT 15
#define HAT_DOWN_SHIFT 14
#define HAT_LEFT_SHIFT 13
#define HAT_RIGHT_SHIFT 12
#define HAT_UP_MASK (1 << HAT_UP_SHIFT)
#define HAT_DOWN_MASK (1 << HAT_DOWN_SHIFT)
#define HAT_LEFT_MASK (1 << HAT_LEFT_SHIFT)
#define HAT_RIGHT_MASK (1 << HAT_RIGHT_SHIFT)
#define HAT_MAP(x, hat) ((x & ((1 << 12) - 1)) | hat)

#define HAT_MASK (HAT_UP_MASK | HAT_DOWN_MASK | HAT_LEFT_MASK | HAT_RIGHT_MASK)
#define GET_HAT_DIR(x) (x & HAT_MASK)
#define GET_HAT(x) (x & (~HAT_MASK))

enum analog_dpad_mode
{
   ANALOG_DPAD_NONE = 0,
   ANALOG_DPAD_LSTICK,
   ANALOG_DPAD_RSTICK,
   ANALOG_DPAD_DUALANALOG,
   ANALOG_DPAD_LAST
};

enum keybind_set_id
{
   KEYBINDS_ACTION_NONE = 0,
   KEYBINDS_ACTION_SET_DEFAULT_BIND,
   KEYBINDS_ACTION_SET_DEFAULT_BINDS,
   KEYBINDS_ACTION_SET_ANALOG_DPAD_NONE,
   KEYBINDS_ACTION_SET_ANALOG_DPAD_LSTICK,
   KEYBINDS_ACTION_SET_ANALOG_DPAD_RSTICK,
   KEYBINDS_ACTION_GET_BIND_LABEL,
   KEYBINDS_ACTION_LAST
};

typedef struct rarch_joypad_driver rarch_joypad_driver_t;

typedef struct input_driver
{
   void *(*init)();
   void (*poll)(void *data);
   int16_t (*input_state)(void *data, const retro_keybind_ptr *retro_keybinds,
         unsigned port, unsigned device, unsigned index, unsigned id);
   bool (*key_pressed)(void *data, int key);
   void (*free)(void *data);
   void (*set_keybinds)(void *data, unsigned device, unsigned port, unsigned id, unsigned keybind_action);
   uint64_t (*get_capabilities)(void *data);
   unsigned (*devices_size)(void *data);
   const char *ident;

   void (*grab_mouse)(void *data, bool state);
   bool (*set_rumble)(void *data, unsigned port, enum retro_rumble_effect effect, uint16_t state);
   const rarch_joypad_driver_t *(*get_joypad_driver)(void *data);
} input_driver_t;

struct rarch_viewport;

struct font_params
{
   float x;
   float y;
   float scale;
   float drop_mod; // Drop shadow color multiplier.
   int drop_x, drop_y; // Drop shadow offset. If both are 0, no drop shadow will be rendered.
   uint32_t color; // ABGR. Use the macros.
   bool full_screen;
};
#define FONT_COLOR_RGBA(r, g, b, a) (((r) << 0) | ((g) << 8) | ((b) << 16) | ((a) << 24))
#define FONT_COLOR_GET_RED(col)   (((col) >>  0) & 0xff)
#define FONT_COLOR_GET_GREEN(col) (((col) >>  8) & 0xff)
#define FONT_COLOR_GET_BLUE(col)  (((col) >> 16) & 0xff)
#define FONT_COLOR_GET_ALPHA(col) (((col) >> 24) & 0xff)

// Optionally implemented interface to poke more deeply into video driver.
typedef struct video_poke_interface
{
   void (*set_filtering)(void *data, unsigned index, bool smooth);
#ifdef HAVE_FBO
   uintptr_t (*get_current_framebuffer)(void *data);
   retro_proc_address_t (*get_proc_address)(void *data, const char *sym);
#endif
   bool (*cfg_sw_fb)(void *data, struct retro_framebuffer_config *fb_cfg);
   void (*set_aspect_ratio)(void *data, unsigned aspectratio_index);
   void (*apply_state_changes)(void *data);

#ifdef HAVE_MENU
   void (*set_texture_frame)(void *data, const void *frame, bool rgb32, unsigned width, unsigned height, float alpha); // Update texture.
   void (*set_texture_enable)(void *data, bool enable, bool full_screen); // Enable/disable rendering.
#endif
   void (*set_osd_msg)(void *data, const char *msg, const struct font_params *params);

   void (*show_mouse)(void *data, bool state);
   void (*grab_mouse_toggle)(void *data);

   struct gfx_shader *(*get_current_shader)(void *data);
} video_poke_interface_t;

typedef struct video_driver
{
   void *(*init)(const video_info_t *video, const input_driver_t **input, void **input_data); 
   // Should the video driver act as an input driver as well? :)
   // The video initialization might preinitialize an input driver to override the settings in case the video driver relies on input driver for event handling, e.g.
   bool (*frame)(void *data, const void *frame, unsigned width, unsigned height, unsigned pitch, const char *msg); // msg is for showing a message on the screen along with the video frame.
   void (*set_nonblock_state)(void *data, bool toggle); // Should we care about syncing to vblank? Fast forwarding.
   // Is the window still active?
   bool (*alive)(void *data);
   bool (*focus)(void *data); // Does the window have focus?
   bool (*set_shader)(void *data, enum rarch_shader_type type, const char *path); // Sets shader. Might not be implemented. Will be moved to poke_interface later.
   void (*free)(void *data);
   const char *ident;

   void (*set_rotation)(void *data, unsigned rotation);
   void (*viewport_info)(void *data, struct rarch_viewport *vp);

   // Reads out in BGR byte order (24bpp).
   bool (*read_viewport)(void *data, uint8_t *buffer);

   void (*poke_interface)(void *data, const video_poke_interface_t **iface);
} video_driver_t;

enum rarch_display_type
{
   RARCH_DISPLAY_NONE = 0, // Non-bindable types like consoles, KMS, etc.
   RARCH_DISPLAY_X11 // video_display => Display*, video_window => Window
};

typedef struct driver
{
   const audio_driver_t *audio;
   const video_driver_t *video;
   const input_driver_t *input;
   void *audio_data;
   void *video_data;
   void *input_data;
#ifdef HAVE_MENU
   menu_handle_t *menu;
   const menu_ctx_driver_t *menu_ctx;
#endif

   bool threaded_video;

   // If set during context deinit, the driver should keep
   // graphics context alive to avoid having to reset all context state.
   bool video_cache_context;
   bool video_cache_context_ack; // Set to true by driver if context caching succeeded.

   // Set this to true if the platform in question needs to 'own' the respective
   // handle and therefore skip regular RetroArch driver teardown/reiniting procedure.
   // If set to true, the 'free' function will get skipped. It is then up to the
   // driver implementation to properly handle 'reiniting' inside the 'init' function
   // and make sure it returns the existing handle instead of allocating and returning
   // a pointer to a new handle.
   //
   // Typically, if a driver intends to make use of this, it should set this to true
   // at the end of its 'init' function.
   bool video_data_own;
   bool audio_data_own;
   bool input_data_own;
#ifdef HAVE_MENU
   bool menu_data_own;
#endif

   rarch_cmd_t *command;

   bool block_hotkey;
   bool block_input;
   bool block_libretro_input;
   bool nonblock_state;

   // Opaque handles to currently running window.
   // Used by e.g. input drivers which bind to a window.
   // Drivers are responsible for setting these if an input driver
   // could potentially make use of this.
   uintptr_t video_display;
   uintptr_t video_window;
   enum rarch_display_type display_type;

   // Used for 15-bit -> 16-bit conversions that take place before being passed to video driver.
   struct scaler_ctx scaler;
   void *scaler_out;

   // Graphics driver requires RGBA byte order data (ABGR on little-endian) for 32-bit.
   // This takes effect for overlay and shader cores that wants to load data into graphics driver.
   // Kinda hackish to place it here, it is only used for GLES.
   // TODO: Refactor this better.
   bool gfx_use_rgba;

   // Interface for "poking".
   const video_poke_interface_t *video_poke;

   // last message given to the video driver
   const char *current_msg;
} driver_t;

void init_drivers();
void init_drivers_pre();
void uninit_drivers();

void init_video_input();
void uninit_video_input();
void init_audio();
void uninit_audio();

void find_prev_resampler_driver();
void find_prev_video_driver();
void find_prev_audio_driver();
void find_prev_input_driver();
void find_next_video_driver();
void find_next_audio_driver();
void find_next_input_driver();
void find_next_resampler_driver();

void driver_set_monitor_refresh_rate(float hz);
bool driver_monitor_fps_statistics(double *refresh_rate, double *deviation, unsigned *sample_points);
void driver_set_nonblock_state(bool nonblock);

// Used by RETRO_ENVIRONMENT_SET_HW_RENDER.
uintptr_t driver_get_current_framebuffer();
retro_proc_address_t driver_get_proc_address(const char *sym);

// Used by RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE
bool driver_set_rumble_state(unsigned port, enum retro_rumble_effect effect, uint16_t strength);

#ifdef HAVE_DYLIB
void rarch_deinit_filter();
void rarch_init_filter(enum retro_pixel_format);
#endif

const char *rarch_dspfilter_get_name(void *data);

#ifdef HAVE_MENU
const void *menu_ctx_find_driver(const char *ident); // Finds driver with ident. Does not initialize.
void find_prev_menu_driver();
void find_next_menu_driver();
void find_menu_driver();
#endif

// Used by RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO
bool driver_update_system_av_info(const struct retro_system_av_info *info);

extern driver_t driver;

//////////////////////////////////////////////// Backends
extern const audio_driver_t audio_alsa;
extern const audio_driver_t audio_alsathread;
extern const audio_driver_t audio_openal;
extern const audio_driver_t audio_jack;
extern const audio_driver_t audio_sdl;
extern const audio_driver_t audio_pulse;
extern const audio_driver_t audio_null;
extern const video_driver_t video_gl;
extern const video_driver_t video_xvideo;
extern const video_driver_t video_sdl;
extern const video_driver_t video_sdl2;
extern const video_driver_t video_null;
extern const video_driver_t video_exynos;
extern const input_driver_t input_sdl;
extern const input_driver_t input_x;
extern const input_driver_t input_wayland;
extern const input_driver_t input_xinput;
extern const input_driver_t input_linuxraw;
extern const input_driver_t input_udev;
extern const input_driver_t input_null;

extern const menu_ctx_driver_t menu_ctx_rmenu;
extern const menu_ctx_driver_t menu_ctx_rgui;

extern const menu_ctx_driver_backend_t menu_ctx_backend_common;

#include "driver_funcs.h"

#endif

