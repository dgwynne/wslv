X11DIR=/usr/X11R6
LDADD+=-L${X11DIR}/lib

LVGL_CONF_PATH=${.CURDIR}/lv_conf.h
LVGL_DIR=${.CURDIR}/lvgl
LVGL_SRC_DIR=${LVGL_DIR}/src

CFLAGS+=-DLV_CONF_PATH=${LVGL_CONF_PATH}

LVCOMPILE=${COMPILE.c} -I${.CURDIR}

LVGL_SRCS = core/lv_disp.c core/lv_group.c core/lv_indev.c \
	core/lv_indev_scroll.c core/lv_obj.c core/lv_obj_class.c \
	core/lv_obj_draw.c core/lv_obj_pos.c core/lv_obj_scroll.c \
	core/lv_obj_style.c core/lv_obj_style_gen.c core/lv_obj_tree.c \
	core/lv_event.c core/lv_refr.c core/lv_theme.c

LVGL_SRCS += draw/lv_draw_arc.c draw/lv_draw.c draw/lv_draw_img.c \
	draw/lv_draw_label.c draw/lv_draw_line.c draw/lv_draw_mask.c \
	draw/lv_draw_rect.c draw/lv_draw_transform.c draw/lv_draw_layer.c \
	draw/lv_draw_triangle.c draw/lv_img_buf.c draw/lv_img_cache.c \
	draw/lv_img_decoder.c

LVGL_SRCS += draw/sw/lv_draw_sw.c draw/sw/lv_draw_sw_arc.c \
	draw/sw/lv_draw_sw_blend.c draw/sw/lv_draw_sw_dither.c \
	draw/sw/lv_draw_sw_gradient.c draw/sw/lv_draw_sw_img.c \
	draw/sw/lv_draw_sw_letter.c draw/sw/lv_draw_sw_line.c \
	draw/sw/lv_draw_sw_polygon.c draw/sw/lv_draw_sw_rect.c \
	draw/sw/lv_draw_sw_transform.c draw/sw/lv_draw_sw_layer.c

LVGL_SRCS += font/lv_font.c font/lv_font_fmt_txt.c font/lv_font_loader.c

LVGL_SRCS += font/lv_font_dejavu_16_persian_hebrew.c
LVGL_SRCS += font/lv_font_montserrat_8.c
LVGL_SRCS += font/lv_font_montserrat_10.c
LVGL_SRCS += font/lv_font_montserrat_12.c
LVGL_SRCS += font/lv_font_montserrat_12_subpx.c
LVGL_SRCS += font/lv_font_montserrat_14.c
LVGL_SRCS += font/lv_font_montserrat_16.c
LVGL_SRCS += font/lv_font_montserrat_18.c
LVGL_SRCS += font/lv_font_montserrat_20.c
LVGL_SRCS += font/lv_font_montserrat_22.c
LVGL_SRCS += font/lv_font_montserrat_24.c
LVGL_SRCS += font/lv_font_montserrat_26.c
LVGL_SRCS += font/lv_font_montserrat_28.c
LVGL_SRCS += font/lv_font_montserrat_28_compressed.c
LVGL_SRCS += font/lv_font_montserrat_30.c
LVGL_SRCS += font/lv_font_montserrat_32.c
LVGL_SRCS += font/lv_font_montserrat_34.c
LVGL_SRCS += font/lv_font_montserrat_36.c
LVGL_SRCS += font/lv_font_montserrat_38.c
LVGL_SRCS += font/lv_font_montserrat_40.c
LVGL_SRCS += font/lv_font_montserrat_42.c
LVGL_SRCS += font/lv_font_montserrat_44.c
LVGL_SRCS += font/lv_font_montserrat_46.c
LVGL_SRCS += font/lv_font_montserrat_48.c
LVGL_SRCS += font/lv_font_simsun_16_cjk.c
LVGL_SRCS += font/lv_font_unscii_8.c
LVGL_SRCS += font/lv_font_unscii_16.c

