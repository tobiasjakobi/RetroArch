/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2013-2016 - Tobias Jakobi
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

#include "exynos_common.h"

#include <assert.h>

#include <libdrm/exynos_drmif.h>
#include <exynos/exynos_fimg2d.h>
#include <drm_fourcc.h>

#include "../general.h"
#include "gfx_common.h"
#include "fonts/fonts.h"

/* TODO: Honor these properties: vsync, menu rotation, menu alpha, aspect ratio change */

// -----------------------------------------------------------------------------------------
// External functions
// -----------------------------------------------------------------------------------------

extern void *memcpy_neon(void *dst, const void *src, size_t n);


// -----------------------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------------------

static bool exynos_cfg_sw_fb(void*, struct retro_framebuffer_config*);
static void exynos_set_aspect_ratio(void*, unsigned);
static void exynos_apply_state_changes(void*);
static void exynos_set_texture_frame(void*, const void*, bool, unsigned, unsigned, float);
static void exynos_set_texture_enable(void*, bool, bool);
static void exynos_set_osd_msg(void*, const char*, const struct font_params*);
static void exynos_show_mouse(void*, bool);

static void* exynos_gfx_init(const video_info_t*, const input_driver_t**, void**);
static bool exynos_gfx_frame(void*, const void*, unsigned, unsigned, unsigned, const char*);
static void exynos_gfx_set_nonblock_state(void *data, bool state);
static bool exynos_gfx_alive(void*);
static bool exynos_gfx_focus(void*);
static void exynos_gfx_free(void*);
static void exynos_gfx_set_rotation(void*, unsigned);
static void exynos_gfx_viewport_info(void*, struct rarch_viewport*);
static void exynos_gfx_get_poke_interface(void*, const video_poke_interface_t**);


// -----------------------------------------------------------------------------------------
// Enumerator definitions
// -----------------------------------------------------------------------------------------

enum constants {
  // Total number of pages used.
  eNumPages = 3,

  /*
   * Hardware limit of the G2D block.
   *
   * The G2D block cannot handle images with dimensions >= eHWLimitG2D, i.e.
   * both width and height of the image has to be strictly smaller.
   */
  eHWLimitG2D = 8000,
};

/*
 * We have to handle three types of 'data' from the frontend, each abstracted by a
 * G2D image object. The image objects are then backed by some storage buffer.
 */
enum image_type {
  eImageEmulator,
  eImageMenu,
  eImageFontAtlas,

  eImageTypeNum,
};


// -----------------------------------------------------------------------------------------
// Structure definitions
// -----------------------------------------------------------------------------------------

struct exynos_perf {
  unsigned memcpy_calls;
  unsigned g2d_calls;

  uint64_t memcpy_time;
  uint64_t g2d_time;

  struct timespec tspec;
};

struct exynos_config_default {
  uint16_t u16Width;
  uint16_t u16Height;
  enum plane_type ptype;
  uint32_t g2d_color_mode;
};

struct blit_destination {
  struct g2d_image img;

  /*
   * @damage: bounding box of the damage done when performing a blitting
   *          operation with this object as the destination.
   */
  struct bounding_box damage;

  // @aspect: aspect ratio of the destination.
  float aspect;
};

struct g2d_blitter {
  // @bo: buffer object backing the source G2D image.
  struct exynos_bo *bo;

  /*
   * @u16Width: width of the blitting source rectangle
   * @u16Height: height of the blitting source rectangle
   *
   * We only store width and height here, since the blitting rectangle
   * is always located at (0,0), i.e. there is no offset.
   */
  uint16_t u16Width;
  uint16_t u16Height;

  /*
   * @src: source image for the blitting operation.
   * @dst: blitting destination of the operation.
   */
  struct g2d_image src;
  struct blit_destination *dst;
};

struct exynos_page {
  struct exynos_page_base base;

  /*
   * @damage[]: bounding boxes for the damage done to each plane of the page.
   *            this is the damage that is currently present on the plane.
   */
  struct bounding_box damage[ePlaneTypeNum];
};

struct exynos_software_framebuffer {
  unsigned max_width, max_height;
  unsigned width, height;

  unsigned g2d_color_mode;
  unsigned stride;

  // Rectangle from previous blitting operation.
  struct g2d_rect old_rect;

  unsigned bEnabled:1;
  unsigned bConfigured:1;
};

struct exynos_data {
  struct exynos_data_base base;

  struct exynos_page *pages;

  /*
   * @g2d: G2D context used for blitting and scaling
   * @dsts: blitting destination objects (one for each plane)
   * @blitters: blitting objects (one of each image)
   */
  struct g2d_context *g2d;
  struct blit_destination dsts[ePlaneTypeNum];
  struct g2d_blitter blitters[eImageTypeNum];

  struct exynos_software_framebuffer sw_fb;

  unsigned bSync:1;

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  struct exynos_perf perf;
#endif
};

struct exynos_video {
  struct exynos_data data;

  void *font;
  const font_renderer_driver_t *font_driver;
  uint16_t font_color; // color format is ARGB4444

  unsigned color_mode;

  // current dimensions of the emulator fb
  unsigned width;
  unsigned height;

  // menu data
  unsigned menu_rotation;

  unsigned bMenuActive:1;
  unsigned bAspectChanged:1;
};


// -----------------------------------------------------------------------------------------
// Local/static variables
// -----------------------------------------------------------------------------------------

static const struct exynos_config_default defaults[eImageTypeNum] = {
  [eImageEmulator] = {
      .u16Width       = 1024,
      .u16Height      = 640,
      .ptype          = ePlanePrimary,
      .g2d_color_mode = G2D_COLOR_FMT_RGB565 | G2D_ORDER_AXRGB,
  },
  [eImageFontAtlas] = {
      .u16Width       = 0,
      .u16Height      = 0,
      .ptype          = ePlaneOverlay,
      .g2d_color_mode = G2D_COLOR_FMT_ARGB4444 | G2D_ORDER_AXRGB,
  },
  [eImageMenu] = {
      .u16Width        = 400,
      .u16Height       = 240,
      .ptype           = ePlanePrimary,
      .g2d_color_mode  = G2D_COLOR_FMT_ARGB4444 | G2D_ORDER_RGBAX,
  },
};

static const video_poke_interface_t exynos_poke_interface = {
  .cfg_sw_fb = exynos_cfg_sw_fb,
  .set_aspect_ratio = exynos_set_aspect_ratio,
  .apply_state_changes = exynos_apply_state_changes,
#if defined(HAVE_MENU)
  .set_texture_frame = exynos_set_texture_frame,
  .set_texture_enable = exynos_set_texture_enable,
#endif
  .set_osd_msg = exynos_set_osd_msg,
  .show_mouse = exynos_show_mouse
};

// -----------------------------------------------------------------------------------------
// Global variables
// -----------------------------------------------------------------------------------------

