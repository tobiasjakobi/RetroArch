/* RetroArch - A frontend for libretro.
* Copyright (C) 2010-2014 - Hans-Kristian Arntzen
* Copyright (C) 2011-2014 - Daniel De Matteis
*
* RetroArch is free software: you can redistribute it and/or modify it under the terms
* of the GNU General Public License as published by the Free Software Found-
* ation, either version 3 of the License, or (at your option) any later version.
*
* RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
* PURPOSE. See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with RetroArch.
* If not, see <http://www.gnu.org/licenses/>.
*/

#if defined(HAVE_CG) || defined(HAVE_HLSL) || defined(HAVE_GLSL)
#define HAVE_SHADERS
#endif


/*============================================================
CONSOLE EXTENSIONS
============================================================ */
#ifdef RARCH_CONSOLE

#if defined(HAVE_LOGGER)
#include "../logger/netlogger/logger.c"
#endif

#endif

#ifdef HAVE_ZLIB
#include "../file_extract.c"
#endif



/*============================================================
RLAUNCH
============================================================ */

#ifdef HAVE_RLAUNCH
#include "../tools/retrolaunch/rl_fnmatch.c"
#include "../tools/retrolaunch/sha1.c"
#include "../tools/retrolaunch/cd_detect.c"
#include "../tools/retrolaunch/parser.c"
#include "../tools/retrolaunch/main.c"
#endif

/*============================================================
PERFORMANCE
============================================================ */
#include "../performance.c"

/*============================================================
COMPATIBILITY
============================================================ */
#include "../compat/compat.c"

/*============================================================
CONFIG FILE
============================================================ */
#include "../conf/config_file.c"
#include "../core_options.c"

/*============================================================
CHEATS
============================================================ */
#include "../cheats.c"
#include "../hash.c"

/*============================================================
VIDEO CONTEXT
============================================================ */

#include "../gfx/gfx_context.c"


#if defined(HAVE_OPENGL)

#if defined(HAVE_KMS)
#include "../gfx/context/drm_egl_ctx.c"
#endif
#if defined(HAVE_VIDEOCORE)
#include "../gfx/context/vc_egl_ctx.c"
#endif
#if defined(HAVE_X11) && defined(HAVE_OPENGLES)
#include "../gfx/context/glx_ctx.c"
#endif
#if defined(HAVE_EGL)
#include "../gfx/context/xegl_ctx.c"
#endif

#endif

#ifdef HAVE_X11
#include "../gfx/context/x11_common.c"
#endif


/*============================================================
VIDEO SHADERS
============================================================ */
#ifdef HAVE_SHADERS
#include "../gfx/shader_common.c"
#include "../gfx/shader_parse.c"

#ifdef HAVE_CG
#include "../gfx/shader_cg.c"
#endif

#ifdef HAVE_HLSL
#include "../gfx/shader_hlsl.c"
#endif

#ifdef HAVE_GLSL
#include "../gfx/shader_glsl.c"
#endif

#endif

/*============================================================
VIDEO IMAGE
============================================================ */
#include "../gfx/image/image_rpng.c"
#include "../gfx/rpng/rpng.c"

/*============================================================
VIDEO DRIVER
============================================================ */

#if defined(HAVE_OPENGL)
#include "../gfx/math/matrix.c"
#endif

#ifdef HAVE_VG
#include "../gfx/vg.c"
#include "../gfx/math/matrix_3x3.c"
#endif

#ifdef HAVE_OMAP
#include "../gfx/omap_gfx.c"
#include "../gfx/fbdev.c"
#endif

#include "../gfx/gfx_common.c"

#ifdef HAVE_OPENGL
#include "../gfx/gl.c"

#ifndef HAVE_PSGL
#include "../gfx/glsym/rglgen.c"
#ifdef HAVE_OPENGLES2
#include "../gfx/glsym/glsym_es2.c"
#else
#include "../gfx/glsym/glsym_gl.c"
#endif
#endif

#endif

#ifdef HAVE_XVIDEO
#include "../gfx/xvideo.c"
#endif

#if defined(HAVE_NULLVIDEO)
#include "../gfx/null.c"
#endif