LVGL_SRCS += hal/lv_hal_disp.c hal/lv_hal_indev.c hal/lv_hal_tick.c

LVGL_SRCS += misc/lv_anim.c misc/lv_anim_timeline.c misc/lv_area.c \
	misc/lv_async.c misc/lv_bidi.c misc/lv_color.c misc/lv_fs.c \
	misc/lv_gc.c misc/lv_ll.c misc/lv_log.c misc/lv_lru.c \
	misc/lv_math.c misc/lv_mem.c misc/lv_printf.c \
	misc/lv_style.c misc/lv_style_gen.c misc/lv_timer.c \
	misc/lv_tlsf.c misc/lv_txt.c misc/lv_txt_ap.c misc/lv_utils.c

LVGL_SRCS += widgets/lv_arc.c widgets/lv_bar.c widgets/lv_btn.c \
	widgets/lv_btnmatrix.c widgets/lv_canvas.c widgets/lv_checkbox.c \
	widgets/lv_dropdown.c widgets/lv_img.c widgets/lv_label.c \
	widgets/lv_line.c widgets/lv_roller.c widgets/lv_slider.c \
	widgets/lv_switch.c widgets/lv_table.c widgets/lv_textarea.c

.for S in ${LVGL_SRCS}
${S:T:.c=.o}: ${LVGL_SRC_DIR}/${S}
	${LVCOMPILE} -I${.IMPSRC:H} -o ${.TARGET} ${.IMPSRC}
.endfor

LVGL_EXTRA_SRCS = lv_extra.c

LVGL_EXTRA_SRCS += libs/fsdrv/lv_fs_posix.c

LVGL_EXTRA_SRCS += layouts/flex/lv_flex.c
LVGL_EXTRA_SRCS += layouts/grid/lv_grid.c

LVGL_EXTRA_SRCS += themes/default/lv_theme_default.c

LVGL_EXTRA_SRCS += widgets/animimg/lv_animimg.c
LVGL_EXTRA_SRCS += widgets/calendar/lv_calendar.c
LVGL_EXTRA_SRCS += widgets/calendar/lv_calendar_header_arrow.c
LVGL_EXTRA_SRCS += widgets/calendar/lv_calendar_header_dropdown.c
LVGL_EXTRA_SRCS += widgets/chart/lv_chart.c
LVGL_EXTRA_SRCS += widgets/colorwheel/lv_colorwheel.c
LVGL_EXTRA_SRCS += widgets/imgbtn/lv_imgbtn.c
LVGL_EXTRA_SRCS += widgets/keyboard/lv_keyboard.c
LVGL_EXTRA_SRCS += widgets/led/lv_led.c
LVGL_EXTRA_SRCS += widgets/list/lv_list.c
LVGL_EXTRA_SRCS += widgets/menu/lv_menu.c
LVGL_EXTRA_SRCS += widgets/meter/lv_meter.c
LVGL_EXTRA_SRCS += widgets/msgbox/lv_msgbox.c
LVGL_EXTRA_SRCS += widgets/span/lv_span.c
LVGL_EXTRA_SRCS += widgets/spinbox/lv_spinbox.c
LVGL_EXTRA_SRCS += widgets/spinner/lv_spinner.c
LVGL_EXTRA_SRCS += widgets/tabview/lv_tabview.c
LVGL_EXTRA_SRCS += widgets/tileview/lv_tileview.c
LVGL_EXTRA_SRCS += widgets/win/lv_win.c

.for S in ${LVGL_EXTRA_SRCS}
${S:T:.c=.o}: ${LVGL_SRC_DIR}/extra/${S}
	${LVCOMPILE} -o ${.TARGET} ${.IMPSRC}
.endfor

OBJS+=${LVGL_SRCS:T:.c=.o} ${LVGL_EXTRA_SRCS:T:.c=.o}

# freetype

LVGL_FT_SRCS = extra/libs/freetype/lv_freetype.c

OBJS+=${LVGL_FT_SRCS:T:.c=.o}

