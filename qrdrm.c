// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Kemnade
// simple display tester displaying a qr code
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <qrencode.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "common.h"

struct drm_draw_data {
	char *qrcode;
};

static void draw_qr(struct drm_buf *buf, QRcode *qr)
{
	uint32_t *base = (uint32_t *) buf->map;
	uint32_t scale;
	uint32_t qr_left, qr_top, x, y, i;
	uint8_t *qr_data = qr->data;
	uint32_t qr_final_width;

	scale = buf->width / qr->width / 2;
	if (buf->height < buf->width)
		scale = buf->height / qr->width / 2;
	qr_final_width = qr->width * scale;

	qr_left = (buf->width - qr_final_width) / 2;
	qr_top = (buf->height - qr_final_width) / 2;

	base = base + buf->pitch / 4 * (qr_top - 2 *scale) + qr_left - 2 * scale;
	for (y = 0; y < qr_final_width + 4 * scale ; y ++) {
		for(x = 0; x < qr_final_width + 4 * scale; x++) {
			base[x] = 0xffffff;
		}
		base += buf->pitch / 4;
	}

	base = (uint32_t *) buf->map;
	base = base + buf->pitch / 4 * qr_top + qr_left;

	for(y = 0; y < qr->width; y++) {
		for (x = 0; x < qr->width; x++) {
			for(i = 0; i < scale; i ++) {
				base[x * scale + i] = (*qr_data) & 1 ? 0: 0xffffff;
			}
			qr_data++;
		}
		for(i = 1; i < scale; i++) {
			memcpy(base + i * buf->pitch / 4, base, sizeof(*base) * qr->width * scale * 4);
		}
		base += scale * buf->pitch / 4;
	}

}

static struct drm_buf bufs[10];
static int bufnum;
static bool found;

static bool draw_qr_code(void *data, int fd, uint32_t conn_id, drmModeModeInfo *mode, uint32_t crtc_id)
{
	struct drm_draw_data *drdata = (struct drm_draw_data *)data;
	struct drm_buf *buf = &bufs[bufnum];

	uint32_t width = mode->hdisplay;
	uint32_t height = mode->vdisplay;

	found = true;
	if (bufnum >= sizeof(bufs) / sizeof(bufs[0]))
		return false;

	create_dumb_buffer(fd, buf, width, height, 32);
	/* initial draw */
	bufnum++;

	QRcode *qr =  QRcode_encodeString(drdata->qrcode, 0, QR_ECLEVEL_Q, QR_MODE_8, 1);
	if(!qr)
		return true;

	draw_qr(buf, qr);

	if (drmModeSetCrtc(fd, crtc_id, buf->fb, 0, 0, &conn_id, 1, mode))
		fprintf(stderr, "failed to drmModeSetCrtc on %d/%d", conn_id, crtc_id);
	
	return true;
}

int main(int argc, char **argv) {
	struct drm_draw_data drdata = {};

	if(!argv[1]) {
		fprintf(stderr, "usage: %s 'text string'\n", argv[0]);
		exit(1);
	}

	drdata.qrcode = argv[1];

	search_drm(draw_qr_code, &drdata);

	if (!found)
		fprintf(stderr, "no suitable output found");
	else
		sleep(5);

	return 0;
}


