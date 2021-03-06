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

/// Config header for RetroArch
//
//

#ifndef __CONFIG_DEF_H
#define __CONFIG_DEF_H

#include "libretro.h"
#include "driver.h"
#include "gfx/gfx_common.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

enum 
{
   VIDEO_GL = 0,
   VIDEO_XVIDEO,
   VIDEO_SDL,
   VIDEO_NULL,
   VIDEO_EXYNOS,

   AUDIO_ALSA,
   AUDIO_ALSATHREAD,
   AUDIO_AL,
   AUDIO_JACK,
   AUDIO_SDL,
   AUDIO_PULSE,
   AUDIO_NULL,

   AUDIO_RESAMPLER_CC,
   AUDIO_RESAMPLER_SINC,

   INPUT_SDL,
   INPUT_X,
   INPUT_WAYLAND,
   INPUT_XINPUT,
   INPUT_UDEV,
   INPUT_LINUXRAW,
   INPUT_NULL,

   MENU_RGUI,
   MENU_RMENU,
};

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
#define VIDEO_DEFAULT_DRIVER VIDEO_GL
#elif defined(HAVE_XVIDEO)
#define VIDEO_DEFAULT_DRIVER VIDEO_XVIDEO
#elif defined(HAVE_SDL)
#define VIDEO_DEFAULT_DRIVER VIDEO_SDL
#else
#define VIDEO_DEFAULT_DRIVER VIDEO_NULL
#endif

#if defined(HAVE_ALSA)
#define AUDIO_DEFAULT_DRIVER AUDIO_ALSA
#elif defined(HAVE_PULSE)
#define AUDIO_DEFAULT_DRIVER AUDIO_PULSE
#elif defined(HAVE_JACK)
#define AUDIO_DEFAULT_DRIVER AUDIO_JACK
#elif defined(HAVE_AL)
#define AUDIO_DEFAULT_DRIVER AUDIO_AL
#elif defined(HAVE_SDL)
#define AUDIO_DEFAULT_DRIVER AUDIO_SDL
#else
#define AUDIO_DEFAULT_DRIVER AUDIO_NULL
#endif

#define AUDIO_DEFAULT_RESAMPLER_DRIVER  AUDIO_RESAMPLER_SINC

#if defined(HAVE_XINPUT2)
#define INPUT_DEFAULT_DRIVER INPUT_XINPUT
#elif defined(HAVE_UDEV)
#define INPUT_DEFAULT_DRIVER INPUT_UDEV
#elif defined(__linux__)
#define INPUT_DEFAULT_DRIVER INPUT_LINUXRAW
#elif defined(HAVE_X11)
#define INPUT_DEFAULT_DRIVER INPUT_X
#elif defined(HAVE_WAYLAND)
#define INPUT_DEFAULT_DRIVER INPUT_WAYLAND
#elif defined(HAVE_SDL)
#define INPUT_DEFAULT_DRIVER INPUT_SDL
#else
#define INPUT_DEFAULT_DRIVER INPUT_NULL
#endif

#if defined(HAVE_RMENU)
#define MENU_DEFAULT_DRIVER MENU_RMENU
#else
#define MENU_DEFAULT_DRIVER MENU_RGUI
#endif

#define DEFAULT_ASPECT_RATIO -1.0f

////////////////
// Video
////////////////

static const unsigned int def_user_language = 0;

// Windowed
// Real x resolution = aspect * base_size * x scale
// Real y resolution = base_size * y scale
static const float scale = 3.0;

// Fullscreen
static const bool fullscreen = false;  // To start in Fullscreen or not.
static const bool windowed_fullscreen = true;  // To use windowed mode or not when going fullscreen.
static const unsigned monitor_index = 0; // Which monitor to prefer. 0 is any monitor, 1 and up selects specific monitors, 1 being the first monitor.
static const unsigned fullscreen_x = 0; // Fullscreen resolution. A value of 0 uses the desktop resolution.
static const unsigned fullscreen_y = 0;

