// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Andreas Kemnade
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "common.h"

static int pageflip_done = 0;

static void die(const char *s) 
{
	perror(s);
	exit(1);
}

static bool check_connector(int fd, drmModeResPtr res,
			    drmModeConnectorPtr conn,
			    drm_found_func_t func, void *data)
{
	int i;
	uint32_t crtc_id;
	drmModeEncoderPtr enc = NULL;
	if (conn->connection != DRM_MODE_CONNECTED)
		return false;

	if (conn->count_modes <= 0)
		return false;

	if (!conn->encoder_id) {
		if (conn->count_encoders)
			conn->encoder_id = conn->encoders[0];
	}

	if (!conn->encoder_id)
		return false;

	enc = drmModeGetEncoder(fd, conn->encoder_id);
	if (!enc)
		return false;

	crtc_id = enc->crtc_id;
	if (!crtc_id) {
		for (i = 0; i < res->count_crtcs; i ++) {
			if (enc->possible_crtcs & (1 << i))
				crtc_id = res->crtcs[i];
		}
	}
	drmModeFreeEncoder(enc);
	if (!crtc_id)
		return false;

	func(data, fd, conn->connector_id, &conn->modes[0], crtc_id);
	return true;
}

static bool check_drm_dev(int fd, drm_found_func_t func, void *data)
{
	int i;
	bool found = false;
	drmModeConnectorPtr conn = NULL;

	drmModeResPtr res = drmModeGetResources(fd);
	if (!res)
		return false;

	for (i = 0; i < res->count_connectors; ++i) {
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn)
			continue;

		found = check_connector(fd, res, conn, func, data);
		drmModeFreeConnector(conn);

		if (found)
			break;
	}

	drmModeFreeResources(res);
	return found;
}

void create_dumb_buffer(int drm_fd, struct drm_buf *b, uint32_t width, uint32_t height, uint32_t bpp)
{
	struct drm_mode_create_dumb creq = {
		.width = width,
		.height = height,
		.bpp = bpp
	};
	uint32_t handles[4] = {0};
	uint32_t pitches[4] = {0};
	uint32_t offsets[4] = {0};
	int ret;

	creq.width = width;
	creq.height = height;
	creq.bpp = bpp;
	if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0)
		die("CREATE_DUMB");

	b->handle = creq.handle;
	b->pitch = creq.pitch;
	b->size = creq.size;
	b->width = width; b->height = height;
	handles[0] = b->handle;
	pitches[0] = b->pitch;

	/* create framebuffer object */
	ret = drmModeAddFB2(drm_fd, width, height, DRM_FORMAT_XRGB8888,
			    handles, pitches, offsets, &b->fb, 0);
	if (ret)
		die("drmModeAddFB2");

	/* map it */
	struct drm_mode_map_dumb mreq = { .handle = b->handle};
	if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0)
		die("MAP_DUMB");

	b->map = mmap(0, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mreq.offset);
	if (b->map == MAP_FAILED)
		die("mmap");
}

void destroy_dumb_buffer(int drm_fd, struct drm_buf *b)
{
	if (b->fb)
		drmModeRmFB(drm_fd, b->fb);

	if (b->map && b->size)
		munmap(b->map, b->size);

	if (b->handle) {
		struct drm_mode_destroy_dumb dreq = {
			.handle = b->handle
		};

		ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	} 
}

static void page_flip_handler(int fd, unsigned int, unsigned int, unsigned int, void *) 
{
	pageflip_done = 1;
}

bool search_drm(drm_found_func_t func, void *data)
{
	int i;
	int drm_fd;
	bool found;

	for(i = 0; i < 32; i++) {
		char buf[64];

		snprintf(buf, sizeof(buf), "/dev/dri/card%d", i);
		drm_fd = open(buf, O_RDWR);
		if (drm_fd < 0)
			continue;

		drmVersionPtr version = drmGetVersion(drm_fd);
		if (!version)
			die("version");

		printf("Name: %s\n", version->name);

		found = check_drm_dev(drm_fd, func, data);

		close(drm_fd);
		drm_fd = -1;
	}
	return found;
}

