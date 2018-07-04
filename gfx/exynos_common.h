/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2015-2016 - Tobias Jakobi
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

#include <poll.h>

#include <stdbool.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

// Set to '1' to enable debug logging code.
#define EXYNOS_GFX_DEBUG_LOG 0

// Set to '1' to enable debug perf code.
#define EXYNOS_GFX_DEBUG_PERF 0


// -----------------------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------------------

struct exynos_data_base;


// -----------------------------------------------------------------------------------------
// Enumerator definitions
// -----------------------------------------------------------------------------------------

enum exynos_page_base_flags {
  // Page is currently in use.
  page_used  = (1 << 0),

  // Page has to be cleared before use.
  page_clear = (1 << 1),

  // Overlay plane is enabled.
  page_overlay = (1 << 2),

  // Use this to extend the flags.
  base_flag  = (1 << 2),
};

enum exynos_plane_type {
  plane_primary,
  plane_overlay,
  exynos_plane_max,
};


// -----------------------------------------------------------------------------------------
// Structure definitions
// -----------------------------------------------------------------------------------------

struct bounding_box {
  uint16_t x, y;
  uint16_t w, h;
};

struct exynos_plane {
  struct exynos_bo *bo;
  uint32_t buf_id;
  drmModeAtomicReq *atomic_request;
  uint32_t atomic_cursor;
};

struct plane_info {
  // Dimensions of the plane.
  unsigned width, height;

  // DRM pixel format used for the plane.
  uint32_t pixel_format;

  // Pitch and size for the BO backing the plane.
  unsigned pitch, size;
};

struct exynos_page_base {
  struct exynos_plane planes[exynos_plane_max];
  struct bounding_box overlay_box;

  struct exynos_data_base *root;

  uint32_t flags;
};

struct exynos_data_base {
  int fd;
  struct exynos_device *device;

  struct exynos_drm *drm;
  struct exynos_fliphandler *fliphandler;

  struct exynos_page_base **pages;
  unsigned num_pages;

  // Currently displayed page.
  struct exynos_page_base *cur_page;

  // Counter of pending pageflips.
  unsigned pageflip_pending;

  // Informations about primary and overlay plane.
  struct plane_info plane_infos[exynos_plane_max];
};


// -----------------------------------------------------------------------------------------
// Global functions
// -----------------------------------------------------------------------------------------

int exynos_open(struct exynos_data_base *data);
void exynos_close(struct exynos_data_base *data);

int exynos_init(struct exynos_data_base *data);
void exynos_deinit(struct exynos_data_base *data);

int exynos_register_page(struct exynos_data_base *data,
                         struct exynos_page_base *page);
void exynos_unregister_pages(struct exynos_data_base *data);

int exynos_alloc(struct exynos_data_base *data);
void exynos_free(struct exynos_data_base *data);

struct exynos_page_base* exynos_get_free_page(struct exynos_data_base *data);

void exynos_wait_for_flip(struct exynos_data_base *data);
int exynos_issue_flip(struct exynos_data_base *data,
                      struct exynos_page_base *page);
