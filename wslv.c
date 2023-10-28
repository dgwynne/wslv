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
#include <sys/param.h> /* for MAXHOSTNAMELEN */

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

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "wslv_drm.h"
#include "lua_lv.h"

#include "amqtt/amqtt.h"

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#define WSLV_REFR_PERIOD 40

uint32_t wslv_disp_refr_period = WSLV_REFR_PERIOD;
uint32_t wslv_indev_refr_period = WSLV_REFR_PERIOD;

LV_IMG_DECLARE(mouse_cursor_icon);

/* lv_spng.c */
int	lv_spng_init(void);

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

#define WS_DISPLAY			"/dev/ttyC0"
#define WS_POINTER			"/dev/wsmouse0"

#define WSLV_IDLE_TIME_MIN		 4
#define WSLV_IDLE_TIME_MAX		 3600
#define WSLV_IDLE_TIME_DEFAULT		 120

struct wslv_softc;

struct wslv_pointer_state {
	uint32_t			 p_x;
	uint32_t			 p_y;
	unsigned int			 p_pressed;
};

struct wslv_pointer_event {
	struct wslv_pointer_state	 pe_state;
	TAILQ_ENTRY(wslv_pointer_event)	 pe_entry;
};
TAILQ_HEAD(wslv_pointer_events, wslv_pointer_event);

struct wslv_pointer {
	struct wslv_softc		*wp_wslv;
	const char			*wp_devname;
	unsigned int			 wp_ws_type;
	struct event			 wp_ev;
	lv_indev_drv_t			 wp_lv_indev_drv;
	lv_indev_t			*wp_lv_indev;
	lv_obj_t			*wp_lv_cursor;

	struct wsmouse_calibcoords	 wp_ws_calib;

	struct wslv_pointer_state	 wp_state;
	struct wslv_pointer_state	 wp_state_synced;

	struct wslv_pointer_events	 wp_events;

	TAILQ_ENTRY(wslv_pointer)	 wp_entry;
};
TAILQ_HEAD(wslv_pointer_list, wslv_pointer);

struct wslv_lua_mqtt_sub {
	char				*filter;
	size_t				 len;
	int				 handler; /* lua ref */

	unsigned int			 refs;

	TAILQ_ENTRY(wslv_lua_mqtt_sub)	 entry;
};
TAILQ_HEAD(wslv_lua_mqtt_subs, wslv_lua_mqtt_sub);

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
	int (*sc_ws_svideo)(struct wslv_softc *, int);

	lv_disp_draw_buf_t		 sc_lv_disp_buf;
	lv_disp_drv_t			 sc_lv_disp_drv;
	lv_disp_t			*sc_lv_disp;


	struct event			 sc_tick;

	struct timeval			 sc_idle_time;
	struct event			 sc_idle_ev;
	unsigned int			 sc_idle;

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

	struct event			 sc_mqtt_tele_period;

	struct event			 sc_clocktick;

	lua_State			*sc_L;
	const char			*sc_L_script;
	int				 sc_L_reload;
	int				 sc_L_in_cmnd;

	struct wslv_lua_mqtt_subs	 sc_L_subs;
};

struct wslv_softc _wslv = {
	.sc_idle_time		= { WSLV_IDLE_TIME_DEFAULT, 0 },
	.sc_idle		= 0,

	.sc_mqtt_family		= AF_UNSPEC,
	.sc_mqtt_host		= NULL,
	.sc_mqtt_serv		= "1883",
	.sc_mqtt_device		= NULL,
	.sc_mqtt_user		= NULL,
	.sc_mqtt_pass		= NULL,

	.sc_L_subs		= TAILQ_HEAD_INITIALIZER(_wslv.sc_L_subs),
};
struct wslv_softc *sc = &_wslv;

static int		wslv_open(struct wslv_softc *, const char *,
			    const char **);

static void		wslv_pointer_add(struct wslv_softc *, const char *);
static void		wslv_pointer_set(struct wslv_softc *);

static void		wslv_ws_rd(int, short, void *);
static void		wslv_tick(int, short, void *);
static void		wslv_idle(int, short, void *);
static void		wslv_wake(struct wslv_softc *);

static void		wslv_lv_flush(lv_disp_drv_t *, const lv_area_t *,
			    lv_color_t *);

static int		wslv_svideo(struct wslv_softc *, int);
static int		wslv_wsfb_svideo(struct wslv_softc *, int);
static int		wslv_drm_svideo(struct wslv_softc *, int);
static void		wslv_refresh(struct wslv_softc *);