const video_driver_t video_exynos = {
  .init               = exynos_gfx_init,
  .frame              = exynos_gfx_frame,
  .set_nonblock_state = exynos_gfx_set_nonblock_state,
  .alive              = exynos_gfx_alive,
  .focus              = exynos_gfx_focus,
  .free               = exynos_gfx_free,
  .ident              = "exynos",
  .set_rotation       = exynos_gfx_set_rotation,
  .viewport_info      = exynos_gfx_viewport_info,
  .poke_interface     = exynos_gfx_get_poke_interface
};


// -----------------------------------------------------------------------------------------
// Local/static functions
// -----------------------------------------------------------------------------------------

static void
bb_clear(struct bounding_box *bb)
{
  assert(bb != NULL);

  *bb = (struct bounding_box) { 0 };
}

__attribute__((pure)) static bool
bb_empty(const struct bounding_box *bb)
{
  assert(bb != NULL);

  return bb->x == 0 && bb->y == 0 && bb->w == 0 && bb->h == 0;
}

static void
bb_merge(struct bounding_box *bb, const struct bounding_box *merge)
{
  assert(bb != NULL);
  assert(merge != NULL);

  if (merge->x < bb->x)
    bb->x = merge->x;

  if (merge->y < bb->y)
    bb->y = merge->y;

  if (merge->x + merge->w > bb->x + bb->w)
    bb->w = merge->x + merge->w - bb->x;

  if (merge->y + merge->h > bb->y + bb->h)
    bb->h = merge->y + merge->h - bb->y;
}

static void
apply_damage(struct exynos_page *page, unsigned index, const struct bounding_box *bb)
{
  assert(page != NULL);
  assert(bb != NULL);

  memcpy(&page->damage[index], bb, sizeof(struct bounding_box));
}

__attribute__((pure)) static unsigned
align_common(unsigned i, unsigned j)
{
  return (i + j - 1) & ~(j - 1);
}

__attribute__((pure)) static unsigned
colormode_to_bpp(unsigned cm)
{
  switch (cm & G2D_COLOR_FMT_MASK) {
    case G2D_COLOR_FMT_XRGB1555:
    case G2D_COLOR_FMT_ARGB4444:
    case G2D_COLOR_FMT_RGB565:
      return 2;

    case G2D_COLOR_FMT_PACKED_RGB888:
      return 3;

    case G2D_COLOR_FMT_XRGB8888:
      return 4;

    default:
      assert(false);
      return 0;
  }
}

__attribute__((pure)) static unsigned
pixelformat_to_colormode(uint32_t pf)
{
  switch (pf) {
    case DRM_FORMAT_XRGB8888:
      return G2D_COLOR_FMT_XRGB8888 | G2D_ORDER_AXRGB;

    case DRM_FORMAT_RGB565:
      return G2D_COLOR_FMT_RGB565 | G2D_ORDER_AXRGB;

    default:
      assert(false);
      return (unsigned)-1;
  }
}

/**
 * Count the number of used pages.
 *
 * @pages: pointer to array of exynos_page objects
 * @num_pages: number of entries in the pages array
 */
static unsigned
pages_used(struct exynos_page pages[], unsigned num_pages)
{
  unsigned i;
  unsigned count;

  assert(pages != NULL);

  count = 0;

  for (i = 0; i < num_pages; ++i) {
    if (pages[i].base.bUsed)
      ++count;
  }

  return count;
}

__attribute__((unused)) static const char*
image_name(enum image_type type)
{
  switch (type) {
    case eImageEmulator:
      return "emulator";

    case eImageMenu:
      return "menu";

    case eImageFontAtlas:
      return "font atlas";

    default:
      assert(false);
      return NULL;
  }
}

/**
 * Create a GEM buffer with userspace mapping. Buffer is cleared after creation.
 *
 * @dev: pointer to a exynos_device object
 * @size: size of the buffer in bytes
 */
static struct exynos_bo*
create_mapped_bo(struct exynos_device *dev, size_t size)
{
  struct exynos_bo *bo;
  void *addr;
  const unsigned flags = 0;

  assert(dev != NULL);

  bo = exynos_bo_create(dev, size, flags);
  if (bo == NULL) {
    RARCH_ERR("video_exynos: failed to create temp buffer object\n");
    return NULL;
  }

  addr = exynos_bo_map(bo);
  if (addr == NULL) {
    RARCH_ERR("video_exynos: failed to map temp buffer object\n");
    exynos_bo_destroy(bo);
    return NULL;
  }

  memset(addr, 0x00, size);

  return bo;
}

/**
 * Reallocate the underlying buffer of a blitter object.
 *
 * @dev: pointer to a exynos_device object
 * @blitter: pointer to a g2d_blitter object
 * @size: new size of the buffer (after reallocation) in bytes
 */
static int
realloc_blitter_bo(struct exynos_device *dev, struct g2d_blitter *blitter, size_t size)
{
  struct exynos_bo *oldbo, *bo;

  assert(dev != NULL);
  assert(blitter != NULL);

  oldbo = blitter->bo;

  if (size <= oldbo->size)
    return 0;

#if (EXYNOS_GFX_DEBUG_LOG == 1)
  RARCH_LOG("video_exynos: reallocating %s buffer (%u -> %u bytes)\n",
            buffer_name(type), buf->size, size);
#endif

  bo = create_mapped_bo(dev, size);
  if (bo == NULL) {
    RARCH_ERR("video_exynos: realloc_blitter_bo: out of memory\n");
    return -1;
  }

  exynos_bo_destroy(oldbo);

  // Assign new GEM buffer to the G2D image backed by it.
  blitter->bo = bo;
  blitter->src.bo[0] = bo->handle;

  return 0;
}

/**
 * Clear a buffer associated to a G2D image by doing a (fast) solid fill.
 *
 * @g2d: pointer to a g2d_context object
 * @img: pointer to a g2d_image object
 */
static int
clear_buffer(struct g2d_context *g2d, struct g2d_image *img)
{
  int ret;

  assert(g2d != NULL);
  assert(img != NULL);

  ret = g2d_solid_fill(g2d, img, 0, 0, img->width, img->height);

  if (ret == 0)
    ret = g2d_exec(g2d);

  if (ret != 0)
    RARCH_ERR("video_exynos: failed to clear buffer using G2D\n");

  return ret;
}

/**
 * Partial clear of a buffer based on old (obb) and new (nbb) boundingbox.
 *
 * @g2d: pointer to a g2d_context object
 * @img: pointer to a g2d_image object
 * @obb: pointer to a bounding_box object (the old BB)
 * @nbb: pointer to a bounding_box object (the new BB)
 */