static const bool load_dummy_on_core_shutdown = true;

// Video VSYNC (recommended)
static const bool vsync = true;

// Attempts to hard-synchronize CPU and GPU. Can reduce latency at cost of performance.
static const bool hard_sync = false;
// Configures how many frames the GPU can run ahead of CPU.
// 0: Syncs to GPU immediately.
// 1: Syncs to previous frame.
// 2: Etc ...
static const unsigned hard_sync_frames = 0;

// Inserts a black frame inbetween frames.
// Useful for 120 Hz monitors who want to play 60 Hz material with eliminated ghosting. video_refresh_rate should still be configured as if it is a 60 Hz monitor (divide refresh rate by 2).
static bool black_frame_insertion = false;

// Uses a custom swap interval for VSync.
// Set this to effectively halve monitor refresh rate.
static unsigned swap_interval = 1;

// Threaded video. Will possibly increase performance significantly at cost of worse synchronization and latency.
static const bool video_threaded = false;

// Set to true if HW render cores should get their private context.
static const bool video_shared_context = false;

// Smooths picture
static const bool video_smooth = true;

// On resize and fullscreen, rendering area will stay 4:3
static const bool force_aspect = true; 

// Enable use of shaders.
static const bool shader_enable = false;

// Only scale in integer steps.
// The base size depends on system-reported geometry and aspect ratio.
// If video_force_aspect is not set, X/Y will be integer scaled independently.
static const bool scale_integer = false;

// Controls aspect ratio handling.
static const float aspect_ratio = DEFAULT_ASPECT_RATIO; // Automatic
static const bool aspect_ratio_auto = false; // 1:1 PAR

static unsigned aspect_ratio_idx = ASPECT_RATIO_CONFIG; // Use g_settings.video.aspect_ratio.

// Save configuration file on exit
static bool config_save_on_exit = true;

static const char *default_filter_dir     = NULL;
static const char *default_dsp_filter_dir = NULL;

#ifdef HAVE_MENU
static bool default_block_config_read = true;
#else
static bool default_block_config_read = false;
#endif

static bool default_core_specific_config = false;

// Crop overscanned frames.
static const bool crop_overscan = true;

// Font size for on-screen messages.
#if defined(HAVE_RMENU)
static const float font_size = 1.0f;
#else
static const float font_size = 32;
#endif

// Offset for where messages will be placed on-screen. Values are in range [0.0, 1.0].
static const float message_pos_offset_x = 0.05;
static const float message_pos_offset_y = 0.05;

// Color of the message.
static const uint32_t message_color = 0xffff00; // RGB hex value.

// Record post-filtered (CPU filter) video rather than raw game output.
static const bool post_filter_record = false;

// Screenshots post-shaded GPU output if available.
static const bool gpu_screenshot = true;

// Record post-shaded GPU output instead of raw game footage if available.
static const bool gpu_record = false;

// OSD-messages
static const bool font_enable = true;

// The accurate refresh rate of your monitor (Hz).
// This is used to calculate audio input rate with the formula:
// audio_input_rate = game_input_rate * display_refresh_rate / game_refresh_rate.
// If the implementation does not report any values,
// NTSC defaults will be assumed for compatibility.
// This value should stay close to 60Hz to avoid large pitch changes.
// If your monitor does not run at 60Hz, or something close to it, disable VSync,
// and leave this at its default.
static const float refresh_rate = 59.95; 

// Allow games to set rotation. If false, rotation requests are honored, but ignored.
// Used for setups where one manually rotates the monitor.
static const bool allow_rotate = true;

////////////////
// Audio
////////////////

// Will enable audio or not.
static const bool audio_enable = true;

// Output samplerate
static const unsigned out_rate = 48000;

// Audio device (e.g. hw:0,0 or /dev/audio). If NULL, will use defaults.
static const char *audio_device = NULL;