/*============================================================
FONTS
============================================================ */

#if defined(HAVE_OPENGL) || defined(HAVE_D3D8) || defined(HAVE_D3D9)

#if defined(HAVE_FREETYPE) || !defined(DONT_HAVE_BITMAPFONTS)
#include "../gfx/fonts/fonts.c"

#if defined(HAVE_FREETYPE)
#include "../gfx/fonts/freetype.c"
#endif

#if !defined(DONT_HAVE_BITMAPFONTS)
#include "../gfx/fonts/bitmapfont.c"
#endif

#endif

#ifdef HAVE_OPENGL
#include "../gfx/fonts/gl_font.c"
#endif

#if defined(HAVE_LIBDBGFONT)
#include "../gfx/fonts/ps_libdbgfont.c"
#elif defined(HAVE_OPENGL)
#include "../gfx/fonts/gl_raster_font.c"
#endif

#endif

/*============================================================
INPUT
============================================================ */
#include "../input/input_common.c"
#include "../input/keyboard_line.c"

#ifdef HAVE_OVERLAY
#include "../input/overlay.c"
#endif

#if defined(__linux__)
#include "../input/linuxraw_input.c"
#include "../input/linuxraw_joypad.c"
#endif

#ifdef HAVE_X11
#include "../input/x11_input.c"
#endif

#if defined(HAVE_NULLINPUT)
#include "../input/null.c"
#endif

/*============================================================
STATE TRACKER
============================================================ */
#ifndef DONT_HAVE_STATE_TRACKER
#include "../gfx/state_tracker.c"
#endif

#ifdef HAVE_PYTHON
#include "../gfx/py_state/py_state.c"
#endif

/*============================================================
FIFO BUFFER
============================================================ */
#include "../fifo_buffer.c"

/*============================================================
AUDIO RESAMPLER
============================================================ */
#include "../audio/resampler.c"
#include "../audio/sinc.c"
#ifdef HAVE_CC_RESAMPLER
#include "../audio/cc_resampler.c"
#endif

/*============================================================
CAMERA
============================================================ */
#ifdef HAVE_CAMERA

#ifdef HAVE_V4L2
#include "../camera/video4linux2.c"
#endif

#endif

/*============================================================
RSOUND
============================================================ */
#ifdef HAVE_RSOUND
#include "../audio/librsound.c"
#include "../audio/rsound.c"
#endif

/*============================================================
AUDIO
============================================================ */
#ifdef HAVE_XAUDIO
#include "../audio/xaudio.c"
#include "../audio/xaudio-c/xaudio-c.cpp"
#endif

#ifdef HAVE_DSOUND
#include "../audio/dsound.c"
#endif

#ifdef HAVE_ALSA
#include "../audio/alsa.c"
#include "../audio/alsathread.c"
#endif

#ifdef HAVE_AL
#include "../audio/openal.c"
#endif

#if defined(HAVE_NULLAUDIO)
#include "../audio/null.c"
#endif

/*============================================================
DRIVERS
============================================================ */
#include "../driver.c"

/*============================================================
SCALERS
============================================================ */
#include "../gfx/scaler/scaler_filter.c"
#include "../gfx/scaler/pixconv.c"
#include "../gfx/scaler/scaler.c"
#include "../gfx/scaler/scaler_int.c"

/*============================================================
FILTERS
============================================================ */

#ifdef HAVE_FILTERS_BUILTIN
#include "../gfx/filters/2xsai.c"
#include "../gfx/filters/super2xsai.c"
#include "../gfx/filters/supereagle.c"
#include "../gfx/filters/2xbr.c"
#include "../gfx/filters/darken.c"
#include "../gfx/filters/epx.c"
#include "../gfx/filters/scale2x.c"
#include "../gfx/filters/blargg_ntsc_snes_rf.c"
#include "../gfx/filters/blargg_ntsc_snes_composite.c"
#include "../gfx/filters/blargg_ntsc_snes_svideo.c"
#include "../gfx/filters/blargg_ntsc_snes_rgb.c"
#include "../gfx/filters/lq2x.c"
#include "../gfx/filters/phosphor2x.c"

