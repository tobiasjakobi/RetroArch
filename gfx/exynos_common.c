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

#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <libdrm/exynos_drmif.h>
#include <drm_fourcc.h>

#include "../general.h"

enum e_connector_type {
  connector_hdmi = 0,
  connector_vga,
  connector_other
};

struct exynos_fliphandler {
  struct pollfd fds;
  drmEventContext evctx;
};

struct exynos_prop {
  uint32_t object_type;
  const char *prop_name;
  uint32_t prop_id;
};

struct exynos_drm {
  /* IDs for connector, CRTC and plane objects. */
  uint32_t connector_id;
  uint32_t crtc_id;
  uint32_t primary_plane_id;
  uint32_t overlay_plane_id;
  uint32_t mode_blob_id;

  struct exynos_prop *properties;

  /* Atomic requests for the initial and the restore modeset. */
  drmModeAtomicReq *modeset_request;
  drmModeAtomicReq *restore_request;
};

static const struct exynos_prop prop_template[] = {
  /* Property IDs of the connector object. */
  { DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", 0 },

  /* Property IDs of the CRTC object. */
  { DRM_MODE_OBJECT_CRTC, "ACTIVE", 0 },
  { DRM_MODE_OBJECT_CRTC, "MODE_ID", 0 },

  /* Property IDs of the plane object. */
  { DRM_MODE_OBJECT_PLANE, "FB_ID", 0 },
  { DRM_MODE_OBJECT_PLANE, "CRTC_ID", 0 },
  { DRM_MODE_OBJECT_PLANE, "CRTC_X", 0 },
  { DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0 },
  { DRM_MODE_OBJECT_PLANE, "CRTC_W", 0 },
  { DRM_MODE_OBJECT_PLANE, "CRTC_H", 0 },
  { DRM_MODE_OBJECT_PLANE, "SRC_X", 0 },
  { DRM_MODE_OBJECT_PLANE, "SRC_Y", 0 },
  { DRM_MODE_OBJECT_PLANE, "SRC_W", 0 },
  { DRM_MODE_OBJECT_PLANE, "SRC_H", 0 }
};

enum e_exynos_prop {
  connector_prop_crtc_id = 0,
  crtc_prop_active,
  crtc_prop_mode_id,
  plane_prop_fb_id,
  plane_prop_crtc_id,
  plane_prop_crtc_x,
  plane_prop_crtc_y,
  plane_prop_crtc_w,
  plane_prop_crtc_h,
  plane_prop_src_x,
  plane_prop_src_y,
  plane_prop_src_w,
  plane_prop_src_h
};

struct prop_assign {
  enum e_exynos_prop prop;
  uint64_t value;
};


/* Find the index of a compatible DRM device. */
static int get_device_index() {
  char buf[32];
  drmVersionPtr ver;

  int index = 0;
  int fd;
  bool found = false;

  while (!found) {
    snprintf(buf, sizeof(buf), "/dev/dri/card%d", index);

    fd = open(buf, O_RDWR);
    if (fd < 0) break;

    ver = drmGetVersion(fd);

    if (strcmp("exynos", ver->name) == 0)
      found = true;
    else
      ++index;

    drmFreeVersion(ver);
    close(fd);
  }

  return (found ? index : -1);
}

static uint32_t bpp_to_pixelformat(unsigned bpp) {
  switch (bpp) {
    case 2:
      return DRM_FORMAT_RGB565;

    case 4:
      return DRM_FORMAT_XRGB8888;

    default:
      assert(false);
      return 0;
  }
}

static void clean_up_drm(struct exynos_drm *d, int fd) {
  if (d) {
    drmModeAtomicFree(d->modeset_request);
    drmModeAtomicFree(d->restore_request);
  }

  free(d);
  close(fd);
}

static void clean_up_pages(void *p, unsigned s, unsigned cnt) {
  unsigned i;

  for (i = 0; i < cnt; ++i) {
    struct exynos_page_base *page = p + i * s;

    if (page->bo) {
      if (page->buf_id)
        drmModeRmFB(page->buf_id, page->bo->handle);

      exynos_bo_destroy(page->bo);
    }

    drmModeAtomicFree(page->atomic_request);
  }
}

/*
 * The main pageflip handler which is used by drmHandleEvent.
 * Decreases the pending pageflip count and updates the current page.
 */
static void page_flip_handler(int fd, unsigned frame, unsigned sec,
                              unsigned usec, void *data) {
  struct exynos_page_base *page = data;
  struct exynos_data_base *root = page->root;

#if (EXYNOS_GFX_DEBUG_LOG == 1)
  RARCH_LOG("video_exynos: in page_flip_handler, page = %p\n", page);
#endif

  if (root->cur_page)
    root->cur_page->flags &= ~page_used;

  root->pageflip_pending--;
  root->cur_page = page;
}

/* Get the ID of an object's property using the property name. */
static bool get_propid_by_name(int fd, uint32_t object_id, uint32_t object_type,
                               const char *name, uint32_t *prop_id) {
  drmModeObjectProperties *properties;
  unsigned i;
  bool found = false;

  properties = drmModeObjectGetProperties(fd, object_id, object_type);

  if (!properties)
    goto out;

  for (i = 0; i < properties->count_props; ++i) {
    drmModePropertyRes *prop;

    prop = drmModeGetProperty(fd, properties->props[i]);
    if (!prop)
      continue;

    if (!strcmp(prop->name, name)) {
      *prop_id = prop->prop_id;
      found = true;
    }

    drmModeFreeProperty(prop);

    if (found)
      break;
  }

  drmModeFreeObjectProperties(properties);

out:
  return found;
}

/* Get the value of an object's property using the ID. */
static bool get_propval_by_id(int fd, uint32_t object_id, uint32_t object_type,
                              uint32_t id, uint64_t *prop_value) {
  drmModeObjectProperties *properties;
  unsigned i;
  bool found = false;

  properties = drmModeObjectGetProperties(fd, object_id, object_type);

  if (!properties)
    goto out;

  for (i = 0; i < properties->count_props; ++i) {
    drmModePropertyRes *prop;

    prop = drmModeGetProperty(fd, properties->props[i]);
    if (!prop)
      continue;

    if (prop->prop_id == id) {
      *prop_value = properties->prop_values[i];
      found = true;
    }

    drmModeFreeProperty(prop);

    if (found)
      break;
  }

  drmModeFreeObjectProperties(properties);

out:
  return found;
}

static bool check_connector_type(uint32_t connector_type) {
  unsigned t;

  switch (connector_type) {
    case DRM_MODE_CONNECTOR_HDMIA:
    case DRM_MODE_CONNECTOR_HDMIB:
      t = connector_hdmi;
      break;

    case DRM_MODE_CONNECTOR_VGA:
      t = connector_vga;
      break;

    default:
      t = connector_other;
      break;
  }

  return (t == g_settings.video.monitor_index);
}

int exynos_open(struct exynos_data_base *data) {
  const uint32_t pixel_format = bpp_to_pixelformat(data->bpp);

  char buf[32];
  int devidx;

  int fd;
  struct exynos_drm *drm;
  struct exynos_fliphandler *fliphandler = NULL;
  unsigned i, j;

  drmModeRes *resources = NULL;
  drmModePlaneRes *plane_resources = NULL;
  drmModeConnector *connector = NULL;
  drmModePlane *planes[2] = {NULL, NULL};

  assert(data->fd == -1);

  devidx = get_device_index();
  if (devidx != -1) {
    snprintf(buf, sizeof(buf), "/dev/dri/card%d", devidx);
  } else {
    RARCH_ERR("exynos_open: no compatible DRM device found\n");
    return -1;
  }

  fd = open(buf, O_RDWR, 0);
  if (fd < 0) {
    RARCH_ERR("exynos_open: failed to open DRM device\n");
    return -1;
  }

  /* Request atomic DRM support. This also enables universal planes. */
  if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
    RARCH_ERR("exynos_open: failed to enable atomic support\n");
    close(fd);
    return -1;
  }

  drm = calloc(1, sizeof(struct exynos_drm));
  if (!drm) {
    RARCH_ERR("exynos_open: failed to allocate DRM\n");
    close(fd);
    return -1;
  }

  resources = drmModeGetResources(fd);
  if (!resources) {
    RARCH_ERR("exynos_open: failed to get DRM resources\n");
    goto fail;
  }

  plane_resources = drmModeGetPlaneResources(fd);
  if (!plane_resources) {
    RARCH_ERR("exynos_open: failed to get DRM plane resources\n");
    goto fail;
  }

  for (i = 0; i < resources->count_connectors; ++i) {
    connector = drmModeGetConnector(fd, resources->connectors[i]);
    if (connector == NULL)
      continue;

    if (check_connector_type(connector->connector_type) &&
        connector->connection == DRM_MODE_CONNECTED &&
        connector->count_modes > 0)
      break;

    drmModeFreeConnector(connector);
    connector = NULL;
  }

  if (i == resources->count_connectors) {
    RARCH_ERR("exynos_open: no currently active connector found\n");
    goto fail;
  }

  drm->connector_id = connector->connector_id;

  for (i = 0; i < connector->count_encoders; i++) {
    drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoders[i]);

    if (!encoder)
      continue;

    /* Find a CRTC that is compatible with the encoder. */
    for (j = 0; j < resources->count_crtcs; ++j) {
      if (encoder->possible_crtcs & (1 << j))
        break;
    }

    drmModeFreeEncoder(encoder);

    /* Stop when a suitable CRTC was found. */
    if (j != resources->count_crtcs)
      break;
  }

  if (i == connector->count_encoders) {
    RARCH_ERR("exynos_open: no compatible encoder found\n");
    goto fail;
  }

  drm->crtc_id = resources->crtcs[j];

  for (i = 0; i < plane_resources->count_planes; ++i) {
    drmModePlane *plane;
    uint32_t plane_id, prop_id;
    uint64_t type;

    plane_id = plane_resources->planes[i];
    plane = drmModeGetPlane(fd, plane_id);

    if (!plane)
      continue;

    /* Make sure that the plane can be used with the selected CRTC. */
    if (!(plane->possible_crtcs & (1 << j)) ||
        !get_propid_by_name(fd, plane_id, DRM_MODE_OBJECT_PLANE, "type", &prop_id) ||
        !get_propval_by_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, prop_id, &type)) {
      drmModeFreePlane(plane);
      continue;
    }

    switch (type) {
      case DRM_PLANE_TYPE_PRIMARY:
        if (planes[0])
          RARCH_WARN("exynos_open: found more than one primary plane\n");
        else
          planes[0] = plane;
        break;

      case DRM_PLANE_TYPE_CURSOR:
        if (!planes[1])
          planes[1] = plane;
        break;

      case DRM_PLANE_TYPE_OVERLAY:
      default:
        drmModeFreePlane(plane);
        break;
    }
  }

  if (!planes[0] || !planes[1]) {
    RARCH_ERR("exynos_open: no primary plane or overlay plane found\n");
    goto fail;
  }

  /* Check that the primary plane supports chose pixel format. */
  for (i = 0; i < planes[0]->count_formats; ++i) {
    if (planes[0]->formats[i] == pixel_format)
      break;
  }

  if (i == planes[0]->count_formats) {
    RARCH_ERR("exynos_open: primary plane has no support for pixel format\n");
    goto fail;
  }

  drm->primary_plane_id = planes[0]->plane_id;
  drm->overlay_plane_id = planes[1]->plane_id;

  fliphandler = calloc(1, sizeof(struct exynos_fliphandler));
  if (!fliphandler) {
    RARCH_ERR("exynos_open: failed to allocate fliphandler\n");
    goto fail;
  }

  /* Setup the flip handler. */
  fliphandler->fds.fd = fd;
  fliphandler->fds.events = POLLIN;
  fliphandler->evctx.version = DRM_EVENT_CONTEXT_VERSION;
  fliphandler->evctx.page_flip_handler = page_flip_handler;

  RARCH_LOG("exynos_open: using DRM device \"%s\" with connector id %u\n",
          buf, drm->connector_id);

  RARCH_LOG("exynos_open: primary plane has ID %u, overlay plane has ID %u\n",
          drm->primary_plane_id, drm->overlay_plane_id);

  data->fd = fd;
  data->drm = drm;
  data->fliphandler = fliphandler;

  return 0;