// Desired audio latency in milliseconds. Might not be honored if driver can't provide given latency.
static const int out_latency = 64;

// Will sync audio. (recommended) 
static const bool audio_sync = true;

// Audio rate control
static const bool rate_control = true;

// Rate control delta. Defines how much rate_control is allowed to adjust input rate.
static const float rate_control_delta = 0.005;

// Default audio volume in dB. (0.0 dB == unity gain).
static const float audio_volume = 0.0;

//////////////
// Misc
//////////////

// Enables displaying the current frames per second.
static const bool fps_show = false;

// Enables use of rewind. This will incur some memory footprint depending on the save state buffer.
static const bool rewind_enable = false;

// The buffer size for the rewind buffer. This needs to be about 15-20MB per minute. Very game dependant.
static const unsigned rewind_buffer_size = 20 << 20; // 20MiB

// How many frames to rewind at a time.
static const unsigned rewind_granularity = 1;

// Pause gameplay when gameplay loses focus.
static const bool pause_nonactive = false;

// Saves non-volatile SRAM at a regular interval. It is measured in seconds. A value of 0 disables autosave.
static const unsigned autosave_interval = 0;

// When being client over netplay, use keybinds for player 1 rather than player 2.
static const bool netplay_client_swap_input = true;

// On save state load, block SRAM from being overwritten.
// This could potentially lead to buggy games.
static const bool block_sram_overwrite = false;

// When saving savestates, state index is automatically incremented before saving.
// When the content is loaded, state index will be set to the highest existing value.
static const bool savestate_auto_index = false;

// Automatically saves a savestate at the end of RetroArch's lifetime.
// The path is $SRAM_PATH.auto.
// RetroArch will automatically load any savestate with this path on startup if savestate_auto_load is set.
static const bool savestate_auto_save = false;
static const bool savestate_auto_load = true;

// Slowmotion ratio.
static const float slowmotion_ratio = 3.0;

// Maximum fast forward ratio (Negative => no limit).
static const float fastforward_ratio = -1.0;

// Enable network/named pipe command interface
static const bool network_cmd_enable = false;
static const uint16_t network_cmd_port = 55355;
static const bool pipe_cmd_enable = false;

// Number of entries that will be kept in content history file.
static const unsigned default_content_history_size = 100;

// Show Menu start-up screen on boot.
static const bool menu_show_start_screen = true;

// Log level for libretro cores (GET_LOG_INTERFACE).
static const unsigned libretro_log_level = 0;


////////////////////
// Keybinds, Joypad
////////////////////

// Axis threshold (between 0.0 and 1.0)
// How far an axis must be tilted to result in a button press
static const float axis_threshold = 0.5;

// Describes speed of which turbo-enabled buttons toggle.
static const unsigned turbo_period = 6;
static const unsigned turbo_duty_cycle = 3;

// Enable input auto-detection. Will attempt to autoconfigure
// gamepads, plug-and-play style.
static const bool input_autodetect_enable = true;

#define RETRO_DEF_JOYPAD_B NO_BTN
#define RETRO_DEF_JOYPAD_Y NO_BTN
#define RETRO_DEF_JOYPAD_SELECT NO_BTN
#define RETRO_DEF_JOYPAD_START NO_BTN
#define RETRO_DEF_JOYPAD_UP NO_BTN
#define RETRO_DEF_JOYPAD_DOWN NO_BTN
#define RETRO_DEF_JOYPAD_LEFT NO_BTN
#define RETRO_DEF_JOYPAD_RIGHT NO_BTN
#define RETRO_DEF_JOYPAD_A NO_BTN
#define RETRO_DEF_JOYPAD_X NO_BTN
#define RETRO_DEF_JOYPAD_L NO_BTN
#define RETRO_DEF_JOYPAD_R NO_BTN
#define RETRO_DEF_JOYPAD_L2 NO_BTN
#define RETRO_DEF_JOYPAD_R2 NO_BTN
#define RETRO_DEF_JOYPAD_L3 NO_BTN
#define RETRO_DEF_JOYPAD_R3 NO_BTN
#define RETRO_DEF_ANALOGL_DPAD_LEFT NO_BTN
#define RETRO_DEF_ANALOGL_DPAD_RIGHT NO_BTN
#define RETRO_DEF_ANALOGL_DPAD_UP NO_BTN
#define RETRO_DEF_ANALOGL_DPAD_DOWN NO_BTN
#define RETRO_DEF_ANALOGR_DPAD_LEFT NO_BTN
#define RETRO_DEF_ANALOGR_DPAD_RIGHT NO_BTN
#define RETRO_DEF_ANALOGR_DPAD_UP NO_BTN
#define RETRO_DEF_ANALOGR_DPAD_DOWN NO_BTN

