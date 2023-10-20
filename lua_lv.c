/* */

/*
 * Copyright (c) 2023 David Gwynne <david@gwynne.id.au>
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

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <lua.h>
#include <lauxlib.h>

#include "lvgl/lvgl.h"

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#define LV_LUA_DEBUG

#ifdef LV_LUA_DEBUG
#include <stdio.h>
#define LVDPRINTF(f...) do { \
	fprintf(stderr, "%s[%u]: ", __FUNCTION__, __LINE__); \
	fprintf(stderr, f); \
	fprintf(stderr, "\n"); \
} while (0)
#else
#define LVDPRINTF(f...) do { } while (0)
#endif

struct lua_lv_constant {
	const char			*k;
	lua_Number			 v;
};

struct lua_lv_constants {
	const char			*name;
	const struct lua_lv_constant	*kvs;
	size_t				 nkvs;
};

#define LUA_LV_CONSTANTS(_n, _a) { (_n), (_a), nitems(_a) }

#define LUA_LV_OBJ_T		"lv_obj_t"
#define LUA_LV_OBJ_STR		"'" LUA_LV_OBJ_T "'"
static const char lua_lv_obj_type[] = LUA_LV_OBJ_T;

/*
 * if we give memory to an obj, we put a table in the registry to track it
 */
#define LUA_LV_OBJ_REF_LOBJ		1 /* this keeps the lobj alive */
#define LUA_LV_OBJ_REF_GRID_COL_DSC	2
#define LUA_LV_OBJ_REF_GRID_ROW_DSC	3

static void
lua_lv_get_obj_table(lua_State *L)
{
	lua_rawgetp(L, LUA_REGISTRYINDEX, lua_lv_obj_type);
	if (!lua_istable(L, -1))
		luaL_error(L, "lua_lv_obj_type table missing");
}

static void
lua_lv_obj_udata(lua_State *L, lv_obj_t *obj)
{
	lua_lv_get_obj_table(L);
	lua_pushlightuserdata(L, obj);
	lua_rawget(L, -2);
}

static void
lua_lv_obj_set(lua_State *L, lv_obj_t *obj)
{
	/* lv_obj_t udata must be at the top of the stack */
	luaL_checkudata(L, -1, lua_lv_obj_type);

	lua_lv_get_obj_table(L);
	lua_pushlightuserdata(L, obj);
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pop(L, 1);
}

static void
lua_lv_obj_clr(lua_State *L, lv_obj_t *obj)
{
	lua_lv_get_obj_table(L);
	lua_pushlightuserdata(L, obj);
	lua_pushnil(L);
	lua_rawset(L, -3);
	lua_pop(L, 1);
}

struct lua_lv_obj {
	lv_obj_t		*lv_obj;
	int			 table;
};

static void
lua_lv_delete_cb(lv_event_t *e)
{
	lua_State *L = lv_event_get_user_data(e);
	lv_obj_t *obj = lv_event_get_target(e);
	struct lua_lv_obj *lobj;
	unsigned int i;

	lua_lv_obj_udata(L, obj);
	lobj = luaL_checkudata(L, -1, lua_lv_obj_type);

	LVDPRINTF("obj:%p, lobj:%p, lobj->lv_obj:%p", obj, lobj, lobj->lv_obj);

	/* destroy the event handlers */
	lua_pushvalue(L, -1);
	lua_pushnil(L);
	lua_rawset(L, LUA_REGISTRYINDEX);

	/* release the table */
	lua_pushnil(L);
	lua_rawsetp(L, LUA_REGISTRYINDEX, obj);

	lobj->lv_obj = NULL;
	lua_lv_obj_clr(L, obj);
}

static lv_obj_t *
lua_lv_check_obj(lua_State *L, int idx)
{
	struct lua_lv_obj *lobj = luaL_checkudata(L, idx, lua_lv_obj_type);
	lv_obj_t *obj = lobj->lv_obj;
	luaL_argcheck(L, obj != NULL, idx, LUA_LV_OBJ_STR " has been deleted");
	return (obj);
}

static lv_obj_t *
lua_lv_check_obj_class(lua_State *L, int idx, const lv_obj_class_t *class)
{
	lv_obj_t *obj = lua_lv_check_obj(L, idx);
	if (obj != NULL) {
		luaL_argcheck(L, lv_obj_has_class(obj, class), idx,
		    LUA_LV_OBJ_STR " wrong class");
	}
	return (obj);
}

static int
lua_lv_scr_act(lua_State *L)
{
	lv_obj_t *obj;
	struct lua_lv_obj *lobj;

	obj = lv_scr_act();
	if (obj == NULL)
		return luaL_error(L, "lv_scr_act() returned NULL");

	lua_lv_obj_udata(L, obj);
	if (lua_isnoneornil(L, -1)) {
		lobj = lua_newuserdata(L, sizeof(*lobj));
		lobj->lv_obj = obj;
		lobj->table = LUA_REFNIL;

		lv_obj_add_event_cb(obj, lua_lv_delete_cb, LV_EVENT_DELETE, L);

		luaL_setmetatable(L, lua_lv_obj_type);
		lua_lv_obj_set(L, obj);
	} else {
		lobj = luaL_checkudata(L, -1, lua_lv_obj_type);
		if (lobj->lv_obj != obj)
			return luaL_error(L, "lv_scr_act udata mismatch");
	}

	LVDPRINTF("obj:%p, lobj:%p", obj, lobj);

	return (1);
}

static int
lua_lv_obj_create_udata(lua_State *L, lv_obj_t *(*lv_create)(lv_obj_t *))
{
	lv_obj_t *parent = NULL;
	lv_obj_t *obj;
	struct lua_lv_obj *lobj;

	if (!lua_isnoneornil(L, 1)) {
		parent = lua_lv_check_obj(L, 1);
		lua_pop(L, 1);
	}

	lobj = lua_newuserdata(L, sizeof(*lobj));
	lobj->lv_obj = NULL;
	lobj->table = LUA_REFNIL;

	luaL_setmetatable(L, lua_lv_obj_type);

	obj = (*lv_create)(parent);
	if (obj == NULL)
		return luaL_error(L, "lv_obj_create failed");

	lv_obj_add_event_cb(obj, lua_lv_delete_cb, LV_EVENT_DELETE, L);

	lobj->lv_obj = obj;
	lua_lv_obj_set(L, obj);

	LVDPRINTF("parent:%p, obj:%p, lobj:%p", parent, obj, lobj);

	return (1);
}

static int
lua_lv_obj_create(lua_State *L)
{
	return (lua_lv_obj_create_udata(L, lv_obj_create));
}

static int
lua_lv_bar_create(lua_State *L)
{
	return (lua_lv_obj_create_udata(L, lv_bar_create));
}

static int
lua_lv_btn_create(lua_State *L)
{
	return (lua_lv_obj_create_udata(L, lv_btn_create));
}

static int
lua_lv_label_create(lua_State *L)
{
	return (lua_lv_obj_create_udata(L, lv_label_create));
}

static int
lua_lv_slider_create(lua_State *L)
{
	return (lua_lv_obj_create_udata(L, lv_slider_create));
}

static int
lua_lv_switch_create(lua_State *L)
{
	return (lua_lv_obj_create_udata(L, lv_switch_create));
}

static void
lua_lv_event_cb_pcall(lua_State *L, lv_event_t *e)
{
	lv_event_code_t event = lv_event_get_code(e);

	/* event table is at top, lobj at top - 2 */
	int top = lua_gettop(L);
	int ret;

	lua_rawgeti(L, top, 1); /* fn for pcall */
	lua_pushvalue(L, top - 2);

	lua_newtable(L); /* event data */

	lua_pushliteral(L, "code");
	lua_pushinteger(L, event);
	lua_rawset(L, -3);

	lua_pushliteral(L, "data");
	lua_rawgeti(L, top, 2); /* this might be nil */
	lua_rawset(L, -3);

	ret = lua_pcall(L, 2, 0, 0);
	switch (ret) {
	case 0:
		break;
	case LUA_ERRRUN:
		LVDPRINTF("callback: %s", lua_tostring(L, -1));
		break;
	case LUA_ERRMEM:
		LVDPRINTF("callback: memory error");
		break;
	case LUA_ERRERR:
		LVDPRINTF("callback: error error");
		break;
	default:
		LVDPRINTF("callback: unknown error %d", ret);
		break;
	}

	lua_settop(L, top);
}

