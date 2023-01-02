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
#include "wslv.h"

#include "amqtt.h"

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

static void
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

	uint32_t			 wp_x;
	uint32_t			 wp_y;
	unsigned int			 wp_pressed;

	struct wslv_pointer_events	 wp_events;

	TAILQ_ENTRY(wslv_pointer)	 wp_entry;
};
TAILQ_HEAD(wslv_pointer_list, wslv_pointer);

struct wslv_softc {
	const char			*sc_name;

	int				 sc_ws_fd;
	unsigned char			*sc_ws_fb;
	unsigned char			*sc_ws_ofb;
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

	int				 sc_mqtt_family;
	const char			*sc_mqtt_host;
	const char			*sc_mqtt_serv;
	const char			*sc_mqtt_device;
	const char			*sc_mqtt_user;
	const char			*sc_mqtt_pass;

	const char			*sc_mqtt_will_topic;
	size_t				 sc_mqtt_will_topic_len;
	struct mqtt_conn		*sc_mqtt_conn;

	struct addrinfo			*sc_mqtt_res0;
	int				 sc_mqtt_fd;
	struct event			 sc_mqtt_ev_rd;
	struct event			 sc_mqtt_ev_wr;
	struct event			 sc_mqtt_ev_to;

	lv_indev_drv_t			 sc_mqtt_keypad_drv;
	lv_indev_t			*sc_mqtt_keypad;

	struct wslv_keypad_events	 sc_mqtt_keypad_events;
	int				 sc_mqtt_keypad_key;

	lv_indev_drv_t			 sc_mqtt_encoder_drv;
	lv_indev_t			*sc_mqtt_encoder;

	struct wslv_keypad_events	 sc_mqtt_encoder_events;
	int				 sc_mqtt_encoder_key;
};

struct wslv_softc _wslv = {
	.sc_mqtt_family		= AF_UNSPEC,
	.sc_mqtt_host		= NULL,
	.sc_mqtt_serv		= "1883",
	.sc_mqtt_device		= NULL,
	.sc_mqtt_user		= NULL,
	.sc_mqtt_pass		= NULL,
};
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

static void		wslv_mqtt_init(struct wslv_softc *);
static void		wslv_mqtt_connect(struct wslv_softc *);

static void		wslv_mqtt_keypad_read(lv_indev_drv_t *,
			    lv_indev_data_t *);
static void		wslv_mqtt_encoder_read(lv_indev_drv_t *,
			    lv_indev_data_t *);