#define RETRO_LBL_JOYPAD_B "RetroPad B Button"
#define RETRO_LBL_JOYPAD_Y "RetroPad Y Button"
#define RETRO_LBL_JOYPAD_SELECT "RetroPad Select Button"
#define RETRO_LBL_JOYPAD_START "RetroPad Start Button"
#define RETRO_LBL_JOYPAD_UP "RetroPad D-Pad Up"
#define RETRO_LBL_JOYPAD_DOWN "RetroPad D-Pad Down"
#define RETRO_LBL_JOYPAD_LEFT "RetroPad D-Pad Left"
#define RETRO_LBL_JOYPAD_RIGHT "RetroPad D-Pad Right"
#define RETRO_LBL_JOYPAD_A "RetroPad A Button"
#define RETRO_LBL_JOYPAD_X "RetroPad X Button"
#define RETRO_LBL_JOYPAD_L "RetroPad L Button"
#define RETRO_LBL_JOYPAD_R "RetroPad R Button"
#define RETRO_LBL_JOYPAD_L2 "RetroPad L2 Button"
#define RETRO_LBL_JOYPAD_R2 "RetroPad R2 Button"
#define RETRO_LBL_JOYPAD_L3 "RetroPad L3 Button"
#define RETRO_LBL_JOYPAD_R3 "RetroPad R3 Button"
#define RETRO_LBL_TURBO_ENABLE "Turbo Enable"
#define RETRO_LBL_ANALOG_LEFT_X_PLUS "Left Analog X +"
#define RETRO_LBL_ANALOG_LEFT_X_MINUS "Left Analog X -"
#define RETRO_LBL_ANALOG_LEFT_Y_PLUS "Left Analog Y +"
#define RETRO_LBL_ANALOG_LEFT_Y_MINUS "Left Analog Y -"
#define RETRO_LBL_ANALOG_RIGHT_X_PLUS "Right Analog X +"
#define RETRO_LBL_ANALOG_RIGHT_X_MINUS "Right Analog X -"
#define RETRO_LBL_ANALOG_RIGHT_Y_PLUS "Right Analog Y +"
#define RETRO_LBL_ANALOG_RIGHT_Y_MINUS "Right Analog Y -"
#define RETRO_LBL_FAST_FORWARD_KEY "Fast Forward"
#define RETRO_LBL_FAST_FORWARD_HOLD_KEY "Fast Forward Hold"
#define RETRO_LBL_LOAD_STATE_KEY "Load State"
#define RETRO_LBL_SAVE_STATE_KEY "Save State"
#define RETRO_LBL_FULLSCREEN_TOGGLE_KEY "Fullscreen Toggle"
#define RETRO_LBL_QUIT_KEY "Quit Key"
#define RETRO_LBL_STATE_SLOT_PLUS "State Slot Plus"
#define RETRO_LBL_STATE_SLOT_MINUS "State Slot Minus"
#define RETRO_LBL_REWIND "Rewind"
#define RETRO_LBL_MOVIE_RECORD_TOGGLE "Movie Record Toggle"
#define RETRO_LBL_PAUSE_TOGGLE "Pause Toggle"
#define RETRO_LBL_FRAMEADVANCE "Frame Advance"
#define RETRO_LBL_RESET "Reset"
#define RETRO_LBL_SHADER_NEXT "Next Shader"
#define RETRO_LBL_SHADER_PREV "Previous Shader"
#define RETRO_LBL_CHEAT_INDEX_PLUS "Cheat Index Plus"
#define RETRO_LBL_CHEAT_INDEX_MINUS "Cheat Index Minus"
#define RETRO_LBL_CHEAT_TOGGLE "Cheat Toggle"
#define RETRO_LBL_SCREENSHOT "Screenshot"
#define RETRO_LBL_MUTE "Mute Audio"
#define RETRO_LBL_NETPLAY_FLIP "Netplay Flip Players"
#define RETRO_LBL_SLOWMOTION "Slowmotion"
#define RETRO_LBL_ENABLE_HOTKEY "Enable Hotkey"
#define RETRO_LBL_VOLUME_UP "Volume Up"
#define RETRO_LBL_VOLUME_DOWN "Volume Down"
#define RETRO_LBL_DISK_EJECT_TOGGLE "Disk Eject Toggle"
#define RETRO_LBL_DISK_NEXT "Disk Swap Next"
#define RETRO_LBL_GRAB_MOUSE_TOGGLE "Grab mouse toggle"
#define RETRO_LBL_MENU_TOGGLE "Menu toggle"