static void		wslv_mqtt_init(struct wslv_softc *);
static void		wslv_mqtt_connect(struct wslv_softc *);

static void		wslv_mqtt_tele(struct wslv_softc *);
static void		wslv_mqtt_tele_period(int, short, void *);

static void		wslv_lua_init(struct wslv_softc *);
static void		wslv_lua_reload(struct wslv_softc *);
static void		wslv_lua_reload_cb(lv_event_t *);

static void		wslv_lua_mqtt_suback(struct wslv_softc *, void *,
			    const uint8_t *, size_t);
static void		wslv_lua_mqtt_unsuback(struct wslv_softc *, void *);
static void		wslv_lua_mqtt_message(struct wslv_softc *,
			    char *, size_t, char *, size_t, int);

static int		wslv_luaopen(struct wslv_softc *, lua_State *);
static void		wslv_lua_cmnd(struct wslv_softc *,
			    const char *, size_t, const char *, size_t);
static void		wslv_lua_clocktick(int, short, void *);

static void __dead
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-46] [-d devname] [-i blanktime] [-p port]\n"
	    "\t[-M wsmouse] [-W wsdiplay] -h mqtthost -l script.lua\n",
	    __progname);

	exit(0);
}

int
main(int argc, char *argv[])
{
	char hostname[MAXHOSTNAMELEN];
	const char *devname = WS_DISPLAY;
	const char *errstr;
	int rv = 0;
	size_t x, y;
	uint32_t *word;
	int ch;

	TAILQ_INIT(&sc->sc_pointer_list);

	while ((ch = getopt(argc, argv, "46d:h:i:K:l:M:p:rW:")) != -1) {
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
		case 'i':
			if (strcmp(optarg, "min") == 0)
				sc->sc_idle_time.tv_sec = WSLV_IDLE_TIME_MIN;
			else if (strcmp(optarg, "max") == 0)
				sc->sc_idle_time.tv_sec = WSLV_IDLE_TIME_MAX;
			else {
				sc->sc_idle_time.tv_sec = strtonum(optarg,
				    WSLV_IDLE_TIME_MIN, WSLV_IDLE_TIME_MAX,
				    &errstr);
				if (errstr != NULL)
					errx(1, "idle time: %s", errstr);
			}
			break;
		case 'l':
			sc->sc_L_script = optarg;
			break;
		case 'M':
			wslv_pointer_add(sc, optarg);
			break;
		case 'p':
			sc->sc_mqtt_serv = optarg;
			break;
		case 'r':
			sc->sc_L_reload = 1;
			break;
		case 'W':
			devname = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (sc->sc_L_script == NULL) {
		warnx("lua script not specified");
		usage();
	}

	if (sc->sc_mqtt_host == NULL) {
		warnx("mqtt host unspecified");
		usage();
	}

	if (sc->sc_mqtt_device == NULL) {
		char *dot;

		if (gethostname(hostname, sizeof(hostname)) == -1)
			err(1, "gethostname");

		dot = strchr(hostname, '.');
		if (dot != NULL)
			*dot = '\0';

		sc->sc_mqtt_device = hostname;
	}

	if (wslv_open(sc, devname, &errstr) == -1)
		err(1, "%s %s", devname, errstr);

	if (TAILQ_EMPTY(&sc->sc_pointer_list))
		wslv_pointer_add(sc, WS_POINTER);

	wslv_mqtt_init(sc);

	lv_init();
	lv_spng_init();
	lv_freetype_init(0, 0, 0);

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

		sc->sc_ws_fb = drm_get_fb(0);
		if (sc->sc_ws_fb == NULL)
			err(1, "drm buffer");

		sc->sc_ws_fb2 = drm_get_fb(1);
		if (sc->sc_ws_fb2 == NULL)
			err(1, "drm buffer 2");

		sc->sc_ws_fblen = w * h;
		sc->sc_ws_svideo = wslv_drm_svideo;
	} else
		sc->sc_ws_svideo = wslv_wsfb_svideo;
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
		sc->sc_lv_disp_drv.full_refresh = 0;
		sc->sc_lv_disp_drv.direct_mode = 1;
		drm_event_set(&sc->sc_lv_disp_drv);
	} else {
		sc->sc_lv_disp_drv.flush_cb = wslv_lv_flush;
		sc->sc_lv_disp_drv.direct_mode = 1;
	}
	sc->sc_lv_disp_drv.user_data = sc;

	sc->sc_lv_disp = lv_disp_drv_register(&sc->sc_lv_disp_drv);
	if (0 && sc->sc_ws_drm) {
		lv_timer_del(sc->sc_lv_disp->refr_timer);
		sc->sc_lv_disp->refr_timer = NULL;
	}

	fprintf(stderr,
	    "%s, %u * %u, %d bit mmap %p+%zu\n",
	    sc->sc_name, sc->sc_ws_vinfo.width, sc->sc_ws_vinfo.height,
	    sc->sc_ws_vinfo.depth, sc->sc_ws_fb, sc->sc_ws_fblen);

	wslv_pointer_set(sc);

	wslv_mqtt_connect(sc);

	event_set(&sc->sc_ws_ev, sc->sc_ws_fd, EV_READ|EV_PERSIST,
	    wslv_ws_rd, sc);
	event_add(&sc->sc_ws_ev, NULL);

	evtimer_set(&sc->sc_tick, wslv_tick, sc);
	wslv_tick(0, 0, sc);

	evtimer_set(&sc->sc_idle_ev, wslv_idle, sc);
	evtimer_add(&sc->sc_idle_ev, &sc->sc_idle_time);

