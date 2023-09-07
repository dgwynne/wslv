/* */

/*
 * Copyright (c) 2022, 2023 David Gwynne <david@gwynne.id.au>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <time.h>
#include <netdb.h>
#include <errno.h>
#include <err.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsksymdef.h>

#include <event.h>

#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "wslv_drm.h"

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#include <ctype.h>
static int
printable(int ch)
{
	if (ch == '\0')
		return ('_');
	if (!isprint(ch))
		return ('~');

	return (ch);
}

void
hexdump(const void *d, size_t datalen)
{
	const uint8_t *data = d;
	size_t i, j = 0;

	for (i = 0; i < datalen; i += j) {
		printf("%4zu: ", i);
		for (j = 0; j < 16 && i+j < datalen; j++)
			printf("%02x ", data[i + j]);
		while (j++ < 16)
			printf("   ");
		printf("|");
		for (j = 0; j < 16 && i+j < datalen; j++)
			putchar(printable(data[i + j]));
		printf("|\n");
	}
}

#define DEVNAME "/dev/ttyC0"

struct wslv_softc;

struct wslv_keypad_event {
	int				 ke_key;
	unsigned int			 ke_state;
	int16_t				 ke_enc_diff;
	TAILQ_ENTRY(wslv_keypad_event)	 ke_entry;
};
TAILQ_HEAD(wslv_keypad_events, wslv_keypad_event);

struct wslv_keypad {
	struct wslv_softc		*wk_wslv;
	const char			*wk_devname;
	struct event			 wk_ev;
	lv_indev_drv_t			 wk_lv_indev_drv;
	lv_indev_t			*wk_lv_indev;

	int				 wk_key;
	unsigned int			 wk_state;

	struct wslv_keypad_events	 wk_events;

	TAILQ_ENTRY(wslv_keypad)	 wk_entry;
};
TAILQ_HEAD(wslv_keypad_list, wslv_keypad);

struct wslv_pointer_event {
	uint32_t			 pe_x;
	uint32_t			 pe_y;
	unsigned int			 pe_pressed;
	TAILQ_ENTRY(wslv_pointer_event)	 pe_entry;
};
TAILQ_HEAD(wslv_pointer_events, wslv_pointer_event);

struct wslv_pointer {
	struct wslv_softc		*wp_wslv;
	const char			*wp_devname;
	struct event			 wp_ev;
	lv_indev_drv_t			 wp_lv_indev_drv;
	lv_indev_t			*wp_lv_indev;

	struct wsmouse_calibcoords	 wp_ws_calib;

	uint32_t			 wp_x;
	uint32_t			 wp_y;
	unsigned int			 wp_pressed;

	struct wslv_pointer_events	 wp_events;

	TAILQ_ENTRY(wslv_pointer)	 wp_entry;
};
TAILQ_HEAD(wslv_pointer_list, wslv_pointer);

struct wslv_softc {
	const char			*sc_name;

	int				 sc_ws_drm;
	int				 sc_ws_fd;
	unsigned char			*sc_ws_fb;
	unsigned char			*sc_ws_fb2;
	struct wsdisplay_fbinfo		 sc_ws_vinfo;
	unsigned int			 sc_ws_linebytes;
	size_t				 sc_ws_fblen;
	struct event			 sc_ws_ev;

	unsigned int			 sc_ws_omode;

	lv_disp_draw_buf_t		 sc_lv_disp_buf;
	lv_disp_drv_t			 sc_lv_disp_drv;
	lv_disp_t			*sc_lv_disp;

	struct event			 sc_tick;

	struct wslv_keypad_list		 sc_keypad_list;
	struct wslv_pointer_list	 sc_pointer_list;
};

struct wslv_softc _wslv;
struct wslv_softc *sc = &_wslv;

static int		wslv_open(struct wslv_softc *, const char *,
			    const char **);

static void		wslv_keypad_add(struct wslv_softc *, const char *);
static void		wslv_keypad_set(struct wslv_softc *);
static void		wslv_pointer_add(struct wslv_softc *, const char *);
static void		wslv_pointer_set(struct wslv_softc *);

static void		wslv_ws_rd(int, short, void *);
static void		wslv_tick(int, short, void *);

static void		wslv_lv_flush(lv_disp_drv_t *, const lv_area_t *,
			    lv_color_t *);

static void __dead
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-46] [-W wsdiplay]\n", __progname);

	exit(0);
}

int
main(int argc, char *argv[])
{
	const char *devname = DEVNAME;
	const char *errstr;
	int rv = 0;
	size_t x, y;
	uint32_t *word;
	int ch;

	TAILQ_INIT(&sc->sc_keypad_list);
	TAILQ_INIT(&sc->sc_pointer_list);

	while ((ch = getopt(argc, argv, "46d:h:K:M:p:W:")) != -1) {
		switch (ch) {
		case 'K':
			wslv_keypad_add(sc, optarg);
			break;
		case 'M':
			wslv_pointer_add(sc, optarg);
			break;
		case 'W':
			devname = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	if (wslv_open(sc, devname, &errstr) == -1)
		err(1, "%s %s", devname, errstr);

#if 0
	word = (uint32_t *)sc->sc_ws_fb;
	for (y = 0; y < sc->sc_ws_vinfo.height; y++) {
		for (x = 0; x < sc->sc_ws_vinfo.width; x++) {
			*word++ = 0x00ff0000;
		}
	}
	sleep(2);

	word = (uint32_t *)sc->sc_ws_fb;
	for (y = 0; y < sc->sc_ws_vinfo.height; y++) {
		for (x = 0; x < sc->sc_ws_vinfo.width; x++) {
			*word++ = 0x0000ff00;
		}
	}
	sleep(2);

	word = (uint32_t *)sc->sc_ws_fb;
	for (y = 0; y < sc->sc_ws_vinfo.height; y++) {
		for (x = 0; x < sc->sc_ws_vinfo.width; x++) {
			*word++ = 0x000000ff;
		}
	}
	sleep(2);
#endif

	lv_init();
	if (sc->sc_ws_drm) {
		lv_coord_t w, h;
		size_t len;

		if (drm_init() == -1)
			exit(1);

		drm_get_sizes(&w, &h, NULL);
		sc->sc_ws_vinfo.width = w;
		sc->sc_ws_vinfo.height = h;
		sc->sc_ws_vinfo.depth = LV_COLOR_DEPTH;
		sc->sc_ws_linebytes = w * (LV_COLOR_SIZE/8);

		len = sc->sc_ws_vinfo.height * sc->sc_ws_linebytes;

		sc->sc_ws_fb = malloc(len);
		if (sc->sc_ws_fb == NULL)
			err(1, "drm buffer");

		sc->sc_ws_fb2 = malloc(len);
		if (sc->sc_ws_fb2 == NULL)
			err(1, "drm buffer 2");

		sc->sc_ws_fblen = len;
	}
	event_init();

	lv_disp_draw_buf_init(&sc->sc_lv_disp_buf,
	    sc->sc_ws_fb, sc->sc_ws_fb2, sc->sc_ws_fblen);

	lv_disp_drv_init(&sc->sc_lv_disp_drv);
	sc->sc_lv_disp_drv.draw_buf = &sc->sc_lv_disp_buf;
	sc->sc_lv_disp_drv.hor_res = sc->sc_ws_vinfo.width;
	sc->sc_lv_disp_drv.ver_res = sc->sc_ws_vinfo.height;
	if (sc->sc_ws_drm) {
		sc->sc_lv_disp_drv.flush_cb = drm_flush;
		sc->sc_lv_disp_drv.wait_cb = drm_wait_vsync;
		sc->sc_lv_disp_drv.full_refresh = 1;
	} else {
		sc->sc_lv_disp_drv.flush_cb = wslv_lv_flush;
		sc->sc_lv_disp_drv.direct_mode = 1;
	}
	sc->sc_lv_disp_drv.user_data = sc;

	sc->sc_lv_disp = lv_disp_drv_register(&sc->sc_lv_disp_drv);

	fprintf(stderr,
	    "%s, %u * %u, %d bit mmap %p+%zu\n",
	    sc->sc_name, sc->sc_ws_vinfo.width, sc->sc_ws_vinfo.height,
	    sc->sc_ws_vinfo.depth, sc->sc_ws_fb, sc->sc_ws_fblen);

	wslv_keypad_set(sc);
	wslv_pointer_set(sc);

	event_set(&sc->sc_ws_ev, sc->sc_ws_fd, EV_READ|EV_PERSIST,
	    wslv_ws_rd, sc);
	event_add(&sc->sc_ws_ev, NULL);

	evtimer_set(&sc->sc_tick, wslv_tick, sc);
	wslv_tick(0, 0, sc);

lv_demo_widgets();
//lv_demo_keypad_encoder();
//lv_demo_benchmark();

	event_dispatch();

	sleep(2);

done:
	if (!sc->sc_ws_drm) {
		if (ioctl(sc->sc_ws_fd, WSDISPLAYIO_SMODE,
		    &sc->sc_ws_omode) == -1)
			warn("set original mode");
	}

	return (rv);
err:
	rv = 1;
	goto done;
}

static void
wslv_keypad_add(struct wslv_softc *sc, const char *devname)
{
	struct wslv_keypad *wk;

	wk = malloc(sizeof(*wk));
	if (wk == NULL)
		err(1, NULL);

	wk->wk_devname = devname;
	wk->wk_state = LV_INDEV_STATE_RELEASED;
	TAILQ_INIT(&wk->wk_events);

	TAILQ_INSERT_TAIL(&sc->sc_keypad_list, wk, wk_entry);
}

static void
wslv_pointer_add(struct wslv_softc *sc, const char *devname)
{
	struct wslv_pointer *wp;

	wp = malloc(sizeof(*wp));
	if (wp == NULL)
		err(1, NULL);

	wp->wp_devname = devname;
	TAILQ_INIT(&wp->wp_events);

	TAILQ_INSERT_TAIL(&sc->sc_pointer_list, wp, wp_entry);
}

static void
wslv_keypad_event_proc(struct wslv_keypad *wk,
    const struct wscons_event *wsevt)
{
	struct wslv_keypad_event *ke;
	lv_disp_t *disp = wk->wk_lv_indev_drv.disp;
	int v = wsevt->value;

	printf("%s: type %u value %d 0x%02x\n", __func__,
	    wsevt->type, v, v);

	switch (wsevt->type) {
	case WSCONS_EVENT_KEY_DOWN:
		wk->wk_key = v;
		wk->wk_state = LV_INDEV_STATE_PRESSED;
		break;
	case WSCONS_EVENT_KEY_UP:
		wk->wk_key = v;
		/* FALLTHROUGH */
	case WSCONS_EVENT_ALL_KEYS_UP:
		wk->wk_state = LV_INDEV_STATE_RELEASED;
		break;
	case WSCONS_EVENT_SYNC:
		break;
	default:
		printf("%s: type %u value %d\n", __func__,
		    wsevt->type, wsevt->value);
		return;
	}

	ke = malloc(sizeof(*ke));
	if (ke == NULL) {
		warn("%s", __func__);
		return;
	}

	ke->ke_key = wk->wk_key;
	ke->ke_state = wk->wk_state;

	TAILQ_INSERT_TAIL(&wk->wk_events, ke, ke_entry);
}

