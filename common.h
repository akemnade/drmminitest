// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Kemnade
#include <stdint.h>
#include <stdbool.h>
#include <xf86drmMode.h>

/* returns true if search should continue */
typedef bool (*drm_found_func_t)(void *data, int fd, uint32_t conn_id, drmModeModeInfo *mode, uint32_t crtc_id);

struct drm_buf {
	uint32_t handle;
	uint32_t fb;
	uint32_t size;
	uint32_t pitch;
	void *map;
	uint32_t width, height;
};

bool search_drm(drm_found_func_t func, void *data);
void create_dumb_buffer(int drm_fd, struct drm_buf *b, uint32_t width, uint32_t height, uint32_t bpp);
void destroy_dumb_buffer(int drm_fd, struct drm_buf *b);
