
/*
 * Copyright (c) 2022 Neo Xu
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

#include <stdlib.h>
#include <unistd.h>
#include <err.h>

#include <lvgl.h>
#include <lua.h>

#include <lauxlib.h>
#include <lualib.h>

#include <luavgl.h>
#include "wslv_luavgl.h"

static int		luaopen_wslv(lua_State *);

typedef struct {
	lua_State *L;
	lv_obj_t *root;
} lua_context_t;

typedef struct {
	lv_obj_t *root;
	make_font_cb make_font;
	delete_font_cb delete_font;
	const char *script;
} luavgl_args_t;

/*
** Prints an error message, adding the program name in front of it
** (if present)
*/
static void
l_message(const char *pname, const char *msg)
{
	printf("%s: %s\n", pname ? pname : " ", msg);
}

/*
** Check whether 'status' is not OK and, if so, prints the error
** message on the top of the stack. It assumes that the error object
** is a string, as it was either generated by Lua or by 'msghandler'.
*/
static int
report(lua_State *L, int status)
{
	if (status != LUA_OK) {
		const char *msg = lua_tostring(L, -1);
		l_message("luactx", msg);
		lua_pop(L, 1); /* remove message */
	}
	return status;
}

/*
** Message handler used to run all chunks
*/
static int
msghandler(lua_State *L)
{
	const char *msg = lua_tostring(L, 1);
	if (msg == NULL) { /* is error object not a string? */
		/* does it have a metamethod that produces a string? */
		if (luaL_callmeta(L, 1, "__tostring") &&
		    lua_type(L, -1) == LUA_TSTRING)
			return 1;
		else {
			msg = lua_pushfstring(L,
			    "(error object is a %s value)",
			    luaL_typename(L, 1));
		}
	}

	/* append a standard traceback */
	luaL_traceback(L, L, msg, 1);

	msg = lua_tostring(L, -1);
	lua_pop(L, 1);

	lv_obj_t *root = NULL;
	luavgl_ctx_t *ctx = luavgl_context(L);
	root = ctx->root ? ctx->root : lv_scr_act();
	lv_obj_t *label = lv_label_create(root);
	lv_label_set_text(label, msg);
	lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
	lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
	lv_obj_set_width(label, LV_PCT(80));
	lv_obj_center(label);

	printf("trace back: \n%s\n", msg);
	return 0; /* return no trace, since we already processed it. */
}

static int
lua_panic(lua_State *L)
{
	printf("LUA panic:\n%s\n", lua_tostring(L, -1));
	return 0; /* return to Lua to abort */
}

/*
** protected main entry
*/
static int
pmain(lua_State *L)
{
	int status;
	const char *script = lua_tostring(L, 1);

	luavgl_args_t *args = lua_touserdata(L, 2);
	if (args == NULL || args->root == NULL) {
		printf("Null root object.\n");
		return 0;
	}

	luavgl_set_root(L, args->root);
	luavgl_set_font_extension(L, args->make_font, args->delete_font);

	/**
	 * Set global variable SCRIPT_PATH, to make image src path easier.
	 */
	char *path = strdup(script);
	if (path == NULL) {
		printf("no memory.\n");
		return 0;
	}

	int i = strlen(path);
	for (; i; i--) {
		if (path[i] == '/') {
			path[i + 1] = '\0';
			break;
		}
	}

	printf("script path: %s\n", path);
	lua_pushstring(L, path);
	lua_setglobal(L, "SCRIPT_PATH");
	luaL_openlibs(L);

	lua_getglobal(L, "package");
	lua_getfield(L, -1, "path");

	const char *pkg_path = lua_tostring(L, -1);
	char *new_path = malloc(strlen(pkg_path) + strlen(script) + 2);
	strcpy(new_path, pkg_path);
	strcat(new_path, ";");
	strcat(new_path, path);
	strcat(new_path, "?.lua");
	lua_pop(L, 1);
	lua_pushstring(L, new_path);
	lua_setfield(L, -2, "path");
	lua_pop(L, 1);
	free(path);
	free(new_path);

	lua_atpanic(L, &lua_panic);

	luaL_requiref(L, "wslv", luaopen_wslv, 1);
	lua_pop(L, 1);

	luaL_requiref(L, "lvgl", luaopen_lvgl, 1);
	lua_pop(L, 1);

	lua_pushcfunction(L, msghandler); /* push message handler */
	int base = lua_gettop(L);
	status = luaL_loadfile(L, script);
	if (status != LUA_OK) {
		printf("%s\n", lua_tostring(L, 1));
		lua_pushfstring(L, "failed to load: %s\n", script);
		/* manually show the error to screen. */
		lua_insert(L, 1);
		msghandler(L);
		return 0;
	}

	status = lua_pcall(L, 0, 0, base);
	lua_remove(L, base); /* remove message handler from the stack */
	report(L, status);
	lua_pushboolean(L, 1); /* signal no errors */
	return 1;
}