// Player 1
static const struct retro_keybind retro_keybinds_1[] = {
    //     | RetroPad button            | desc                           | keyboard key  | js btn |     js axis   |
   { true, RETRO_DEVICE_ID_JOYPAD_B,      RETRO_LBL_JOYPAD_B,              RETROK_z,       RETRO_DEF_JOYPAD_B,      0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_Y,      RETRO_LBL_JOYPAD_Y,              RETROK_a,       RETRO_DEF_JOYPAD_Y,      0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_LBL_JOYPAD_SELECT,         RETROK_RSHIFT,  RETRO_DEF_JOYPAD_SELECT, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_START,  RETRO_LBL_JOYPAD_START,          RETROK_RETURN,  RETRO_DEF_JOYPAD_START,  0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_UP,     RETRO_LBL_JOYPAD_UP,             RETROK_UP,      RETRO_DEF_JOYPAD_UP,     0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_DOWN,   RETRO_LBL_JOYPAD_DOWN,           RETROK_DOWN,    RETRO_DEF_JOYPAD_DOWN,   0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_LEFT,   RETRO_LBL_JOYPAD_LEFT,           RETROK_LEFT,    RETRO_DEF_JOYPAD_LEFT,   0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_RIGHT,  RETRO_LBL_JOYPAD_RIGHT,          RETROK_RIGHT,   RETRO_DEF_JOYPAD_RIGHT,  0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_A,      RETRO_LBL_JOYPAD_A,              RETROK_x,       RETRO_DEF_JOYPAD_A,      0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_X,      RETRO_LBL_JOYPAD_X,              RETROK_s,       RETRO_DEF_JOYPAD_X,      0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_L,      RETRO_LBL_JOYPAD_L,              RETROK_q,       RETRO_DEF_JOYPAD_L,      0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_R,      RETRO_LBL_JOYPAD_R,              RETROK_w,       RETRO_DEF_JOYPAD_R,      0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_L2,     RETRO_LBL_JOYPAD_L2,             RETROK_UNKNOWN, RETRO_DEF_JOYPAD_L2,     0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_R2,     RETRO_LBL_JOYPAD_R2,             RETROK_UNKNOWN, RETRO_DEF_JOYPAD_R2,     0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_L3,     RETRO_LBL_JOYPAD_L3,             RETROK_UNKNOWN, RETRO_DEF_JOYPAD_L3,     0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_R3,     RETRO_LBL_JOYPAD_R3,             RETROK_UNKNOWN, RETRO_DEF_JOYPAD_R3,     0, AXIS_NONE },

   { true, RARCH_ANALOG_LEFT_X_PLUS,      RETRO_LBL_ANALOG_LEFT_X_PLUS,    RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_LEFT_X_MINUS,     RETRO_LBL_ANALOG_LEFT_X_MINUS,   RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_LEFT_Y_PLUS,      RETRO_LBL_ANALOG_LEFT_Y_PLUS,    RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_LEFT_Y_MINUS,     RETRO_LBL_ANALOG_LEFT_Y_MINUS,   RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_RIGHT_X_PLUS,     RETRO_LBL_ANALOG_RIGHT_X_PLUS,   RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_RIGHT_X_MINUS,    RETRO_LBL_ANALOG_RIGHT_X_MINUS,  RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_RIGHT_Y_PLUS,     RETRO_LBL_ANALOG_RIGHT_Y_PLUS,   RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_RIGHT_Y_MINUS,    RETRO_LBL_ANALOG_RIGHT_Y_MINUS,  RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },

   { true, RARCH_TURBO_ENABLE,             RETRO_LBL_TURBO_ENABLE,          RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_FAST_FORWARD_KEY,         RETRO_LBL_FAST_FORWARD_KEY,     RETROK_SPACE,   NO_BTN, 0, AXIS_NONE },
   { true, RARCH_FAST_FORWARD_HOLD_KEY,    RETRO_LBL_FAST_FORWARD_HOLD_KEY,RETROK_l,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_LOAD_STATE_KEY,           RETRO_LBL_LOAD_STATE_KEY,       RETROK_F4,      NO_BTN, 0, AXIS_NONE },
   { true, RARCH_SAVE_STATE_KEY,           RETRO_LBL_SAVE_STATE_KEY,       RETROK_F2,      NO_BTN, 0, AXIS_NONE },
   { true, RARCH_FULLSCREEN_TOGGLE_KEY,    RETRO_LBL_FULLSCREEN_TOGGLE_KEY,RETROK_f,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_QUIT_KEY,                 RETRO_LBL_QUIT_KEY,             RETROK_ESCAPE,  NO_BTN, 0, AXIS_NONE },
   { true, RARCH_STATE_SLOT_PLUS,          RETRO_LBL_STATE_SLOT_PLUS,      RETROK_F7,      NO_BTN, 0, AXIS_NONE },
   { true, RARCH_STATE_SLOT_MINUS,         RETRO_LBL_STATE_SLOT_MINUS,     RETROK_F6,      NO_BTN, 0, AXIS_NONE },
   { true, RARCH_REWIND,                   RETRO_LBL_REWIND,               RETROK_r,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_MOVIE_RECORD_TOGGLE,      RETRO_LBL_MOVIE_RECORD_TOGGLE,  RETROK_o,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_PAUSE_TOGGLE,             RETRO_LBL_PAUSE_TOGGLE,         RETROK_p,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_FRAMEADVANCE,             RETRO_LBL_FRAMEADVANCE,         RETROK_k,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_RESET,                    RETRO_LBL_RESET,                RETROK_h,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_SHADER_NEXT,              RETRO_LBL_SHADER_NEXT,          RETROK_m,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_SHADER_PREV,              RETRO_LBL_SHADER_PREV,          RETROK_n,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_CHEAT_INDEX_PLUS,         RETRO_LBL_CHEAT_INDEX_PLUS,     RETROK_y,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_CHEAT_INDEX_MINUS,        RETRO_LBL_CHEAT_INDEX_MINUS,    RETROK_t,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_CHEAT_TOGGLE,             RETRO_LBL_CHEAT_TOGGLE,         RETROK_u,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_SCREENSHOT,               RETRO_LBL_SCREENSHOT,           RETROK_F8,      NO_BTN, 0, AXIS_NONE },
   { true, RARCH_MUTE,                     RETRO_LBL_MUTE,                 RETROK_F9,      NO_BTN, 0, AXIS_NONE },
   { true, RARCH_NETPLAY_FLIP,             RETRO_LBL_NETPLAY_FLIP,         RETROK_i,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_SLOWMOTION,               RETRO_LBL_SLOWMOTION,           RETROK_e,       NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ENABLE_HOTKEY,            RETRO_LBL_ENABLE_HOTKEY,        RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_VOLUME_UP,                RETRO_LBL_VOLUME_UP,            RETROK_KP_PLUS, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_VOLUME_DOWN,              RETRO_LBL_VOLUME_DOWN,          RETROK_KP_MINUS,NO_BTN, 0, AXIS_NONE },
   { true, RARCH_DISK_EJECT_TOGGLE,        RETRO_LBL_DISK_EJECT_TOGGLE,    RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_DISK_NEXT,                RETRO_LBL_DISK_NEXT,            RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_GRAB_MOUSE_TOGGLE,        RETRO_LBL_GRAB_MOUSE_TOGGLE,    RETROK_F11,     NO_BTN, 0, AXIS_NONE },
   { true, RARCH_MENU_TOGGLE,              RETRO_LBL_MENU_TOGGLE,          RETROK_F1,      NO_BTN, 0, AXIS_NONE },
};

