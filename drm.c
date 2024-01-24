/**
 * @file drm.c
 *
 */

/*
 * Copyright (c) 2019 LittlevGL
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*********************
 *      INCLUDES
 *********************/

#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <event.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "lvgl/lvgl.h"
#include "wslv_drm.h"

#define DRM_CONNECTOR_ID -1
#define DRM_CARD "/dev/dri/card0"

#define DBG_TAG "drm"

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

#define print(msg, ...)	fprintf(stderr, msg, ##__VA_ARGS__);
#define err(msg, ...)  print("error: " msg "\n", ##__VA_ARGS__)
#define info(msg, ...) print(msg "\n", ##__VA_ARGS__)
#if 1
#define dbg(msg, ...)  do { } while (0)
#else
#define dbg(msg, ...)  print(DBG_TAG ": " msg "\n", ##__VA_ARGS__)
#endif

static void drm_done_vsync(int, short, void *);

struct drm_buffer {
	uint32_t handle;
	uint32_t pitch;
	uint32_t offset;
	size_t size;
	void *map;
	uint32_t fb_handle;
};

struct drm_dev {
	int fd;
	uint32_t conn_id, enc_id, crtc_id, plane_id, crtc_idx;
	uint32_t width, height;
	uint32_t mmWidth, mmHeight;
	uint32_t fourcc;
	drmModeModeInfo mode;
	uint32_t blob_id;
	drmModeCrtc *saved_crtc;
	drmModeAtomicReq *req;
	drmEventContext drm_event_ctx;
	drmModePlane *plane;
	drmModeCrtc *crtc;
	drmModeConnector *conn;
	uint32_t count_plane_props;
	uint32_t count_crtc_props;
	uint32_t count_conn_props;
	drmModePropertyPtr plane_props[128];
	drmModePropertyPtr crtc_props[128];
	drmModePropertyPtr conn_props[128];
	struct drm_buffer drm_bufs[2]; /* DUMB buffers */

	struct event ev;

	int dpms;
	struct drm_buffer *cur_buf;

	unsigned long stat_done_vsync;
	unsigned long stat_wait_vsync;

	struct event stat_ev;
} drm_dev;

static uint32_t
get_plane_property_id(const char *name)
{
	uint32_t i;

	dbg("Find plane property: %s", name);

	for (i = 0; i < drm_dev.count_plane_props; ++i) {
		if (!strcmp(drm_dev.plane_props[i]->name, name))
			return (drm_dev.plane_props[i]->prop_id);
	}

	dbg("Unknown plane property: %s", name);

	return (0);
}

static uint32_t
get_crtc_property_id(const char *name)
{
	uint32_t i;

	dbg("Find crtc property: %s", name);

	for (i = 0; i < drm_dev.count_crtc_props; ++i) {
		if (!strcmp(drm_dev.crtc_props[i]->name, name))
			return (drm_dev.crtc_props[i]->prop_id);
	}

	dbg("Unknown crtc property: %s", name);

	return (0);
}

static uint32_t
get_conn_property_id(const char *name)
{
	uint32_t i;

	dbg("Find conn property: %s", name);

	for (i = 0; i < drm_dev.count_conn_props; ++i) {
		if (!strcmp(drm_dev.conn_props[i]->name, name))
			return (drm_dev.conn_props[i]->prop_id);
	}

	dbg("Unknown conn property: %s", name);

	return (0);
}

static void
page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
    unsigned int tv_usec, void *user_data)
{
	LV_UNUSED(fd);
	LV_UNUSED(sequence);
	LV_UNUSED(tv_sec);
	LV_UNUSED(tv_usec);
	LV_UNUSED(user_data);
	dbg("flip");
}

static int
drm_get_plane_props(void)
{
	uint32_t i;

	drmModeObjectPropertiesPtr props =
	    drmModeObjectGetProperties(drm_dev.fd, drm_dev.plane_id,
	    DRM_MODE_OBJECT_PLANE);
	if (!props) {
		err("drmModeObjectGetProperties failed");
		return (-1);
	}
	dbg("Found %u plane props", props->count_props);
	drm_dev.count_plane_props = props->count_props;
	for (i = 0; i < props->count_props; i++) {
		drm_dev.plane_props[i] = drmModeGetProperty(drm_dev.fd,
		    props->props[i]);
		dbg("Added plane prop %u:%s", drm_dev.plane_props[i]->prop_id,
		    drm_dev.plane_props[i]->name);
	}
	drmModeFreeObjectProperties(props);

	return (0);
}