static void
lua_lv_event_cb(lv_event_t *e)
{
	lua_State *L = lv_event_get_user_data(e);
	lv_obj_t *obj = lv_event_get_current_target(e);
	lv_event_code_t event = lv_event_get_code(e);
	struct lua_lv_obj *lobj;

	int top = lua_gettop(L);

	lua_lv_obj_udata(L, obj);
	lobj = luaL_checkudata(L, -1, lua_lv_obj_type);

	lua_pushvalue(L, -1);
	lua_rawget(L, LUA_REGISTRYINDEX);
	if (!lua_istable(L, -1))
		goto pop;

	lua_pushinteger(L, event);
	lua_rawget(L, -2);
	if (lua_istable(L, -1))
		lua_lv_event_cb_pcall(L, e);
	lua_pop(L, 1);

	lua_pushinteger(L, LV_EVENT_ALL);
	lua_rawget(L, -2);
	if (lua_istable(L, -1))
		lua_lv_event_cb_pcall(L, e);
	/* lua_pop(L, 1); - going to pop anyway */

pop:
	lua_settop(L, top);
}

static int
lua_lv_obj_add_event_cb(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	lv_event_code_t event = luaL_checkinteger(L, 2);
	int add = 0;

	luaL_argcheck(L, lua_isfunction(L, 3), 3, "callback function required");

	lua_pushvalue(L, 1);
	lua_rawget(L, LUA_REGISTRYINDEX);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);

		lua_pushvalue(L, 1);
		lua_newtable(L);

		add = 1;
	} else if (!lua_istable(L, -1))
		return luaL_error(L, "event table is not a table");

	lua_pushinteger(L, event);

	/* { fn, arg } */
	lua_newtable(L);

	lua_pushvalue(L, 3);
	lua_rawseti(L, -2, 1);

	if (!lua_isnoneornil(L, 4)) {
		lua_pushvalue(L, 4);
		lua_rawseti(L, -2, 2);
	}

	lua_rawset(L, -3);

	if (add) {
		lua_rawset(L, LUA_REGISTRYINDEX);
		lv_obj_add_event_cb(obj, lua_lv_event_cb, LV_EVENT_ALL, L);
	}

	return (0);
}

static int
lua_lv_obj_del_event_cb(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	lv_event_code_t event = luaL_checkinteger(L, 2);

	lua_pushvalue(L, 1);
	lua_rawget(L, LUA_REGISTRYINDEX);
	if (lua_isnil(L, -1)) {
		LVDPRINTF("obj:%p, no event found", obj);
		/* no event table found */
		return (0);
	} else if (!lua_istable(L, -1))
		return luaL_error(L, "event table is not a table");

	lua_pushinteger(L, event);
	lua_pushnil(L);
	lua_rawset(L, -3);

	lua_pushnil(L);
	if (!lua_next(L, -2)) {
		LVDPRINTF("obj:%p, empty event table", obj);
		lua_pushvalue(L, 1);
		lua_pushnil(L);
		lua_rawset(L, LUA_REGISTRYINDEX);

		lv_obj_remove_event_cb_with_user_data(obj, lua_lv_event_cb, L);
	}

	return (0);
}

static int
lua_lv_obj__gc(lua_State *L)
{
	struct lua_lv_obj *lobj = luaL_checkudata(L, 1, lua_lv_obj_type);
	lv_obj_t *obj = lobj->lv_obj;

	LVDPRINTF("obj:%p, lobj:%p", obj, lobj);

	if (obj != NULL) {
		lua_lv_obj_clr(L, obj);
		lv_obj_remove_event_cb_with_user_data(obj,
		    lua_lv_delete_cb, L);
		lv_obj_remove_event_cb_with_user_data(obj,
		    lua_lv_event_cb, L);

		lobj->lv_obj = NULL;
	}

	return (0);
}

static int
lua_lv_obj__index(lua_State *L)
{
	struct lua_lv_obj *lobj = luaL_checkudata(L, 1, lua_lv_obj_type);
	const char *key = lua_tostring(L, 2);
	lv_obj_t *obj = lobj->lv_obj;

	if (obj != NULL) {
		const lv_obj_class_t *c;
		for (c = lv_obj_get_class(obj); c != NULL; c = c->base_class) {
			lua_rawgetp(L, LUA_REGISTRYINDEX, c);
			if (!lua_istable(L, -1)) {
				lua_pop(L, 1);
				continue;
			}

			lua_pushstring(L, key);
			lua_rawget(L, -2);
			if (lua_isfunction(L, -1))
				return (1);
		}
	}

	if (lua_getmetatable(L, -2)) {
		lua_pushstring(L, key);
		lua_rawget(L, -2);
	} else
		lua_pushnil(L);

	return (1);
}

static int
lua_lv_obj__newindex(lua_State *L)
{
	struct lua_lv_obj *lobj = luaL_checkudata(L, 1, lua_lv_obj_type);
	LVDPRINTF("lobj:%p, gettop():%d", lobj, lua_gettop(L));
	return (0);
}

static int
lua_lv_obj_center(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	LVDPRINTF("obj:%p", obj);
	lv_obj_center(obj);
	return (0);
}

static int
lua_lv_obj_del(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	LVDPRINTF("obj:%p", obj);
	lv_obj_del(obj);
	return (0);
}

static int
lua_lv_obj_del_async(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	LVDPRINTF("obj:%p", obj);
	lv_obj_del_async(obj);
	return (0);
}

static int
lua_lv_obj_del_delayed(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	int ms = luaL_checkinteger(L, 2);
	luaL_argcheck(L, ms > 0, 2, "milliseconds must be > 0");
	LVDPRINTF("obj:%p, ms:%d", obj, ms);
	lv_obj_del_delayed(obj, ms);
	return (0);
}

static int
lua_lv_obj_remove_style_all(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	LVDPRINTF("obj:%p", obj);
	lv_obj_remove_style_all(obj);
	return (0);
}

static int
lua_lv_obj_size(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	lv_coord_t w, h;

	switch (lua_gettop(L)) {
	case 3:
		w = luaL_checkinteger(L, 2);
		h = luaL_checkinteger(L, 3);
		lv_obj_set_size(obj, w, h);
		break;
	case 1:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushinteger(L, lv_obj_get_width(obj));
	lua_pushinteger(L, lv_obj_get_height(obj));

	return (2);
}

static int
lua_lv_obj_refr_size(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);

	lv_obj_refr_size(obj);

	return (0);
}

static int
lua_lv_obj_width(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);

	switch (lua_gettop(L)) {
	case 2:
		lv_obj_set_width(obj, luaL_checknumber(L, 2));
		break;
	case 1:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushnumber(L, lv_obj_get_width(obj));
	return (1);
}

static int
lua_lv_obj_height(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);

	switch (lua_gettop(L)) {
	case 2:
		lv_obj_set_height(obj, luaL_checknumber(L, 2));
		break;
	case 1:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushnumber(L, lv_obj_get_height(obj));
	return (1);
}

