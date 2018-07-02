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


// -----------------------------------------------------------------------------------------
// Enumerator definitions
// -----------------------------------------------------------------------------------------

enum e_connector_type {
  connector_hdmi = 0,
  connector_vga,
  connector_other
};

enum e_prop {
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
  plane_prop_src_h,
  plane_prop_zpos,
};


// -----------------------------------------------------------------------------------------
// Structure definitions
// -----------------------------------------------------------------------------------------

struct exynos_fliphandler {
  struct pollfd fds;
  drmEventContext evctx;
};

struct drm_prop {
  uint32_t object_type;
  enum e_prop prop;
  const char *prop_name;
};

struct prop_key {
  uint32_t object_id;
  enum e_prop prop;
};

struct prop_mapping {
  struct prop_key key;
  uint32_t id;
};

struct object_ids {
  unsigned num_ids;
  uint32_t ids[2];
};

struct exynos_drm {
  // IDs for connector, CRTC and plane objects.
  uint32_t connector_id;
  uint32_t crtc_id;
  uint32_t plane_id[exynos_plane_max];
  uint32_t mode_blob_id;

  // Mapping from properties to IDs.
  struct prop_mapping *pmap;
  unsigned pmap_size;

  // Atomic requests for the initial and the restore modeset.
  drmModeAtomicReq *modeset_request;
  drmModeAtomicReq *restore_request;
};

struct prop_assign {
  enum e_prop prop;
  uint64_t value;
};


// -----------------------------------------------------------------------------------------
// Local/static variables
// -----------------------------------------------------------------------------------------

static const struct drm_prop prop_template[] = {
  // Property IDs of the connector object.
  { DRM_MODE_OBJECT_CONNECTOR, connector_prop_crtc_id, "CRTC_ID" },

  // Property IDs of the CRTC object.
  { DRM_MODE_OBJECT_CRTC, crtc_prop_active, "ACTIVE" },
  { DRM_MODE_OBJECT_CRTC, crtc_prop_mode_id, "MODE_ID" },

  // Property IDs of the plane object.
  { DRM_MODE_OBJECT_PLANE, plane_prop_fb_id, "FB_ID" },
  { DRM_MODE_OBJECT_PLANE, plane_prop_crtc_id, "CRTC_ID" },
  { DRM_MODE_OBJECT_PLANE, plane_prop_crtc_x, "CRTC_X" },
  { DRM_MODE_OBJECT_PLANE, plane_prop_crtc_y, "CRTC_Y" },
  { DRM_MODE_OBJECT_PLANE, plane_prop_crtc_w, "CRTC_W" },
  { DRM_MODE_OBJECT_PLANE, plane_prop_crtc_h, "CRTC_H" },
  { DRM_MODE_OBJECT_PLANE, plane_prop_src_x, "SRC_X" },
  { DRM_MODE_OBJECT_PLANE, plane_prop_src_y, "SRC_Y" },
  { DRM_MODE_OBJECT_PLANE, plane_prop_src_w, "SRC_W" },
  { DRM_MODE_OBJECT_PLANE, plane_prop_src_h, "SRC_H" },
  { DRM_MODE_OBJECT_PLANE, plane_prop_zpos, "zpos" },
};


// -----------------------------------------------------------------------------------------
// Local/static functions
// -----------------------------------------------------------------------------------------

/**
 * Find the index of a compatible DRM device.
 */