fail:
  free(fliphandler);

  drmModeFreePlane(planes[0]);
  drmModeFreePlane(planes[1]);

  drmModeFreeConnector(connector);
  drmModeFreePlaneResources(plane_resources);
  drmModeFreeResources(resources);

  clean_up_drm(drm, fd);

  return -1;
}

/* Counterpart to exynos_open. */
void exynos_close(struct exynos_data_base *data) {
  free(data->fliphandler);
  data->fliphandler = NULL;

  clean_up_drm(data->drm, data->fd);
  data->fd = -1;
  data->drm = NULL;
}

static uint32_t get_id_from_type(struct exynos_drm *drm, uint32_t object_type) {
  switch (object_type) {
    case DRM_MODE_OBJECT_CONNECTOR:
      return drm->connector_id;

    case DRM_MODE_OBJECT_CRTC:
      return drm->crtc_id;

    case DRM_MODE_OBJECT_PLANE:
      return drm->primary_plane_id;

    default:
      assert(false);
      return 0;
  }
}

static int exynos_get_properties(int fd, struct exynos_drm *drm) {
  const unsigned num_props = sizeof(prop_template) / sizeof(prop_template[0]);
  unsigned i;

  assert(!drm->properties);

  drm->properties = calloc(num_props, sizeof(struct exynos_prop));

  for (i = 0; i < num_props; ++i) {
    const uint32_t object_type = prop_template[i].object_type;
    const uint32_t object_id = get_id_from_type(drm, object_type);
    const char* prop_name = prop_template[i].prop_name;

    uint32_t prop_id;

    if (!get_propid_by_name(fd, object_id, object_type, prop_name, &prop_id))
      goto fail;

    drm->properties[i] = (struct exynos_prop){ object_type, prop_name, prop_id };
  }

  return 0;

fail:
  free(drm->properties);
  drm->properties = NULL;

  return -1;
}