static void __dead
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-46] [-p mqttport] [-W wsdiplay] "
	    "-d mqttdev -h mqtthost" "\n", __progname);

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

	TAILQ_INIT(&sc->sc_mqtt_keypad_events);
	TAILQ_INIT(&sc->sc_mqtt_encoder_events);

	while ((ch = getopt(argc, argv, "46d:h:K:M:p:W:")) != -1) {
		switch (ch) {
		case '4':
			sc->sc_mqtt_family = AF_INET;
			break;
		case '6':
			sc->sc_mqtt_family = AF_INET6;
			break;
		case 'd':
			sc->sc_mqtt_device = optarg;
			break;
		case 'h':
			sc->sc_mqtt_host = optarg;
			break;
		case 'K':
			wslv_keypad_add(sc, optarg);
			break;
		case 'M':
			wslv_pointer_add(sc, optarg);
			break;
		case 'p':
			sc->sc_mqtt_serv = optarg;
			break;
		case 'W':
			devname = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	if (sc->sc_mqtt_host == NULL) {
		warnx("mqtt host unspecified");
		usage();
	}

	if (sc->sc_mqtt_device == NULL) {
		warnx("mqtt device name unspecified");
		usage();
	}

	if (wslv_open(sc, devname, &errstr) == -1)
		err(1, "%s %s", devname, errstr);

	wslv_mqtt_init(sc);

	memcpy(sc->sc_ws_ofb, sc->sc_ws_fb, sc->sc_ws_fblen);

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
	event_init();

	lv_disp_draw_buf_init(&sc->sc_lv_disp_buf, sc->sc_ws_fb, NULL,
	    sc->sc_ws_fblen);

	lv_disp_drv_init(&sc->sc_lv_disp_drv);
	sc->sc_lv_disp_drv.draw_buf = &sc->sc_lv_disp_buf;
	sc->sc_lv_disp_drv.hor_res = sc->sc_ws_vinfo.width;
	sc->sc_lv_disp_drv.ver_res = sc->sc_ws_vinfo.height;
	sc->sc_lv_disp_drv.flush_cb = wslv_lv_flush;
	sc->sc_lv_disp_drv.user_data = sc;
	sc->sc_lv_disp_drv.direct_mode = 1;

	sc->sc_lv_disp = lv_disp_drv_register(&sc->sc_lv_disp_drv);

	lv_indev_drv_init(&sc->sc_mqtt_keypad_drv);
	sc->sc_mqtt_keypad_drv.type = LV_INDEV_TYPE_KEYPAD;
	sc->sc_mqtt_keypad_drv.read_cb = wslv_mqtt_keypad_read;
	sc->sc_mqtt_keypad_drv.user_data = sc;

	sc->sc_mqtt_keypad = lv_indev_drv_register(&sc->sc_mqtt_keypad_drv);

	lv_indev_drv_init(&sc->sc_mqtt_encoder_drv);
	sc->sc_mqtt_encoder_drv.type = LV_INDEV_TYPE_ENCODER;
	sc->sc_mqtt_encoder_drv.read_cb = wslv_mqtt_encoder_read;
	sc->sc_mqtt_encoder_drv.user_data = sc;

	sc->sc_mqtt_encoder = lv_indev_drv_register(&sc->sc_mqtt_encoder_drv);

	fprintf(stderr,
	    "%s, %u * %u, %d bit mmap %p+%zu\n",
	    sc->sc_name, sc->sc_ws_vinfo.width, sc->sc_ws_vinfo.height,
	    sc->sc_ws_vinfo.depth, sc->sc_ws_fb, sc->sc_ws_fblen);

	wslv_keypad_set(sc);
	wslv_pointer_set(sc);

	wslv_mqtt_connect(sc);

	event_set(&sc->sc_ws_ev, sc->sc_ws_fd, EV_READ|EV_PERSIST,
	    wslv_ws_rd, sc);
	event_add(&sc->sc_ws_ev, NULL);

	evtimer_set(&sc->sc_tick, wslv_tick, sc);
	wslv_tick(0, 0, sc);

//lv_demo_widgets();
lv_demo_keypad_encoder();

	event_dispatch();

	sleep(2);

	memcpy(sc->sc_ws_fb, sc->sc_ws_ofb, sc->sc_ws_fblen);

done:
	if (ioctl(sc->sc_ws_fd, WSDISPLAYIO_SMODE, &sc->sc_ws_omode) == -1)
		warn("set original mode");

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

static void
wslv_pointer_event_proc(struct wslv_pointer *wp,
    const struct wscons_event *wsevt)
{
	struct wslv_pointer_event *pe;
	lv_disp_t *disp = wp->wp_lv_indev_drv.disp;
	int v = wsevt->value;

	switch (wsevt->type) {
	case WSCONS_EVENT_MOUSE_ABSOLUTE_X:
		wp->wp_x = (v * lv_disp_get_hor_res(disp)) / 32768;
		break;
	case WSCONS_EVENT_MOUSE_ABSOLUTE_Y:
		wp->wp_y = (v * lv_disp_get_ver_res(disp)) / 32768;
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
		break;
	default:
		printf("%s: type %u value %d\n", __func__,
		    wsevt->type, wsevt->value);
		return;
	}

	pe = malloc(sizeof(*pe));
	if (pe == NULL) {
		warn("%s", __func__);
		return;
	}

	pe->pe_x = wp->wp_x;
	pe->pe_y = wp->wp_y;
	pe->pe_pressed = wp->wp_pressed;

	TAILQ_INSERT_TAIL(&wp->wp_events, pe, pe_entry);
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

	TAILQ_FOREACH(wp, &sc->sc_pointer_list, wp_entry) {
		fd = open(wp->wp_devname, O_RDWR|O_NONBLOCK);
		if (fd == -1)
			err(1, "pointer %s", wp->wp_devname);

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
	static const struct timeval rate = { 0, 1000000 / 25 };

	evtimer_add(&sc->sc_tick, &rate);

	lv_timer_handler();
}

static int
wslv_open(struct wslv_softc *sc, const char *devname, const char **errstr)
{
	unsigned int mode = WSDISPLAYIO_MODE_MAPPED;
	size_t len;
	int fd;

	sc->sc_name = devname;

	fd = open(devname, O_RDWR);
	if (fd == -1) {
		*errstr = "open";
		return (-1);
	}

	if (ioctl(fd, WSDISPLAYIO_GMODE, &sc->sc_ws_omode) == -1) {
		*errstr = "get wsdisplay mode";
		return (-1);
	}

	if (ioctl(fd, WSDISPLAYIO_SMODE, &mode) == -1) {
		*errstr = "set wsdisplay mode";
		return (-1);
	}

	if (ioctl(fd, WSDISPLAYIO_GINFO, &sc->sc_ws_vinfo) == -1) {
		*errstr = "get wsdisplay info";
		return (-1);
	}

	if (ioctl(fd, WSDISPLAYIO_LINEBYTES, &sc->sc_ws_linebytes) == -1) {
		*errstr = "get wsdisplay line bytes";
		return (-1);
	}

	len = sc->sc_ws_linebytes * sc->sc_ws_vinfo.height;
	sc->sc_ws_ofb = malloc(len);
	if (sc->sc_ws_ofb == NULL) {
		*errstr = "original fb copy";
		return (-1);
	}

	sc->sc_ws_fb = mmap(NULL, len,
	    PROT_WRITE|PROT_READ, MAP_SHARED, fd, (off_t)0);
	if (sc->sc_ws_fb == MAP_FAILED) {
		*errstr = "wsdisplay mmap";
		goto free;
	}

	sc->sc_ws_fd = fd;
	sc->sc_ws_fblen = len;

	return (0);

free:
	free(sc->sc_ws_ofb);
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

/* */
struct wslv_mqtt_cmnd;
static const struct wslv_mqtt_cmnd *
		wslv_mqtt_cmnd(const char *, size_t);

/* wrappers */

static void	wslv_mqtt_rd(int, short, void *);
static void	wslv_mqtt_wr(int, short, void *);
static void	wslv_mqtt_to(int, short, void *);

/* callbacks */

static void	wslv_mqtt_want_output(struct mqtt_conn *);
static ssize_t	wslv_mqtt_output(struct mqtt_conn *, const void *, size_t);

static void	wslv_mqtt_on_connect(struct mqtt_conn *);
static void	wslv_mqtt_on_suback(struct mqtt_conn *, void *,
		    const uint8_t *, size_t);
static void	wslv_mqtt_on_message(struct mqtt_conn *,
		    char *, size_t, char *, size_t, enum mqtt_qos);
static void	wslv_mqtt_dead(struct mqtt_conn *);

static void	wslv_mqtt_want_timeout(struct mqtt_conn *,
		     const struct timespec *);

static const struct mqtt_settings wslv_mqtt_settings = {
	.mqtt_want_output = wslv_mqtt_want_output,
	.mqtt_output = wslv_mqtt_output,
	.mqtt_want_timeout = wslv_mqtt_want_timeout,

	.mqtt_on_connect = wslv_mqtt_on_connect,
	.mqtt_on_suback = wslv_mqtt_on_suback,
	.mqtt_on_message = wslv_mqtt_on_message,
	.mqtt_dead = wslv_mqtt_dead,
};

static int
wslv_mqtt_check_topic(const char *t, const char **errstr)
{
	int ch = *t;

	if (ch == '\0') {
		*errstr = "empty";
		return (-1);
	}

	do {
		if (ch >= 'a' && ch <= 'z')
			continue;
		if (ch >= 'A' && ch <= 'Z')
			continue;
		if (ch >= '0' && ch <= '9')
			continue;

		switch (ch) {
		case '.':
		case '-':
		case '_':
			break;
		default:
			*errstr = "invalid character";
			return (-1);
		}
	} while ((ch = *(++t)) != '\0');

	return (0);
}

static int
wslv_mqtt_socket(struct wslv_softc *sc)
{
	struct addrinfo hints, *res, *res0;
	int error, serrno;
	int s;
	const char *cause = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = sc->sc_mqtt_family;
	hints.ai_socktype = SOCK_STREAM;

	error = getaddrinfo(sc->sc_mqtt_host, sc->sc_mqtt_serv, &hints, &res0);
	if (error) {
		errx(1, "MQTT host %s port %s: %s",
		    sc->sc_mqtt_host, sc->sc_mqtt_serv,
		    gai_strerror(error));
	}

	s = -1;
	for (res = res0; res != NULL; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s == -1) {
			serrno = errno;
			cause = "socket";
			continue;
		}

		if (connect(s, res->ai_addr, res->ai_addrlen) == -1) {
			serrno = errno;
			cause = "connect";
			close(s);
			s = -1;
			continue;
		}

		break;  /* okay we got one */
	}

	if (s == -1) {
		errc(1, serrno, "MQTT host %s port %s %s",
		    sc->sc_mqtt_host, sc->sc_mqtt_serv, cause);
	}

	sc->sc_mqtt_res0 = res0;

	return (s);
}

static void
wslv_mqtt_init(struct wslv_softc *sc)
{
	int nbio = 1;
	char *topic;
	int rv;
	int s;
	FILE *f;
	const char *errstr;

	if (wslv_mqtt_check_topic(sc->sc_mqtt_device, &errstr) == -1)
		errx(1, "mqtt device topic: %s", errstr);

	s = wslv_mqtt_socket(sc);

	if (ioctl(s, FIONBIO, &nbio) == -1)
		err(1, "set mqtt nbio");

	rv = asprintf(&topic, "tele/%s/LWT", sc->sc_mqtt_device);
	if (rv == -1)
		errx(1, "mqtt lwt topic printf error");

	sc->sc_mqtt_will_topic = topic;
	sc->sc_mqtt_will_topic_len = rv;

	sc->sc_mqtt_conn = mqtt_conn_create(&wslv_mqtt_settings, sc);
	if (sc->sc_mqtt_conn == NULL)
		errx(1, "unable to create mqtt connection");

	sc->sc_mqtt_fd = s;
}

static void
wslv_mqtt_connect(struct wslv_softc *sc)
{
	static const char offline[] = "Offline";
	struct mqtt_conn_settings mcs = {
		.clean_session = 1,
		.keep_alive = 30,

		.clientid = sc->sc_mqtt_device,
		.clientid_len = strlen(sc->sc_mqtt_device),

		.will_topic = sc->sc_mqtt_will_topic,
		.will_topic_len = sc->sc_mqtt_will_topic_len,
		.will_payload = offline,
		.will_payload_len = sizeof(offline) - 1,
		.will_retain = MQTT_RETAIN,
	};
	struct mqtt_conn *mc = sc->sc_mqtt_conn;

//warnx("%s", __func__);

	event_set(&sc->sc_mqtt_ev_rd, sc->sc_mqtt_fd, EV_READ|EV_PERSIST,
	    wslv_mqtt_rd, sc);
	event_set(&sc->sc_mqtt_ev_wr, sc->sc_mqtt_fd, EV_WRITE,
	    wslv_mqtt_wr, sc);
	evtimer_set(&sc->sc_mqtt_ev_to, wslv_mqtt_to, sc);

	if (mqtt_connect(mc, &mcs) == -1)
		errx(1, "failed to connect mqtt");

	event_add(&sc->sc_mqtt_ev_rd, NULL);
}

void
wslv_mqtt_rd(int fd, short events, void *arg)
{
	struct wslv_softc *sc = arg;
	struct mqtt_conn *mc = sc->sc_mqtt_conn;
	char buf[8192];
	ssize_t rv;

	rv = read(fd, buf, sizeof(buf));
//warnx("%s %zd", __func__, rv);
	switch (rv) {
	case -1:
		switch (errno) {
		case EAGAIN:
		case EINTR:
			return;
		default:
			break;
		}
		err(1, "%s", __func__);
		/* NOTREACHED */
	case 0:
		mqtt_disconnect(mc);
		mqtt_conn_destroy(mc);
		errx(1, "disconnected");
		/* NOTREACHED */
	default:
		break;
	}

//	hexdump(buf, rv);
	mqtt_input(mc, buf, rv);
}

void
wslv_mqtt_wr(int fd, short events, void *arg)
{
	struct wslv_softc *sc = arg;
	struct mqtt_conn *mc = sc->sc_mqtt_conn;

	mqtt_output(mc);
}

static void
wslv_mqtt_want_output(struct mqtt_conn *mc)
{
	struct wslv_softc *sc = mqtt_cookie(mc);

	event_add(&sc->sc_mqtt_ev_wr, NULL);
}

static ssize_t
wslv_mqtt_output(struct mqtt_conn *mc, const void *buf, size_t len)
{
	struct wslv_softc *sc = mqtt_cookie(mc);
	int fd = EVENT_FD(&sc->sc_mqtt_ev_wr);
	ssize_t rv;

//	hexdump(buf, len);

	rv = write(fd, buf, len);
//warnx("%s %zd/%zu", __func__, rv, len);
	if (rv == -1) {
		switch (errno) {
		case EAGAIN:
		case EINTR:
			return (0);
		default:
			break;
		}

		err(1, "%s", __func__);
		/* XXX reconnect */
	}

	return (rv);
}

static void
wslv_mqtt_to(int nil, short events, void *arg)
{
	struct wslv_softc *sc = arg;
	struct mqtt_conn *mc = sc->sc_mqtt_conn;

	mqtt_timeout(mc);
}

static void
wslv_mqtt_want_timeout(struct mqtt_conn *mc, const struct timespec *ts)
{
	struct wslv_softc *sc = mqtt_cookie(mc);
	struct timeval tv;

	TIMESPEC_TO_TIMEVAL(&tv, ts);

	event_add(&sc->sc_mqtt_ev_to, &tv);
}

static const char prefix_cmnd[] = "cmnd";
#define prefix_cmnd_len (sizeof(prefix_cmnd) - 1)

static void
wslv_mqtt_on_connect(struct mqtt_conn *mc)
{
	struct wslv_softc *sc = mqtt_cookie(mc);
	static const char online[] = "Online";
	char filter[128];
	int rv;

	warnx("%s", __func__);

	if (mqtt_publish(mc,
	    sc->sc_mqtt_will_topic, sc->sc_mqtt_will_topic_len,
	    online, sizeof(online) - 1, MQTT_QOS0, MQTT_RETAIN) == -1)
		errx(1, "mqtt publish %s %s", sc->sc_mqtt_will_topic, online);

	rv = snprintf(filter, sizeof(filter), "%s/%s/#",
	    prefix_cmnd, sc->sc_mqtt_device);
	if (rv == -1 || (size_t)rv >= sizeof(filter))
		errx(1, "mqtt subscribe filter");

	if (mqtt_subscribe(mc, NULL, filter, rv, MQTT_QOS0) == -1)
		errx(1, "mqtt subscribe %s failed", filter);
}

static void
wslv_mqtt_on_suback(struct mqtt_conn *mc, void *cookie,
    const uint8_t *rcodes, size_t nrcodes)
{
	warnx("Subscribed!");
}

struct wslv_mqtt_cmnd {
	const char *name;
	void (*handler)(struct wslv_softc *, const char *, size_t);
};

static void	wslv_mqtt_keypad(struct wslv_softc *, const char *, size_t);
static void	wslv_mqtt_encoder(struct wslv_softc *, const char *, size_t);
static void	wslv_mqtt_encoder_move(struct wslv_softc *,
		    const char *, size_t);

static const struct wslv_mqtt_cmnd wslv_mqtt_cmnds[] = {
	{ "keypad",		wslv_mqtt_keypad },
	{ "encoder",		wslv_mqtt_encoder },
	{ "encoder_move",	wslv_mqtt_encoder_move },
};

static const struct wslv_mqtt_cmnd *
wslv_mqtt_cmnd(const char *name, size_t name_len)
{
	size_t i;

	for (i = 0; i < nitems(wslv_mqtt_cmnds); i++) {
		const struct wslv_mqtt_cmnd *cmnd = &wslv_mqtt_cmnds[i];
		if (strncasecmp(cmnd->name, name, name_len) == 0)
			return (cmnd);
	}

	return (NULL);
}

static void
wslv_mqtt_on_message(struct mqtt_conn *mc,
    char *topic, size_t topic_len, char *payload, size_t payload_len,
    enum mqtt_qos qos)
{
	struct wslv_softc *sc = mqtt_cookie(mc);
	size_t name_len, device_len, off;
	const char *name;
	const char *sep;
	const struct wslv_mqtt_cmnd *cmnd;

	warnx("topic %s payload %s", topic, payload);
	if (payload == NULL || *payload == '\0')
		goto drop;

	if (topic_len <= prefix_cmnd_len) /* <= includes '/' */
		goto drop;
	if (strncmp(topic, prefix_cmnd, prefix_cmnd_len) != 0)
		goto drop;
	off = prefix_cmnd_len;
	if (topic[off++] != '/')
		goto drop;

	device_len = strlen(sc->sc_mqtt_device);
	if (topic_len <= (off + device_len)) /* <= includes '/' */
		goto drop;
	if (strncmp(topic + off, sc->sc_mqtt_device, device_len) != 0)
		goto drop;
	off += device_len;
	if (topic[off++] != '/')
		goto drop;

	name = topic + off;
	name_len = topic_len - off;
	sep = memchr(name, '/', name_len);
	if (sep != NULL)
		name_len = sep - name;

	warnx("command %.*s (%s)", (int)name_len, name, name);

	cmnd = wslv_mqtt_cmnd(name, name_len);
	if (cmnd == NULL)
                goto drop;

        (*cmnd->handler)(sc, payload, payload_len);

drop:
        free(topic);
        free(payload);
}

struct wslv_mqtt_key {
	const char *name;
	int value;
};

static const struct wslv_mqtt_key wslv_mqtt_keys[] = {
	{ "up",			LV_KEY_UP },
	{ "down",		LV_KEY_DOWN },
	{ "right",		LV_KEY_RIGHT },
	{ "left",		LV_KEY_LEFT },
	{ "esc",		LV_KEY_ESC },
	{ "del",		LV_KEY_DEL },
	{ "backspace",		LV_KEY_BACKSPACE },
	{ "enter",		LV_KEY_ENTER },
	{ "next",		LV_KEY_NEXT },
	{ "prev",		LV_KEY_PREV },
	{ "previous",		LV_KEY_PREV },
	{ "home",		LV_KEY_HOME },
	{ "end",		LV_KEY_END },
};

static int
wslv_mqtt_key(const char *name, size_t name_len)
{
	size_t i;

	for (i = 0; i < nitems(wslv_mqtt_keys); i++) {
		const struct wslv_mqtt_key *key = &wslv_mqtt_keys[i];
		if (strncasecmp(key->name, name, name_len) == 0)
			return (key->value);
	}

	return (-1);
}

static void
wslv_mqtt_keypad(struct wslv_softc *sc,
    const char *payload, size_t payload_len)
{
	struct wslv_keypad_event *ke;
	int key;

	key = wslv_mqtt_key(payload, payload_len);
	if (key == -1)
		return;

	ke = malloc(sizeof(*ke));
	if (ke == NULL) {
		warn("%s", __func__);
		return;
	}

	ke->ke_key = key;
	ke->ke_enc_diff = 0;

	TAILQ_INSERT_TAIL(&sc->sc_mqtt_keypad_events, ke, ke_entry);

	warnx("%s %s -> 0x%02x", __func__, payload, key);
}

void
wslv_mqtt_keypad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
	struct wslv_softc *sc = drv->user_data;
	struct wslv_keypad_event *ke;
	lv_indev_state_t state;

	ke = TAILQ_FIRST(&sc->sc_mqtt_keypad_events);
	if (ke == NULL) {
		data->key = sc->sc_mqtt_keypad_key;
		data->state = LV_INDEV_STATE_RELEASED;
		return;
	}

warnx("%s 0x%02x", __func__, ke->ke_key);

	data->key = ke->ke_key;
	data->state = LV_INDEV_STATE_PRESSED;
	data->continue_reading = 1;

	sc->sc_mqtt_keypad_key = ke->ke_key;
	TAILQ_REMOVE(&sc->sc_mqtt_keypad_events, ke, ke_entry);
	free(ke);
}

static void
wslv_mqtt_encoder(struct wslv_softc *sc,
    const char *payload, size_t payload_len)
{
	struct wslv_keypad_event *ke;
	int key;

	key = wslv_mqtt_key(payload, payload_len);
	if (key == -1)
		return;

	ke = malloc(sizeof(*ke));
	if (ke == NULL) {
		warn("%s", __func__);
		return;
	}

	ke->ke_key = key;
	ke->ke_enc_diff = 0;

	TAILQ_INSERT_TAIL(&sc->sc_mqtt_encoder_events, ke, ke_entry);

	warnx("%s %s -> 0x%02x", __func__, payload, key);
}

static void
wslv_mqtt_encoder_move(struct wslv_softc *sc,
    const char *payload, size_t payload_len)
{
	struct wslv_keypad_event *ke;
	int enc_diff;
	const char *errstr;

	enc_diff = strtonum(payload, INT16_MIN, INT16_MAX, &errstr);
	if (errstr != NULL)
		return;

	ke = malloc(sizeof(*ke));
	if (ke == NULL) {
		warn("%s", __func__);
		return;
	}

	ke->ke_key = '\0';
	ke->ke_enc_diff = enc_diff;

	TAILQ_INSERT_TAIL(&sc->sc_mqtt_encoder_events, ke, ke_entry);

	warnx("%s %s -> %d", __func__, payload, enc_diff);
}

void
wslv_mqtt_encoder_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
	struct wslv_softc *sc = drv->user_data;
	struct wslv_keypad_event *ke;
	lv_indev_state_t state;

	ke = TAILQ_FIRST(&sc->sc_mqtt_encoder_events);
	if (ke == NULL) {
		data->key = sc->sc_mqtt_encoder_key;
		data->state = LV_INDEV_STATE_RELEASED;
		return;
	}

warnx("%s 0x%02x %d", __func__, ke->ke_key, ke->ke_enc_diff);

	data->key = ke->ke_key;
	data->state = LV_INDEV_STATE_PRESSED;
	data->enc_diff = ke->ke_enc_diff;
	data->continue_reading = 1;

	sc->sc_mqtt_encoder_key = ke->ke_key;
	TAILQ_REMOVE(&sc->sc_mqtt_encoder_events, ke, ke_entry);
	free(ke);
}

static void
wslv_mqtt_dead(struct mqtt_conn *mc)
{
	err(1, "%s", __func__);
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