#include "../audio/filters/echo.c"
#include "../audio/filters/eq.c"
#include "../audio/filters/chorus.c"
#include "../audio/filters/iir.c"
#include "../audio/filters/panning.c"
#include "../audio/filters/phaser.c"
#include "../audio/filters/reverb.c"
#include "../audio/filters/wahwah.c"
#endif
/*============================================================
DYNAMIC
============================================================ */
#include "../dynamic.c"
#include "../dynamic_dummy.c"
#include "../gfx/filter.c"
#include "../audio/dsp_filter.c"


/*============================================================
FILE
============================================================ */
#include "../file.c"
#include "../file_path.c"
#include "../file_list.c"

/*============================================================
MESSAGE
============================================================ */
#include "../message_queue.c"

/*============================================================
PATCH
============================================================ */
#include "../patch.c"

/*============================================================
SETTINGS
============================================================ */
#include "../settings.c"

/*============================================================
REWIND
============================================================ */
#include "../rewind.c"

/*============================================================
FRONTEND
============================================================ */

#include "../frontend/frontend_context.c"
#include "../frontend/platform/platform_null.c"

#include "../core_info.c"

/*============================================================
MAIN
============================================================ */
#include "../frontend/frontend.c"

/*============================================================
RETROARCH
============================================================ */
#include "../retroarch.c"

/*============================================================
RECORDING
============================================================ */
#include "../record/ffemu.c"

/*============================================================
THREAD
============================================================ */
#if defined(HAVE_THREADS)
#include "../thread.c"
#include "../gfx/video_thread_wrapper.c"
#include "../audio/thread_wrapper.c"
#include "../autosave.c"
#endif


/*============================================================
NETPLAY
============================================================ */
#ifdef HAVE_NETPLAY
#include "../netplay.c"
#endif

/*============================================================
SCREENSHOTS
============================================================ */
#include "../screenshot.c"


/*============================================================
HISTORY
============================================================ */
#include "../history.c"

/*============================================================
MENU
============================================================ */
#ifdef HAVE_MENU
#include "../frontend/menu/menu_input_line_cb.c"
#include "../frontend/menu/menu_common.c"
#include "../frontend/menu/menu_navigation.c"

#ifdef HAVE_MENU
#include "../frontend/menu/backend/menu_common_backend.c"
#endif

#ifdef HAVE_RMENU
#include "../frontend/menu/disp/rmenu.c"
#endif

#ifdef HAVE_RGUI
#include "../frontend/menu/disp/rgui.c"
#endif

#ifdef HAVE_RMENU_XUI
#include "../frontend/menu/disp/rmenu_xui.cpp"
#endif

#if defined(HAVE_LAKKA) && defined(HAVE_OPENGL)
#include "../frontend/menu/disp/lakka.c"
#endif

#endif

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================
RZLIB
============================================================ */
#ifdef WANT_MINIZ
#include "../deps/rzlib/adler32.c"
#include "../deps/rzlib/compress.c"
#include "../deps/rzlib/crc32.c"
#include "../deps/rzlib/deflate.c"
#include "../deps/rzlib/gzclose.c"
#include "../deps/rzlib/gzlib.c"
#include "../deps/rzlib/gzread.c"
#include "../deps/rzlib/gzwrite.c"
#include "../deps/rzlib/inffast.c"
#include "../deps/rzlib/inflate.c"
#include "../deps/rzlib/inftrees.c"
#include "../deps/rzlib/trees.c"
#include "../deps/rzlib/uncompr.c"
#include "../deps/rzlib/zutil.c"
#include "../deps/rzlib/ioapi.c"
#include "../deps/rzlib/unzip.c"
#endif

/*============================================================
XML
============================================================ */
#ifndef HAVE_LIBXML2
#define RXML_LIBXML2_COMPAT
#include "../compat/rxml/rxml.c"
#endif
    
#include "../settings_data.c"
/*============================================================
 AUDIO UTILS
============================================================ */
#include "../audio/utils.c"

#ifdef __cplusplus
}
#endif
