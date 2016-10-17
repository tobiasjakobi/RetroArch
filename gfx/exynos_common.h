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

/* Set to '1' to enable debug logging code. */
#define EXYNOS_GFX_DEBUG_LOG 0

/* Set to '1' to enable debug perf code. */
#define EXYNOS_GFX_DEBUG_PERF 0

enum exynos_page_base_flags {
  /* Page is currently in use. */
  page_used  = (1 << 0),

  /* Page has to be cleared before use. */
  page_clear = (1 << 1),

  /* Use this to extend the flags. */
  base_flag  = (1 << 2)
};

struct exynos_data_base;

struct exynos_page_base {
  struct exynos_bo *bo;
  uint32_t buf_id;
  drmModeAtomicReq *atomic_request;

  struct exynos_data_base *root;

  uint32_t flags;
};

struct exynos_data_base {
  int fd;
  struct exynos_device *device;

  struct exynos_drm *drm;
  struct exynos_fliphandler *fliphandler;

  void *pages;
  unsigned page_size; /* size of a page object in size */
  unsigned num_pages;

  /* currently displayed page */
  struct exynos_page_base *cur_page;

  unsigned pageflip_pending;

  /* framebuffer dimensions */
  unsigned width, height;

  /* DRM pixel_format */
  uint32_t pixel_format;

  /* framebuffer parameters */
  unsigned pitch, size;
};

int exynos_open(struct exynos_data_base *data);
void exynos_close(struct exynos_data_base *data);
int exynos_init(struct exynos_data_base *data);
void exynos_deinit(struct exynos_data_base *data);
int exynos_alloc(struct exynos_data_base *data);
void exynos_free(struct exynos_data_base *data);

void exynos_wait_for_flip(struct exynos_data_base *data);
int exynos_issue_flip(struct exynos_data_base *data, struct exynos_page_base *page);