static int
lua_lv_obj_pos(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	lv_coord_t x, y;

	switch (lua_gettop(L)) {
	case 3:
		x = luaL_checkinteger(L, 2);
		y = luaL_checkinteger(L, 3);
		lv_obj_set_pos(obj, x, y);
		break;
	case 1:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushinteger(L, lv_obj_get_x(obj));
	lua_pushinteger(L, lv_obj_get_y(obj));

	return (2);
}

static int
lua_lv_obj_x(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);

	switch (lua_gettop(L)) {
	case 2:
		lv_obj_set_x(obj, luaL_checknumber(L, 2));
		break;
	case 1:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushnumber(L, lv_obj_get_x(obj));
	return (1);
}

static int
lua_lv_obj_y(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);

	switch (lua_gettop(L)) {
	case 2:
		lv_obj_set_y(obj, luaL_checknumber(L, 2));
		break;
	case 1:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushnumber(L, lv_obj_get_y(obj));
	return (1);
}

static int
lua_lv_obj_align(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	lv_align_t align;
	lv_coord_t x = 0;
	lv_coord_t y = 0;

	switch (lua_gettop(L)) {
	case 4:
		x = lua_tonumber(L, 3);
		y = lua_tonumber(L, 4);
		/* FALLTHROUGH */
	case 2:
		align = luaL_checknumber(L, 2);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lv_obj_align(obj, align, x, y);
	return (0);
}

static int
lua_lv_obj_align_to(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	lv_obj_t *robj = lua_lv_check_obj(L, 2);
	lv_align_t align;
	lv_coord_t x;
	lv_coord_t y;

	if (lua_gettop(L) != 5)
		return luaL_error(L, "invalid number of arguments");

	align = luaL_checknumber(L, 3);
	x = lua_tonumber(L, 4);
	y = lua_tonumber(L, 5);

	lv_obj_align_to(obj, robj, align, x, y);
	return (0);
}

static int
lua_lv_obj_event_send(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	uint32_t code = luaL_checkinteger(L, 2);
	lv_res_t res;

	res = lv_event_send(obj, code, NULL);

	lua_pushboolean(L, res == LV_RES_OK);
	return (1);
}

static int
lua_lv_obj_update_layout(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);

	lv_obj_update_layout(obj);

	return (0);
}

static lv_coord_t *
lua_lv_obj_set_grid_array_dsc(lua_State *L, int idx)
{
	lv_coord_t *dsc = NULL;
	int len;
	int i;

	if (lua_isinteger(L, idx)) {
		len = lua_tointeger(L, idx);
		luaL_argcheck(L, len >= 1, idx, "must be >= 1");

		dsc = lua_newuserdata(L, sizeof(*dsc) * (len + 1));
		for (i = 0; i < len; i++)
			dsc[i] = LV_GRID_FR(1);

	} else if (lua_istable(L, idx)) {
		len = lua_rawlen(L, idx);
		luaL_argcheck(L, len >= 1, idx, "table length must be >= 1");

		dsc = lua_newuserdata(L, sizeof(*dsc) * (len + 1));
		for (i = 0; i < len; i++) {
			int k = i + 1;

			lua_rawgeti(L, idx, k);
			if (!lua_isinteger(L, -1)) {
				luaL_error(L, "key %d is not an integer", k);
				/* NOTREACHED */
				return (NULL);
			}

			dsc[i] = lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
	} else {
		luaL_error(L, "invalid type");
		/* NOTREACHED */
		return (NULL);
	}

	dsc[len] = LV_GRID_TEMPLATE_LAST;
	return (dsc);
}

static int
lua_lv_obj_set_grid_array(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	lv_coord_t *col_dsc;
	lv_coord_t *row_dsc;

	if (lua_gettop(L) != 3)
		return luaL_error(L, "invalid number of arguments");

	lua_rawgetp(L, LUA_REGISTRYINDEX, obj);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);

		lua_pushvalue(L, 1);
		lua_rawseti(L, -2, LUA_LV_OBJ_REF_LOBJ);
	}

	col_dsc = lua_lv_obj_set_grid_array_dsc(L, 2);
	lua_rawseti(L, -2, LUA_LV_OBJ_REF_GRID_COL_DSC);

	row_dsc = lua_lv_obj_set_grid_array_dsc(L, 3);
	lua_rawseti(L, -2, LUA_LV_OBJ_REF_GRID_ROW_DSC);

	lv_obj_set_grid_dsc_array(obj, col_dsc, row_dsc);

	/* doing this again doesn't hurt */
	lua_rawsetp(L, LUA_REGISTRYINDEX, obj);

	return (0);
}