static void
wslv_keypad_event(int fd, short revents, void *arg)
{
	struct wslv_keypad *wk = arg;
	struct wscons_event wsevts[64];
	ssize_t rv;
	size_t i, n;

	rv = read(fd, wsevts, sizeof(wsevts));
	if (rv == -1) {
		warn("%s", __func__);
		return;
	}

	n = rv / sizeof(wsevts[0]);
	for (i = 0; i < n; i++)
		wslv_keypad_event_proc(wk, &wsevts[i]);
}

static const char *wsevt_type_names[] = {
	[WSCONS_EVENT_MOUSE_DELTA_X]		= "mouse rel x",
	[WSCONS_EVENT_MOUSE_DELTA_Y]		= "mouse rel x",
	[WSCONS_EVENT_MOUSE_ABSOLUTE_X]		= "mouse abs x",
	[WSCONS_EVENT_MOUSE_ABSOLUTE_Y]		= "mouse abs y",
	[WSCONS_EVENT_MOUSE_UP]			= "mouse up",
	[WSCONS_EVENT_MOUSE_DOWN]		= "mouse down",
	[WSCONS_EVENT_SYNC]			= "sync",
};

static const char *
wsevt_type_name(u_int type)
{
	if (type >= nitems(wsevt_type_names))
		return (NULL);

	return (wsevt_type_names[type]);
}