static int
clear_buffer_bb(struct g2d_context *g2d, struct g2d_image *img,
                const struct bounding_box *obb,
                const struct bounding_box *nbb)
{
  int ret;

  assert(g2d != NULL);
  assert(img != NULL);
  assert(obb == NULL);
  assert(nbb == NULL);

  ret = 0;

  if (bb_empty(obb))
    goto out; /* nothing to clear */

  if (obb->x == 0 && nbb->x == 0) {
    if (obb->y >= nbb->y) {
      goto out; /* old bb contained in new bb */
    } else {
      const uint16_t edge_y = nbb->y + nbb->h;

      ret = g2d_solid_fill(g2d, img, 0, obb->y, img->width, nbb->y - obb->y) ||
            g2d_solid_fill(g2d, img, 0, edge_y, img->width, obb->y + obb->h - edge_y);
    }
  } else if (obb->y == 0 && nbb->y == 0) {
    if (obb->x >= nbb->x) {
      goto out; /* old bb contained in new bb */
    } else {
      const uint16_t edge_x = nbb->x + nbb->w;

      ret = g2d_solid_fill(g2d, img, obb->x, 0, nbb->x - obb->x, img->height) ||
            g2d_solid_fill(g2d, img, edge_x, 0, obb->x + obb->w - edge_x, img->height);
    }
  } else {
    // Clear the entire old boundingbox.
    ret = g2d_solid_fill(g2d, img, obb->x, obb->y, obb->w, obb->h);
  }

  if (ret == 0)
    ret = g2d_exec(g2d);

  if (ret != 0)
    RARCH_ERR("video_exynos: failed to clear buffer (bb) using G2D\n");

out:
  return ret;
}

/**
 * Partial clear of a buffer by taking the complement of the (bb) boundingbox.
 *
 * @g2d: pointer to a g2d_context object
 * @img: pointer to a g2d_image object
 * @bb: pointer to a bounding_box object
 */
static int
clear_buffer_complement(struct g2d_context *g2d, struct g2d_image *img,
                        const struct bounding_box *bb)
{
  int ret;

  assert(g2d != NULL);
  assert(img != NULL);
  assert(bb != NULL);

  ret = 0;

  if (bb->x == 0) {
    ret = g2d_solid_fill(g2d, img, 0, 0, img->width, bb->y) ||
          g2d_solid_fill(g2d, img, 0, bb->y + bb->h, img->width, img->height);
  } else if (bb->y == 0) {
    ret = g2d_solid_fill(g2d, img, 0, 0, bb->x, img->height) ||
          g2d_solid_fill(g2d, img, bb->x + bb->w, 0, img->width, img->height);
  } else {
    // Clear the entire buffer.
    ret = g2d_solid_fill(g2d, img, 0, 0, img->width, img->height);
  }

  if (ret == 0)
    ret = g2d_exec(g2d);

  if (ret != 0)
    RARCH_ERR("video_exynos: failed to clear buffer (complement) using G2D\n");

  return ret;
}

/**
 * Clear the planes of a page if necessary.
 */
static int
clear_page(struct g2d_context *g2d, struct exynos_page *page, struct blit_destination dsts[])
{
  unsigned i;

  assert(g2d != NULL);
  assert(page != NULL);
  assert(dsts != NULL);

  for (i = 0; i < ePlaneTypeNum; ++i) {
    struct exynos_plane *plane;
    struct blit_destination *dst;
    int ret;

    plane = &page->base.planes[i];
    dst = &dsts[i];

    switch (plane->ctype) {
      case eClearNone:
        continue;

      case eClearAll:
        ret = clear_buffer(g2d, &dst->img);
      break;

      case eClearPartial:
        ret = clear_buffer_bb(g2d, &dst->img, &page->damage[i], &dst->damage);
      break;

      case eClearComplement:
        ret = clear_buffer_complement(g2d, &dst->img, &page->damage[i]);
      break;

      default:
        assert(false);
        ret = -1;
    }

    if (ret < 0)
      break;

    plane->ctype = eClearNone;
  }

  if (i != ePlaneTypeNum)
    return -1;

  return 0;
}

/**
 * Flag the plane of a page for clearing.
 *
 * @page: pointer to a exynos_page object
 * @type: the plane of the page to clear
 */
static void
flag_clear_page(struct exynos_page *page, enum plane_type type)
{
  struct exynos_plane *plane;

  assert(type < ePlaneTypeNum);
  assert(page != NULL);

  plane = &page->base.planes[type];

  // Ignore the request if the plane is already flagged for clearing.
  if (plane->ctype != eClearNone)
    return;

  // Issue a partial clear for this plane.
  plane->ctype = eClearPartial;
}

/**
 * Flag the plane of a page for a forced full clear.
 *
 * @page: pointer to a exynos_page object
 * @type: the plane of the page to clear
 */
static void
force_full_clear_page(struct exynos_page *page, enum plane_type type)
{
  struct exynos_plane *plane;

  assert(type < ePlaneTypeNum);
  assert(page != NULL);

  plane = &page->base.planes[type];
  plane->ctype = eClearAll;
}

/**
 * Update the BOs of the blitting destinations based on a given page.
 *
 * @page: pointer to a exynos_page object
 * @dsts: pointer to array of blit_destination objects
 */
static void
update_blit_destination(struct exynos_page *page, struct blit_destination dsts[])
{
  unsigned i;

  assert(page != NULL);
  assert(dsts != NULL);

  for (i = 0; i < ePlaneTypeNum; ++i) {
    // Update BO handle of the destination image.
    dsts[i].img.bo[0] = page->base.planes[i].buf_id;
  }
}

/**
 * Put a font glyph at a position in the buffer that is backing the G2D font image object.
 */
static void
put_glyph_rgba4444(struct exynos_data *data, const uint8_t *__restrict__ src,
                   uint16_t color, unsigned g_width, unsigned g_height,
                   unsigned g_pitch, unsigned dst_x, unsigned dst_y)
{
#if 0
  const enum exynos_image_type buf_type = defaults[exynos_image_font].buf_type;
  const unsigned buf_width = data->src[exynos_image_font].width;

  uint16_t *__restrict__ dst = (uint16_t*)data->buf[buf_type]->vaddr +
                               dst_y * buf_width + dst_x;

  for (unsigned y = 0; y < g_height; ++y, src += g_pitch, dst += buf_width) {
    for (unsigned x = 0; x < g_width; ++x) {
      const uint16_t blend = src[x];

      dst[x] = color | ((blend << 8) & 0xf000);
    }
  }
#endif
}

#if (EXYNOS_GFX_DEBUG_PERF == 1)
static void
perf_init(struct exynos_perf *p)
{
  assert(p != NULL);

  *p = (struct exynos_perf) { 0 };
}