static int exynos_create_restore_req(int fd, struct exynos_drm *drm) {
  static const enum e_exynos_prop restore_props[] = {
    connector_prop_crtc_id,
    crtc_prop_active,
    crtc_prop_mode_id,
    plane_prop_fb_id,
    plane_prop_crtc_id,
    plane_prop_crtc_x, plane_prop_crtc_y,
    plane_prop_crtc_w, plane_prop_crtc_h,
    plane_prop_src_x, plane_prop_src_y,
    plane_prop_src_w, plane_prop_src_h
  };
  const unsigned num_props = sizeof(restore_props) / sizeof(restore_props[0]);

  uint64_t temp;
  unsigned i;

  assert(!drm->restore_request);

  drm->restore_request = drmModeAtomicAlloc();

  for (i = 0; i < num_props; ++i) {
    const struct exynos_prop* prop = &drm->properties[restore_props[i]];
    const uint32_t object_type = prop->object_type;
    const uint32_t object_id = get_id_from_type(drm, object_type);

    uint64_t prop_value;

    if (!get_propval_by_id(fd, object_id, object_type, prop->prop_id, &prop_value))
      goto fail;

    if (drmModeAtomicAddProperty(drm->restore_request, object_id, prop->prop_id, prop_value) < 0)
      goto fail;
  }

  return 0;

fail:
  drmModeAtomicFree(drm->restore_request);
  drm->restore_request = NULL;

  return -1;
}