static int
get_device_index(void)
{
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

static unsigned
pixelformat_to_bpp(uint32_t pf)
{
  switch (pf) {
  case DRM_FORMAT_ARGB4444:
  case DRM_FORMAT_RGBA4444:
  case DRM_FORMAT_RGB565:
    return 2;

  case DRM_FORMAT_XRGB8888:
    return 4;

  default:
    assert(false);
    return 0;
  }
}

static void
fill_plane_info(struct plane_info *pi)
{
  const unsigned bpp = pixelformat_to_bpp(pi->pixel_format);

  pi->pitch = bpp * pi->width;
  pi->size = pi->pitch * pi->height;
}

static const char*
fmt_pixel_format(const struct plane_info *pi)
{
  static char buffer[5];

  buffer[4] = 0;
  memcpy(buffer, &pi->pixel_format, sizeof(uint32_t));

  return buffer;
}

static void
clean_up_drm(struct exynos_drm *d, int fd)
{
  if (d) {
    drmModeAtomicFree(d->modeset_request);
    drmModeAtomicFree(d->restore_request);
  }

  free(d);
  close(fd);
}

static void
clean_up_plane(struct exynos_plane *plane, int fd)
{
  if (plane->bo) {
    if (plane->buf_id)
      drmModeRmFB(fd, plane->buf_id);

    exynos_bo_destroy(plane->bo);
  }

  drmModeAtomicFree(plane->atomic_request);
}

static void
clean_up_pages(struct exynos_page_base **pages, unsigned cnt)
{
  unsigned i, j;

  for (i = 0; i < cnt; ++i) {
    struct exynos_page_base *p = pages[i];

    for (j = 0; j < exynos_plane_max; ++j)
      clean_up_plane(&p->planes[j], p->root->fd);

    memset(p, 0, sizeof(struct exynos_page_base));
  }
}

/**
 * The main pageflip handler which is used by drmHandleEvent.
 * Decreases the pending pageflip count and updates the current page.
 */
static void
page_flip_handler(int fd, unsigned frame, unsigned sec,
                  unsigned usec, void *data)
{
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

static bool
is_key_equal(const struct prop_key *k1, const struct prop_key *k2)
{
  return k1->object_id == k2->object_id && k1->prop == k2->prop;
}

static const struct prop_mapping*
lookup_mapping(const struct exynos_drm *drm,
               uint32_t object_id, enum e_prop prop)
{
  const struct prop_key key = {object_id, prop};
  unsigned i;

  for (i = 0; i < drm->pmap_size; ++i) {
    const struct prop_mapping *p = &drm->pmap[i];

    if (is_key_equal(&p->key, &key))
      return p;
  }

  return NULL;
}

/**
 * Get the ID of an object's property using the property name.
 */
static bool
get_propid_by_name(int fd, uint32_t object_id, uint32_t object_type,
                   const char *name, uint32_t *prop_id)
{
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

/**
 * Get the value of an object's property using the ID.
 */
static bool
get_propval_by_id(int fd, uint32_t object_id, uint32_t object_type,
                  uint32_t id, uint64_t *prop_value)
{
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

static bool
check_connector_type(uint32_t connector_type)
{
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

static void
get_ids_from_type(struct exynos_drm *drm, uint32_t object_type,
                  struct object_ids *ids)
{
  switch (object_type) {
    case DRM_MODE_OBJECT_CONNECTOR:
      ids->num_ids = 1;
      ids->ids[0] = drm->connector_id;
    break;

    case DRM_MODE_OBJECT_CRTC:
      ids->num_ids = 1;
      ids->ids[0] = drm->crtc_id;
    break;

    case DRM_MODE_OBJECT_PLANE:
      ids->num_ids = 2;
      ids->ids[0] = drm->plane_id[plane_primary];
      ids->ids[1] = drm->plane_id[plane_overlay];
    break;

    default:
      assert(false);
    break;
  }
}

static int
setup_properties(int fd, struct exynos_drm *drm, const drmModeRes *res,
                 const drmModePlaneRes *pres)
{
  const unsigned num_props = sizeof(prop_template) / sizeof(prop_template[0]);
  unsigned i;

  assert(!drm->pmap);

  drm->pmap_size = num_props;
  drm->pmap = calloc(num_props, sizeof(struct prop_mapping));

  for (i = 0; i < num_props; ++i) {
    const uint32_t object_type = prop_template[i].object_type;
    const char* prop_name = prop_template[i].prop_name;

    int j, object_count;
    uint32_t *object_ids;

    switch (object_type) {
      case DRM_MODE_OBJECT_CONNECTOR:
        object_count = res->count_connectors;
        object_ids = res->connectors;
      break;

      case DRM_MODE_OBJECT_CRTC:
        object_count = res->count_crtcs;
        object_ids = res->crtcs;
      break;

      case DRM_MODE_OBJECT_PLANE:
      default:
        object_count = pres->count_planes;
        object_ids = pres->planes;
      break;
    }

    for (int j = 0; j < object_count; ++j) {
      const uint32_t object_id = object_ids[j];
      uint32_t prop_id;

      if (!get_propid_by_name(fd, object_id, object_type, prop_name, &prop_id))
        goto fail;

      drm->pmap[i] = (struct prop_mapping){
        (struct prop_key){ object_id, prop_template[i].prop },
        prop_id
      };
    }
  }

  return 0;

fail:
  free(drm->pmap);
  drm->pmap = NULL;
  drm->pmap_size = 0;

  return -1;
}

static int
create_restore_request(int fd, struct exynos_drm *drm)
{
  const unsigned num_props = sizeof(prop_template) / sizeof(prop_template[0]);

  uint64_t temp;
  unsigned i;

  assert(!drm->restore_request);

  drm->restore_request = drmModeAtomicAlloc();

  for (i = 0; i < num_props; ++i) {
    const uint32_t object_type = prop_template[i].object_type;

    struct object_ids ids;
    unsigned j;

    get_ids_from_type(drm, object_type, &ids);

    for (j = 0; j < ids.num_ids; ++i) {
      const uint32_t object_id = ids.ids[j];
      const struct prop_mapping *mapping = lookup_mapping(drm, object_id, prop_template[i].prop);

      uint64_t prop_value;

      if (mapping == NULL)
        goto fail;

      if (!get_propval_by_id(fd, j, object_type, mapping->id, &prop_value))
        goto fail;

      if (drmModeAtomicAddProperty(drm->restore_request, object_id, mapping->id, prop_value) < 0)
        goto fail;
    }
  }

  return 0;

fail:
  drmModeAtomicFree(drm->restore_request);
  drm->restore_request = NULL;

  return -1;
}

static int
create_modeset_request(int fd, struct exynos_drm *drm, unsigned w, unsigned h)
{
  uint64_t temp;
  unsigned i;

  const struct prop_mapping *mapping;

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

  mapping = lookup_mapping(drm, drm->connector_id, connector_prop_crtc_id);
  if (drmModeAtomicAddProperty(drm->modeset_request, drm->connector_id, mapping->id, drm->crtc_id) < 0)
    goto fail;

  mapping = lookup_mapping(drm, drm->crtc_id, crtc_prop_active);
  if (drmModeAtomicAddProperty(drm->modeset_request, drm->crtc_id, mapping->id, 1) < 0)
    goto fail;

  mapping = lookup_mapping(drm, drm->crtc_id, crtc_prop_mode_id);
  if (drmModeAtomicAddProperty(drm->modeset_request, drm->crtc_id, mapping->id, drm->mode_blob_id) < 0)
    goto fail;

  for (i = 0; i < num_assign; ++i) {
    mapping = lookup_mapping(drm, drm->plane_id[plane_primary], assign[i].prop);

    if (drmModeAtomicAddProperty(drm->modeset_request, drm->plane_id[plane_primary],
                                 mapping->id, assign[i].value) < 0)
      goto fail;
  }

  return 0;

fail:
  drmModeAtomicFree(drm->modeset_request);
  drm->modeset_request = NULL;

  return -1;
}

static int
create_page_request(struct exynos_page_base *page)
{
  struct exynos_drm *drm;
  struct exynos_plane *plane;
  int ret;

  const struct prop_mapping *mapping;

  assert(page != NULL);

  drm = page->root->drm;
  plane = &page->planes[plane_primary];

  assert(plane->atomic_request == NULL);

  plane->atomic_request = drmModeAtomicAlloc();
  if (!plane->atomic_request)
    goto fail;

  mapping = lookup_mapping(drm, drm->plane_id[plane_primary], plane_prop_fb_id);
  ret = drmModeAtomicAddProperty(plane->atomic_request, drm->plane_id[plane_primary],
                                 mapping->id, plane->buf_id);

  if (ret < 0) {
    drmModeAtomicFree(plane->atomic_request);
    plane->atomic_request = NULL;
    goto fail;
  }

  return 0;

fail:
  return -1;
}

static int
initial_modeset(int fd, struct exynos_page_base *page, struct exynos_drm *drm)
{
  int ret;
  struct exynos_plane *plane;
  drmModeAtomicReq *request;

  ret = 0;
  plane = &page->planes[plane_primary];

  request = drmModeAtomicDuplicate(drm->modeset_request);
  if (request == NULL) {
    ret = -1;
    goto out;
  }

  if (drmModeAtomicMerge(request, plane->atomic_request) < 0) {
    ret = -2;
    goto out;
  }

  if (drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL) < 0)
    ret = -3;

out:
  drmModeAtomicFree(request);
  return ret;
}

static bool
plane_has_format(const drmModePlane *plane, uint32_t pixel_format)
{
  unsigned i;

  for (i = 0; i < plane->count_formats; ++i) {
    if (plane->formats[i] == pixel_format)
      return true;
  }

  return false;
}

static void
check_plane(struct exynos_data_base *data, unsigned crtc_index,
            drmModePlane *plane, drmModePlane **planes)
{
  uint32_t prop_id;
  uint64_t type;
  struct plane_info *primary, *overlay;

  // Make sure that the plane can be used with the selected CRTC.
  if (!(plane->possible_crtcs & (1 << crtc_index)))
    return;

  if (!get_propid_by_name(data->fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type", &prop_id))
    return;

  if (!get_propval_by_id(data->fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, prop_id, &type))
    return;

  primary = &data->plane_infos[plane_primary];
  overlay = &data->plane_infos[plane_overlay];

  if (planes[plane_primary] == NULL) {
    if (plane_has_format(plane, primary->pixel_format) && type == DRM_PLANE_TYPE_PRIMARY)
      planes[plane_primary] = plane;
  } else if (planes[plane_overlay] == NULL) {
    if (plane_has_format(plane, overlay->pixel_format) && type != DRM_PLANE_TYPE_PRIMARY)
          planes[plane_overlay] = plane;
  }
}


// -----------------------------------------------------------------------------------------
// Global functions
// -----------------------------------------------------------------------------------------

int exynos_open(struct exynos_data_base *data)
{
  char buf[32];
  int devidx;

  struct exynos_drm *drm;
  struct exynos_fliphandler *fliphandler = NULL;
  unsigned i, j;

  drmModeRes *resources = NULL;
  drmModePlaneRes *plane_resources = NULL;
  drmModeConnector *connector = NULL;
  drmModePlane *planes[exynos_plane_max] = { 0 };

  assert(data->fd == -1);
  assert(data->drm == NULL);
  assert(data->fliphandler == NULL);

  devidx = get_device_index();
  if (devidx < 0) {
    RARCH_ERR("exynos_open: no compatible DRM device found\n");
    return -1;
  }

  snprintf(buf, sizeof(buf), "/dev/dri/card%d", devidx);

  data->fd = open(buf, O_RDWR, 0);
  if (data->fd < 0) {
    RARCH_ERR("exynos_open: failed to open DRM device\n");
    return -1;
  }

  // Request atomic DRM support. This also enables universal planes.
  if (drmSetClientCap(data->fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
    RARCH_ERR("exynos_open: failed to enable atomic support\n");
    close(data->fd);
    return -1;
  }

  drm = calloc(1, sizeof(struct exynos_drm));
  if (!drm) {
    RARCH_ERR("exynos_open: failed to allocate DRM\n");
    close(data->fd);
    return -1;
  }

  resources = drmModeGetResources(data->fd);
  if (!resources) {
    RARCH_ERR("exynos_open: failed to get DRM resources\n");
    goto fail;
  }

  plane_resources = drmModeGetPlaneResources(data->fd);
  if (!plane_resources) {
    RARCH_ERR("exynos_open: failed to get DRM plane resources\n");
    goto fail;
  }

  for (i = 0; i < resources->count_connectors; ++i) {
    connector = drmModeGetConnector(data->fd, resources->connectors[i]);
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
    drmModeEncoder *encoder = drmModeGetEncoder(data->fd, connector->encoders[i]);

    if (!encoder)
      continue;

    // Find a CRTC that is compatible with the encoder.
    for (j = 0; j < resources->count_crtcs; ++j) {
      if (encoder->possible_crtcs & (1 << j))
        break;
    }

    drmModeFreeEncoder(encoder);

    // Stop when a suitable CRTC was found.
    if (j != resources->count_crtcs)
      break;
  }

  if (i == connector->count_encoders) {
    RARCH_ERR("exynos_open: no compatible encoder found\n");
    goto fail;
  }

  drm->crtc_id = resources->crtcs[j];

  for (i = 0; i < plane_resources->count_planes; ++i) {
    const uint32_t plane_id = plane_resources->planes[i];
    drmModePlane *plane;

    plane = drmModeGetPlane(data->fd, plane_id);

    if (!plane)
      continue;

    check_plane(data, j, plane, planes);

    drmModeFreePlane(plane);
  }

  if (planes[plane_primary] == NULL) {
    RARCH_ERR("exynos_open: no primary plane found\n");
    goto fail;
  }

  if (planes[plane_overlay] == NULL) {
    RARCH_ERR("exynos_open: no overlay plane found\n");
    goto fail;
  }

  for (i = 0; i < exynos_plane_max; ++i)
    drm->plane_id[i] = planes[i]->plane_id;

  if (setup_properties(data->fd, drm, resources, plane_resources)) {
    RARCH_ERR("exynos_open: failed to get object properties\n");
    goto fail;
  }

  fliphandler = calloc(1, sizeof(struct exynos_fliphandler));
  if (!fliphandler) {
    RARCH_ERR("exynos_open: failed to allocate fliphandler\n");
    goto fail;
  }

  // Setup the flip handler.
  fliphandler->fds.fd = data->fd;
  fliphandler->fds.events = POLLIN;
  fliphandler->evctx.version = DRM_EVENT_CONTEXT_VERSION;
  fliphandler->evctx.page_flip_handler = page_flip_handler;

  RARCH_LOG("exynos_open: using DRM device \"%s\" with connector id %u\n",
          buf, drm->connector_id);

  RARCH_LOG("exynos_open: primary plane has ID %u, overlay plane has ID %u\n",
          drm->plane_id[plane_primary], drm->plane_id[plane_overlay]);

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

  clean_up_drm(drm, data->fd);

  return -1;
}

/**
 * Counterpart to exynos_open().
 */
void exynos_close(struct exynos_data_base *data) {
  free(data->fliphandler);
  data->fliphandler = NULL;

  clean_up_drm(data->drm, data->fd);
  data->fd = -1;
  data->drm = NULL;
}

int exynos_init(struct exynos_data_base *data)
{
  struct exynos_drm *drm = data->drm;
  struct plane_info *primary, *overlay;

  const int fd = data->fd;

  drmModeConnector *connector = NULL;
  drmModeModeInfo *mode = NULL;
  unsigned i;

  const unsigned fullscreen[2] = {
    g_settings.video.fullscreen_x,
    g_settings.video.fullscreen_y
  };

  assert(drm != NULL);
  assert(fd >= 0);

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
    // Select first mode, which is the native one.
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

  if (create_restore_request(fd, drm)) {
    RARCH_ERR("exynos_init: failed to create restore atomic request\n");
    goto fail;
  }

  if (create_modeset_request(fd, drm, mode->hdisplay, mode->vdisplay)) {
    RARCH_ERR("exynos_init: failed to create modeset atomic request\n");
    goto fail;
  }

  primary = &data->plane_infos[plane_primary];
  overlay = &data->plane_infos[plane_overlay];

  primary->width = mode->hdisplay;
  primary->height = mode->vdisplay;
  overlay->width = mode->hdisplay / 2;
  overlay->height = mode->vdisplay / 2;

  fill_plane_info(primary);
  fill_plane_info(overlay);

  RARCH_LOG("exynos_init: selected %ux%u resolution with %s pixel format (primary plane)\n",
            mode->hdisplay, mode->vdisplay, fmt_pixel_format(primary));

  drmModeFreeConnector(connector);

  return 0;

fail:
  drmModeDestroyPropertyBlob(fd, drm->mode_blob_id);
  drmModeFreeConnector(connector);

  return -1;
}

/*
 * Counterpart to exynos_init().
 */
void exynos_deinit(struct exynos_data_base *data)
{
  struct exynos_drm *drm = data->drm;

  assert(data->num_pages == 0);
  assert(data->pages == NULL);

  drmModeDestroyPropertyBlob(data->fd, drm->mode_blob_id);

  drm = NULL;

  memset(data->plane_infos, 0, sizeof(struct plane_info) * exynos_plane_max);
}

int exynos_register_page(struct exynos_data_base *data,
                         struct exynos_page_base *page)
{
  void *npages;

  npages = realloc(data->pages, sizeof(void*) * (data->num_pages + 1));
  if (!npages)
    return -1;

  data->pages = npages;
  data->pages[data->num_pages] = page;

  data->num_pages++;

  return 0;
}

void exynos_unregister_pages(struct exynos_data_base *data)
{
  if (data->num_pages == 0)
    return;

  free(data->pages);
  data->num_pages = 0;
}

int exynos_alloc(struct exynos_data_base *data)
{
  //struct exynos_device *device;

  unsigned i;

  uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
  const unsigned flags = 0;

  assert(data != NULL);

  data->device = exynos_device_create(data->fd);
  if (data->device == NULL) {
    RARCH_ERR("exynos_alloc: failed to create device from fd\n");
    return -1;
  }

  if (data->num_pages == 0) {
    RARCH_ERR("exynos_alloc: no pages registered\n");
    goto fail_register;
  }

  for (i = 0; i < data->num_pages; ++i) {
    struct exynos_page_base *p = data->pages[i];
    unsigned j;

    for (j = 0; j < exynos_plane_max; ++j) {
      struct exynos_plane *plane;
      struct plane_info *pi;

      plane = &p->planes[j];
      pi = &data->plane_infos[j];

      plane->bo = exynos_bo_create(data->device, pi->size, flags);
      if (plane->bo == NULL) {
        RARCH_ERR("exynos_alloc: failed to create buffer object %u\n", i);
        goto fail;
      }

      // Don't map the BO, since we don't access it through userspace.

      handles[0] = plane->bo->handle;
      pitches[0] = pi->pitch;

      if (drmModeAddFB2(data->fd, pi->width, pi->height, pi->pixel_format,
                        handles, pitches, offsets, &plane->buf_id, flags)) {
        RARCH_ERR("exynos_alloc: failed to add bo %u to fb\n", i);
        goto fail;
      }

      if (create_page_request(p)) {
        RARCH_ERR("exynos_alloc: failed to create atomic request for page %u\n", i);
        goto fail;
      }
    }

    p->root = data;
    p->flags |= page_clear;
  }

  // Setup framebuffer: display the last allocated page.
  if (initial_modeset(data->fd, data->pages[data->num_pages - 1], data->drm)) {
    RARCH_ERR("exynos_alloc: initial atomic modeset failed\n");
    goto fail;
  }

  return 0;

fail:
  clean_up_pages(data->pages, data->num_pages);

fail_register:
  exynos_device_destroy(data->device);
  data->device = NULL;

  return -1;
}

/**
 * Counterpart to exynos_alloc().
 */
void exynos_free(struct exynos_data_base *data)
{
  // Restore the display state.
  if (drmModeAtomicCommit(data->fd, data->drm->restore_request,
      DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
    RARCH_WARN("exynos_free: failed to restore the display\n");
  }

  clean_up_pages(data->pages, data->num_pages);

  exynos_device_destroy(data->device);
  data->device = NULL;
}

struct exynos_page_base* exynos_get_free_page(struct exynos_data_base *data)
{
  struct exynos_page_base **pages = data->pages;
  unsigned i;

  for (i = 0; i < data->num_pages; ++i) {
    if (pages[i]->flags & page_used)
      continue;

    return pages[i];
  }

  return NULL;
}

void exynos_wait_for_flip(struct exynos_data_base *data)
{
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

int exynos_issue_flip(struct exynos_data_base *data, struct exynos_page_base *page)
{
  struct exynos_plane *primary, *overlay;
  drmModeAtomicReq *request;

  int ret;

  assert(data != NULL);
  assert(page != NULL);

  primary = &page->planes[plane_primary];

  if (page->flags & page_overlay) {
    overlay = &page->planes[plane_overlay];

    request = drmModeAtomicDuplicate(primary->atomic_request);

    ret = drmModeAtomicMerge(request, overlay->atomic_request);
    if (ret < 0) {
      RARCH_ERR("exynos_issue_flip: failed to merge requests (%d)\n", ret);
      return -1;
    }
  } else {
    request = primary->atomic_request;
  }

  // We don't queue multiple page flips.
  if (data->pageflip_pending > 0)
    exynos_wait_for_flip(data);

  // Issue a page flip at the next vblank interval.
  ret = drmModeAtomicCommit(data->fd, request, DRM_MODE_PAGE_FLIP_EVENT, page);
  if (ret < 0) {
    RARCH_ERR("exynos_issue_flip: failed to issue atomic page flip (%d)\n", ret);

    ret = -2;
    goto out;
  }

  data->pageflip_pending++;

  // On startup no frame is displayed. We therefore wait for the initial flip to finish.
  if (!data->cur_page)
    exynos_wait_for_flip(data);

out:
  if (page->flags & page_overlay)
    drmModeAtomicFree(request);

  return ret;
}