//	lv_theme_default_init(sc->sc_lv_disp,
//	    lv_palette_main(LV_PALETTE_BLUE),
//	    lv_palette_main(LV_PALETTE_RED),
//	    LV_THEME_DEFAULT_DARK, LV_FONT_DEFAULT);

	if (sc->sc_L_reload) {
		lv_obj_t *btn, *label;

		btn = lv_btn_create(lv_layer_sys());
		label = lv_label_create(btn);

		lv_label_set_text(label, "Reload");
		lv_obj_center(label);

		lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
		lv_obj_add_event_cb(btn, wslv_lua_reload_cb,
		    LV_EVENT_CLICKED, sc);
	}

	wslv_lua_init(sc);
	//wsluav(sc, lv_scr_act(), lfile);
	//lv_demo_widgets();
	//lv_demo_keypad_encoder();
	//lv_demo_benchmark();

	evtimer_set(&sc->sc_clocktick, wslv_lua_clocktick, sc);
	wslv_lua_clocktick(0, 0, sc);

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
		wp->wp_state.p_x = v;
		break;
	case WSCONS_EVENT_MOUSE_ABSOLUTE_Y:
		v -= cc->miny;
		v *= lv_disp_get_ver_res(disp);
		v /= cc->maxy - cc->miny;
		wp->wp_state.p_y = v;
		break;

	case WSCONS_EVENT_MOUSE_DELTA_X:
		v += wp->wp_state.p_x;
		if (v < 0)
			v = 0;
		if (v >= lv_disp_get_hor_res(disp))
			v = lv_disp_get_hor_res(disp) - 1;
		wp->wp_state.p_x = v;
		break;
	case WSCONS_EVENT_MOUSE_DELTA_Y:
		v = wp->wp_state.p_y - v;
		if (v < 0)
			v = 0;
		if (v >= lv_disp_get_ver_res(disp))
			v = lv_disp_get_ver_res(disp) - 1;
		wp->wp_state.p_y = v;
		break;
	case WSCONS_EVENT_MOUSE_UP:
		if (v != 0)
			return;
		wp->wp_state.p_pressed = 0;
		break;
	case WSCONS_EVENT_MOUSE_DOWN:
		if (v != 0)
			return;
		wp->wp_state.p_pressed = 1;
		break;
	case WSCONS_EVENT_SYNC:
		evtimer_add(&sc->sc_idle_ev, &sc->sc_idle_time);
		wp->wp_state_synced = wp->wp_state;

		if (sc->sc_idle) {
			/* wake the display up as soon as anything happens */
			wslv_svideo(sc, 1);

			/* only wake up input after the touch is released */
			if (wp->wp_ws_type != WSMOUSE_TYPE_TPANEL ||
			    wp->wp_state.p_pressed == 0)
				wslv_wake(sc);

			return;
		}

		pe = malloc(sizeof(*pe));
		if (pe != NULL) {
			pe->pe_state = wp->wp_state_synced;
			TAILQ_INSERT_TAIL(&wp->wp_events, pe, pe_entry);
		}

		lv_indev_read_timer_cb(wp->wp_lv_indev_drv.read_timer);
		wslv_refresh(sc);
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
wslv_pointer_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
	struct wslv_pointer *wp = drv->user_data;
	struct wslv_pointer_state *p = &wp->wp_state_synced;
	struct wslv_pointer_event *pe;

	pe = TAILQ_FIRST(&wp->wp_events);
	if (pe != NULL) {
		p = &pe->pe_state;
		TAILQ_REMOVE(&wp->wp_events, pe, pe_entry);
	}

	data->point.x = p->p_x;
	data->point.y = p->p_y;
	data->state = p->p_pressed ?
	    LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
	data->continue_reading = !TAILQ_EMPTY(&wp->wp_events);

	free(pe);
}

