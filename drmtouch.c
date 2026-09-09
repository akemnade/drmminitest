// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Kemnade
// simple touchscreen tester, draws points
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
#include <qrencode.h>
#include <dirent.h>
#include <linux/input.h>
#include <limits.h>
#include "common.h"

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

struct drm_draw_data {
	bool invert;
	bool use_mt;
	int ifd;
};

static int get_touchscreen()
{
	int fd = -1;
	struct dirent *ent;
	unsigned long propbits[INPUT_PROP_MAX];
	unsigned long evbits[EV_MAX];
	DIR *d = opendir("/dev/input");
	if (!d)
		return -1;

	while((ent = readdir(d))) {
		bool is_direct = false;
		bool has_abs = false;
		fd = openat(dirfd(d), ent->d_name, O_RDONLY);
		if (fd < 0)
			continue;

		memset(propbits, 0, sizeof(propbits));
		if (0 <= ioctl(fd, EVIOCGPROP(sizeof(propbits)), propbits)) {
			int i;
			unsigned long tmp;

			if (test_bit(INPUT_PROP_DIRECT, propbits))
				is_direct = true;

			tmp = 0;
			for(i = 0; i < INPUT_PROP_MAX; i++)
				tmp |= propbits[i];

			/* e.g. tsc2007 has no props at all, at least
			 * PROP_POINTER is not set
			 */
			if (!tmp)
				is_direct = true;
		}

		if (0 <= ioctl(fd, EVIOCGBIT(0, EV_MAX), evbits)) {
			if (test_bit(EV_ABS, evbits))
				has_abs = true;
		}

		if (has_abs && is_direct)  {
			printf("touchscreen found at %s\n", ent->d_name);
			break;
		}

		close(fd);
		fd = -1;
	}

	closedir(d);
	return fd;
}

static void put_pixel(struct drm_buf *buf, struct drm_clip_rect *r, unsigned int x, unsigned int y, uint32_t value)
{
	uint32_t *base = (uint32_t *) buf->map;
	if (x >= buf->width)
		return;

	if (y >= buf->height)
		return;

	base[y * buf->pitch / sizeof(uint32_t) + x] = value;

	if (x < r->x1)
		r->x1 = x;
	
	if (x > r->x2)
		r->x2 = x;

	if (y < r->y1)
		r->y1 = y;

	if (y > r->y2)
		r->y2 = y;
}

static void put_2x2_pixel(struct drm_buf *buf, struct drm_clip_rect *r, unsigned int x, unsigned int y, uint32_t value)
{
	unsigned int xs, ys;
	x&=~1;
	y&=~1;
	for(ys=0 ; ys < 2; ys++)
		for(xs=0; xs < 2; xs++)
			put_pixel(buf, r, x + xs, y + ys, value);
}

static int translate_abs(int val, struct input_absinfo *absi, int target)
{
	val = (val - absi->minimum) * target / (absi->maximum - absi->minimum);
	return val;
}