static void
perf_finish(struct exynos_perf *p)
{
  assert(p != NULL);

  RARCH_LOG("video_exynos: debug: total memcpy calls: %u\n", p->memcpy_calls);
  RARCH_LOG("video_exynos: debug: total g2d calls: %u\n", p->g2d_calls);

  RARCH_LOG("video_exynos: debug: total memcpy time: %f seconds\n",
            (double)p->memcpy_time / 1000000.0);
  RARCH_LOG("video_exynos: debug: total g2d time: %f seconds\n",
            (double)p->g2d_time / 1000000.0);

  RARCH_LOG("video_exynos: debug: average time per memcpy call: %f microseconds\n",
            (double)p->memcpy_time / (double)p->memcpy_calls);
  RARCH_LOG("video_exynos: debug: average time per g2d call: %f microseconds\n",
            (double)p->g2d_time / (double)p->g2d_calls);
}

static void
perf_memcpy(struct exynos_perf *p, bool start)
{
  assert(p != NULL);

  if (start) {
    clock_gettime(CLOCK_MONOTONIC, &p->tspec);
  } else {
    struct timespec new = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &new);

    p->memcpy_time += (new.tv_sec - p->tspec.tv_sec) * 1000000;
    p->memcpy_time += (new.tv_nsec - p->tspec.tv_nsec) / 1000;
    ++p->memcpy_calls;
  }
}

static void
perf_g2d(struct exynos_perf *p, bool start)
{
  assert(p != NULL);

  if (start) {
    clock_gettime(CLOCK_MONOTONIC, &p->tspec);
  } else {
    struct timespec new = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &new);

    p->g2d_time += (new.tv_sec - p->tspec.tv_sec) * 1000000;
    p->g2d_time += (new.tv_nsec - p->tspec.tv_nsec) / 1000;
    ++p->g2d_calls;
  }
}
#else
#define perf_init(p)
#define perf_finish(p)
#define perf_memcpy(p, start)
#define perf_g2d(p, start)
#endif // (EXYNOS_GFX_DEBUG_PERF == 1)

static int
additional_init(struct exynos_data *data)
{
  struct exynos_data_base *base;
  unsigned i;

  assert(data != NULL);

  base = &data->base;

  for (i = 0; i < ePlaneTypeNum; ++i) {
    struct plane_info *pinfo;

    pinfo = &base->plane_infos[i];

    data->dsts[i] = (struct blit_destination) {
      .img = (struct g2d_image) {
        .buf_type = G2D_IMGBUF_GEM,
        .color_mode = pixelformat_to_colormode(pinfo->pixel_format),

        .width = pinfo->width,
        .height = pinfo->height,
        .stride = pinfo->pitch,

        // Clear color for solid fill operation.
        .color = 0xff000000,
      },
      .aspect = (float)pinfo->width / (float)pinfo->height,
    };
  }

  for (i = 0; i < eImageTypeNum; ++i) {
    const unsigned bpp = colormode_to_bpp(defaults[i].g2d_color_mode);
    const size_t buffer_size = defaults[i].u16Width * defaults[i].u16Height * bpp;
    struct exynos_bo *bo = NULL;

    if (buffer_size != 0) {
      bo = create_mapped_bo(base->device, buffer_size);
      if (bo == NULL)
        break;
    }

    data->blitters[i] = (struct g2d_blitter) {
      .bo = bo,
      .src = {
        .width = defaults[i].u16Width,
        .height = defaults[i].u16Height,
        .stride = defaults[i].u16Width * bpp,

        .color_mode = defaults[i].g2d_color_mode,

        // Associate GEM buffer storage with G2D image.
        .buf_type = G2D_IMGBUF_GEM,
        .bo[0] = bo->handle,

        // Pad creates no border artifacts.
        .repeat_mode = G2D_REPEAT_MODE_PAD,
      },
      .dst = &data->dsts[defaults[i].ptype],
    };

    /*
     * Make sure that the storage buffer is large enough. If the code is working
     * properly, then this is just a NOP. Still put it here as an insurance.
     */
    realloc_blitter_bo(data->base.device, &data->blitters[i], buffer_size);
  }

  if (i != eImageTypeNum)
    goto fail;

  data->g2d = g2d_init(base->fd);
  if (data->g2d == NULL)
    goto fail;

  return 0;

fail:
  for (i = 0; i < eImageTypeNum; ++i) {
    exynos_bo_destroy(data->blitters[i].bo);
    data->blitters[i] = (struct g2d_blitter) { 0 };
  }

  return -1;
}

static void
additional_deinit(struct exynos_data *data)
{
  unsigned i;

  assert(data != NULL);

  g2d_fini(data->g2d);

  for (i = 0; i < eImageTypeNum; ++i) {
    exynos_bo_destroy(data->blitters[i].bo);
    data->blitters[i] = (struct g2d_blitter) { 0 };
  }
}

__attribute__((unused)) static void
alloc_status(struct exynos_data *data)
{
  // TODO: correct location?
  static const char *plane_names[ePlaneTypeNum] = {
      "primary",
      "overlay",
  };

  unsigned i;

  assert(data != NULL);
  assert(data->pages != NULL);

  RARCH_LOG("video_exynos: allocated %u pages\n", data->base.num_pages);

  for (i = 0; i < ePlaneTypeNum; ++i) {
    struct plane_info *pi;

    pi = &data->base.plane_infos[i];

    RARCH_LOG("video_exynos: %s plane has total size of %u bytes (pitch = %u bytes)\n",
              plane_names[i], pi->size, pi->pitch);
  }

  for (i = 0; i < data->base.num_pages; ++i) {
    unsigned j;
    struct exynos_page *page;

    page = &data->pages[i];

    for (j = 0; j < ePlaneTypeNum; ++j) {
      struct exynos_plane *plane;

      plane = &page->base.planes[j];

      RARCH_LOG("video_exynos: page %u: %s plane: BO at %p, buffer id = %u\n",
                i, plane_names[j], plane->bo, plane->buf_id);
    }
  }
}

/**
 * Find a free page, clear it if necessary, and return the page.
 *
 * If no free page is available when called, wait for a page flip.
 */
static struct exynos_page*
find_free_page(struct exynos_data *data)
{
  struct exynos_page *page;
  int ret;

  assert(data != NULL);

  // Wait until a free page is available.
  page = NULL;
  while (page == NULL) {
    page = (struct exynos_page*)exynos_get_free_page(&data->base);

    if (page == NULL)
      exynos_wait_for_flip(&data->base);
  }

  ret = clear_page(data->g2d, page, data->dsts);
  if (ret < 0)
    return NULL; // TODO: maybe only warning?

  page->base.bUsed = true;
  page->base.bOverlay = false;

  return page;
}

/**
 * Update the source parameter of a given blitter object.
 *
 * @data: pointer to a exynos_data object
 * @type: image type to lookup the blitter
 * @width: new source width for the blitter
 * @height: new source height for the blitter
 * @color_mode: new source color mode for the blitter
 * @pitch: new source pitch for the blitter
 */