static void
wslv_pointer_idle(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
	struct wslv_pointer *wp = drv->user_data;

	data->point.x = wp->wp_state_synced.p_x;
	data->point.y = wp->wp_state_synced.p_y;
	data->state = LV_INDEV_STATE_RELEASED;
	data->continue_reading = 0;
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

		if (ioctl(fd, WSMOUSEIO_GTYPE, &wp->wp_ws_type) == -1)
			err(1, "get pointer %s type", wp->wp_devname);

		if (wp->wp_ws_type == WSMOUSE_TYPE_TPANEL) {
			if (ioctl(fd, WSMOUSEIO_GCALIBCOORDS,
			    &wp->wp_ws_calib) == -1) {
				err(1, "get pointer %s calibration coordinates",
				    wp->wp_devname);
			}
		}

		wp->wp_wslv = sc;
		event_set(&wp->wp_ev, fd, EV_READ|EV_PERSIST,
		    wslv_pointer_event, wp);

		lv_indev_drv_init(&wp->wp_lv_indev_drv);
		wp->wp_lv_indev_drv.type = LV_INDEV_TYPE_POINTER;
		wp->wp_lv_indev_drv.read_cb = wslv_pointer_read;
		wp->wp_lv_indev_drv.user_data = wp;

		wp->wp_lv_indev = lv_indev_drv_register(&wp->wp_lv_indev_drv);

		if (wp->wp_ws_type != WSMOUSE_TYPE_TPANEL) {
			wp->wp_lv_cursor = lv_img_create(lv_scr_act());
			if (wp->wp_lv_cursor == NULL)
				err(1, "%s cursor", wp->wp_devname);
			lv_img_set_src(wp->wp_lv_cursor, &mouse_cursor_icon);
			lv_indev_set_cursor(wp->wp_lv_indev, wp->wp_lv_cursor);
		}

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
	static const struct timeval rate = { 0, 1000000 / WSLV_REFR_PERIOD };

	evtimer_add(&sc->sc_tick, &rate);

	lv_timer_handler();
}

static void
wslv_idle(int nil, short events, void *arg)
{
	struct wslv_softc *sc = arg;
	struct wslv_pointer *wp;

	TAILQ_FOREACH(wp, &sc->sc_pointer_list, wp_entry)
		wp->wp_lv_indev_drv.read_cb = wslv_pointer_idle;

	sc->sc_idle = 1;
	warnx("idle");
	wslv_svideo(sc, 0);

	wslv_mqtt_tele(sc);
}