static void
wslv_pointer_event_proc(struct wslv_pointer *wp,
    const struct wscons_event *wsevt)
{
	const struct wsmouse_calibcoords *cc = &wp->wp_ws_calib;
	struct wslv_pointer_event *pe;
	lv_disp_t *disp = wp->wp_lv_indev_drv.disp;
	int v = wsevt->value;
	int d;

	if (0) {
		const char *typename;

		typename = wsevt_type_name(wsevt->type);
		if (typename != NULL) {
			warnx("%s: evt \"%s\" value %d", __func__,
			    typename, v);
		} else {
			warnx("%s: evt type %u value %d", __func__,
			    wsevt->type, v);
		}
	}

	switch (wsevt->type) {
	case WSCONS_EVENT_MOUSE_ABSOLUTE_X:
		v -= cc->minx;
		v *= lv_disp_get_hor_res(disp);
		v /= cc->maxx - cc->minx;
		wp->wp_x = v;
		break;
	case WSCONS_EVENT_MOUSE_ABSOLUTE_Y:
		v -= cc->miny;
		v *= lv_disp_get_ver_res(disp);
		v /= cc->maxy - cc->miny;
		wp->wp_y = v;
		break;
	case WSCONS_EVENT_MOUSE_UP:
		if (v != 0)
			return;
		wp->wp_pressed = 0;
		break;
	case WSCONS_EVENT_MOUSE_DOWN:
		if (v != 0)
			return;
		wp->wp_pressed = 1;
		break;
	case WSCONS_EVENT_SYNC:
		pe = malloc(sizeof(*pe));
		if (pe == NULL) {
			warn("%s", __func__);
			return;
		}

		pe->pe_x = wp->wp_x;
		pe->pe_y = wp->wp_y;
		pe->pe_pressed = wp->wp_pressed;

		TAILQ_INSERT_TAIL(&wp->wp_events, pe, pe_entry);
		break;
	default:
		printf("%s: type %u value %d\n", __func__,
		    wsevt->type, wsevt->value);
		return;
	}
}