static int exynos_create_modeset_req(int fd, struct exynos_drm *drm, unsigned w, unsigned h) {
  uint64_t temp;
  unsigned i;

  const struct prop_assign assign[] = {
    { plane_prop_crtc_id, drm->crtc_id },
    { plane_prop_crtc_x, 0 },
    { plane_prop_crtc_y, 0 },
    { plane_prop_crtc_w, w },
    { plane_prop_crtc_h, h },
    { plane_prop_src_x, 0 },
    { plane_prop_src_y, 0 },
    { plane_prop_src_w, w << 16 },
    { plane_prop_src_h, h << 16 }
  };

  const unsigned num_assign = sizeof(assign) / sizeof(assign[0]);

  assert(!drm->modeset_request);

  drm->modeset_request = drmModeAtomicAlloc();

  if (drmModeAtomicAddProperty(drm->modeset_request, drm->connector_id,
      drm->properties[connector_prop_crtc_id].prop_id, drm->crtc_id) < 0)
    goto fail;

  if (drmModeAtomicAddProperty(drm->modeset_request, drm->crtc_id,
      drm->properties[crtc_prop_active].prop_id, 1) < 0)
    goto fail;

  if (drmModeAtomicAddProperty(drm->modeset_request, drm->crtc_id,
      drm->properties[crtc_prop_mode_id].prop_id, drm->mode_blob_id) < 0)
    goto fail;

  for (i = 0; i < num_assign; ++i) {
    if (drmModeAtomicAddProperty(drm->modeset_request, drm->primary_plane_id,
        drm->properties[assign[i].prop].prop_id, assign[i].value) < 0)
      goto fail;
  }

  return 0;

fail:
  drmModeAtomicFree(drm->modeset_request);
  drm->modeset_request = NULL;

  return -1;
}