.for S in ${LVGL_FT_SRCS}
${S:T:.c=.o}: ${LVGL_SRC_DIR}/${S}
	${LVCOMPILE} -I${X11DIR}/include/freetype2 -o ${.TARGET} ${.IMPSRC}
.endfor

LDADD+=-lfreetype

.if 1
LVGL_DEMO_SRCS !!= find ${LVGL_DIR}/demos -name "*.c"

.for S in ${LVGL_DEMO_SRCS}
${S:T:.c=.o}: ${S}
	${LVCOMPILE} -o ${.TARGET} ${.IMPSRC}
.endfor

OBJS+=${LVGL_DEMO_SRCS:T:.c=.o}

.endif

# lua

LUAPKG=lua54
LUA_CFLAGS!=pkg-config --cflags ${LUAPKG}
LUA_LDFLAGS!=pkg-config --libs ${LUAPKG}

# luavgl

LUAVGL_DIR=${.CURDIR}/luavgl
LUAVGL_SRCDIR=${LUAVGL_DIR}/src

LUAVGL_SRCS = luavgl.c

.for S in ${LUAVGL_SRCS}
${S:T:.c=.o}: ${LUAVGL_SRCDIR}/${S}
	${LVCOMPILE} ${LUA_CFLAGS} -I${LVGL_SRC_DIR} -I${LUAVGL_SRCDIR} -o ${.TARGET} ${.IMPSRC}
.endfor

OBJS+=${LUAVGL_SRCS:T:.c=.o}

# wslv and luavgl glue

WSLUAV_SRCS=wsluav.c

${WSLUAV_SRCS:.c=.o}: ${WSLUAV_SRCS}
	${LVCOMPILE} ${LUA_CFLAGS} -I${LVGL_SRC_DIR} -I${LUAVGL_SRCDIR} -o ${.TARGET} ${.IMPSRC}

OBJS+=${WSLUAV_SRCS:.c=.o}

# steal the mouse cursor

CURSOR_SRCS=${LUAVGL_DIR}/simulator/mouse_cursor_icon.c

.for S in ${CURSOR_SRCS}
${S:T:.c=.o}: ${S}
	${LVCOMPILE} -I${LVGL_SRC_DIR} -o ${.TARGET} ${.IMPSRC}
.endfor

OBJS+=${CURSOR_SRCS:T:.c=.o}

# drm

DRM_SRCS=drm.c
DRM_CFLAGS=-I${X11DIR}/include -I${X11DIR}/include/libdrm

OBJS+=${DRM_SRCS:T:.c=.o}

.for S in ${DRM_SRCS}
${S:T:.c=.o}: ${S}
	${COMPILE.c} ${DRM_CFLAGS} -o ${.TARGET} ${.IMPSRC}
.endfor

LDADD+=-ldrm

# spng

SPNGPKG=spng
SPNG_CFLAGS!=pkg-config --cflags ${SPNGPKG}
SPNG_LDFLAGS!=pkg-config --libs ${SPNGPKG}

SPNG_SRCS=lv_spng.c

${SPNG_SRCS:.c=.o}: ${SPNG_SRCS}
	${LVCOMPILE} ${SPNG_CFLAGS} -I${LVGL_SRC_DIR} -o ${.TARGET} ${.IMPSRC}

OBJS+=${SPNG_SRCS:.c=.o}
LDADD+=${SPNG_LDFLAGS}

# amqtt

AMQTT_SRCS=amqtt/amqtt.c

.for S in ${AMQTT_SRCS}
${S:T:.c=.o}: ${S}
	${COMPILE.c} -o ${.TARGET} ${.IMPSRC}
.endfor

OBJS+=${AMQTT_SRCS:T:.c=.o}

CFLAGS+=-I${.CURDIR}/amqtt

# actual program

PROG=wslv
SRCS=wslv.c
MAN=

LDADD+=-levent ${LUA_LDFLAGS}
DPADD+=${LIBEVENT}

DEBUG=-g

.include <bsd.prog.mk>