static bool found;
static bool draw_touch(void *data, int drm_fd, uint32_t conn_id, drmModeModeInfo *mode, uint32_t crtc_id)
{
	struct drm_draw_data *drdata = (struct drm_draw_data *)data;
	static struct drm_buf bufs;
	unsigned short x = 0;
	unsigned short y = 0;
	uint32_t pen_color = 0xffffff;
	bool have_abs = false;

	uint32_t width = mode->hdisplay;
	uint32_t height = mode->vdisplay;

	struct input_absinfo absx = {
		.minimum = 0,
		.maximum = (int)width
	};

	struct input_absinfo absy = {
		.minimum = 0,
		.maximum = (int)height
	};
	found = true;
	ioctl(drdata->ifd, EVIOCGABS(0), &absx);
	ioctl(drdata->ifd, EVIOCGABS(1), &absy);
	printf("x: min: %d max: %d\n", absx.minimum, absx.maximum);
	printf("y: min: %d max: %d\n", absy.minimum, absy.maximum);

	create_dumb_buffer(drm_fd, &bufs, width, height, 32);

	usleep(100000);
	struct drm_clip_rect dummyclip = {0};

	if (drdata->invert) {
		pen_color = 0;

		for(y = 0; y < bufs.height; y++)
			for(x=0; x < bufs.width; x++) 
				put_pixel(&bufs, &dummyclip, x, y, 0xffffff);
	}

	if (drmModeSetCrtc(drm_fd, crtc_id, bufs.fb, 0, 0, &conn_id, 1, mode)) {
		fprintf(stderr, "failed to drmModeSetCrtc on %d/%d", conn_id, crtc_id);
		return false;
	}

	struct drm_clip_rect clip = {
		.x1 = USHRT_MAX,
		.x2 = 0,
		.y1 = USHRT_MAX,
		.y2 = 0,
	};
	unsigned short mt_x[10];
	unsigned short mt_y[10];
	int current_slot = -1;
	unsigned int slots_used = 0;

	while(1) {
		struct input_event iev;
		int i;
		struct pollfd pfd = {
			.fd = drdata->ifd,
			.events = POLLIN
		};

		if (clip.x1 <= clip.x2) {
			/* somehow smaller rectangles are not wanted in mxc_pdc -> todo fix */
			clip.x2 += 4;
			clip.y2 += 4;
			
			if (!poll(&pfd, 1, 0)) {
				int ret;
				ret = drmModeDirtyFB(drm_fd, bufs.fb, &clip, 1);
				printf("dirty: (%d,%d) - (%d, %d) -> %d\n", clip.x1, clip.y1, clip.x2, clip.y2, ret);
				//printf("page flip: %d\n", drmModePageFlip(drm_fd, crtc_id, bufs.fb, DRM_MODE_PAGE_FLIP_EVENT, NULL));
				//usleep(100000);
				clip.x2 = 0;
				clip.y2 = 0;
				clip.x1 = USHRT_MAX;
				clip.y1 = USHRT_MAX;
			}
		}
		if (!pfd.revents)
			poll(&pfd, 1, 10000);

		if (!(pfd.revents & POLLIN))
			break;

		if (read(drdata->ifd, &iev, sizeof(iev)) != sizeof(iev))
			break;

		if (iev.type == EV_SYN) {
			if (have_abs && ((!slots_used) || (!drdata->use_mt))) {
				printf("%d, %d\n", x, y);

				put_2x2_pixel(&bufs, &clip, x, y, pen_color);
			}
			have_abs = false;
			current_slot = -1;
			for(i = 0; i < sizeof(mt_x) / sizeof(mt_x[0]); i++) {
				if (slots_used & (1 << i)) {
					printf("%d: %d, %d\n", i, mt_x[i], mt_y[i]);
					put_2x2_pixel(&bufs, &clip, mt_x[i], mt_y[i], pen_color);
				}
			}

			slots_used = 0;
		}

		if (iev.type == EV_ABS) {
			switch (iev.code) {
				case ABS_X:
					x = translate_abs(iev.value, &absx, width);
					have_abs = true;
					break;
				case ABS_Y:
					y = translate_abs(iev.value, &absy, height);
					have_abs = true;
					break;
				case ABS_MT_SLOT:
					current_slot = iev.value;
					break; 
				case ABS_MT_POSITION_X:
				case ABS_MT_POSITION_Y:
					if (drdata->use_mt && (current_slot >= 0) && (current_slot <= sizeof(mt_x) / sizeof(mt_x[0]))) {
						if (iev.code == ABS_MT_POSITION_X)
							mt_x[current_slot] = translate_abs(iev.value, &absx, width);
						else
							mt_y[current_slot] = translate_abs(iev.value, &absy, height);

						slots_used |= 1 << current_slot;
					}
					break;
			}
		}

	}

	/* cleanup */
	destroy_dumb_buffer(drm_fd, &bufs);
	return false;
}

int main(int argc, char **argv)
{
	struct drm_draw_data drdata = {0};
	int i;

	for(i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "invert"))
			drdata.invert = true;
		else if (!strcmp(argv[i], "mt"))
			drdata.use_mt = true;
		else {
			fprintf(stderr, "Usage: %s [invert] [mt]\n", argv[0]);
			return 1;
		}

	}

	drdata.ifd = get_touchscreen();
	if (drdata.ifd < 0) {
		fprintf(stderr, "no touchscreen\n");
		return 1;
	}

	search_drm(draw_touch, &drdata);
	if (!found) {
		fprintf(stderr, "no suitable output found\n");
		return 1;
	}

	return 0;
}
