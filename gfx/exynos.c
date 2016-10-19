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

extern void *memcpy_neon(void *dst, const void *src, size_t n);

typedef unsigned short ushort;

typedef union exynos_boundingbox {
  struct {
    ushort x, y;
    ushort w, h;
  };
  uint64_t data;
} exynos_boundingbox_t;

/* We use two GEM buffers (main and aux) to handle 'data' from the frontend. */
enum exynos_buffer_type {
  exynos_buffer_main = 0,
  exynos_buffer_aux,
  exynos_buffer_count
};

/* We have to handle three types of 'data' from the frontend, each abstracted by a *
 * G2D image object. The image objects are then backed by some storage buffer.     *
 * (1) the emulator framebuffer (backed by main buffer)                            *
 * (2) the menu buffer (backed by aux buffer)                                      *
 * (3) the font rendering buffer (backed by aux buffer)                            */
enum exynos_image_type {
  exynos_image_frame = 0,
  exynos_image_font,
  exynos_image_menu,
  exynos_image_count
};

static const struct exynos_config_default {
  unsigned width, height;
  enum exynos_buffer_type buf_type;
  unsigned g2d_color_mode;
} defaults[exynos_image_count] = {
  {1024, 640, exynos_buffer_main, G2D_COLOR_FMT_RGB565   | G2D_ORDER_AXRGB}, /* frame */
  {720,  368, exynos_buffer_aux,  G2D_COLOR_FMT_ARGB4444 | G2D_ORDER_AXRGB}, /* font */
  {400,  240, exynos_buffer_aux,  G2D_COLOR_FMT_ARGB4444 | G2D_ORDER_RGBAX}  /* menu */
};

#if (EXYNOS_GFX_DEBUG_PERF == 1)
struct exynos_perf {
  unsigned memcpy_calls;
  unsigned g2d_calls;

  unsigned long long memcpy_time;
  unsigned long long g2d_time;

  struct timespec tspec;
};
#endif

enum exynos_page_flags {
  /*
   * If set the page needs a full clear, otherwise only
   * a partial clear suffices.
   */
  page_clear_all   = (base_flag << 0),

  /* If set the partial clear is of complement type. */
  page_clear_compl = (base_flag << 1)
};

struct exynos_page {
  struct exynos_page_base base;

  /*
   * Track damage done by blit operations (damage[0])
   * and damage by font rendering (damage[1]).
   */
  exynos_boundingbox_t damage[2];
};

struct exynos_data {
  struct exynos_data_base base;

  /* BOs backing the G2D images. */
  struct exynos_bo *buf[exynos_buffer_count];

  /* G2D is used for scaling to framebuffer dimensions. */
  struct g2d_context *g2d;
  struct g2d_image dst;
  struct g2d_image src[exynos_image_count];

  /* framebuffer aspect ratio */
  float aspect;

  /* parameters for blitting emulator fb to screen */
  exynos_boundingbox_t blit_damage;
  ushort blit_w, blit_h;

  bool sync;

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  struct exynos_perf perf;
#endif
};


static inline void exynos_boundingbox_clear(exynos_boundingbox_t *bb) {
  bb->data = 0;
}

static inline bool exynos_boundingbox_empty(const exynos_boundingbox_t *bb) {
  return (bb->data == 0);
}

static void exynos_boundingbox_merge(exynos_boundingbox_t *bb,
                                     const exynos_boundingbox_t* merge) {
  if (merge->x < bb->x) bb->x = merge->x;
  if (merge->y < bb->y) bb->y = merge->y;
  if (merge->x + merge->w > bb->x + bb->w)
    bb->w = merge->x + merge->w - bb->x;
  if (merge->y + merge->h > bb->y + bb->h)
    bb->h = merge->y + merge->h - bb->y;
}

static inline void apply_damage(struct exynos_page *p, unsigned idx,
                                const exynos_boundingbox_t *bb) {
  p->damage[idx].data = bb->data;
}