lua_context_t *
lua_load_script(luavgl_args_t *args)
{
	const char *script = args->script;
	int ret, status;

	/* create the thread to run script. */
	if (script == NULL) {
		printf("args error.\n");
		return NULL;
	}

	printf("run script: %s\n", script);
	lua_State *L = luaL_newstate(); /* create state */
	if (L == NULL) {
		printf("no mem for lua state.\n");
		return NULL;
	}

	lua_pushcfunction(L, &pmain); /* to call 'pmain' in protected mode */
	lua_pushstring(L, script);
	lua_pushlightuserdata(L, args);
	status = lua_pcall(L, 2, 1, 0); /* do the call */
	ret = lua_toboolean(L, -1);
	report(L, status);
	if (!ret || status != LUA_OK) {
		/* This should never happen */
		printf("pcall failed.\n");
		lua_close(L);
		return NULL;
	}

	/* script may fail, but we continue until page destoried. */
	lua_context_t *luactx = calloc(sizeof(*luactx), 1);
	if (luactx == NULL) {
		printf("no memory.\n");
		goto lua_exit;
	}

	luactx->L = L;
	luactx->root = args->root;
	return luactx;

lua_exit:
	lua_close(L);

	return NULL;
}

int
lua_terminate(lua_context_t *luactx)
{
	lua_State *L = luactx->L;

	lua_close(L);
	free(luactx);
	return 0;
}

static lua_context_t *lua_ctx;
static luavgl_args_t args;

static struct wslv_softc *wslv;

static void
reload_cb(lv_event_t *e)
{
	(void)e;
	if (lua_ctx != NULL) {
		lua_terminate(lua_ctx);
	}

	lua_ctx = lua_load_script(&args);
}

static const lv_font_t *
wslv_make_font(const char *name, int size, int weight)
{
	lv_ft_info_t info = {
		.name = name,
		.weight = size,
		.style = FT_FONT_STYLE_NORMAL,
	};

printf("%s %s %d %d\n", __func__, name, size, weight);

	if (!lv_ft_font_init(&info))
		return (NULL);

	return (info.font);
}

static void
wslv_delete_font(const lv_font_t *f)
{
	lv_ft_font_destroy((lv_font_t *)f);
}

void
wsluav(struct wslv_softc *sc, lv_obj_t *lvroot, const char *script)
{
	args.root = lvroot;
	args.make_font = wslv_make_font;
	args.delete_font = wslv_delete_font;
	args.script = script;

	wslv = sc;

	lv_obj_set_style_bg_color(lvroot, lv_color_black(), 0);

	lua_ctx = lua_load_script(&args);

	lv_obj_t *btn = lv_btn_create(lv_layer_sys());
	lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, -50);
	lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(btn, 5, 0);
	lv_obj_add_event_cb(btn, reload_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t* label = lv_label_create(btn);
	lv_label_set_text(label, "RELOAD");
	lv_obj_center(label);
}

static int _wslv_in_cmnd;

void
wsluav_cmnd(struct wslv_softc *sc, const char *topic, size_t topic_len,
    const char *payload, size_t payload_len)
{
	lua_State *L;
	int rv;

	if (lua_ctx == NULL)
		return;

	L = lua_ctx->L;

	lua_getglobal(L, "cmnd");
	if (!lua_isfunction(L, -1))
		return;

	lua_pushlstring(L, topic, topic_len);
	lua_pushlstring(L, payload, payload_len);

	_wslv_in_cmnd = 1;
	rv = lua_pcall(L, 2, 0, 0);
	_wslv_in_cmnd = 0;

	if (rv != 0)
		warnx("lua pcall cmnd %s", lua_tostring(L, -1));

	lua_pop(L, lua_gettop(L));
}

static int
wsluav_in_cmnd(lua_State *L)
{
	lua_pushboolean(L, _wslv_in_cmnd);
	return (1);
}

static int
wsluav_tele(lua_State *L)
{
	const char *topic;
	const char *payload;
	size_t topic_len, payload_len;

	topic = luaL_checklstring(L, 1, &topic_len);
	payload = luaL_checklstring(L, 2, &payload_len);

	wslv_tele(wslv, topic, topic_len, payload, payload_len);

	return (0);
}

static const struct luaL_Reg wslv_funcs[] = {
	{ "tele",	wsluav_tele },
	{ "in_cmnd",	wsluav_in_cmnd },
	{ NULL,		NULL }
};

static int
luaopen_wslv(lua_State *L)
{
	luaL_newlib(L, wslv_funcs);
	return (1);
}