static int
drm_get_crtc_props(void)
{
	uint32_t i;

	drmModeObjectPropertiesPtr props =
	    drmModeObjectGetProperties(drm_dev.fd, drm_dev.crtc_id,
	    DRM_MODE_OBJECT_CRTC);
	if (!props) {
		err("drmModeObjectGetProperties failed");
		return (-1);
	}
	dbg("Found %u crtc props", props->count_props);
	drm_dev.count_crtc_props = props->count_props;
	for (i = 0; i < props->count_props; i++) {
		drm_dev.crtc_props[i] = drmModeGetProperty(drm_dev.fd,
		    props->props[i]);
		dbg("Added crtc prop %u:%s", drm_dev.crtc_props[i]->prop_id,
		    drm_dev.crtc_props[i]->name);
	}
	drmModeFreeObjectProperties(props);

	return (0);
}

static int
drm_get_conn_props(void)
{
	uint32_t i;

	drmModeObjectPropertiesPtr props =
	    drmModeObjectGetProperties(drm_dev.fd, drm_dev.conn_id,
	    DRM_MODE_OBJECT_CONNECTOR);
	if (!props) {
		err("drmModeObjectGetProperties failed");
		return (-1);
	}
	dbg("Found %u connector props", props->count_props);
	drm_dev.count_conn_props = props->count_props;
	for (i = 0; i < props->count_props; i++) {
		drm_dev.conn_props[i] = drmModeGetProperty(drm_dev.fd,
		    props->props[i]);
		dbg("Added connector prop %u:%s",
		    drm_dev.conn_props[i]->prop_id,
		    drm_dev.conn_props[i]->name);
	}
	drmModeFreeObjectProperties(props);

	return (0);
}

static int
drm_add_plane_property(const char *name, uint64_t value)
{
	int ret;
	uint32_t prop_id = get_plane_property_id(name);

	if (!prop_id) {
		err("Couldn't find plane prop %s", name);
		return (-1);
	}

	ret = drmModeAtomicAddProperty(drm_dev.req, drm_dev.plane_id,
	    get_plane_property_id(name), value);
	if (ret < 0) {
		err("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d",
		    name, value, ret);
		return (ret);
	}

	return (0);
}

static int
drm_add_crtc_property(const char *name, uint64_t value)
{
	int ret;
	uint32_t prop_id = get_crtc_property_id(name);

	if (!prop_id) {
		err("Couldn't find crtc prop %s", name);
		return (-1);
	}

	ret = drmModeAtomicAddProperty(drm_dev.req, drm_dev.crtc_id,
	    get_crtc_property_id(name), value);
	if (ret < 0) {
		err("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d",
		    name, value, ret);
		return (ret);
	}

	return (0);
}

static int
drm_add_conn_property(const char *name, uint64_t value)
{
	int ret;
	uint32_t prop_id = get_conn_property_id(name);

	if (!prop_id) {
		err("Couldn't find conn prop %s", name);
		return (-1);
	}

	ret = drmModeAtomicAddProperty(drm_dev.req, drm_dev.conn_id,
	    get_conn_property_id(name), value);
	if (ret < 0) {
		err("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d",
		    name, value, ret);
		return (ret);
	}

	return (0);
}

static int
drm_dmabuf_set_plane(struct drm_buffer *buf)
{
	int ret;
	static int first = 1;
	uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT;

	drm_dev.req = drmModeAtomicAlloc();

	/* On first Atomic commit, do a modeset */
	if (first) {
		drm_add_conn_property("CRTC_ID", drm_dev.crtc_id);

		drm_add_crtc_property("MODE_ID", drm_dev.blob_id);
		drm_add_crtc_property("ACTIVE", 1);

		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

		first = 0;
	}

	drm_add_plane_property("FB_ID", buf->fb_handle);
	drm_add_plane_property("CRTC_ID", drm_dev.crtc_id);
	drm_add_plane_property("SRC_X", 0);
	drm_add_plane_property("SRC_Y", 0);
	drm_add_plane_property("SRC_W", drm_dev.width << 16);
	drm_add_plane_property("SRC_H", drm_dev.height << 16);
	drm_add_plane_property("CRTC_X", 0);
	drm_add_plane_property("CRTC_Y", 0);
	drm_add_plane_property("CRTC_W", drm_dev.width);
	drm_add_plane_property("CRTC_H", drm_dev.height);

	ret = drmModeAtomicCommit(drm_dev.fd, drm_dev.req, flags, NULL);
	if (ret) {
		err("drmModeAtomicCommit failed: %s", strerror(errno));
		drmModeAtomicFree(drm_dev.req);
		return (ret);
	}

	return (0);
}