static int
lua_lv_obj_set_grid_cell(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	int col_align = LV_GRID_ALIGN_START;
	int row_align = LV_GRID_ALIGN_START;
	int col_span = 1;
	int row_span = 1;
	int col_idx, row_idx;
	int col, row;

	switch (lua_gettop(L)) {
	case 7:
		col_align = luaL_checkinteger(L, 2);
		col_idx = 3;
		col_span = luaL_checkinteger(L, 4);
		luaL_argcheck(L, col_span >= 1, 4, "col span must be >= 1");

		row_align = luaL_checkinteger(L, 5);
		row_idx = 6;
		row_span = luaL_checkinteger(L, 7);
		luaL_argcheck(L, row_span >= 1, 7, "row span must be >= 1");
		break;
	case 3:
		col_idx = 2;
		row_idx = 3;
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	col = luaL_checkinteger(L, col_idx);
	luaL_argcheck(L, col >= 1, col_idx, "col must be >= 1");
	row = luaL_checkinteger(L, row_idx);
	luaL_argcheck(L, row >= 1, row_idx, "row must be >= 1");

	lv_obj_set_grid_cell(obj,
	    col_align, col - 1, col_span,
	    row_align, row - 1, row_span);

	return (0);
}

static int
lua_lv_obj_set_grid_align(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	int col_align = luaL_checkinteger(L, 2);
	int row_align = luaL_checkinteger(L, 3);

	lv_obj_set_grid_align(obj, col_align, row_align);

	return (0);
}

static int
lua_lv_obj_state(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	lv_state_t state = luaL_checkinteger(L, 2);
	int set;

	switch (lua_gettop(L)) {
	case 3:
		set = lua_toboolean(L, 3);
		if (set)
			lv_obj_add_state(obj, state);
		else
			lv_obj_clear_state(obj, state);
		break;
	case 2:
		set = lv_obj_has_state(obj, state);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushboolean(L, set);
	return (1);
}

static int
lua_lv_obj_states(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);

	lua_pushinteger(L, lv_obj_get_state(obj));
	return (1);
}

static int
lua_lv_obj_flag(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	lv_obj_flag_t flag = luaL_checkinteger(L, 2);
	int set;

	switch (lua_gettop(L)) {
	case 3:
		set = lua_toboolean(L, 3);
		if (set)
			lv_obj_add_flag(obj, flag);
		else
			lv_obj_clear_flag(obj, flag);
		break;
	case 2:
		set = lv_obj_has_flag(obj, flag);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushboolean(L, set);
	return (1);
}

static int
lua_lv_obj_state_bit(lua_State *L, lv_state_t state)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	int set;

	switch (lua_gettop(L)) {
	case 2:
		set = lua_toboolean(L, 2);
		if (set)
			lv_obj_add_state(obj, state);
		else
			lv_obj_clear_state(obj, state);
		break;
	case 1:
		set = lv_obj_has_state(obj, state);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushboolean(L, set);
	return (1);
}

static int
lua_lv_obj_checked(lua_State *L)
{
	return lua_lv_obj_state_bit(L, LV_STATE_CHECKED);
}

static int
lua_lv_obj_disabled(lua_State *L)
{
	return lua_lv_obj_state_bit(L, LV_STATE_DISABLED);
}

static int
lua_lv_pct(lua_State *L)
{
	lua_pushinteger(L, lv_pct(luaL_checkinteger(L, 1)));
	return (1);
}

/*
 * color stuff
 */

static const struct lua_lv_constant lua_lv_palette_t[] = {
	{ "red",		LV_PALETTE_RED },
	{ "pink",		LV_PALETTE_PINK },
	{ "purple",		LV_PALETTE_PURPLE },
	{ "deep_purple",	LV_PALETTE_DEEP_PURPLE },
	{ "deep-purple",	LV_PALETTE_DEEP_PURPLE },
	{ "deep purple",	LV_PALETTE_DEEP_PURPLE },
	{ "indigo",		LV_PALETTE_INDIGO },
	{ "blue",		LV_PALETTE_BLUE },
	{ "light_blue",		LV_PALETTE_LIGHT_BLUE },
	{ "light-blue",		LV_PALETTE_LIGHT_BLUE },
	{ "light blue",		LV_PALETTE_LIGHT_BLUE },
	{ "cyan",		LV_PALETTE_CYAN },
	{ "teal",		LV_PALETTE_TEAL },
	{ "green",		LV_PALETTE_GREEN },
	{ "light_green",	LV_PALETTE_LIGHT_GREEN },
	{ "light-green",	LV_PALETTE_LIGHT_GREEN },
	{ "light green",	LV_PALETTE_LIGHT_GREEN },
	{ "lime",		LV_PALETTE_LIME },
	{ "yellow",		LV_PALETTE_YELLOW },
	{ "amber",		LV_PALETTE_AMBER },
	{ "orange",		LV_PALETTE_ORANGE },
	{ "deep_orange",	LV_PALETTE_DEEP_ORANGE },
	{ "deep-orange",	LV_PALETTE_DEEP_ORANGE },
	{ "deep orange",	LV_PALETTE_DEEP_ORANGE },
	{ "brown",		LV_PALETTE_BROWN },
	{ "blue_grey",		LV_PALETTE_BLUE_GREY },
	{ "blue-grey",		LV_PALETTE_BLUE_GREY },
	{ "blue grey",		LV_PALETTE_BLUE_GREY },
	{ "grey",		LV_PALETTE_GREY },
};

static void
lua_lv_palette_init(lua_State *L)
{
	size_t i;

	lua_newtable(L);
	for (i = 0; i < nitems(lua_lv_palette_t); i++) {
		const struct lua_lv_constant *c = &lua_lv_palette_t[i];

		lua_pushstring(L, c->k);
		lua_pushinteger(L, c->v);
		lua_settable(L, -3);
	}
	lua_rawsetp(L, LUA_REGISTRYINDEX, lua_lv_palette_t);
}

/* returns -1 if the thing wasnt found */
static int
lua_lv_palette_get(lua_State *L, int idx)
{
	int rv = -1;

	lua_rawgetp(L, LUA_REGISTRYINDEX, lua_lv_palette_t);
	lua_pushvalue(L, idx);
	lua_rawget(L, -2);
	if (lua_isinteger(L, -1))
		rv = lua_tointeger(L, -1);
	lua_pop(L, 2);

	return (rv);
}

static uint8_t
lua_lv_checku8(lua_State *L, int idx)
{
	int i = luaL_checkinteger(L, idx);
	luaL_argcheck(L, i >= 0 && i <= 255, idx, "invalid value");
	return (i);
}

static int
lua_lv_hexdec(lua_State *L, int ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	if (ch >= 'a' && ch <= 'f')
		return (ch - 'a' + 10);
	if (ch >= 'A' && ch <= 'F')
		return (ch - 'A' + 10);

	return luaL_error(L, "invalid hex digit");
}

static lv_color_t
lua_lv_color_arg(lua_State *L, int idx)
{
	int r, g, b;
	int palette;
	const char *str;

	if (lua_istable(L, idx)) {
		lua_rawgeti(L, idx, 1);
		r = lua_lv_checku8(L, -1);
		lua_rawgeti(L, idx, 2);
		g = lua_lv_checku8(L, -1);
		lua_rawgeti(L, idx, 3);
		b = lua_lv_checku8(L, -1);
		lua_pop(L, 3);

		return lv_color_make(r, g, b);
	}

	if (lua_isinteger(L, idx)) {
		int hex = luaL_checkinteger(L, idx);
		luaL_argcheck(L, hex >= 0x0 && hex <= 0xffffff, idx,
		    "invalid value");
			
		return lv_color_hex(hex);
	}

	palette = lua_lv_palette_get(L, idx);
	if (palette != -1)
		return lv_palette_main(palette);

	str = lua_tostring(L, idx);
	luaL_argcheck(L, str[0] == '#', idx, "hex strings start with #");

	switch (strlen(str)) {
	case 4:
		r = lua_lv_hexdec(L, str[1]);
		g = lua_lv_hexdec(L, str[2]);
		b = lua_lv_hexdec(L, str[3]);

		r |= r << 4;
		g |= g << 4;
		b |= b << 4;
		break;
	case 7:
		r  = lua_lv_hexdec(L, str[1]) << 4;
		r |= lua_lv_hexdec(L, str[2]);
		g  = lua_lv_hexdec(L, str[3]) << 4;
		g |= lua_lv_hexdec(L, str[4]);
		b  = lua_lv_hexdec(L, str[5]) << 4;
		b |= lua_lv_hexdec(L, str[6]);
		break;
	default:
		luaL_error(L, "invalid hex string");
		return lv_color_hex(0x000000); /* notreached */
	}

	return lv_color_make(r, g, b);
}

static int
lua_lv_color(lua_State *L)
{
	lv_color_t c;
	int r, g, b;
	const char *str;
	int palette;

	switch (lua_gettop(L)) {
	case 3:
		r = lua_lv_checku8(L, 1);
		g = lua_lv_checku8(L, 2);
		b = lua_lv_checku8(L, 3);

		c = lv_color_make(r, g, b);
		break;
	case 1:
		c = lua_lv_color_arg(L, 1);
		break;

	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushinteger(L, c.full);
	return (1);
}

static int
lua_lv_grid_fr(lua_State *L)
{
	lua_pushinteger(L, lv_grid_fr(luaL_checkinteger(L, 1)));
	return (1);
}

/*
 * style stuff
 */

struct lua_lv_style {
	const char		*name;
	lv_style_prop_t		 prop;
	lv_style_value_t (*check)(lua_State *, int);
};

static lv_style_value_t
lua_lv_style_num(lua_State *L, int idx)
{
	lv_style_value_t v = {
		.num = luaL_checkinteger(L, idx)
	};

	return v;
}

static lv_style_value_t
lua_lv_style_bool(lua_State *L, int idx)
{
	lv_style_value_t v = {
		.num = lua_toboolean(L, idx)
	};

	return v;
}

static lv_style_value_t
lua_lv_style_color(lua_State *L, int idx)
{
	lv_style_value_t v = {
		.color = lua_lv_color_arg(L, idx)
	};

	return (v);
}

static const struct lua_lv_style lua_lv_styles[] = {
	{ "width",		LV_STYLE_WIDTH,		lua_lv_style_num },
	{ "w",			LV_STYLE_WIDTH,		lua_lv_style_num },
	{ "min_width",		LV_STYLE_MIN_WIDTH,	lua_lv_style_num },
	{ "min_w",		LV_STYLE_MIN_WIDTH,	lua_lv_style_num },
	{ "max_width",		LV_STYLE_MAX_WIDTH,	lua_lv_style_num },
	{ "max_w",		LV_STYLE_MAX_WIDTH,	lua_lv_style_num },
	{ "height",		LV_STYLE_HEIGHT,	lua_lv_style_num },
	{ "h",			LV_STYLE_HEIGHT,	lua_lv_style_num },
	{ "min_height",		LV_STYLE_MIN_HEIGHT,	lua_lv_style_num },
	{ "min_h",		LV_STYLE_MIN_HEIGHT,	lua_lv_style_num },
	{ "max_height",		LV_STYLE_MAX_HEIGHT,	lua_lv_style_num },
	{ "max_h",		LV_STYLE_MAX_HEIGHT,	lua_lv_style_num },
	{ "x",			LV_STYLE_X,		lua_lv_style_num },
	{ "y",			LV_STYLE_Y,		lua_lv_style_num },
	{ "align",		LV_STYLE_ALIGN,		lua_lv_style_num },
	{ "layout",		LV_STYLE_LAYOUT,	lua_lv_style_num },
	{ "radius",		LV_STYLE_RADIUS,	lua_lv_style_num },

	{ "pad_top",		LV_STYLE_PAD_TOP,	lua_lv_style_num },
	{ "pad_bottom",		LV_STYLE_PAD_BOTTOM,	lua_lv_style_num },
	{ "pad_left",		LV_STYLE_PAD_LEFT,	lua_lv_style_num },
	{ "pad_right",		LV_STYLE_PAD_RIGHT,	lua_lv_style_num },
	{ "pad_row",		LV_STYLE_PAD_ROW,	lua_lv_style_num },
	{ "pad_column",		LV_STYLE_PAD_COLUMN,	lua_lv_style_num },
	{ "base_dir",		LV_STYLE_BASE_DIR,	lua_lv_style_num },
	{ "clip_corner",	LV_STYLE_CLIP_CORNER,	lua_lv_style_bool },

	{ "bg_color",		LV_STYLE_BG_COLOR,	lua_lv_style_color },
	{ "bg_opa",		LV_STYLE_BG_OPA,	lua_lv_style_num },
	{ "bg_grad_color",	LV_STYLE_BG_GRAD_COLOR,	lua_lv_style_color },
	{ "bg_grad_dir",	LV_STYLE_BG_GRAD_DIR,	lua_lv_style_num },
	{ "bg_main_stop",	LV_STYLE_BG_MAIN_STOP,	lua_lv_style_num },
	{ "bg_grad_stop",	LV_STYLE_BG_GRAD_STOP,	lua_lv_style_num },
#if 0
	{ "bg_grad",		LV_STYLE_BG_GRAD,	lua_lv_style_grad_dsc },
	{ "bg_dither_mode",	LV_STYLE_BG_DITHER_MODE,
							lua_lv_style_num },
	{ "bg_img_src",		LV_STYLE_BG_IMG_SRC,	lua_lv_style_ },
	{ "bg_img_opa",		LV_STYLE_BG_IMG_OPA,	lua_lv_style_num },
	{ "bg_img_recolor",	LV_STYLE_BG_IMG_RECOLOR,
							lua_lv_style_color },
	{ "bg_img_recolor_opa",	LV_STYLE_BG_IMG_RECOLOR_OPA,
							lua_lv_style_num },
	{ "bg_img_tiled",	LV_STYLE_BG_IMG_TILED,	lua_lv_style_bool },
#endif

	{ "border_color",	LV_STYLE_BORDER_COLOR,	lua_lv_style_color },
	{ "border_opa",		LV_STYLE_BORDER_OPA,	lua_lv_style_num },
	{ "border_width",	LV_STYLE_BORDER_WIDTH,	lua_lv_style_num },
	{ "border_side",	LV_STYLE_BORDER_SIDE,	lua_lv_style_num },
	{ "border_post",	LV_STYLE_BORDER_POST,	lua_lv_style_bool },
	{ "outline_width",	LV_STYLE_OUTLINE_WIDTH,	lua_lv_style_num },
	{ "outline_color",	LV_STYLE_OUTLINE_COLOR,	lua_lv_style_color },
	{ "outline_opa",	LV_STYLE_OUTLINE_OPA,	lua_lv_style_num },
	{ "outline_pad",	LV_STYLE_OUTLINE_PAD,	lua_lv_style_num },

	{ "anim_time",		LV_STYLE_ANIM_TIME,	lua_lv_style_num },
};

static void
lua_lv_styles_init(lua_State *L)
{
	size_t i;

	lua_newtable(L);
	for (i = 0; i < nitems(lua_lv_styles); i++) {
		const struct lua_lv_style *s = &lua_lv_styles[i];

		lua_pushstring(L, s->name);
		lua_pushlightuserdata(L, (void *)s);
		lua_settable(L, -3);
	}
	lua_rawsetp(L, LUA_REGISTRYINDEX, lua_lv_styles);
}

static int
lua_lv_obj_set_style(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj(L, 1);
	const struct lua_lv_style *s;
	lv_style_value_t v;
	int selector = LV_PART_MAIN;

	switch (lua_gettop(L)) {
	case 4:
		selector = luaL_checkinteger(L, 4);
		/* FALLTHROUGH */
	case 3:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_rawgetp(L, LUA_REGISTRYINDEX, lua_lv_styles);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);

	s = lua_touserdata(L, -1);
	luaL_argcheck(L, s != NULL, 2, "unknown style property");

	v = s->check(L, 3);

	lv_obj_set_local_style_prop(obj, s->prop, v, selector);

	return (0);
}

/*
 * lua_lv_obj metatable methods
 */

static const luaL_Reg lua_lv_obj_methods[] = {
	{ "center",		lua_lv_obj_center },
	{ "del",		lua_lv_obj_del },
	{ "del_async",		lua_lv_obj_del_async },
	{ "del_delayed",	lua_lv_obj_del_delayed },
	{ "remove_style_all",	lua_lv_obj_remove_style_all },

	{ "size",		lua_lv_obj_size },
	{ "refr_size",		lua_lv_obj_refr_size },
	{ "w",			lua_lv_obj_width },
	{ "width",		lua_lv_obj_width },
	{ "h",			lua_lv_obj_height },
	{ "height",		lua_lv_obj_height },

	{ "pos",		lua_lv_obj_pos },
	{ "x",			lua_lv_obj_x },
	{ "y",			lua_lv_obj_y },

	{ "align",		lua_lv_obj_align },
	{ "align_to",		lua_lv_obj_align_to },
	{ "update_layout",	lua_lv_obj_update_layout },

	{ "set_grid_array",	lua_lv_obj_set_grid_array },
	{ "set_grid_cell",	lua_lv_obj_set_grid_cell },
	{ "set_grid_align",	lua_lv_obj_set_grid_align },

	{ "event_send",		lua_lv_obj_event_send },
	{ "add_event_cb",	lua_lv_obj_add_event_cb },
	{ "del_event_cb",	lua_lv_obj_del_event_cb },

	{ "state",		lua_lv_obj_state },
	{ "states",		lua_lv_obj_states },
	{ "flag",		lua_lv_obj_flag },

	/* these are convenience wrappers */
	{ "checked",		lua_lv_obj_checked },
	{ "disabled",		lua_lv_obj_disabled },

	{ "set_style",		lua_lv_obj_set_style },

	{ NULL,			NULL }
};

/*
 * lv_bar
 */

static int
lua_lv_bar_value(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_bar_class);
	int32_t value;
	lv_anim_enable_t anim = LV_ANIM_OFF;

	switch (lua_gettop(L)) {
	case 3:
		anim = lua_toboolean(L, 2) ? LV_ANIM_ON : LV_ANIM_OFF;
		/* FALLTHROUGH */
	case 2:
		value = luaL_checkinteger(L, 2);
		lv_bar_set_value(obj, value, anim);
		break;
	case 1:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	value = lv_bar_get_value(obj);
	lua_pushinteger(L, value);

	return (1);
}

static int
lua_lv_bar_start_value(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_bar_class);
	int32_t value;
	lv_anim_enable_t anim = LV_ANIM_OFF;

	switch (lua_gettop(L)) {
	case 3:
		anim = lua_toboolean(L, 2) ? LV_ANIM_ON : LV_ANIM_OFF;
		/* FALLTHROUGH */
	case 2:
		value = luaL_checkinteger(L, 2);
		lv_bar_set_start_value(obj, value, anim);
		break;
	case 1:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	value = lv_bar_get_start_value(obj);
	lua_pushinteger(L, value);

	return (1);
}

static int
lua_lv_bar_range(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_bar_class);
	int32_t min, max;

	switch (lua_gettop(L)) {
	case 3:
		min = luaL_checkinteger(L, 2);
		max = luaL_checkinteger(L, 3);
		lv_bar_set_range(obj, min, max);
		break;
	case 1:
		min = lv_bar_get_min_value(obj);
		max = lv_bar_get_max_value(obj);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushinteger(L, min);
	lua_pushinteger(L, max);

	return (2);
}

static int
lua_lv_bar_mode(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_bar_class);
	lv_bar_mode_t mode;

	switch (lua_gettop(L)) {
	case 2:
		mode = luaL_checkinteger(L, 2);
		lv_bar_set_mode(obj, mode);
		break;
	case 1:
		mode = lv_bar_get_mode(obj);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushinteger(L, mode);

	return (1);
}

static int
lua_lv_bar_min(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_bar_class);
	int32_t min;

	switch (lua_gettop(L)) {
	case 2:
		min = luaL_checkinteger(L, 2);
		lv_bar_set_range(obj, min, lv_bar_get_max_value(obj));
		break;
	case 1:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	min = lv_bar_get_min_value(obj);
	lua_pushinteger(L, min);

	return (1);
}

static int
lua_lv_bar_max(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_bar_class);
	int32_t max;

	switch (lua_gettop(L)) {
	case 2:
		max = luaL_checkinteger(L, 2);
		lv_bar_set_range(obj, lv_bar_get_min_value(obj), max);
		break;
	case 1:
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	max = lv_bar_get_max_value(obj);
	lua_pushinteger(L, max);

	return (1);
}

static const luaL_Reg lua_lv_bar_methods[] = {
	{ "value",		lua_lv_bar_value },
	{ "start_value",	lua_lv_bar_start_value },
	{ "range",		lua_lv_bar_range },
	{ "mode",		lua_lv_bar_range },
	{ "min",		lua_lv_bar_min },
	{ "max",		lua_lv_bar_max },

	{ NULL,			NULL }
};

static const struct lua_lv_constant lua_lv_bar_mode_t[] = {
	{ "NORMAL",		LV_BAR_MODE_NORMAL },
	{ "SYMMETRICAL",	LV_BAR_MODE_SYMMETRICAL },
	{ "RANGE",		LV_BAR_MODE_RANGE },
};

/*
 * lv_btn
 */

static const luaL_Reg lua_lv_btn_methods[] = {

	{ NULL,			NULL }
};

/*
 * lv_label
 */

static int
lua_lv_label_text(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_label_class);
	const char *str;

	switch (lua_gettop(L)) {
	case 2:
		str = luaL_checkstring(L, 2);
		lv_label_set_text(obj, str);
		break;
	case 1:
		str = lv_label_get_text(obj);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushstring(L, str);

	return (1);
}

static int
lua_lv_label_recolor(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_label_class);
	int recolor;

	switch (lua_gettop(L)) {
	case 2:
		recolor = lua_toboolean(L, 2);
		lv_label_set_recolor(obj, recolor);
		break;
	case 1:
		recolor = lv_label_get_recolor(obj);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushboolean(L, recolor);

	return (1);
}

static int
lua_lv_label_long_mode(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_label_class);
	int mode;

	switch (lua_gettop(L)) {
	case 2:
		mode = luaL_checkinteger(L, 2);
		lv_label_set_long_mode(obj, mode);
		break;
	case 1:
		mode = lv_label_get_long_mode(obj);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	lua_pushinteger(L, mode);

	return (1);
}

static int
lua_lv_label_ins_text(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_label_class);
	uint32_t pos;
	const char *str;

	switch (lua_gettop(L)) {
	case 3:
		pos = luaL_checkinteger(L, 2);
		str = luaL_checkstring(L, 3);
		lv_label_ins_text(obj, pos, str);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	return (0);
}

static int
lua_lv_label_cut_text(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_label_class);
	uint32_t pos, cnt;

	switch (lua_gettop(L)) {
	case 3:
		pos = luaL_checkinteger(L, 2);
		cnt = luaL_checkinteger(L, 3);
		lv_label_cut_text(obj, pos, cnt);
		break;
	default:
		return luaL_error(L, "invalid number of arguments");
	}

	return (0);
}

static const luaL_Reg lua_lv_label_methods[] = {
	{ "text",		lua_lv_label_text },
	{ "recolor",		lua_lv_label_recolor },
	{ "long_mode",		lua_lv_label_long_mode },
	{ "ins_text",		lua_lv_label_ins_text },
	{ "cut_text",		lua_lv_label_cut_text },

	{ NULL,			NULL }
};

static const struct lua_lv_constant lua_lv_label_long_mode_t[] = {
	{ "WRAP",		LV_LABEL_LONG_WRAP },
	{ "DOT",		LV_LABEL_LONG_DOT },
	{ "SCROLL",		LV_LABEL_LONG_SCROLL },
	{ "SCROLL_CIRCULAR",	LV_LABEL_LONG_SCROLL_CIRCULAR },
	{ "CLIP",		LV_LABEL_LONG_CLIP },
};

/*
 * lv_slider
 *
 * lv_slider is mostly lv_bar, so let lv_bar do most of the work
 */

static int
lua_lv_slider_is_dragged(lua_State *L)
{
	lv_obj_t *obj = lua_lv_check_obj_class(L, 1, &lv_slider_class);

	lua_pushboolean(L, lv_slider_is_dragged(obj));

	return (1);
}

static const luaL_Reg lua_lv_slider_methods[] = {
	{ "is_dragged",		lua_lv_slider_is_dragged },

	{ NULL,			NULL }
};

#define lua_lv_slider_mode_t lua_lv_bar_mode_t

/*
 * lv_switch
 */

static const luaL_Reg lua_lv_switch_methods[] = {

	{ NULL,			NULL }
};

/*
 * widgets
 */

struct lua_lv_obj_class {
	const lv_obj_class_t	*obj_class;
	const luaL_Reg		*methods;
};

static const struct lua_lv_obj_class lua_lv_obj_classes[] = {
	{ &lv_obj_class,	lua_lv_obj_methods },
	{ &lv_bar_class,	lua_lv_bar_methods },
	{ &lv_btn_class,	lua_lv_btn_methods },
	{ &lv_label_class,	lua_lv_label_methods },
	{ &lv_slider_class,	lua_lv_slider_methods },
	{ &lv_switch_class,	lua_lv_switch_methods },
};

/*
 * constants
 */

static const struct lua_lv_constant lua_lv_state_t[] = {
	{ "DEFAULT",		LV_STATE_DEFAULT },
	{ "CHECKED",		LV_STATE_CHECKED },
	{ "FOCUSED",		LV_STATE_FOCUSED },
	{ "FOCUS_KEY",		LV_STATE_FOCUS_KEY },
	{ "EDITED",		LV_STATE_EDITED },
	{ "HOVERED",		LV_STATE_HOVERED },
	{ "PRESSED",		LV_STATE_PRESSED },
	{ "SCROLLED",		LV_STATE_SCROLLED },
	{ "DISABLED",		LV_STATE_DISABLED },

	{ "USER_1",		LV_STATE_USER_1 },
	{ "USER_2",		LV_STATE_USER_2 },
	{ "USER_2",		LV_STATE_USER_3 },
	{ "USER_2",		LV_STATE_USER_4 },

	{ "ANY",		LV_STATE_ANY },
};

static const struct lua_lv_constant lua_lv_part_t[] = {
	{ "MAIN",		LV_PART_MAIN },
	{ "SCROLLBAR",		LV_PART_SCROLLBAR },
	{ "INDICATOR",		LV_PART_INDICATOR },
	{ "KNOB",		LV_PART_KNOB },
	{ "SELECTED",		LV_PART_SELECTED },
	{ "ITEMS",		LV_PART_ITEMS },
	{ "TICKS",		LV_PART_TICKS },
	{ "CURSOR",		LV_PART_CURSOR },
	{ "CUSTOM_FIRST",	LV_PART_CUSTOM_FIRST },

	{ "ANY",		LV_PART_ANY },
};

static const struct lua_lv_constant lua_lv_obj_flag_t[] = {
	{ "HIDDEN",		LV_OBJ_FLAG_HIDDEN },
	{ "CLICKABLE",		LV_OBJ_FLAG_CLICKABLE },
	{ "CLICK_FOCUSABLE",	LV_OBJ_FLAG_CLICK_FOCUSABLE },
	{ "CHECKABLE",		LV_OBJ_FLAG_CHECKABLE },
	{ "SCROLLABLE",		LV_OBJ_FLAG_SCROLLABLE },
	{ "SCROLL_ELASTIC",	LV_OBJ_FLAG_SCROLL_ELASTIC },
	{ "SCROLL_MOMENTUM",	LV_OBJ_FLAG_SCROLL_MOMENTUM },
	{ "SCROLL_ONE",		LV_OBJ_FLAG_SCROLL_ONE },
	{ "SCROLL_CHAIN_HOR",	LV_OBJ_FLAG_SCROLL_CHAIN_HOR },
	{ "SCROLL_CHAIN_VER",	LV_OBJ_FLAG_SCROLL_CHAIN_VER },
	{ "SCROLL_CHAIN",	LV_OBJ_FLAG_SCROLL_CHAIN },
	{ "SCROLL_ON_FOCUS",	LV_OBJ_FLAG_SCROLL_ON_FOCUS },
	{ "SCROLL_WITH_ARROW",	LV_OBJ_FLAG_SCROLL_WITH_ARROW },
	{ "SNAPPABLE",		LV_OBJ_FLAG_SNAPPABLE },
	{ "PRESS_LOCK",		LV_OBJ_FLAG_PRESS_LOCK },
	{ "EVENT_BUBBLE",	LV_OBJ_FLAG_EVENT_BUBBLE },
	{ "GESTURE_BUBBLE",	LV_OBJ_FLAG_GESTURE_BUBBLE },
	{ "ADV_HITTEST",	LV_OBJ_FLAG_ADV_HITTEST },
	{ "IGNORE_LAYOUT",	LV_OBJ_FLAG_IGNORE_LAYOUT },
	{ "FLOATING",		LV_OBJ_FLAG_FLOATING },
	{ "OVERFLOW_VISIBLE",	LV_OBJ_FLAG_OVERFLOW_VISIBLE },
	{ "LAYOUT_1",		LV_OBJ_FLAG_LAYOUT_1 },
	{ "LAYOUT_2",		LV_OBJ_FLAG_LAYOUT_2 },
	{ "WIDGET_1",		LV_OBJ_FLAG_WIDGET_1 },
	{ "WIDGET_2",		LV_OBJ_FLAG_WIDGET_2 },
	{ "USER_1",		LV_OBJ_FLAG_USER_1 },
	{ "USER_2",		LV_OBJ_FLAG_USER_2 },
	{ "USER_3",		LV_OBJ_FLAG_USER_3 },
	{ "USER_4",		LV_OBJ_FLAG_USER_4 },
};

static const struct lua_lv_constant lua_lv_align_t[] = {
	{ "DEFAULT",		LV_ALIGN_DEFAULT },
	{ "TOP_LEFT",		LV_ALIGN_TOP_LEFT },
	{ "TOP_MID",		LV_ALIGN_TOP_MID },
	{ "TOP_RIGHT",		LV_ALIGN_TOP_RIGHT },
	{ "BOTTOM_LEFT",	LV_ALIGN_BOTTOM_LEFT },
	{ "BOTTOM_MID",		LV_ALIGN_BOTTOM_MID },
	{ "BOTTOM_RIGHT",	LV_ALIGN_BOTTOM_RIGHT },
	{ "LEFT_MID",		LV_ALIGN_LEFT_MID },
	{ "RIGHT_MID",		LV_ALIGN_RIGHT_MID },
	{ "CENTER",		LV_ALIGN_CENTER },

	{ "OUT_TOP_LEFT",	LV_ALIGN_OUT_TOP_LEFT },
	{ "OUT_TOP_MID",	LV_ALIGN_OUT_TOP_MID },
	{ "OUT_TOP_RIGHT",	LV_ALIGN_OUT_TOP_RIGHT },
	{ "OUT_BOTTOM_LEFT",	LV_ALIGN_OUT_BOTTOM_LEFT },
	{ "OUT_BOTTOM_MID",	LV_ALIGN_OUT_BOTTOM_MID },
	{ "OUT_BOTTOM_RIGHT",	LV_ALIGN_OUT_BOTTOM_RIGHT },
	{ "OUT_LEFT_TOP",	LV_ALIGN_OUT_LEFT_TOP },
	{ "OUT_LEFT_MID",	LV_ALIGN_OUT_LEFT_MID },
	{ "OUT_LEFT_BOTTOM",	LV_ALIGN_OUT_LEFT_BOTTOM },
	{ "OUT_RIGHT_TOP",	LV_ALIGN_OUT_RIGHT_TOP },
	{ "OUT_RIGHT_MID",	LV_ALIGN_OUT_RIGHT_MID },
	{ "OUT_RIGHT_BOTTOM",	LV_ALIGN_OUT_RIGHT_BOTTOM },
};

static const struct lua_lv_constant lua_lv_dir_t[] = {
	{ "NONE",		LV_DIR_NONE },
	{ "LEFT",		LV_DIR_LEFT },
	{ "RIGHT",		LV_DIR_RIGHT },
	{ "TOP",		LV_DIR_TOP },
	{ "BOTTOM",		LV_DIR_BOTTOM },
	{ "HOR",		LV_DIR_HOR },
	{ "VER",		LV_DIR_VER },
};

static const struct lua_lv_constant lua_lv_event_t[] = {
	{ "ALL",		LV_EVENT_ALL },

	{ "PRESSED",		LV_EVENT_PRESSED },
	{ "PRESSING",		LV_EVENT_PRESSING },
	{ "PRESS_LOST",		LV_EVENT_PRESS_LOST },
	{ "SHORT_CLICKED",	LV_EVENT_SHORT_CLICKED },
	{ "LONG_PRESSED",	LV_EVENT_LONG_PRESSED },
	{ "LONG_PRESSED_REPEAT",
				LV_EVENT_LONG_PRESSED_REPEAT },
	{ "CLICKED",		LV_EVENT_CLICKED },
	{ "RELEASED",		LV_EVENT_RELEASED },
	{ "SCROLL_BEGIN",	LV_EVENT_SCROLL_BEGIN },
	{ "SCROLL_END",		LV_EVENT_SCROLL_END },
	{ "SCROLL",		LV_EVENT_SCROLL },
	{ "GESTURE",		LV_EVENT_GESTURE },
	{ "KEY",		LV_EVENT_KEY },
	{ "FOCUSED",		LV_EVENT_FOCUSED },
	{ "DEFOCUSED",		LV_EVENT_DEFOCUSED },
	{ "LEAVE",		LV_EVENT_LEAVE },
	{ "HIT_TEST",		LV_EVENT_HIT_TEST },

	{ "COVER_CHECK",	LV_EVENT_COVER_CHECK },
	{ "REFR_EXT_DRAW_SIZE",	LV_EVENT_REFR_EXT_DRAW_SIZE },
	{ "DRAW_MAIN_BEGIN",	LV_EVENT_DRAW_MAIN_BEGIN },
	{ "DRAW_MAIN",		LV_EVENT_DRAW_MAIN },
	{ "DRAW_MAIN_END",	LV_EVENT_DRAW_MAIN_END },
	{ "DRAW_POST_BEGIN",	LV_EVENT_DRAW_POST_BEGIN },
	{ "DRAW_POST",		LV_EVENT_DRAW_POST },
	{ "DRAW_POST_END",	LV_EVENT_DRAW_POST_END },
	{ "DRAW_PART_BEGIN",	LV_EVENT_DRAW_PART_BEGIN },
	{ "DRAW_PART_END",	LV_EVENT_DRAW_PART_END },

	{ "VALUE_CHANGED",	LV_EVENT_VALUE_CHANGED },
	{ "INSERT",		LV_EVENT_INSERT },
	{ "REFRESH",		LV_EVENT_REFRESH },
	{ "READY",		LV_EVENT_READY },
	{ "CANCEL",		LV_EVENT_CANCEL },

	{ "DELETE",		LV_EVENT_DELETE },
	{ "CHILD_CHANGED",	LV_EVENT_CHILD_CHANGED },
	{ "CHILD_CREATED",	LV_EVENT_CHILD_CREATED },
	{ "CHILD_DELETED",	LV_EVENT_CHILD_DELETED },

	{ "SCREEN_UNLOAD_START",
				LV_EVENT_SCREEN_UNLOAD_START },
	{ "SCREEN_LOAD_START",	LV_EVENT_SCREEN_LOAD_START },
	{ "SCREEN_LOADED",	LV_EVENT_SCREEN_LOADED },
	{ "SCREEN_UNLOADED",	LV_EVENT_SCREEN_UNLOADED },
	{ "SIZE_CHANGED",	LV_EVENT_SIZE_CHANGED },
	{ "STYLE_CHANGED",	LV_EVENT_STYLE_CHANGED },
	{ "LAYOUT_CHANGED",	LV_EVENT_LAYOUT_CHANGED },
	{ "GET_SELF_SIZE",	LV_EVENT_GET_SELF_SIZE },

	{ "PREPROCESS",		LV_EVENT_PREPROCESS },
};

static const struct lua_lv_constant lua_lv_grid_align_t[] = {
	{ "START",		LV_GRID_ALIGN_START },
	{ "CENTER",		LV_GRID_ALIGN_CENTER },
	{ "END",		LV_GRID_ALIGN_END },
	{ "STRETCH",		LV_GRID_ALIGN_STRETCH },
	{ "SPACE_EVENLY",	LV_GRID_ALIGN_SPACE_EVENLY },
	{ "SPACE_AROUND",	LV_GRID_ALIGN_SPACE_AROUND },
	{ "SPACE_BETWEEN",	LV_GRID_ALIGN_SPACE_BETWEEN },
};

static const struct lua_lv_constant lua_lv_misc_constants[] = {
	{ "SIZE_CONTENT",	LV_SIZE_CONTENT },
	{ "RADIUS_CIRCLE",	LV_RADIUS_CIRCLE },
};

static const struct lua_lv_constants lua_lv_constants_table[] = {
	LUA_LV_CONSTANTS("STATE",	lua_lv_state_t),
	LUA_LV_CONSTANTS("PART",	lua_lv_part_t),
	LUA_LV_CONSTANTS("OBJ_FLAG",	lua_lv_obj_flag_t),
	LUA_LV_CONSTANTS("ALIGN",	lua_lv_align_t),
	LUA_LV_CONSTANTS("DIR",		lua_lv_dir_t),
	LUA_LV_CONSTANTS("EVENT",	lua_lv_event_t),
	LUA_LV_CONSTANTS("GRID_ALIGN",	lua_lv_grid_align_t),

	LUA_LV_CONSTANTS("BAR_MODE",	lua_lv_bar_mode_t),
	LUA_LV_CONSTANTS("LABEL_LONG",	lua_lv_label_long_mode_t),
	LUA_LV_CONSTANTS("SLIDER_MODE",	lua_lv_slider_mode_t),
};

static int
lua_lv_constant__newindex(lua_State *L)
{
	return luaL_error(L, "constants are constant");
}

static void
lua_lv_constants_new(lua_State *L)
{
	lua_newtable(L);
	lua_newtable(L); /* metatable */

	lua_pushliteral(L, "__newindex");
	lua_pushcfunction(L, lua_lv_constant__newindex);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__index");
	lua_newtable(L);
}

static void
lua_lv_constants_push(lua_State *L,
    const struct lua_lv_constant *kvs, size_t nkvs)
{
	size_t i;

	for (i = 0; i < nkvs; i++) {
		const struct lua_lv_constant *kv = &kvs[i];
		lua_pushstring(L, kv->k);
		lua_pushinteger(L, kv->v);
		lua_rawset(L, -3);
	}
}

static void
lua_lv_constants_set(lua_State *L)
{
	lua_rawset(L, -3); /* metatable["_index"] = { ... } */
	lua_setmetatable(L, -2);
}

static void
lua_lv_constants(lua_State *L)
{
	size_t t;

	for (t = 0; t < nitems(lua_lv_constants_table); t++) {
		const struct lua_lv_constants *c = &lua_lv_constants_table[t];
		size_t i;

		lua_pushstring(L, c->name);

		lua_lv_constants_new(L);
		lua_lv_constants_push(L, c->kvs, c->nkvs);
		lua_lv_constants_set(L);

		lua_rawset(L, -3); /* lv[c->name] = { blah } */
	}

	lua_pushliteral(L, "GRID");
	lua_lv_constants_new(L);

	lua_pushliteral(L, "CONTENT");
	lua_pushinteger(L, LV_GRID_CONTENT);
	lua_rawset(L, -3);

	lua_pushliteral(L, "FR");
	lua_pushcfunction(L, lua_lv_grid_fr);
	lua_rawset(L, -3);

	lua_lv_constants_set(L);
	lua_rawset(L, -3); /* lv["GRID"] = { blah } */

	/* grumble grumble */
	lua_lv_constants_push(L,
	    lua_lv_misc_constants, nitems(lua_lv_misc_constants));
}

static const luaL_Reg lua_lv[] = {
	{ "obj",		lua_lv_obj_create },
	{ "object",		lua_lv_obj_create },
	{ "bar",		lua_lv_bar_create },
	{ "btn",		lua_lv_btn_create },
	{ "button",		lua_lv_btn_create },
	{ "label",		lua_lv_label_create },
	{ "slider",		lua_lv_slider_create },
	{ "switch",		lua_lv_switch_create },

	{ "scr_act",		lua_lv_scr_act },

	{ "pct",		lua_lv_pct },
	{ "color",		lua_lv_color },

	{ NULL,			NULL }
};

int
luaopen_lv(lua_State *L)
{
	size_t i;

	lua_newtable(L); /* new lua_lv_obj_type table */
	lua_newtable(L); /* new metatable */
	lua_pushliteral(L, "__mode");
	lua_pushliteral(L, "v");
	lua_rawset(L, -3); /* metatable["__mode"] = "v" */
	lua_setmetatable(L, -2);
	lua_rawsetp(L, LUA_REGISTRYINDEX, &lua_lv_obj_type);

	for (i = 0; i < nitems(lua_lv_obj_classes); i++) {
		const struct lua_lv_obj_class *c = &lua_lv_obj_classes[i];

		lua_newtable(L);
		luaL_setfuncs(L, c->methods, 0);
		lua_rawsetp(L, LUA_REGISTRYINDEX, c->obj_class);
	}

	if (luaL_newmetatable(L, lua_lv_obj_type)) {
		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, lua_lv_obj__gc);
		lua_settable(L, -3);

		lua_pushliteral(L, "__index");
		lua_pushcfunction(L, lua_lv_obj__index);
		lua_settable(L, -3);

		lua_pushliteral(L, "__newindex");
		lua_pushcfunction(L, lua_lv_obj__newindex);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "nope");
		lua_settable(L, -3);
	}

	lua_lv_palette_init(L);
	lua_lv_styles_init(L);

	luaL_newlib(L, lua_lv);
	lua_lv_constants(L);

	return (1);
}