static int
update_blitter_source(struct exynos_data *data, enum image_type type,
                      unsigned width, unsigned height,
                      unsigned color_mode, unsigned pitch)
{
  struct g2d_blitter *blitter;

  size_t size;

  assert(data != NULL);
  assert(type < eImageTypeNum);

  blitter = &data->blitters[type];

  blitter->src.width = width;
  blitter->src.height = height;
  blitter->src.color_mode = color_mode;
  blitter->src.stride = pitch;

  size = pitch * blitter->u16Height;

  if (realloc_blitter_bo(data->base.device, blitter, size) < 0)
    return -1;

  return 0;
}

/**
 * Update the config of a given blitter object.
 *
 * @data: pointer to a exynos_data object
 * @type: image type to lookup the blitter
 * @width: new width for the source blitting rectangle
 * @height: new height for the source blitting rectangle
 *
 * Updates the source blitting rectangle and the damage
 * bounding box in the destination image.
 */
static void
update_blitter_config(struct exynos_data *data, enum image_type type,
                      unsigned width, unsigned height)
{
  struct g2d_blitter *blitter;

  struct bounding_box *bb;
  const struct g2d_image *img;

  unsigned i;

  assert(data != NULL);
  assert(type < eImageTypeNum);

  blitter = &data->blitters[type];

  bb = &blitter->dst->damage;
  img = &blitter->dst->img;

  const float aspect = (float)width / (float)height;
  const float dst_aspect = blitter->dst->aspect;

  unsigned w, h;

  if (fabsf(dst_aspect - aspect) < 0.0001f) {
    w = img->width;
    h = img->height;
  } else if (dst_aspect > aspect) {
    w = (float)img->width * aspect / dst_aspect;
    h = img->height;
  } else {
    w = img->width;
    h = (float)img->height * dst_aspect / aspect;
  }

  bb->x = (img->width - w) / 2;
  bb->y = (img->height - h) / 2;
  bb->w = w;
  bb->h = h;
  blitter->u16Width = width;
  blitter->u16Height = height;

  for (i = 0; i < data->base.num_pages; ++i)
    flag_clear_page(&data->pages[i], defaults[type].ptype);
}

/**
 * Create a fake config for a given blitter object.
 *
 * @data: pointer to a exynos_data object
 * @type: image type to lookup the blitter
 *
 * Configures the blitter to do full scaling, i.e. not taking the aspect
 * ratio into account, and does a full clear of all pages.
 */
static void
fake_blitter_config(struct exynos_data *data, enum image_type type)
{
  struct g2d_blitter *blitter;

  struct bounding_box *bb;
  const struct g2d_image *img;

  unsigned i;

  assert(data != NULL);
  assert(type < eImageTypeNum);

  blitter = &data->blitters[type];

  bb = &blitter->dst->damage;
  img = &blitter->dst->img;

  bb->x = 0;
  bb->y = 0;
  bb->w = img->width;
  bb->h = img->width;

  for (i = 0; i < data->base.num_pages; ++i)
    force_full_clear_page(&data->pages[i], defaults[type].ptype);
}

static int
blit_pixels(struct g2d_context *g2d, struct g2d_blitter *blitter, const void *pixels)
{
  const struct bounding_box *bb;
  int ret;
  size_t size;

  size = blitter->src.stride * blitter->u16Height;
  bb = &blitter->dst->damage;

  perf_memcpy(&data->perf, true);

  // Without IOMMU the G2D only works properly between GEM buffers.
  memcpy_neon(blitter->bo->vaddr, pixels, size);

  perf_memcpy(&data->perf, false);

  perf_g2d(&data->perf, true);

  if (g2d_copy_with_scale(g2d, &blitter->src, &blitter->dst->img, 0, 0,
                          blitter->u16Width, blitter->u16Height,
                          bb->x, bb->y, bb->w, bb->h, 0) ||
      g2d_exec(g2d)) {
    RARCH_ERR("video_exynos: failed to blit frame\n");
    return -1;
  }

  perf_g2d(&data->perf, false);

  return 0;
}

#if defined(FIXCODE)
static int
exynos_blend_menu(struct exynos_data *data, unsigned rotation)
{
  struct g2d_image *src = &data->src[exynos_image_menu];

  perf_g2d(&data->perf, true);

  if (g2d_scale_and_blend(data->g2d, src, &data->dst, 0, 0,
                          src->width, src->height, data->blit_damage.x,
                          data->blit_damage.y, data->blit_damage.w,
                          data->blit_damage.h, G2D_OP_INTERPOLATE) ||
      g2d_exec(data->g2d)) {
    RARCH_ERR("video_exynos: failed to blend menu\n");
    return -1;
  }

  perf_g2d(&data->perf, false);

  return 0;
}

static int
exynos_blend_font(struct exynos_data *data)
{
  struct g2d_image *src = &data->src[exynos_image_font];
  struct plane_info *primary = &data->base.plane_infos[plane_primary];

  perf_g2d(&data->perf, true);

  if (g2d_scale_and_blend(data->g2d, src, &data->dst, 0, 0, src->width,
                          src->height, 0, 0, primary->width,
                          primary->height, G2D_OP_INTERPOLATE) ||
      g2d_exec(data->g2d)) {
    RARCH_ERR("video_exynos: failed to blend font\n");
    return -1;
  }

  perf_g2d(&data->perf, false);

  return 0;
}
#endif


#if defined(FIXCODE)
static int
exynos_init_font(struct exynos_video *vid)
{
  struct exynos_data *data = &vid->data;
  struct g2d_image *src = &data->src[image_font_atlas];

  const unsigned buf_height = defaults[exynos_image_font].height;
  const unsigned buf_width = align_common(data->aspect * (float)buf_height, 16);
  const unsigned buf_bpp = colormode_to_bpp(defaults[exynos_image_font].g2d_color_mode);

  if (!g_settings.video.font_enable)
    return 0;

  if (font_renderer_create_default(&vid->font_driver, &vid->font,
      *g_settings.video.font_path ? g_settings.video.font_path : NULL,
      g_settings.video.font_size)) {
    const int r = g_settings.video.msg_color_r * 15;
    const int g = g_settings.video.msg_color_g * 15;
    const int b = g_settings.video.msg_color_b * 15;

    vid->font_color = ((b < 0 ? 0 : (b > 15 ? 15 : b)) << 0) |
                      ((g < 0 ? 0 : (g > 15 ? 15 : g)) << 4) |
                      ((r < 0 ? 0 : (r > 15 ? 15 : r)) << 8);
  } else {
    RARCH_ERR("video_exynos: creating font renderer failed\n");
    return -1;
  }

  // The font buffer color type is ARGB4444.
  if (realloc_buffer(data, image_font_atlas,
                     buf_width * buf_height * buf_bpp) < 0) {
    vid->font_driver->free(vid->font);
    return -1;
  }

  src->width = buf_width;
  src->height = buf_height;
  src->stride = buf_width * buf_bpp;

#if (EXYNOS_GFX_DEBUG_LOG == 1)
  RARCH_LOG("video_exynos: using font rendering image with size %ux%u\n",
            buf_width, buf_height);
#endif

  return 0;
}