int
drm_svideo(int on)
{
	int rv;
	uint32_t prop;
	int dpms = on ? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF;

	prop = get_conn_property_id("DPMS");
	if (prop == 0) {
		errno = EOPNOTSUPP;
		return (-1);
	}

	rv = drmModeConnectorSetProperty(drm_dev.fd, drm_dev.conn_id,
	    prop, dpms);
	if (rv == -1) {
		printf("svideo drmModeConnectorSetProperty failed: %s\n",
		    strerror(errno));
		return (-1);
	}

	drm_dev.dpms = dpms;
	if (on)
		drm_dmabuf_set_plane(drm_dev.cur_buf);

	return (0);
}

static int
find_plane(unsigned int fourcc, uint32_t *plane_id,
    uint32_t crtc_id, uint32_t crtc_idx)
{
	LV_UNUSED(crtc_id);
	drmModePlaneResPtr planes;
	drmModePlanePtr plane;
	unsigned int i;
	unsigned int j;
	int ret = 0;
	unsigned int format = fourcc;

	planes = drmModeGetPlaneResources(drm_dev.fd);
	if (!planes) {
		err("drmModeGetPlaneResources failed");
		return (-1);
	}

	dbg("drm: found planes %u", planes->count_planes);

	for (i = 0; i < planes->count_planes; ++i) {
		plane = drmModeGetPlane(drm_dev.fd, planes->planes[i]);
		if (!plane) {
			err("drmModeGetPlane failed: %s", strerror(errno));
			break;
		}

		if (!(plane->possible_crtcs & (1 << crtc_idx))) {
			drmModeFreePlane(plane);
			continue;
		}

		for (j = 0; j < plane->count_formats; ++j) {
			if (plane->formats[j] == format)
				break;
		}

		if (j == plane->count_formats) {
			drmModeFreePlane(plane);
			continue;
		}

		*plane_id = plane->plane_id;
		drmModeFreePlane(plane);

		dbg("found plane %d", *plane_id);

		break;
	}

	if (i == planes->count_planes)
		ret = -1;

	drmModeFreePlaneResources(planes);

	return (ret);
}