static void
wslv_pointer_event(int fd, short revents, void *arg)
{
	struct wslv_pointer *wp = arg;
	struct wscons_event wsevts[64];
	ssize_t rv;
	size_t i, n;

	rv = read(fd, wsevts, sizeof(wsevts));
	if (rv == -1) {
		warn("%s", __func__);
		return;
	}

	n = rv / sizeof(wsevts[0]);
	for (i = 0; i < n; i++)
		wslv_pointer_event_proc(wp, &wsevts[i]);
}

static void
wslv_keypad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
	struct wslv_keypad *wk = drv->user_data;
	struct wslv_keypad_event *ke;
	lv_indev_state_t state;

	ke = TAILQ_FIRST(&wk->wk_events);
	if (ke == NULL) {
		data->key = wk->wk_key;
		data->state = wk->wk_state;
		return;
	}

	data->key = ke->ke_key;
	data->state = ke->ke_state;

	TAILQ_REMOVE(&wk->wk_events, ke, ke_entry);
	free(ke);

	data->continue_reading = !TAILQ_EMPTY(&wk->wk_events);
}

static void
wslv_pointer_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
	struct wslv_pointer *wp = drv->user_data;
	struct wslv_pointer_event *pe;
	lv_indev_state_t state;

	pe = TAILQ_FIRST(&wp->wp_events);
	if (pe == NULL) {
		data->point.x = wp->wp_x;
		data->point.y = wp->wp_y;
		data->state = wp->wp_pressed ?
		    LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
		return;
	}

	data->point.x = pe->pe_x;
	data->point.y = pe->pe_y;
	data->state = pe->pe_pressed ?
	    LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

	TAILQ_REMOVE(&wp->wp_events, pe, pe_entry);
	free(pe);

	data->continue_reading = !TAILQ_EMPTY(&wp->wp_events);
}

static void
wslv_keypad_set(struct wslv_softc *sc)
{
	struct wslv_keypad *wk;
	int fd;

	TAILQ_FOREACH(wk, &sc->sc_keypad_list, wk_entry) {
		fd = open(wk->wk_devname, O_RDWR|O_NONBLOCK);
		if (fd == -1)
			err(1, "keypad %s", wk->wk_devname);

		wk->wk_wslv = sc;
		event_set(&wk->wk_ev, fd, EV_READ|EV_PERSIST,
		    wslv_keypad_event, wk);

		lv_indev_drv_init(&wk->wk_lv_indev_drv);
		wk->wk_lv_indev_drv.type = LV_INDEV_TYPE_KEYPAD;
		wk->wk_lv_indev_drv.read_cb = wslv_keypad_read;
		wk->wk_lv_indev_drv.user_data = wk;

		wk->wk_lv_indev = lv_indev_drv_register(&wk->wk_lv_indev_drv);

		event_add(&wk->wk_ev, NULL);
	}
}