static int
exynos_render_msg(struct exynos_video *vid, const char *msg)
{
  struct exynos_data *data = &vid->data;
  struct g2d_image *dst = &data->src[exynos_image_font];

  const struct font_atlas *atlas;

  int msg_base_x = g_settings.video.msg_pos_x * dst->width;
  int msg_base_y = (1.0f - g_settings.video.msg_pos_y) * dst->height;

  if (vid->font == NULL || vid->font_driver == NULL)
    return -1;

  if (clear_buffer(data->g2d, dst) != 0)
    return -1;

  atlas = vid->font_driver->get_atlas(vid->font);

  for (; *msg; ++msg) {
    const struct font_glyph *glyph = vid->font_driver->get_glyph(vid->font, (uint8_t)*msg);
    if (glyph == NULL)
      continue;

    int base_x = msg_base_x + glyph->draw_offset_x;
    int base_y = msg_base_y + glyph->draw_offset_y;

    const int max_width  = dst->width - base_x;
    const int max_height = dst->height - base_y;

    int glyph_width  = glyph->width;
    int glyph_height = glyph->height;

    const uint8_t *src = atlas->buffer + glyph->atlas_offset_x + glyph->atlas_offset_y * atlas->width;

    if (base_x < 0) {
       src -= base_x;
       glyph_width += base_x;
       base_x = 0;
    }

    if (base_y < 0) {
       src -= base_y * (int)atlas->width;
       glyph_height += base_y;
       base_y = 0;
    }

    if (max_width <= 0 || max_height <= 0) continue;

    if (glyph_width > max_width) glyph_width = max_width;
    if (glyph_height > max_height) glyph_height = max_height;

    put_glyph_rgba4444(data, src, vid->font_color,
                       glyph_width, glyph_height,
                       atlas->width, base_x, base_y);

    msg_base_x += glyph->advance_x;
    msg_base_y += glyph->advance_y;
  }

  return exynos_blend_font(data);
}
#endif


// -----------------------------------------------------------------------------------------
// Software framebuffer API
// -----------------------------------------------------------------------------------------

static bool
swfb_set_format(void *ctx, enum retro_pixel_format pixel_format,
                unsigned width, unsigned height)
{
  struct exynos_data *data;

  int ret;
  unsigned color_mode;

  assert(ctx != NULL);

  data = ctx;

  if (!data->sw_fb.bEnabled)
    return false;

  // Translate from libretro pixelformat to G2D colormode.
  switch (pixel_format) {
    case RETRO_PIXEL_FORMAT_0RGB1555:
      color_mode = G2D_COLOR_FMT_XRGB1555 | G2D_ORDER_AXRGB;
    break;

    case RETRO_PIXEL_FORMAT_XRGB8888:
      color_mode = G2D_COLOR_FMT_XRGB8888 | G2D_ORDER_AXRGB;
    break;

    case RETRO_PIXEL_FORMAT_RGB565:
      color_mode = G2D_COLOR_FMT_RGB565 | G2D_ORDER_AXRGB;
    break;

    case RETRO_PIXEL_FORMAT_XBGR1555:
      color_mode = G2D_COLOR_FMT_XRGB1555 | G2D_ORDER_AXBGR;
    break;

    case RETRO_PIXEL_FORMAT_PACKED_BGR888:
      color_mode = G2D_COLOR_FMT_PACKED_RGB888 | G2D_ORDER_BGRAX;
    break;

    default:
      RARCH_ERR("video_exynos: software framebuffer: unsupported pixel format (0x%x)\n",
                pixel_format);
      return false;
  }

  if (width > data->sw_fb.max_width || height > data->sw_fb.max_height)
    return false;

  RARCH_LOG("video_exynos: software framebuffer: format = %ux%u (0x%x) -> %ux%u (0x%x)\n",
    data->sw_fb.width, data->sw_fb.height, data->sw_fb.g2d_color_mode,
    width, height, color_mode);

  data->sw_fb.g2d_color_mode = color_mode;
  data->sw_fb.stride = width * colormode_to_bpp(color_mode);
  data->sw_fb.width = width;
  data->sw_fb.height = height;

  ret = update_blitter_source(data, eImageEmulator, width, height,
                              color_mode, data->sw_fb.stride);
  if (ret < 0)
    return false;

  data->sw_fb.bConfigured = true;

  return true;
}

static bool
swfb_get_current_addr(void *ctx, void **addr, size_t *pitch)
{
  struct exynos_data *data;
  struct g2d_blitter *blitter;

  int ret;

  assert(ctx != NULL);
  data = ctx;

  blitter = &data->blitters[eImageEmulator];

  if (!data->sw_fb.bConfigured)
    return false;

  const unsigned size = data->sw_fb.stride * data->sw_fb.height;

  ret = realloc_blitter_bo(data->base.device, blitter, size);
  if (ret < 0)
    return false;

  *addr = blitter->bo->vaddr;
  *pitch = data->sw_fb.stride;

  return true;
}

static bool
swfb_video_refresh(void *ctx, const struct retro_rectangle *rect)
{
  struct exynos_data *data = ctx;
  struct g2d_blitter *blitter = &data->blitters[eImageEmulator];
  const struct bounding_box *bb = &blitter->dst->damage;
  struct exynos_software_framebuffer *sw_fb = &data->sw_fb;

  struct exynos_page *page;

  if (!sw_fb->bConfigured)
    return false;

  /*
   * struct g2d_rect and struct retro_rectangle are compatible, so
   * it is safe to cast the pointer here.
   */
  if (rect == NULL)
    rect = (struct retro_rectangle*)&sw_fb->old_rect;

  if (sw_fb->old_rect.w != rect->w || sw_fb->old_rect.h != rect->h) {
    update_blitter_config(data, eImageEmulator, rect->w, rect->h);

    sw_fb->old_rect = (struct g2d_rect) {
      .x = rect->x,
      .y = rect->y,
      .w = rect->w,
      .h = rect->h
    };
  }

  page = find_free_page(data);
  // TODO: handle page == NULL

  update_blit_destination(page, data->dsts);

  /*
   * TODO:
   * - add menu rendering
   * - add font rendering
   * - integrate soft filter
   */

  if (rect->w == 0 || rect->h == 0)
    goto skip_blit;

  /*
   * This clips the blitting rectangle, should it be
   * not inside [0, w] x [0, h].
   */
  if (g2d_copy_with_scale(data->g2d, &blitter->src, &blitter->dst->img,
                          rect->x, rect->y, rect->w, rect->h,
                          bb->x, bb->y, bb->w, bb->h, 0) ||
      g2d_exec(data->g2d)) {
    RARCH_ERR("video_exynos: software framebuffer: failed to blit\n");
    return false;
  }

  apply_damage(page, 0, bb);

skip_blit:
  if (exynos_issue_flip(&data->base, &page->base) < 0)
    goto fail;

  g_extern.frame_count++;

  return true;

fail:
  page->base.bUsed = false;

  return false;
}