static inline unsigned align_common(unsigned i, unsigned j) {
  return (i + j - 1) & ~(j - 1);
}

static unsigned colormode_to_bpp(unsigned cm) {
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

static unsigned pixelformat_to_colormode(uint32_t pf) {
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


static struct exynos_page *get_free_page(struct exynos_page *p, unsigned cnt) {
  for (unsigned i = 0; i < cnt; ++i) {
    if (p[i].base.flags & page_used)
      continue;

    return &p[i];
  }

  return NULL;
}

/* Count the number of used pages. */
static unsigned pages_used(struct exynos_page *p, unsigned cnt) {
  unsigned count = 0;

  for (unsigned i = 0; i < cnt; ++i) {
    if (p[i].base.flags & page_used)
      ++count;
  }

  return count;
}

#if (EXYNOS_GFX_DEBUG_LOG == 1)
static const char *buffer_name(enum exynos_buffer_type type) {
  switch (type) {
    case exynos_buffer_main:
      return "main";

    case exynos_buffer_aux:
      return "aux";

    default:
      assert(false);
      return NULL;
  }
}
#endif

/* Create a GEM buffer with userspace mapping. Buffer is cleared after creation. */
static struct exynos_bo *create_mapped_buffer(struct exynos_device *dev, unsigned size) {
  struct exynos_bo *buf;
  const unsigned flags = 0;

  buf = exynos_bo_create(dev, size, flags);
  if (!buf) {
    RARCH_ERR("video_exynos: failed to create temp buffer object\n");
    return NULL;
  }

  if (!exynos_bo_map(buf)) {
    RARCH_ERR("video_exynos: failed to map temp buffer object\n");
    exynos_bo_destroy(buf);
    return NULL;
  }

  memset(buf->vaddr, 0, size);

  return buf;
}

static int realloc_buffer(struct exynos_data *pdata,
                          enum exynos_buffer_type type, unsigned size) {
  struct exynos_bo *buf = pdata->buf[type];

  if (size > buf->size) {
#if (EXYNOS_GFX_DEBUG_LOG == 1)
    RARCH_LOG("video_exynos: reallocating %s buffer (%u -> %u bytes)\n",
              buffer_name(type), buf->size, size);
#endif

    exynos_bo_destroy(buf);
    buf = create_mapped_buffer(pdata->base.device, size);

    if (!buf) {
      RARCH_ERR("video_exynos: reallocation failed\n");
      return -1;
    }

    pdata->buf[type] = buf;

    /* Map new GEM buffer to the G2D images backed by it. */
    for (unsigned i = 0; i < exynos_image_count; ++i) {
      if (defaults[i].buf_type == type)
        pdata->src[i].bo[0] = buf->handle;
    }
  }

  return 0;
}

/* Clear a buffer associated to a G2D image by doing a (fast) solid fill. */
static int clear_buffer(struct g2d_context *g2d, struct g2d_image *img) {
  int ret;

  ret = g2d_solid_fill(g2d, img, 0, 0, img->width, img->height);

  if (!ret)
    ret = g2d_exec(g2d);

  if (ret)
    RARCH_ERR("video_exynos: failed to clear buffer using G2D\n");

  return ret;
}

/* Partial clear of a buffer based on old (obb) and new (nbb) boundingbox. */
static int clear_buffer_bb(struct g2d_context *g2d, struct g2d_image *img,
  const exynos_boundingbox_t *obb, const exynos_boundingbox_t *nbb) {
  int ret = 0;

  if (exynos_boundingbox_empty(obb))
    goto out; /* nothing to clear */

  if (obb->x == 0 && nbb->x == 0) {
    if (obb->y >= nbb->y) {
      goto out; /* old bb contained in new bb */
    } else {
      const ushort edge_y = nbb->y + nbb->h;

      ret = g2d_solid_fill(g2d, img, 0, obb->y, img->width, nbb->y - obb->y) ||
            g2d_solid_fill(g2d, img, 0, edge_y, img->width, obb->y + obb->h - edge_y);
    }
  } else if (obb->y == 0 && nbb->y == 0) {
    if (obb->x >= nbb->x) {
      goto out; /* old bb contained in new bb */
    } else {
      const ushort edge_x = nbb->x + nbb->w;

      ret = g2d_solid_fill(g2d, img, obb->x, 0, nbb->x - obb->x, img->height) ||
            g2d_solid_fill(g2d, img, edge_x, 0, obb->x + obb->w - edge_x, img->height);
    }
  } else {
    /* Clear the entire old boundingbox. */
    ret = g2d_solid_fill(g2d, img, obb->x, obb->y, obb->w, obb->h);
  }

  if (!ret)
    ret = g2d_exec(g2d);

  if (ret)
    RARCH_ERR("video_exynos: failed to clear buffer (bb) using G2D\n");

out:
  return ret;
}

/* Partial clear of a buffer by taking the complement of the (bb) boundingbox. */
static int clear_buffer_complement(struct g2d_context *g2d, struct g2d_image *img,
  const exynos_boundingbox_t *bb) {
  int ret = 0;

  if (bb->x == 0) {
    ret = g2d_solid_fill(g2d, img, 0, 0, img->width, bb->y) ||
          g2d_solid_fill(g2d, img, 0, bb->y + bb->h, img->width, img->height);
  } else if (bb->y == 0) {
    ret = g2d_solid_fill(g2d, img, 0, 0, bb->x, img->height) ||
          g2d_solid_fill(g2d, img, bb->x + bb->w, 0, img->width, img->height);
  } else {
    /* Clear the entire buffer. */
    ret = g2d_solid_fill(g2d, img, 0, 0, img->width, img->height);
  }

  if (!ret)
    ret = g2d_exec(g2d);

  if (ret)
    RARCH_ERR("video_exynos: failed to clear buffer (complement) using G2D\n");

  return ret;
}

/* Put a font glyph at a position in the buffer that is backing the G2D font image object. */
static void put_glyph_rgba4444(struct exynos_data *pdata, const uint8_t *__restrict__ src,
                               uint16_t color, unsigned g_width, unsigned g_height,
                               unsigned g_pitch, unsigned dst_x, unsigned dst_y) {
  const enum exynos_image_type buf_type = defaults[exynos_image_font].buf_type;
  const unsigned buf_width = pdata->src[exynos_image_font].width;

  uint16_t *__restrict__ dst = (uint16_t*)pdata->buf[buf_type]->vaddr +
                               dst_y * buf_width + dst_x;

  for (unsigned y = 0; y < g_height; ++y, src += g_pitch, dst += buf_width) {
    for (unsigned x = 0; x < g_width; ++x) {
      const uint16_t blend = src[x];

      dst[x] = color | ((blend << 8) & 0xf000);
    }
  }
}

#if (EXYNOS_GFX_DEBUG_PERF == 1)
static void perf_init(struct exynos_perf *p) {
  p->memcpy_calls = 0;
  p->g2d_calls = 0;

  p->memcpy_time = 0;
  p->g2d_time = 0;

  memset(&p->tspec, 0, sizeof(struct timespec));
}

static void perf_finish(struct exynos_perf *p) {
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

static void perf_memcpy(struct exynos_perf *p, bool start) {
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

static void perf_g2d(struct exynos_perf *p, bool start) {
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
#endif

static int exynos_additional_init(struct exynos_data *pdata) {
  struct g2d_context *g2d;
  unsigned i;

  for (i = 0; i < exynos_buffer_count; ++i) {
    const unsigned bpp = colormode_to_bpp(defaults[i].g2d_color_mode);
    const unsigned buffer_size = defaults[i].width * defaults[i].height * bpp;
    struct exynos_bo *bo;

    bo = create_mapped_buffer(pdata->base.device, buffer_size);
    if (!bo)
      break;

    pdata->buf[i] = bo;
  }

  if (i != exynos_buffer_count) {
    while (i-- > 0) {
      exynos_bo_destroy(pdata->buf[i]);
      pdata->buf[i] = NULL;
    }

    return -1;
  }

  g2d = g2d_init(pdata->base.fd);
  if (!g2d)
    goto init_fail;

  pdata->dst = (struct g2d_image){
    .buf_type = G2D_IMGBUF_GEM,
    .color_mode = pixelformat_to_colormode(pdata->base.pixel_format),

    .width = pdata->base.width,
    .height = pdata->base.height,
    .stride = pdata->base.pitch,

    /* Clear color for solid fill operation. */
    .color = 0xff000000
  };

  for (i = 0; i < exynos_image_count; ++i) {
    const enum exynos_buffer_type buf_type = defaults[i].buf_type;
    const unsigned bpp = colormode_to_bpp(defaults[i].g2d_color_mode);
    const unsigned buf_size = defaults[i].width * defaults[i].height * bpp;

    pdata->src[i] = (struct g2d_image){
      .width = defaults[i].width,
      .height = defaults[i].height,
      .stride = defaults[i].width * bpp,

      .color_mode = defaults[i].g2d_color_mode,

      /* Associate GEM buffer storage with G2D image. */
      .buf_type = G2D_IMGBUF_GEM,
      .bo[0] = pdata->buf[buf_type]->handle,

      /* Pad creates no border artifacts. */
      .repeat_mode = G2D_REPEAT_MODE_PAD
    };

    /* Make sure that the storage buffer is large enough. If the code is working *
     * properly, then this is just a NOP. Still put it here as an insurance.     */
    realloc_buffer(pdata, buf_type, buf_size);
  }

  pdata->g2d = g2d;

  pdata->aspect = (float)pdata->base.width / (float)pdata->base.height;

  return 0;

init_fail:
  for (i = 0; i < exynos_buffer_count; ++i) {
    exynos_bo_destroy(pdata->buf[i]);
    pdata->buf[i] = NULL;
  }

  return -1;
}

static void exynos_additional_deinit(struct exynos_data *pdata) {
  unsigned i;

  g2d_fini(pdata->g2d);

  for (i = 0; i < exynos_buffer_count; ++i) {
    exynos_bo_destroy(pdata->buf[i]);
    pdata->buf[i] = NULL;
  }
}

#if (EXYNOS_GFX_DEBUG_LOG == 1)
static void exynos_alloc_status(struct exynos_data *pdata) {
  struct exynos_page *pages = pdata->pages;

  RARCH_LOG("video_exynos: allocated %u pages with %u bytes each (pitch = %u bytes)\n",
            pdata->num_pages, pdata->size, pdata->pitch);

  for (unsigned i = 0; i < pdata->num_pages; ++i) {
    RARCH_LOG("video_exynos: page %u: BO at %p, buffer id = %u\n",
              i, pages[i].bo, pages[i].buf_id);
  }
}
#endif

/* Find a free page, clear it if necessary, and return the page. If  *
 * no free page is available when called, wait for a page flip.      */
static struct exynos_page *exynos_free_page(struct exynos_data *pdata) {
  struct exynos_page *page = NULL;
  struct g2d_image *dst = &pdata->dst;

  /* Wait until a free page is available. */
  while (!page) {
    page = get_free_page(pdata->base.pages, pdata->base.num_pages);

    if (!page)
      exynos_wait_for_flip(&pdata->base);
  }

  dst->bo[0] = page->base.bo->handle;

  /* Check if we have to clear the page. */
  if (page->base.flags & page_clear) {
    int ret;

    if (page->base.flags & page_clear_all)
      ret = clear_buffer(pdata->g2d, dst);
    else if (page->base.flags & page_clear_compl)
      ret = clear_buffer_complement(pdata->g2d, dst, &page->damage[0]);
    else
      ret = clear_buffer_bb(pdata->g2d, dst, &page->damage[0], &pdata->blit_damage);

    if (!ret)
      page->base.flags &= ~(page_clear | page_clear_all | page_clear_compl);
  }

  page->base.flags |= page_used;
  return page;
}

static void exynos_setup_blit_src(struct exynos_data *pdata, unsigned width,
                                  unsigned height, unsigned color_mode,
                                  unsigned pitch) {
  struct g2d_image *src = &pdata->src[exynos_image_frame];

  src->width = width;
  src->height = height;
  src->color_mode = color_mode;
  src->stride = pitch;
}

static void exynos_setup_scale(struct exynos_data *pdata,
                               unsigned width, unsigned height) {
  struct exynos_page *pages = pdata->base.pages;
  unsigned w, h;

  const float aspect = (float)width / (float)height;

  if (fabsf(pdata->aspect - aspect) < 0.0001f) {
    w = pdata->base.width;
    h = pdata->base.height;
  } else {
    if (pdata->aspect > aspect) {
      w = (float)pdata->base.width * aspect / pdata->aspect;
      h = pdata->base.height;
    } else {
      w = pdata->base.width;
      h = (float)pdata->base.height * pdata->aspect / aspect;
    }
  }

  pdata->blit_damage.x = (pdata->base.width - w) / 2;
  pdata->blit_damage.y = (pdata->base.height - h) / 2;
  pdata->blit_damage.w = w;
  pdata->blit_damage.h = h;
  pdata->blit_w = width;
  pdata->blit_h = height;

  for (unsigned i = 0; i < pdata->base.num_pages; ++i) {
    if (pages[i].base.flags & page_clear)
      continue;

    /* Issue a partial clear for this page. */
    pages[i].base.flags |= page_clear;
    pages[i].base.flags &= ~(page_clear_all | page_clear_compl);
  }
}

static void exynos_set_fake_blit(struct exynos_data *pdata) {
  struct exynos_page *pages = pdata->base.pages;

  pdata->blit_damage.x = 0;
  pdata->blit_damage.y = 0;
  pdata->blit_damage.w = pdata->base.width;
  pdata->blit_damage.h = pdata->base.height;

  /* For all pages issue a full clear. */
  for (unsigned i = 0; i < pdata->base.num_pages; ++i) {
    pages[i].base.flags |= (page_clear | page_clear_all);
    pages[i].base.flags &= ~page_clear_compl;
  }
}

static int exynos_blit_frame(struct exynos_data *pdata, const void *frame,
                             unsigned src_pitch) {
  const enum exynos_buffer_type buf_type = defaults[exynos_image_frame].buf_type;
  const unsigned size = src_pitch * pdata->blit_h;

  struct g2d_image *src = &pdata->src[exynos_image_frame];

  src->buf_type = G2D_IMGBUF_GEM;

  if (realloc_buffer(pdata, buf_type, size) != 0)
    return -1;

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_memcpy(&pdata->perf, true);
#endif

  /* Without IOMMU the G2D only works properly between GEM buffers. */
  memcpy_neon(pdata->buf[buf_type]->vaddr, frame, size);
  src->stride = src_pitch;

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_memcpy(&pdata->perf, false);
#endif

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_g2d(&pdata->perf, true);
#endif

  if (g2d_copy_with_scale(pdata->g2d, src, &pdata->dst, 0, 0,
                          pdata->blit_w, pdata->blit_h,
                          pdata->blit_damage.x, pdata->blit_damage.y,
                          pdata->blit_damage.w, pdata->blit_damage.h, 0) ||
      g2d_exec(pdata->g2d)) {
    RARCH_ERR("video_exynos: failed to blit frame\n");
    return -1;
  }

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_g2d(&pdata->perf, false);
#endif

  return 0;
}

static int exynos_blend_menu(struct exynos_data *pdata,
                             unsigned rotation) {
  struct g2d_image *src = &pdata->src[exynos_image_menu];

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_g2d(&pdata->perf, true);
#endif

  if (g2d_scale_and_blend(pdata->g2d, src, &pdata->dst, 0, 0,
                          src->width, src->height, pdata->blit_damage.x,
                          pdata->blit_damage.y, pdata->blit_damage.w,
                          pdata->blit_damage.h, G2D_OP_INTERPOLATE) ||
      g2d_exec(pdata->g2d)) {
    RARCH_ERR("video_exynos: failed to blend menu\n");
    return -1;
  }

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_g2d(&pdata->perf, false);
#endif

  return 0;
}

static int exynos_blend_font(struct exynos_data *pdata) {
  struct g2d_image *src = &pdata->src[exynos_image_font];

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_g2d(&pdata->perf, true);
#endif

  if (g2d_scale_and_blend(pdata->g2d, src, &pdata->dst, 0, 0, src->width,
                          src->height, 0, 0, pdata->base.width,
                          pdata->base.height, G2D_OP_INTERPOLATE) ||
      g2d_exec(pdata->g2d)) {
    RARCH_ERR("video_exynos: failed to blend font\n");
    return -1;
  }

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_g2d(&pdata->perf, false);
#endif

  return 0;
}


struct exynos_video {
  struct exynos_data *data;

  void *font;
  const font_renderer_driver_t *font_driver;
  uint16_t font_color; /* ARGB4444 */

  unsigned color_mode;

  /* current dimensions of the emulator fb */
  unsigned width;
  unsigned height;

  /* menu data */
  unsigned menu_rotation;
  bool menu_active;

  bool aspect_changed;
};


static int exynos_init_font(struct exynos_video *vid) {
  struct exynos_data *pdata = vid->data;
  struct g2d_image *src = &pdata->src[exynos_image_font];

  const unsigned buf_height = defaults[exynos_image_font].height;
  const unsigned buf_width = align_common(pdata->aspect * (float)buf_height, 16);
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

  /* The font buffer color type is ARGB4444. */
  if (realloc_buffer(pdata, defaults[exynos_image_font].buf_type,
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

static int exynos_render_msg(struct exynos_video *vid,
                             const char *msg) {
  struct exynos_data *pdata = vid->data;
  struct g2d_image *dst = &pdata->src[exynos_image_font];

  const struct font_atlas *atlas;

  int msg_base_x = g_settings.video.msg_pos_x * dst->width;
  int msg_base_y = (1.0f - g_settings.video.msg_pos_y) * dst->height;

  if (vid->font == NULL || vid->font_driver == NULL)
    return -1;

  if (clear_buffer(pdata->g2d, dst) != 0)
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

    put_glyph_rgba4444(pdata, src, vid->font_color,
                       glyph_width, glyph_height,
                       atlas->width, base_x, base_y);

    msg_base_x += glyph->advance_x;
    msg_base_y += glyph->advance_y;
  }

  return exynos_blend_font(pdata);
}


static void *exynos_gfx_init(const video_info_t *video, const input_driver_t **input, void **input_data) {
  struct exynos_video *vid;
  struct exynos_data *data;

  vid = calloc(1, sizeof(struct exynos_video));
  if (!vid)
    return NULL;

  data = calloc(1, sizeof(struct exynos_data));
  if (!data)
    goto fail_data;

  if (video->rgb32)
    vid->color_mode = G2D_COLOR_FMT_XRGB8888 | G2D_ORDER_AXRGB;
  else
    vid->color_mode = G2D_COLOR_FMT_RGB565 | G2D_ORDER_AXRGB;

  data->base.fd = -1;
  data->base.page_size = sizeof(struct exynos_page);
  data->base.num_pages = 3;
  data->base.pixel_format = DRM_FORMAT_XRGB8888;

  if (exynos_open(&data->base) < 0) {
    RARCH_ERR("video_exynos: opening device failed\n");
    goto fail;
  }

  if (exynos_init(&data->base) < 0) {
    RARCH_ERR("video_exynos: initialization failed\n");
    goto fail_init;
  }

  if (exynos_alloc(&data->base) < 0) {
    RARCH_ERR("video_exynos: allocation failed\n");
    goto fail_alloc;
  }

  if (exynos_additional_init(data) < 0) {
    RARCH_ERR("video_exynos: additional initialization failed\n");
    goto fail_add;
  }

#if (EXYNOS_GFX_DEBUG_LOG == 1)
  exynos_alloc_status(data);
#endif

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_init(&data->perf);
#endif

  vid->data = data;

  if (input && input_data) {
    *input = NULL;
  }

  if (exynos_init_font(vid) < 0) {
    RARCH_ERR("video_exynos: font initialization failed\n");
    goto fail_font;
  }

  return vid;

fail_font:
  exynos_additional_deinit(vid->data);

fail_add:
  exynos_free(&data->base);

fail_alloc:
  exynos_deinit(&data->base);

fail_init:
  exynos_close(&data->base);

fail:
  free(data);

fail_data:
  free(vid);
  return NULL;
}

static void exynos_gfx_free(void *driver_data) {
  struct exynos_video *vid = driver_data;
  struct exynos_data *data;

  if (!vid)
    return;

  data = vid->data;

  exynos_additional_deinit(data);

  /* Flush pages: One page remains, the one being displayed at this moment. */
  while (pages_used(data->base.pages, data->base.num_pages) > 1)
    exynos_wait_for_flip(&data->base);

  exynos_free(&data->base);
  exynos_deinit(&data->base);
  exynos_close(&data->base);

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_finish(&data->perf);
#endif

  free(data);

  if (vid->font && vid->font_driver)
    vid->font_driver->free(vid->font);

  free(vid);
}

static bool exynos_gfx_frame(void *driver_data, const void *frame, unsigned width,
                             unsigned height, unsigned pitch, const char *msg) {
  struct exynos_video *vid = driver_data;
  struct exynos_data *data = vid->data;

  struct exynos_page *page = NULL;

  /* Check if neither menu nor emulator framebuffer is to be displayed. */
  if (!vid->menu_active && !frame)
    return true;

  if (frame) {
    if (width != vid->width || height != vid->height) {
      /* Sanity check on new dimension parameters. */
      if (!width || !height)
        return true;

      RARCH_LOG("video_exynos: resolution changed by core: %ux%u -> %ux%u\n",
                vid->width, vid->height, width, height);
      exynos_setup_scale(vid->data, width, height);

      vid->width = width;
      vid->height = height;
    }

    page = exynos_free_page(data);

    exynos_setup_blit_src(data, vid->width, vid->height, vid->color_mode, pitch);

    if (exynos_blit_frame(data, frame, pitch) != 0)
      goto fail;
  }

  if (g_settings.fps_show) {
    char buffer[128], buffer_fps[128];

    gfx_get_fps(buffer, sizeof(buffer), g_settings.fps_show ? buffer_fps : NULL, sizeof(buffer_fps));
    msg_queue_push(g_extern.msg_queue, buffer_fps, 1, 1);
  }

  if (!vid->width || !vid->height) {
    /* If at this point the dimension parameters are still zero, setup some  *
     * fake blit parameters so that menu and font rendering work properly.   */
    exynos_set_fake_blit(data);
  }

  if (!page)
    page = exynos_free_page(data);

  if (vid->menu_active) {
    if (exynos_blend_menu(data, vid->menu_rotation) != 0)
      goto fail;
  }

  if (msg) {
    if (exynos_render_msg(vid, msg) < 0)
      goto fail;

    /* Font is blitted to the entire screen, so issue clear afterwards. */
    page->base.flags |= (page_clear | page_clear_compl);
    page->base.flags &= ~page_clear_all;
  }

  apply_damage(page, 0, &data->blit_damage);

  if (exynos_issue_flip(&data->base, &page->base) < 0)
    goto fail;

  g_extern.frame_count++;

  return true;

fail:
  /* Since we didn't manage to issue a pageflip to this page, set *
   * it to 'unused' again, and hope that it works next time.      */
  page->base.flags &= ~page_used;

  return false;
}

static void exynos_gfx_set_nonblock_state(void *data, bool state) {
  struct exynos_video *vid = data;

  vid->data->sync = !state;
}

static bool exynos_gfx_alive(void *data) {
  (void)data;
  return true; /* always alive */
}

static bool exynos_gfx_focus(void *data) {
  (void)data;
  return true; /* drm device always has focus */
}

static void exynos_gfx_set_rotation(void *data, unsigned rotation) {
  struct exynos_video *vid = data;

  vid->menu_rotation = rotation;
}

static void exynos_gfx_viewport_info(void *data, struct rarch_viewport *vp) {
  struct exynos_video *vid = data;

  vp->x = vp->y = 0;

  vp->width  = vp->full_width  = vid->width;
  vp->height = vp->full_height = vid->height;
}

static void exynos_set_aspect_ratio(void *data, unsigned aspect_ratio_idx) {
  struct exynos_video *vid = data;

  switch (aspect_ratio_idx) {
    case ASPECT_RATIO_SQUARE:
      gfx_set_square_pixel_viewport(g_extern.system.av_info.geometry.base_width, g_extern.system.av_info.geometry.base_height);
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
  vid->aspect_changed = true;
}

static void exynos_apply_state_changes(void *data) {
  (void)data;
}

static void exynos_set_texture_frame(void *data, const void *frame, bool rgb32,
                                     unsigned width, unsigned height, float alpha) {
  const enum exynos_buffer_type buf_type = defaults[exynos_image_menu].buf_type;

  struct exynos_video *vid = data;
  struct exynos_data *pdata = vid->data;
  struct g2d_image *src = &pdata->src[exynos_image_menu];

  const unsigned size = width * height * (rgb32 ? 4 : 2);

  if (realloc_buffer(pdata, buf_type, size) < 0)
    return;

  src->width = width;
  src->height = height;
  src->stride = width * (rgb32 ? 4 : 2);
  src->color_mode = rgb32 ? G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_RGBAX :
                            G2D_COLOR_FMT_ARGB4444 | G2D_ORDER_RGBAX;

  src->component_alpha = (unsigned char)(255.0f * alpha);

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_memcpy(&pdata->perf, true);
#endif

  memcpy_neon(pdata->buf[buf_type]->vaddr, frame, size);

#if (EXYNOS_GFX_DEBUG_PERF == 1)
  perf_memcpy(&pdata->perf, false);
#endif
}

static void exynos_set_texture_enable(void *data, bool state, bool full_screen) {
  struct exynos_video *vid = data;
  vid->menu_active = state;
}

static void exynos_set_osd_msg(void *data, const char *msg, const struct font_params *params) {
  struct exynos_video *vid = data;

  /* TODO: what does this do? */
  (void)msg;
  (void)params;
}

static void exynos_show_mouse(void *data, bool state) {
  (void)data;
}

static const video_poke_interface_t exynos_poke_interface = {
  .set_aspect_ratio = exynos_set_aspect_ratio,
  .apply_state_changes = exynos_apply_state_changes,
#ifdef HAVE_MENU
  .set_texture_frame = exynos_set_texture_frame,
  .set_texture_enable = exynos_set_texture_enable,
#endif
  .set_osd_msg = exynos_set_osd_msg,
  .show_mouse = exynos_show_mouse
};

static void exynos_gfx_get_poke_interface(void *data, const video_poke_interface_t **iface) {
  (void)data;
  *iface = &exynos_poke_interface;
}

const video_driver_t video_exynos = {
  .init = exynos_gfx_init,
  .frame = exynos_gfx_frame,
  .set_nonblock_state = exynos_gfx_set_nonblock_state,
  .alive = exynos_gfx_alive,
  .focus = exynos_gfx_focus,
  .free = exynos_gfx_free,
  .ident = "exynos",
  .set_rotation = exynos_gfx_set_rotation,
  .viewport_info = exynos_gfx_viewport_info,
  .poke_interface = exynos_gfx_get_poke_interface
};