static int drm_find_connector(void)
{
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc = NULL;
	drmModeRes *res;
	int i;

	if ((res = drmModeGetResources(drm_dev.fd)) == NULL) {
		err("drmModeGetResources() failed");
		return (-1);
	}

	if (res->count_crtcs <= 0) {
		err("no Crtcs");
		goto free_res;
	}

	/* find all available connectors */
	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(drm_dev.fd, res->connectors[i]);
		if (!conn)
			continue;

#if DRM_CONNECTOR_ID >= 0
		if (conn->connector_id != DRM_CONNECTOR_ID) {
			drmModeFreeConnector(conn);
			continue;
		}
#endif

		if (conn->connection == DRM_MODE_CONNECTED) {
			dbg("drm: connector %d: connected",
			    conn->connector_id);
		} else if (conn->connection == DRM_MODE_DISCONNECTED) {
			dbg("drm: connector %d: disconnected",
			    conn->connector_id);
		} else if (conn->connection == DRM_MODE_UNKNOWNCONNECTION) {
			dbg("drm: connector %d: unknownconnection",
			    conn->connector_id);
		} else {
			dbg("drm: connector %d: unknown",
			    conn->connector_id);
		}

		if (conn->connection == DRM_MODE_CONNECTED &&
		    conn->count_modes > 0)
			break;

		drmModeFreeConnector(conn);
		conn = NULL;
	};

	if (!conn) {
		err("suitable connector not found");
		goto free_res;
	}

	drm_dev.conn_id = conn->connector_id;
	dbg("conn_id: %d", drm_dev.conn_id);
	drm_dev.mmWidth = conn->mmWidth;
	drm_dev.mmHeight = conn->mmHeight;

	memcpy(&drm_dev.mode, &conn->modes[0], sizeof(drmModeModeInfo));

	if (drmModeCreatePropertyBlob(drm_dev.fd,
	    &drm_dev.mode, sizeof(drm_dev.mode), &drm_dev.blob_id)) {
		err("error creating mode blob");
		goto free_res;
	}

	drm_dev.width = conn->modes[0].hdisplay;
	drm_dev.height = conn->modes[0].vdisplay;

	for (i = 0 ; i < res->count_encoders; i++) {
		enc = drmModeGetEncoder(drm_dev.fd, res->encoders[i]);
		if (!enc)
			continue;

		dbg("enc%d enc_id %d conn enc_id %d", i,
		    enc->encoder_id, conn->encoder_id);

		if (enc->encoder_id == conn->encoder_id)
			break;

		drmModeFreeEncoder(enc);
		enc = NULL;
	}

	if (enc) {
		drm_dev.enc_id = enc->encoder_id;
		dbg("enc_id: %d", drm_dev.enc_id);
		drm_dev.crtc_id = enc->crtc_id;
		dbg("crtc_id: %d", drm_dev.crtc_id);
		drmModeFreeEncoder(enc);
	} else {
		/* Encoder hasn't been associated yet, look it up */
		for (i = 0; i < conn->count_encoders; i++) {
			int crtc, crtc_id = -1;

			enc = drmModeGetEncoder(drm_dev.fd, conn->encoders[i]);
			if (!enc)
				continue;

			for (crtc = 0 ; crtc < res->count_crtcs; crtc++) {
				uint32_t crtc_mask = 1 << crtc;

				crtc_id = res->crtcs[crtc];

				dbg("enc_id %d "
				    "crtc%d id %d mask %x "
				    "possible %x",
				    enc->encoder_id,
				    crtc, crtc_id, crtc_mask,
				    enc->possible_crtcs);

				if (enc->possible_crtcs & crtc_mask)
					break;
			}

			if (crtc_id > 0) {
				drm_dev.enc_id = enc->encoder_id;
				dbg("enc_id: %d", drm_dev.enc_id);
				drm_dev.crtc_id = crtc_id;
				dbg("crtc_id: %d", drm_dev.crtc_id);
				break;
			}

			drmModeFreeEncoder(enc);
			enc = NULL;
		}

		if (!enc) {
			err("suitable encoder not found");
			goto free_res;
		}

		drmModeFreeEncoder(enc);
	}

	drm_dev.crtc_idx = UINT32_MAX;

	for (i = 0; i < res->count_crtcs; ++i) {
		if (drm_dev.crtc_id == res->crtcs[i]) {
			drm_dev.crtc_idx = i;
			break;
		}
	}

	if (drm_dev.crtc_idx == UINT32_MAX) {
		err("drm: CRTC not found");
		goto free_res;
	}

	dbg("crtc_idx: %d", drm_dev.crtc_idx);

	return (0);

free_res:
	drmModeFreeResources(res);

	return (-1);
}

static int
drm_open(const char *path)
{
	int fd, flags;
	uint64_t has_dumb;
	int ret;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		err("cannot open \"%s\"", path);
		return (-1);
	}

	/* set FD_CLOEXEC flag */
	if ((flags = fcntl(fd, F_GETFD)) < 0 ||
	     fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
		err("fcntl FD_CLOEXEC failed");
		goto err;
	}

	/* check capability */
	ret = drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb);
	if (ret < 0 || has_dumb == 0) {
		err("drmGetCap DRM_CAP_DUMB_BUFFER failed "
		    "or \"%s\" doesn't have dumb buffer", path);
		goto err;
	}

	return (fd);
err:
	close(fd);
	return (-1);
}