int exynos_init(struct exynos_data_base *data) {
  struct exynos_drm *drm = data->drm;
  const int fd = data->fd;

  drmModeConnector *connector = NULL;
  drmModeModeInfo *mode = NULL;
  unsigned i;

  const unsigned fullscreen[2] = {
    g_settings.video.fullscreen_x,
    g_settings.video.fullscreen_y
  };

  connector = drmModeGetConnector(fd, drm->connector_id);

  if (fullscreen[0] != 0 && fullscreen[1] != 0) {

    for (i = 0; i < connector->count_modes; i++) {
      if (connector->modes[i].hdisplay == fullscreen[0] &&
          connector->modes[i].vdisplay == fullscreen[1]) {
        mode = &connector->modes[i];
        break;
      }
    }

    if (!mode) {
      RARCH_ERR("exynos_init: requested resolution (%dx%d) not available\n",
              fullscreen[0], fullscreen[1]);
      goto fail;
    }

  } else {
    /* Select first mode, which is the native one. */
    mode = &connector->modes[0];
  }

  if (mode->hdisplay == 0 || mode->vdisplay == 0) {
    RARCH_ERR("exynos_init: failed to select sane resolution\n");
    goto fail;
  }

  if (drmModeCreatePropertyBlob(fd, mode, sizeof(drmModeModeInfo), &drm->mode_blob_id)) {
    RARCH_ERR("exynos_init: failed to blobify mode info\n");
    goto fail;
  }

  if (exynos_get_properties(fd, drm)) {
    RARCH_ERR("exynos_init: failed to get object properties\n");
    goto fail;
  }

  if (exynos_create_restore_req(fd, drm)) {
    RARCH_ERR("exynos_init: failed to create restore atomic request\n");
    goto fail;
  }

  if (exynos_create_modeset_req(fd, drm, mode->hdisplay, mode->vdisplay)) {
    RARCH_ERR("exynos_init: failed to create modeset atomic request\n");
    goto fail;
  }

  data->width = mode->hdisplay;
  data->height = mode->vdisplay;

  drmModeFreeConnector(connector);

  data->pitch = data->bpp * data->width;
  data->size = data->pitch * data->height;

  RARCH_LOG("exynos_init: selected %ux%u resolution with %u bpp\n",
            data->width, data->height, data->bpp);

  return 0;

fail:
  drmModeDestroyPropertyBlob(fd, drm->mode_blob_id);
  drmModeFreeConnector(connector);

  return -1;
}

/* Counterpart to exynos_init. */
void exynos_deinit(struct exynos_data_base *data) {
  struct exynos_drm *drm = data->drm;

  drmModeDestroyPropertyBlob(data->fd, drm->mode_blob_id);

  drm = NULL;

  data->width = 0;
  data->height = 0;

  data->pitch = 0;
  data->size = 0;
}

static int exynos_create_page_req(struct exynos_page_base *p) {
  struct exynos_drm *drm;

  assert(p);

  drm = p->root->drm;

  assert(!p->atomic_request);

  p->atomic_request = drmModeAtomicAlloc();
  if (!p->atomic_request)
    goto fail;

  if (drmModeAtomicAddProperty(p->atomic_request, drm->primary_plane_id,
      drm->properties[plane_prop_fb_id].prop_id, p->buf_id) < 0) {
    drmModeAtomicFree(p->atomic_request);
    p->atomic_request = NULL;
    goto fail;
  }

  return 0;

fail:
  return -1;
}