static void
wslv_wake(struct wslv_softc *sc)
{
	struct wslv_pointer *wp;

	warnx("wake");
	/* the display has already been woken up */

	sc->sc_idle = 0;
	TAILQ_FOREACH(wp, &sc->sc_pointer_list, wp_entry)
		wp->wp_lv_indev_drv.read_cb = wslv_pointer_read;

	wslv_mqtt_tele(sc);
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
	sc->sc_ws_fblen = sc->sc_ws_vinfo.width * sc->sc_ws_vinfo.height;

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
wslv_refresh(struct wslv_softc *sc)
{
	if (sc->sc_ws_drm)
		drm_refresh();
}

static int
wslv_svideo(struct wslv_softc *sc, int on)
{
	return ((*sc->sc_ws_svideo)(sc, on));
}

static int
wslv_drm_svideo(struct wslv_softc *sc, int on)
{
	return (drm_svideo(on));
}

static int
wslv_wsfb_svideo(struct wslv_softc *sc, int on)
{
	u_int svideo = on ? WSDISPLAYIO_VIDEO_ON : WSDISPLAYIO_VIDEO_OFF;

	if (ioctl(sc->sc_ws_fd, WSDISPLAYIO_SVIDEO, &svideo) == -1)
		warn("set video %s %s", sc->sc_name, on ? "on" : "off");

	return (0);
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
static void	wslv_mqtt_on_unsuback(struct mqtt_conn *, void *);
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
	.mqtt_on_unsuback = wslv_mqtt_on_unsuback,
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

	event_set(&sc->sc_mqtt_ev_rd, sc->sc_mqtt_fd, EV_READ|EV_PERSIST,
	    wslv_mqtt_rd, sc);
	event_set(&sc->sc_mqtt_ev_wr, sc->sc_mqtt_fd, EV_WRITE,
	    wslv_mqtt_wr, sc);
	evtimer_set(&sc->sc_mqtt_ev_to, wslv_mqtt_to, sc);

	if (mqtt_connect(mc, &mcs) == -1)
		errx(1, "failed to connect mqtt");

	event_add(&sc->sc_mqtt_ev_rd, NULL);

	evtimer_set(&sc->sc_mqtt_tele_period, wslv_mqtt_tele_period, sc);
}

void
wslv_mqtt_rd(int fd, short events, void *arg)
{
	struct wslv_softc *sc = arg;
	struct mqtt_conn *mc = sc->sc_mqtt_conn;
	char buf[8192];
	ssize_t rv;

	rv = read(fd, buf, sizeof(buf));
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

	rv = write(fd, buf, len);
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
	char filter[128];
	int rv;

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
	struct wslv_softc *sc = mqtt_cookie(mc);
	static const char online[] = "Online";

	if (cookie != NULL) {
		wslv_lua_mqtt_suback(sc, cookie, rcodes, nrcodes);
		return;
	}

	if (mqtt_publish(mc,
	    sc->sc_mqtt_will_topic, sc->sc_mqtt_will_topic_len,
	    online, sizeof(online) - 1, MQTT_QOS0, MQTT_RETAIN) == -1)
		errx(1, "mqtt publish %s %s", sc->sc_mqtt_will_topic, online);

	wslv_mqtt_tele_period(0, 0, sc);
}

static void
wslv_mqtt_on_unsuback(struct mqtt_conn *mc, void *cookie)
{
	struct wslv_softc *sc = mqtt_cookie(mc);

	if (cookie != NULL) {
		wslv_lua_mqtt_unsuback(sc, cookie);
		return;
	}
}

struct wslv_mqtt_cmnd {
	const char *name;
	void (*handler)(struct wslv_softc *, const char *,
	    const char *, size_t);
};

static void	wslv_mqtt_blank(struct wslv_softc *, const char *,
		    const char *, size_t);

static const struct wslv_mqtt_cmnd wslv_mqtt_cmnds[] = {
	{ "blank",		wslv_mqtt_blank },
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
	size_t name_len, device_len, cmnd_len, off;
	const char *name;
	const char *sep;
	const struct wslv_mqtt_cmnd *cmnd;

	if (payload == NULL || *payload == '\0')
		goto free;

	if (topic_len <= prefix_cmnd_len) /* <= includes '/' */
		goto decline;
	if (strncmp(topic, prefix_cmnd, prefix_cmnd_len) != 0)
		goto decline;
	off = prefix_cmnd_len;
	if (topic[off++] != '/')
		goto decline;

	device_len = strlen(sc->sc_mqtt_device);
	if (topic_len <= (off + device_len)) /* <= includes '/' */
		goto decline;
	if (strncmp(topic + off, sc->sc_mqtt_device, device_len) != 0)
		goto decline;
	off += device_len;
	if (topic[off++] != '/')
		goto decline;

	name = topic + off;
	cmnd_len = name_len = topic_len - off;
	sep = memchr(name, '/', name_len);
	if (sep != NULL)
		cmnd_len = sep - name;

	cmnd = wslv_mqtt_cmnd(name, cmnd_len);
	if (cmnd != NULL)
		(*cmnd->handler)(sc, name, payload, payload_len);
	else
		wslv_lua_cmnd(sc, name, name_len, payload, payload_len);
	goto free;

free:
        free(topic);
        free(payload);
	return;

decline:
	wslv_lua_mqtt_message(sc, topic, topic_len, payload, payload_len, qos);
}

static void
wslv_mqtt_tele(struct wslv_softc *sc)
{
	struct mqtt_conn *mc = sc->sc_mqtt_conn;
	char topic[128];
	char payload[128];
	size_t topic_len, payload_len;
	int rv;

	rv = snprintf(topic, sizeof(topic), "tele/%s/STATUS",
	    sc->sc_mqtt_device);
	if (rv == -1)
		errx(1, "mqtt tele topic");
	topic_len = rv;
	if (topic_len >= sizeof(topic))
		errx(1, "mqtt tele topic len");

	rv = snprintf(payload, sizeof(payload), "{\"blank\":\"%s\"}",
	    sc->sc_idle ? "ON" : "OFF");
	if (rv == -1)
		errx(1, "mqtt tele payload");
	payload_len = rv;
	if (payload_len >= sizeof(payload))
		errx(1, "mqtt tele payload len");

	if (mqtt_publish(mc, topic, topic_len, payload, payload_len,
	    MQTT_QOS0, MQTT_NORETAIN) == -1)
		errx(1, "mqtt publish %s", topic);
}

void
wslv_tele(struct wslv_softc *sc, const char *suffix, size_t suffix_len,
    const char *payload, size_t payload_len)
{
	struct mqtt_conn *mc = sc->sc_mqtt_conn;
	char topic[128];
	size_t topic_len;
	int rv;

	rv = snprintf(topic, sizeof(topic), "tele/%s/%s",
	    sc->sc_mqtt_device, suffix);
	if (rv == -1)
		errx(1, "mqtt tele topic");
	topic_len = rv;
	if (topic_len >= sizeof(topic)) {
		warnx("mqtt_tele topic len");
		return;
	}

	if (mqtt_publish(mc, topic, topic_len, payload, payload_len,
	    MQTT_QOS0, MQTT_NORETAIN) == -1)
		errx(1, "mqtt publish %s", topic);
}

static void
wslv_mqtt_tele_period(int nope, short events, void *arg)
{
	static const struct timeval rate = { 300, 0 };
	struct wslv_softc *sc = arg;

	evtimer_add(&sc->sc_mqtt_tele_period, &rate);

	wslv_mqtt_tele(sc);
}

static void
wslv_mqtt_blank(struct wslv_softc *sc, const char *name,
    const char *payload, size_t payload_len)
{
	int blank;

	if (strcasecmp(payload, "on") == 0 ||
	    strcasecmp(payload, "1") == 0)
		blank = 1;
	else if (strcasecmp(payload, "off") == 0 ||
	    strcasecmp(payload, "0") == 0) {
		evtimer_add(&sc->sc_idle_ev, &sc->sc_idle_time);
		blank = 0;
	} else if (strcasecmp(payload, "toggle") == 0 ||
	    strcasecmp(payload, "2") == 0)
		blank = !sc->sc_idle;
	else
		goto publish;

	if (blank != sc->sc_idle) {
		if (blank) {
			evtimer_del(&sc->sc_idle_ev);
			wslv_idle(0, 0, sc);
		} else {
			wslv_svideo(sc, 1);
			wslv_wake(sc);
		}
		return;
	}

publish:
	wslv_mqtt_tele(sc);
}

static void
wslv_mqtt_dead(struct mqtt_conn *mc)
{
	errx(1, "%s", __func__);
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

const lv_font_t *
wslv_font_default(void)
{
	return (&lv_font_montserrat_12_subpx);
}

/*
 * lua binding
 */

static int
wslv_lua_panic(lua_State *L)
{
	warnx("lua panic: %s", lua_tostring(L, -1));
	return (0);
}

static void
wslv_lua_init(struct wslv_softc *sc)
{
	const char *lfile = sc->sc_L_script;
	lua_State *L;
	int status;

	L = luaL_newstate();
	if (L == NULL)
		errx(1, "unable to create new lua state");

	luaL_openlibs(L);
	lua_atpanic(L, wslv_lua_panic);

	luaL_requiref(L, "lv", luaopen_lv, 1);
	lua_pop(L, 1);

	wslv_luaopen(sc, L); /* wslv.tele etc */

	status = luaL_loadfile(L, lfile);
	if (status != 0) {
		switch (status) {
		case LUA_ERRSYNTAX:
			warnx("unable to load %s: %s", lfile,
			    lua_tostring(L, -1));
			break;
		case LUA_ERRMEM:
			warnx("unable to load %s: memory allocation error",
			    lfile);
			break;
		case LUA_ERRFILE:
			warnx("unable to load %s: open or read failure",
			    lfile);
			break;
		default:
			warnx("unable to load %s: error %d", lfile, status);
			break;
		}
		goto close;
	}

	status = lua_pcall(L, 0, 0, 0);
	if (status != 0) {
		switch (status) {
		case LUA_ERRRUN:
		case LUA_ERRMEM:
		case LUA_ERRERR:
			warnx("%s: %s", lfile, lua_tostring(L, -1));
			break;
		default:
			warnx("%s: pcall failed: unknown status %d",
			    lfile, status);
			break;
		}

		goto close;
	}

	sc->sc_L = L;
	return;

close:
	lua_close(L);
}

/*
 * wslv lua bits
 */

static void
wslv_lua_mqtt_sub_rele(struct wslv_lua_mqtt_sub *lsub)
{
	if (--lsub->refs == 0) {
		free(lsub->filter);
		free(lsub);
	}
}

static void
wslv_lua_reload(struct wslv_softc *sc)
{
	lua_State *L = sc->sc_L;
	struct mqtt_conn *mc = sc->sc_mqtt_conn;
	struct wslv_lua_mqtt_sub *lsub;

	if (L != NULL) {
		sc->sc_L = NULL;
		lua_close(L);
	}

	TAILQ_FOREACH(lsub, &sc->sc_L_subs, entry) {
		lsub->handler = LUA_NOREF;

		/* give this ref to unsub */
		if (mqtt_unsubscribe(mc, lsub,
		    lsub->filter, lsub->len) == -1)
			errx(1, "lsub %s unsub", lsub->filter);
	}
	TAILQ_INIT(&sc->sc_L_subs);

	wslv_lua_init(sc);
}

static void
wslv_lua_reload_cb(lv_event_t *e)
{
	struct wslv_softc *sc = lv_event_get_user_data(e);

	wslv_lua_reload(sc);
}

static void
wslv_lua_clocktick(int nil, short events, void *arg)
{
	struct wslv_softc *sc = arg;
	lua_State *L = sc->sc_L;
	static const struct timeval rate = { 1, 0 };
	int top;
	int rv;

	evtimer_add(&sc->sc_clocktick, &rate);

	if (L == NULL)
		return;

	top = lua_gettop(L);

	lua_getglobal(L, "clocktick");
	if (!lua_isfunction(L, -1))
		goto pop;

	rv = lua_pcall(L, 0, 0, 0);
	if (rv != 0)
		warnx("lua pcall clocktick %s", lua_tostring(L, -1));

pop:
	lua_settop(L, top);
}

static void
wslv_lua_cmnd(struct wslv_softc *sc, const char *topic, size_t topic_len,
    const char *payload, size_t payload_len)
{
	lua_State *L = sc->sc_L;
	int top;
	int rv;

	if (L == NULL)
		return;

	top = lua_gettop(L);

	lua_getglobal(L, "cmnd");
	if (!lua_isfunction(L, -1))
		goto pop;

	lua_pushlstring(L, topic, topic_len);
	lua_pushlstring(L, payload, payload_len);

	sc->sc_L_in_cmnd = 1;
	rv = lua_pcall(L, 2, 0, 0);
	sc->sc_L_in_cmnd = 0;

	if (rv != 0)
		warnx("lua pcall cmnd %s", lua_tostring(L, -1));

pop:
	lua_settop(L, top);
}

static int
wslv_luaL_publish(lua_State *L)
{
	struct wslv_softc *sc = &_wslv; /* XXX */
	struct mqtt_conn *mc = sc->sc_mqtt_conn;
	const char *topic, *payload;
	size_t topic_len, payload_len;

	topic = lua_tolstring(L, 1, &topic_len);
	payload = lua_tolstring(L, 2, &payload_len);

	if (mqtt_publish(mc, topic, topic_len, payload, payload_len,
	    MQTT_QOS0, MQTT_NORETAIN) == -1)
		errx(1, "mqtt publish %s", topic);

        return (0);
}

static void
wslv_lua_mqtt_suback(struct wslv_softc *sc, void *cookie,
    const uint8_t *rcodes, size_t nrcodes)
{
	struct wslv_lua_mqtt_sub *lsub = cookie;
	uint8_t rcode;

	if (nrcodes < 1) {
		warnx("%s: rcodes < 1", __func__);
		return;
	}

	rcode = rcodes[0];
	switch (rcode) {
	case 0x00:
		break;
	case 0x01:
	case 0x02:
		warnx("%s suback, unexpected success rcode 0x%02x",
		    lsub->filter, rcode);
		break;
	case 0x80:
		warnx("%s suback failed", lsub->filter);
		/* XXX unlink lsub from sc */
		return;
	default:
		warnx("%s suback, unexpected rcode 0x%02x",
		    lsub->filter, rcode);
		break;
	}

	wslv_lua_mqtt_sub_rele(lsub);
}

static void
wslv_lua_mqtt_unsuback(struct wslv_softc *sc, void *cookie)
{
	struct wslv_lua_mqtt_sub *lsub = cookie;

	wslv_lua_mqtt_sub_rele(lsub);
}

static void
wslv_lua_mqtt_message(struct wslv_softc *sc,
    char *topic, size_t topiclen, char *payload, size_t payloadlen, int qos)
{
	lua_State *L = sc->sc_L;
	int top;
	int rv;

	struct wslv_lua_mqtt_sub *lsub;

	if (L == NULL)
		goto free;

	top = lua_gettop(L);

	lua_getglobal(L, "mqtt_message");
	if (!lua_isfunction(L, -1))
		goto pop;

	lua_pushlstring(L, topic, topiclen);
	lua_pushlstring(L, payload, payloadlen);
	lua_pushinteger(L, qos);

	sc->sc_L_in_cmnd = 1;
	rv = lua_pcall(L, 3, 0, 0);
	sc->sc_L_in_cmnd = 0;

	if (rv != 0)
		warnx("lua pcall mqtt_message %s", lua_tostring(L, -1));

pop:
	lua_settop(L, top);
free:
	free(topic);
	free(payload);
}

static int
wslv_luaL_subscribe(lua_State *L)
{
	struct wslv_softc *sc = &_wslv; /* XXX */
	struct mqtt_conn *mc = sc->sc_mqtt_conn;
	struct wslv_lua_mqtt_sub *lsub;
	const char *filter;
	size_t len;

	filter = luaL_checklstring(L, 1, &len);

	lsub = malloc(sizeof(*lsub));
	if (lsub == NULL) {
		return luaL_error(L, "wslv_lua_mqtt_sub alloc: %s",
		    strerror(errno));
	}
	lsub->filter = malloc(len);
	if (lsub->filter == NULL) {
		int serrno = errno;
		free(lsub);
		return luaL_error(L, "wslv_lua_mqtt_sub filter alloc: %s",
		    strerror(serrno));
	}

	memcpy(lsub->filter, filter, len);
	lsub->len = len;

	lsub->handler = LUA_NOREF;
	if (lua_isfunction(L, 2)) {
		lua_pushvalue(L, 2);
		lsub->len = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	lsub->refs = 2; /* one for amqtt, one for sc */

	if (mqtt_subscribe(mc, lsub, filter, len, MQTT_QOS0) == -1) {
		free(lsub->filter);
		free(lsub);
		return luaL_error(L, "mqtt subscribe %s failed", filter);
	}

	TAILQ_INSERT_TAIL(&sc->sc_L_subs, lsub, entry);

	return (0);
}

static int
wslv_luaL_tele(lua_State *L)
{
	struct wslv_softc *sc = &_wslv; /* XXX */
	const char *topic, *payload;
	size_t topic_len, payload_len;

	topic = lua_tolstring(L, 1, &topic_len);
	payload = lua_tolstring(L, 2, &payload_len);

	wslv_tele(sc, topic, topic_len, payload, payload_len);

        return (0);
}

static int
wslv_luaL_in_cmnd(lua_State *L)
{
	struct wslv_softc *sc = &_wslv; /* XXX */
	lua_pushboolean(L, sc->sc_L_in_cmnd);
        return (1);
}

static const luaL_Reg wslv_luaL[] = {
	{ "publish",		wslv_luaL_publish },
	{ "subscribe",		wslv_luaL_subscribe },
	{ "tele",		wslv_luaL_tele },
	{ "in_cmnd",		wslv_luaL_in_cmnd },

        { NULL,                 NULL }
};

static int
wslv_luaopen(struct wslv_softc *sc, lua_State *L)
{
	warnx("gettop:%d", lua_gettop(L));
	luaL_newlib(L, wslv_luaL);
	warnx("gettop:%d", lua_gettop(L));
	lua_setglobal(L, "wslv");
	warnx("gettop:%d", lua_gettop(L));

	return (0);
}