static int
drm_setup(unsigned int fourcc)
{
	int ret;
	const char *device_path = NULL;

	device_path = getenv("DRM_CARD");
	if (!device_path)
		device_path = DRM_CARD;

	drm_dev.fd = drm_open(device_path);
	if (drm_dev.fd < 0)
		return (-1);

	ret = drmSetClientCap(drm_dev.fd, DRM_CLIENT_CAP_ATOMIC, 1);
	if (ret) {
		err("No atomic modesetting support: %s", strerror(errno));
		goto err;
	}

	ret = drm_find_connector();
	if (ret) {
		err("available drm devices not found");
		goto err;
	}

	ret = find_plane(fourcc, &drm_dev.plane_id,
	    drm_dev.crtc_id, drm_dev.crtc_idx);
	if (ret) {
		err("Cannot find plane");
		goto err;
	}

	drm_dev.plane = drmModeGetPlane(drm_dev.fd, drm_dev.plane_id);
	if (!drm_dev.plane) {
		err("Cannot get plane");
		goto err;
	}

	drm_dev.crtc = drmModeGetCrtc(drm_dev.fd, drm_dev.crtc_id);
	if (!drm_dev.crtc) {
		err("Cannot get crtc");
		goto err;
	}

	drm_dev.conn = drmModeGetConnector(drm_dev.fd, drm_dev.conn_id);
	if (!drm_dev.conn) {
		err("Cannot get connector");
		goto err;
	}

	ret = drm_get_plane_props();
	if (ret) {
		err("Cannot get plane props");
		goto err;
	}

	ret = drm_get_crtc_props();
	if (ret) {
		err("Cannot get crtc props");
		goto err;
	}

	ret = drm_get_conn_props();
	if (ret) {
		err("Cannot get connector props");
		goto err;
	}

	drm_dev.drm_event_ctx.version = DRM_EVENT_CONTEXT_VERSION;
	drm_dev.drm_event_ctx.page_flip_handler = page_flip_handler;
	drm_dev.fourcc = fourcc;

	info("drm: Found plane_id: %u connector_id: %d crtc_id: %d",
		drm_dev.plane_id, drm_dev.conn_id, drm_dev.crtc_id);

	info("drm: %dx%d (%dmm X% dmm) pixel format %c%c%c%c",
	    drm_dev.width, drm_dev.height, drm_dev.mmWidth, drm_dev.mmHeight,
	    (fourcc>>0)&0xff, (fourcc>>8)&0xff,
	    (fourcc>>16)&0xff, (fourcc>>24)&0xff);

	drm_dev.dpms = DRM_MODE_DPMS_ON;

	return (0);

err:
	close(drm_dev.fd);
	return (-1);
}

static int
drm_allocate_dumb(struct drm_buffer *buf)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_map_dumb mreq;
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	int ret;

	/* create dumb buffer */
	memset(&creq, 0, sizeof(creq));
	creq.width = drm_dev.width;
	creq.height = drm_dev.height;
	creq.bpp = LV_COLOR_DEPTH;
	ret = drmIoctl(drm_dev.fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		err("DRM_IOCTL_MODE_CREATE_DUMB fail");
		return (-1);
	}

	buf->handle = creq.handle;
	buf->pitch = creq.pitch;
	dbg("pitch %d", buf->pitch);
	buf->size = creq.size;
	dbg("size %zu", buf->size);

	/* prepare buffer for memory mapping */
	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = creq.handle;
	ret = drmIoctl(drm_dev.fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		err("DRM_IOCTL_MODE_MAP_DUMB fail");
		return (-1);
	}

	buf->offset = mreq.offset;

	/* perform actual memory mapping */
	buf->map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED,
	    drm_dev.fd, mreq.offset);
	if (buf->map == MAP_FAILED) {
		err("mmap fail");
		return (-1);
	}

	/* clear the framebuffer to 0 (= full transparency in ARGB8888) */
	memset(buf->map, 0, creq.size);

	/* create framebuffer object for the dumb-buffer */
	handles[0] = creq.handle;
	pitches[0] = creq.pitch;
	offsets[0] = 0;
	ret = drmModeAddFB2(drm_dev.fd, drm_dev.width, drm_dev.height,
	    drm_dev.fourcc, handles, pitches, offsets, &buf->fb_handle, 0);
	if (ret) {
		err("drmModeAddFB fail");
		return (-1);
	}

	return (0);
}

static int
drm_setup_buffers(void)
{
	int ret;

	/* Allocate DUMB buffers */
	ret = drm_allocate_dumb(&drm_dev.drm_bufs[0]);
	if (ret)
		return (ret);

	ret = drm_allocate_dumb(&drm_dev.drm_bufs[1]);
	if (ret)
		return (ret);

	if (drm_dev.drm_bufs[0].pitch != drm_dev.drm_bufs[1].pitch) {
		err("buffer pitch mismatch");
		return (-1);
	}

	return (0);
}

uint64_t wslv_ms(void);

void
drm_wait_vsync(lv_disp_drv_t *disp_drv)
{
	LV_UNUSED(disp_drv);

	int ret;
	struct pollfd pfd;

	if (drm_dev.req == NULL) {
		lv_disp_flush_ready(drm_dev.ev.ev_arg);
		return;
	}

	do {
		pfd.fd = drm_dev.fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		ret = poll(&pfd, 1, INFTIM);
	} while (ret == -1 && errno == EINTR);

	if (ret < 0) {
		err("select failed: %s", strerror(errno));
		drmModeAtomicFree(drm_dev.req);
		drm_dev.req = NULL;
		return;
	}

	if (ret == 1 && pfd.revents & POLLIN) {
		dbg("%s handled", __func__);
		drmHandleEvent(drm_dev.fd, &drm_dev.drm_event_ctx);
	}

	drmModeAtomicFree(drm_dev.req);
	drm_dev.req = NULL;

	drm_dev.stat_wait_vsync++;
}