static int initial_modeset(int fd, struct exynos_page_base *page, struct exynos_drm *drm) {
  int ret = 0;
  drmModeAtomicReq *request;

  request = drmModeAtomicDuplicate(drm->modeset_request);
  if (!request) {
    ret = -1;
    goto out;
  }

  if (drmModeAtomicMerge(request, page->atomic_request)) {
    ret = -2;
    goto out;
  }

  if (drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL))
    ret = -3;

out:
  drmModeAtomicFree(request);
  return ret;
}

int exynos_alloc(struct exynos_data_base *data) {
  const uint32_t pixel_format = bpp_to_pixelformat(data->bpp);

  struct exynos_device *device;
  struct exynos_bo *bo;
  void *pages;
  unsigned i;

  uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
  const unsigned flags = 0;

  device = exynos_device_create(data->fd);
  if (!device) {
    RARCH_ERR("exynos_alloc: failed to create device from fd\n");
    return -1;
  }

  assert(data->page_size);

  pages = calloc(data->num_pages, data->page_size);
  if (!pages) {
    RARCH_ERR("exynos_alloc: failed to allocate pages\n");
    goto fail_alloc;
  }

  for (i = 0; i < data->num_pages; ++i) {
    struct exynos_page_base *p = pages + i * data->page_size;

    bo = exynos_bo_create(device, data->size, flags);
    if (bo == NULL) {
      RARCH_ERR("exynos_alloc: failed to create buffer object %u\n", i);
      goto fail;
    }

    /* Don't map the BO, since we don't access it through userspace. */
    p->bo = bo;
    p->root = data;

    p->flags |= page_clear;
  }

  pitches[0] = data->pitch;

  for (i = 0; i < data->num_pages; ++i) {
    struct exynos_page_base *p = pages + i * data->page_size;

    handles[0] = p->bo->handle;

    if (drmModeAddFB2(data->fd, data->width, data->height, pixel_format,
                      handles, pitches, offsets, &p->buf_id, flags)) {
      RARCH_ERR("exynos_alloc: failed to add bo %u to fb\n", i);
      goto fail;
    }

    if (exynos_create_page_req(p)) {
      RARCH_ERR("exynos_alloc: failed to create atomic request for page %u\n", i);
      goto fail;
    }
  }

  /* Setup framebuffer: display the last allocated page. */
  if (initial_modeset(data->fd, pages + (data->num_pages - 1) * data->page_size, data->drm)) {
    RARCH_ERR("exynos_alloc: initial atomic modeset failed\n");
    goto fail;
  }

  data->pages = pages;
  data->device = device;

  return 0;

fail:
  clean_up_pages(pages, data->page_size, data->num_pages);

fail_alloc:
  exynos_device_destroy(device);

  return -1;
}

/* Counterpart to exynos_alloc. */
void exynos_free(struct exynos_data_base *data) {
  /* Restore the display state. */
  if (drmModeAtomicCommit(data->fd, data->drm->restore_request,
      DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
    RARCH_WARN("exynos_free: failed to restore the display\n");
  }

  clean_up_pages(data->pages, data->page_size, data->num_pages);

  free(data->pages);
  data->pages = NULL;

  exynos_device_destroy(data->device);
  data->device = NULL;
}

void exynos_wait_for_flip(struct exynos_data_base *data) {
  struct exynos_fliphandler *fh = data->fliphandler;
  const int timeout = -1;

  fh->fds.revents = 0;

  if (poll(&fh->fds, 1, timeout) < 0)
    return;

  if (fh->fds.revents & (POLLHUP | POLLERR))
    return;

  if (fh->fds.revents & POLLIN)
    drmHandleEvent(fh->fds.fd, &fh->evctx);
}

int exynos_issue_flip(struct exynos_data_base *data, struct exynos_page_base *page) {
  assert(data && page);

  /* We don't queue multiple page flips. */
  if (data->pageflip_pending > 0)
    exynos_wait_for_flip(data);

  /* Issue a page flip at the next vblank interval. */
  if (drmModeAtomicCommit(data->fd, page->atomic_request,
                          DRM_MODE_PAGE_FLIP_EVENT, page)) {
    RARCH_ERR("exynos_issue_flip: failed to issue atomic page flip\n");
    return -1;
  } else {
    data->pageflip_pending++;
  }

  /* On startup no frame is displayed. We therefore wait for the initial flip to finish. */
  if (!data->cur_page)
    exynos_wait_for_flip(data);

  return 0;
}