// -----------------------------------------------------------------------------------------
// RetroArch GFX API
// -----------------------------------------------------------------------------------------

static void*
exynos_gfx_init(const video_info_t *video, const input_driver_t **input, void **input_data)
{
  struct exynos_video *vid;
  struct exynos_data *data;

  int ret;
  unsigned i;

  vid = calloc(1, sizeof(struct exynos_video));
  if (vid == NULL) {
    RARCH_ERR("video_exynos: failed to allocate exynos_video\n");
    return NULL;
  }

  data = &vid->data;

  if (video->rgb32)
    vid->color_mode = G2D_COLOR_FMT_XRGB8888 | G2D_ORDER_AXRGB;
  else
    vid->color_mode = G2D_COLOR_FMT_RGB565 | G2D_ORDER_AXRGB;

  data->base.fd = -1;
  data->base.plane_infos[ePlanePrimary].pixel_format = DRM_FORMAT_XRGB8888;
  data->base.plane_infos[ePlaneOverlay].pixel_format = DRM_FORMAT_ARGB4444;

  ret = exynos_open(&data->base);
  if (ret < 0) {
    RARCH_ERR("video_exynos: failed to open (%d)\n", ret);
    goto fail;
  }

  ret = exynos_init(&data->base);
  if (ret < 0) {
    RARCH_ERR("video_exynos: failed to init (%d)\n", ret);
    goto fail_init;
  }

  data->pages = calloc(eNumPages, sizeof(struct exynos_page));
  if (data->pages == NULL) {
    RARCH_ERR("video_exynos: failed to allocate pages\n");
    goto fail_pages;
  }

  for (i = 0; i < eNumPages; ++i) {
      ret = exynos_register_page(&data->base, &data->pages[i].base);
      if (ret < 0) {
          RARCH_ERR("video_exynos: failed to register page (%d)\n", ret);
          break;
      }
  }

  if (i != eNumPages)
      goto fail_register_pages;

  ret = exynos_alloc(&data->base);
  if (ret < 0) {
    RARCH_ERR("video_exynos: failed to alloc (%d)\n", ret);
    goto fail_alloc;
  }

  ret = additional_init(data);
  if (ret < 0) {
    RARCH_ERR("video_exynos: additional initialization failed (%d)\n", ret);
    goto fail_add;
  }

#if (EXYNOS_GFX_DEBUG_LOG == 1)
  exynos_alloc_status(data);
#endif

  perf_init(&data->perf);

  if (input != NULL && input_data != NULL)
    *input = NULL;

#if defined(FIXCODE)
  ret = exynos_init_font(vid);
  if (ret < 0) {
    RARCH_ERR("video_exynos: font initialization failed (%d)\n", ret);
    goto fail_font;
  }
#endif

  return vid;

fail_font:
  additional_deinit(&vid->data);

fail_add:
  exynos_free(&data->base);

fail_alloc:
fail_register_pages:
  exynos_unregister_pages(&data->base);
  free(data->pages);

fail_pages:
  exynos_deinit(&data->base);

fail_init:
  exynos_close(&data->base);

fail:
  free(vid);

  return NULL;
}

static void
exynos_gfx_free(void *driver_data)
{
  struct exynos_video *vid;
  struct exynos_data *data;

  vid = driver_data;
  if (vid == NULL)
    return;

  data = &vid->data;

  additional_deinit(data);

  // Flush pages: One page remains, the one being displayed at this moment.
  while (pages_used(data->pages, data->base.num_pages) > 1)
    exynos_wait_for_flip(&data->base);

  exynos_free(&data->base);
  exynos_unregister_pages(&data->base);
  free(data->pages);
  exynos_deinit(&data->base);
  exynos_close(&data->base);

  perf_finish(&data->perf);

  free(data);

  if (vid->font != NULL && vid->font_driver != NULL)
    vid->font_driver->free(vid->font);

  free(vid);
}

static bool
exynos_gfx_frame(void *driver_data, const void *frame, unsigned width,
                 unsigned height, unsigned pitch, const char *msg)
{
  struct exynos_video *vid;
  struct exynos_data *data;

  struct exynos_page *page;

  assert(driver_data != NULL);
  vid = driver_data;

  data = &vid->data;

  if (data->sw_fb.bEnabled)
    return false;

  // Check if neither menu nor emulator framebuffer is to be displayed.
  if (!vid->bMenuActive && frame == NULL)
    return true;

  page = NULL;

  if (frame != NULL) {
    int ret;

    if (width != vid->width || height != vid->height) {
      // Sanity check on new dimension parameters.
      if (width == 0 || height == 0)
        return true;

      RARCH_LOG("video_exynos: resolution changed by core: %ux%u -> %ux%u\n",
                vid->width, vid->height, width, height);
      update_blitter_config(data, eImageEmulator, width, height);

      vid->width = width;
      vid->height = height;
    }

    page = find_free_page(data);
    // TODO: handle page == NULL

    update_blit_destination(page, data->dsts);

    // TODO. check page for NULL

    ret = update_blitter_source(data, eImageEmulator, vid->width, vid->height,
                                vid->color_mode, pitch);

    if (blit_pixels(data->g2d, &data->blitters[eImageEmulator], frame) != 0)
      goto fail;
  }

  if (g_settings.fps_show) {
    char buffer[128], buffer_fps[128];

    gfx_get_fps(buffer, sizeof(buffer), g_settings.fps_show ? buffer_fps : NULL, sizeof(buffer_fps));
    msg_queue_push(g_extern.msg_queue, buffer_fps, 1, 1);
  }

  if (vid->width == 0 || vid->height == 0) {
    /*
     * If at this point the dimension parameters are still zero, setup some
     * fake blit parameters so that menu and font rendering work properly.
     */
    fake_blitter_config(data, eImageEmulator);
  }

  if (page == NULL) {
    page = find_free_page(data);
    // TODO: handle page == NULL

    update_blit_destination(page, data->dsts);
  }

  if (vid->bMenuActive) {
    if (exynos_blend_menu(data, vid->menu_rotation) != 0)
      goto fail;
  }

#if defined(FIXCODE)
  if (msg != NULL) {
    if (exynos_render_msg(vid, msg) < 0)
      goto fail;

    // Font is blitted to the entire screen, so issue clear afterwards.
    page->base.flags |= (page_clear | page_clear_compl);
    page->base.flags &= ~page_clear_all;
  }
#endif

  apply_damage(page, 0, &data->blit_damage);

  if (exynos_issue_flip(&data->base, &page->base) < 0)
    goto fail;

  g_extern.frame_count++;

  return true;

fail:
  /*
   * Since we didn't manage to issue a pageflip to this page, set
   * it to 'unused' again, and hope that it works next time.
   */
  page->base.bUsed = false;

  return false;
}