// Player 2-5
static const struct retro_keybind retro_keybinds_rest[] = {
    //     | RetroPad button            | desc                           | keyboard key  | js btn |     js axis   |
   { true, RETRO_DEVICE_ID_JOYPAD_B,      RETRO_LBL_JOYPAD_B,              RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_Y,      RETRO_LBL_JOYPAD_Y,              RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_LBL_JOYPAD_SELECT,         RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_START,  RETRO_LBL_JOYPAD_START,          RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_UP,     RETRO_LBL_JOYPAD_UP,             RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_DOWN,   RETRO_LBL_JOYPAD_DOWN,           RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_LEFT,   RETRO_LBL_JOYPAD_LEFT,           RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_RIGHT,  RETRO_LBL_JOYPAD_RIGHT,          RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_A,      RETRO_LBL_JOYPAD_A,              RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_X,      RETRO_LBL_JOYPAD_X,              RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_L,      RETRO_LBL_JOYPAD_L,              RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_R,      RETRO_LBL_JOYPAD_R,              RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_L2,     RETRO_LBL_JOYPAD_L2,             RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_R2,     RETRO_LBL_JOYPAD_R2,             RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_L3,     RETRO_LBL_JOYPAD_L3,             RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RETRO_DEVICE_ID_JOYPAD_R3,     RETRO_LBL_JOYPAD_R3,             RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },

   { true, RARCH_ANALOG_LEFT_X_PLUS,      RETRO_LBL_ANALOG_LEFT_X_PLUS,    RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_LEFT_X_MINUS,     RETRO_LBL_ANALOG_LEFT_X_MINUS,   RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_LEFT_Y_PLUS,      RETRO_LBL_ANALOG_LEFT_Y_PLUS,    RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_LEFT_Y_MINUS,     RETRO_LBL_ANALOG_LEFT_Y_MINUS,   RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_RIGHT_X_PLUS,     RETRO_LBL_ANALOG_RIGHT_X_PLUS,   RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_RIGHT_X_MINUS,    RETRO_LBL_ANALOG_RIGHT_X_MINUS,  RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_RIGHT_Y_PLUS,     RETRO_LBL_ANALOG_RIGHT_Y_PLUS,   RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_ANALOG_RIGHT_Y_MINUS,    RETRO_LBL_ANALOG_RIGHT_Y_MINUS,  RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
   { true, RARCH_TURBO_ENABLE,            RETRO_LBL_TURBO_ENABLE,          RETROK_UNKNOWN, NO_BTN, 0, AXIS_NONE },
};

#endif