static void
drm_done_vsync(int fd, short events, void *arg)
{
	lv_disp_drv_t *disp_drv = arg;

	drmHandleEvent(drm_dev.fd, &drm_dev.drm_event_ctx);
	drmModeAtomicFree(drm_dev.req);
	drm_dev.req = NULL;

	lv_disp_flush_ready(disp_drv);

	lv_refr_now(NULL);
	drm_dev.stat_done_vsync++;
}

void
drm_refresh(void)
{
	if (drm_dev.req == NULL)
		lv_refr_now(NULL);
}

void
drm_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
	int bufi = (void *)color_p == drm_dev.drm_bufs[1].map;
	struct drm_buffer *fbuf = &drm_dev.drm_bufs[bufi];
	lv_color_int_t *map = fbuf->map;
	uint32_t w = (area->x2 - area->x1 + 1);
	uint32_t h = (area->y2 - area->y1 + 1);
	int x, y;

	dbg("bufi %d x %d:%d y %d:%d w %d h %d", bufi,
	    area->x1, area->x2, area->y1, area->y2, w, h);

	if (!lv_disp_flush_is_last(disp_drv)) {
		lv_disp_flush_ready(disp_drv);
		return;
	}

	if (drm_dev.req)
		drm_wait_vsync(disp_drv);

	drm_dev.cur_buf = fbuf;
	if (drm_dev.dpms != DRM_MODE_DPMS_ON) {
		lv_disp_flush_ready(disp_drv);
		return;
	}

	/* show fbuf plane */
	if (drm_dmabuf_set_plane(fbuf)) {
		err("Flush fail");
		return;
	} else
		dbg("Flush done");

	event_add(&drm_dev.ev, NULL);
}

void *
drm_get_fb(int i)
{
	return (drm_dev.drm_bufs[i].map);
}

static const struct timeval drm_stat_ival = { 1, 0 };

static void
drm_stats(int nil, short revents, void *null)
{
	evtimer_add(&drm_dev.stat_ev, &drm_stat_ival);

	printf("wait %lu, done %lu\n",
	    drm_dev.stat_wait_vsync, drm_dev.stat_done_vsync);
	drm_dev.stat_wait_vsync = 0;
	drm_dev.stat_done_vsync = 0;
}

void
drm_event_set(lv_disp_drv_t *disp_drv)
{
	event_set(&drm_dev.ev, drm_dev.fd, EV_READ, drm_done_vsync, disp_drv);
	evtimer_set(&drm_dev.stat_ev, drm_stats, NULL);
	//evtimer_add(&drm_dev.stat_ev, &drm_stat_ival);

	printf("clock %u htotal %u vtotal %u vrefresh %u\n",
	    drm_dev.mode.clock, drm_dev.mode.htotal,
	    drm_dev.mode.vtotal, drm_dev.mode.vrefresh);
}

#if LV_COLOR_DEPTH == 32
#define DRM_FOURCC DRM_FORMAT_XRGB8888
#elif LV_COLOR_DEPTH == 16
#define DRM_FOURCC DRM_FORMAT_RGB565
#else
#error LV_COLOR_DEPTH not supported
#endif

void
drm_get_sizes(lv_coord_t *pitch, lv_coord_t *width, lv_coord_t *height,
    uint32_t *dpi)
{
	*pitch = drm_dev.drm_bufs[0].pitch;

	if (width)
		*width = drm_dev.width;

	if (height)
		*height = drm_dev.height;

	if (dpi && drm_dev.mmWidth)
		*dpi = DIV_ROUND_UP(drm_dev.width * 25400, drm_dev.mmWidth * 1000);
}

int
drm_init(void)
{
	int ret;

	ret = drm_setup(DRM_FOURCC);
	if (ret) {
		close(drm_dev.fd);
		drm_dev.fd = -1;
		return (-1);
	}

	ret = drm_setup_buffers();
	if (ret) {
		err("DRM buffer allocation failed");
		close(drm_dev.fd);
		drm_dev.fd = -1;
		return (-1);
	}

	info("DRM subsystem and buffer mapped successfully");

	return (0);
}

void
drm_exit(void)
{
	close(drm_dev.fd);
	drm_dev.fd = -1;
}