static void
exynos_gfx_set_nonblock_state(void *driver_data, bool state)
{
  struct exynos_video *vid;

  assert(driver_data != NULL);
  vid = driver_data;

  vid->data.bSync = !state;
}

static bool
exynos_gfx_alive(void *driver_data)
{
  (void)driver_data;
  return true; // always alive
}

static bool
exynos_gfx_focus(void *driver_data)
{
  (void)driver_data;
  return true; // DRM device always has focus
}

static void
exynos_gfx_set_rotation(void *driver_data, unsigned rotation)
{
  struct exynos_video *vid = driver_data;

  vid->menu_rotation = rotation;
}

static void
exynos_gfx_viewport_info(void *driver_data, struct rarch_viewport *vp)
{
  struct exynos_video *vid;

  assert(driver_data != NULL);
  vid = driver_data;

  assert(vp != NULL);

  vp->x = 0;
  vp->y = 0;

  vp->width  = vid->width;
  vp->height = vid->height;
  vp->full_width  = vid->width;
  vp->full_height = vid->height;
}

static bool
exynos_cfg_sw_fb(void *driver_data, struct retro_framebuffer_config *fb_cfg)
{
  static const enum retro_pixel_format formats[] = {
    RETRO_PIXEL_FORMAT_0RGB1555,
    RETRO_PIXEL_FORMAT_XRGB8888,
    RETRO_PIXEL_FORMAT_RGB565,
    RETRO_PIXEL_FORMAT_XBGR1555,
    RETRO_PIXEL_FORMAT_PACKED_BGR888
  };

  struct exynos_video *vid;
  struct exynos_data *data;

  assert(driver_data != NULL);
  vid = driver_data;

  data = &vid->data;

  fprintf(stderr, "DEBUG: exynos_cfg_sw_fb()\n");

  // Disable software framebuffer support if requested.
  if (fb_cfg->max_width == 0 || fb_cfg->max_height == 0) {
    struct g2d_image *src = &data->src[eImageEmulator];

    *fb_cfg = (struct retro_framebuffer_config) { 0 };

    data->sw_fb.bEnabled = false;
    data->sw_fb.bConfigured = false;

    // Restore the color mode of the temporary G2D image.
    src->color_mode = defaults[eImageEmulator].g2d_color_mode;

    /*
     * This triggers a scaling reconfig event the next
     * time exynos_gfx_frame() is called.
     */
    vid->width = 0;
    vid->height = 0;

    return true;
  }

  // Check against G2D hardware limitations.
  if (fb_cfg->max_width >= eHWLimitG2D || fb_cfg->max_height >= eHWLimitG2D)
    return false;

  data->sw_fb.max_width = fb_cfg->max_width;
  data->sw_fb.max_height = fb_cfg->max_height;

  fb_cfg->framebuffer_context = data;

  // Set the function pointers.
  fb_cfg->set_format = swfb_set_format;
  fb_cfg->get_current_addr = swfb_get_current_addr;
  fb_cfg->video_refresh = swfb_video_refresh;

  fb_cfg->num_formats = sizeof(formats) / sizeof(formats[0]);
  fb_cfg->formats = formats;

  data->sw_fb.bEnabled = true;
  data->sw_fb.bConfigured = false;

  return true;
}

static void
exynos_set_aspect_ratio(void *driver_data, unsigned aspect_ratio_idx)
{
  struct exynos_video *vid;
  struct retro_game_geometry *geom;

  assert(driver_data != NULL);
  vid = driver_data;

  switch (aspect_ratio_idx) {
    case ASPECT_RATIO_SQUARE:
      geom = &g_extern.system.av_info.geometry;
      gfx_set_square_pixel_viewport(geom->base_width, geom->base_height);
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

  g_extern.system.aspect_ratio = aspectratio_lut[aspect_ratio_idx].value;
  vid->bAspectChanged = true;
}

static void
exynos_apply_state_changes(void *driver_data)
{
  (void)driver_data;
}

static void
exynos_set_texture_frame(void *driver_data, const void *frame, bool rgb32,
                         unsigned width, unsigned height, float alpha)
{
  struct exynos_video *vid = driver_data;
  struct exynos_data *data = &vid->data;
  struct g2d_blitter *blitter = &data->blitters[eImageMenu];

  uint32_t pitch, color_mode;
  int ret;

  pitch = width * (rgb32 ? 4 : 2);
  color_mode = rgb32 ? G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_RGBAX :
                       G2D_COLOR_FMT_ARGB4444 | G2D_ORDER_RGBAX;

  ret = update_blitter_source(data, eImageMenu, width, height, color_mode, pitch);
  if (ret < 0)
    return; // TODO: print warning

  blitter->src.component_alpha = 255.0f * alpha;

  //const enum exynos_buffer_type buf_type = defaults[exynos_image_menu].buf_type;


#if 0
    update_blitter_source(struct exynos_data *data, enum image_type type,
                          unsigned width, unsigned height,
                          unsigned color_mode, unsigned pitch)
#endif

  //struct g2d_image *src = &data->src[exynos_image_menu];


#if 0
  const unsigned size = width * height * (rgb32 ? 4 : 2);

  if (realloc_blitter_bo(data->base.device, image_menu, size) < 0)
    return;

  src->width = width;
  src->height = height;
  src->stride = width * (rgb32 ? 4 : 2);
  src->color_mode = rgb32 ? G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_RGBAX :
                            G2D_COLOR_FMT_ARGB4444 | G2D_ORDER_RGBAX;
#endif

  //src->

  perf_memcpy(&data->perf, true);

  memcpy_neon(data->buf[buf_type]->vaddr, frame, size);

  perf_memcpy(&data->perf, false);
}

static void
exynos_set_texture_enable(void *driver_data, bool state, bool full_screen)
{
  struct exynos_video *vid;

  assert(driver_data != NULL);
  vid = driver_data;

  vid->bMenuActive = state;
}

static void
exynos_set_osd_msg(void *driver_data, const char *msg, const struct font_params *params)
{
  struct exynos_video *vid;

  assert(driver_data != NULL);
  vid = driver_data;

  /* TODO: what does this do? */
  (void)msg;
  (void)params;
}

static void
exynos_show_mouse(void *driver_data, bool state)
{
  (void)driver_data;
}

static void
exynos_gfx_get_poke_interface(void *driver_data, const video_poke_interface_t **iface)
{
  (void)driver_data;
  *iface = &exynos_poke_interface;
}