static void
wslv_pointer_set(struct wslv_softc *sc)
{
	struct wslv_pointer *wp;
	int fd;
	u_int type;

	TAILQ_FOREACH(wp, &sc->sc_pointer_list, wp_entry) {
		fd = open(wp->wp_devname, O_RDWR|O_NONBLOCK);
		if (fd == -1)
			err(1, "pointer %s", wp->wp_devname);

		if (ioctl(fd, WSMOUSEIO_GTYPE, &type) == -1)
			err(1, "get pointer %s type", wp->wp_devname);

		if (type != WSMOUSE_TYPE_TPANEL) {
			errx(1, "pointer %s is not a touch panel (%u)",
			    wp->wp_devname, type);
		}

		if (ioctl(fd, WSMOUSEIO_GCALIBCOORDS,
		    &wp->wp_ws_calib) == -1) {
			err(1, "get pointer %s calibration coordinates",
			    wp->wp_devname);
		}

		wp->wp_wslv = sc;
		event_set(&wp->wp_ev, fd, EV_READ|EV_PERSIST,
		    wslv_pointer_event, wp);

		lv_indev_drv_init(&wp->wp_lv_indev_drv);
		wp->wp_lv_indev_drv.type = LV_INDEV_TYPE_POINTER;
		wp->wp_lv_indev_drv.read_cb = wslv_pointer_read;
		wp->wp_lv_indev_drv.user_data = wp;

		wp->wp_lv_indev = lv_indev_drv_register(&wp->wp_lv_indev_drv);

		event_add(&wp->wp_ev, NULL);
	}
}

static void
wslv_ws_rd(int fd, short revents, void *arg)
{
	struct wscons_event events[64];
	ssize_t rv;
	size_t i, n;

	rv = read(fd, events, sizeof(events));
	if (rv == -1) {
		warn("%s", __func__);
		return;
	}

	n = rv / sizeof(events[0]);
	for (i = 0; i < n; i++) {
		struct wscons_event *event = &events[i];
		printf("%s: type %u value %d\n", __func__,
		    event->type, event->value);
	}
}

static void
wslv_tick(int nil, short events, void *arg)
{
	static const struct timeval rate = { 0, 1000000 / 100 };

	evtimer_add(&sc->sc_tick, &rate);

	lv_timer_handler();
}

static int
wslv_open(struct wslv_softc *sc, const char *devname, const char **errstr)
{
	unsigned int mode = WSDISPLAYIO_MODE_MAPPED;
	u_int gtype;
	size_t len;
	int fd;

	sc->sc_name = devname;

	fd = open(devname, O_RDWR);
	if (fd == -1) {
		*errstr = "open";
		return (-1);
	}

	if (ioctl(fd, WSDISPLAYIO_GTYPE, &gtype) == -1) {
		*errstr = "get wsdisplay type";
		goto close;
	}
	switch (gtype) {
	case WSDISPLAY_TYPE_INTELDRM:
		sc->sc_ws_drm = 1;
		return (0);
	}

	if (ioctl(fd, WSDISPLAYIO_GMODE, &sc->sc_ws_omode) == -1) {
		*errstr = "get wsdisplay mode";
		goto close;
	}

	if (ioctl(fd, WSDISPLAYIO_SMODE, &mode) == -1) {
		*errstr = "set wsdisplay mode";
		goto close;
	}

	if (ioctl(fd, WSDISPLAYIO_GINFO, &sc->sc_ws_vinfo) == -1) {
		*errstr = "get wsdisplay info";
		goto mode;
	}

	if (ioctl(fd, WSDISPLAYIO_LINEBYTES,
	    &sc->sc_ws_linebytes) == -1) {
		*errstr = "get wsdisplay line bytes";
		goto mode;
	}

	len = sc->sc_ws_linebytes * sc->sc_ws_vinfo.height;
	sc->sc_ws_fb = mmap(NULL, len,
	    PROT_WRITE|PROT_READ, MAP_SHARED, fd, (off_t)0);
	if (sc->sc_ws_fb == MAP_FAILED) {
		*errstr = "wsdisplay mmap";
		goto mode;
	}

	sc->sc_ws_fd = fd;
	sc->sc_ws_fblen = len;

	return (0);

mode:
	if (ioctl(fd, WSDISPLAYIO_SMODE, &sc->sc_ws_omode) == -1) {
		/* what do i do here? */
	}
close:
	close(fd);

	return (-1);
}

static void
wslv_lv_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area,
    lv_color_t *color_p)
{
	struct wslv_softc *sc = disp_drv->user_data;

	if (lv_disp_flush_is_last(disp_drv)) {
		//warnx("%s: hi", __func__);
		/* msync? */
	} else {
		//warnx("%s: lo", __func__);
	}

	lv_disp_flush_ready(disp_drv);
}

uint64_t
wslv_ms(void)
{
	struct timespec ts;
	uint64_t rv;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		abort();

	rv = (uint64_t)ts.tv_sec * 1000;
	rv += (uint64_t)ts.tv_nsec / 1000000;

	return (rv);
}
