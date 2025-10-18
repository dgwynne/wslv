X11DIR=/usr/X11R6
LDADD+=-L${X11DIR}/lib -lfreetype

LVGL_CONF_PATH=${.CURDIR}/lv_conf.h
LVGL_DIR=${.CURDIR}/lvgl
LVGL_SRC_DIR=${LVGL_DIR}/src

CFLAGS+=-DLV_CONF_PATH='"${LVGL_CONF_PATH}"'

LVCFLAGS=-I${X11DIR}/include/freetype2
LVCOMPILE:=${COMPILE.c} ${LVCFLAGS}

LVGL_ASRCS !!= find ${LVGL_SRC_DIR} -name "*.S"
LVGL_CSRCS !!= find ${LVGL_SRC_DIR} -name "*.c"

LVGL_SRCS = ${LVGL_CSRCS}

.for S in ${LVGL_SRCS}
O=${S:T:.c=.o}
${O}: ${S}
	${LVCOMPILE} -o ${.TARGET} ${.IMPSRC}
.endfor

OBJS+=${LVGL_SRCS:T:.c=.o}
#${LVGL_EXTRA_SRCS:T:.c=.o}

# lua

LUAPKG=lua53
LUA_CFLAGS!=pkg-config --cflags ${LUAPKG}
LUA_LDFLAGS!=pkg-config --libs ${LUAPKG}

LUA_LV_SRCS=lua_lv.c

.for S in ${LUA_LV_SRCS}
${S:.c=.o}: ${S}
	${LVCOMPILE} ${LUA_CFLAGS} -I${LVGL_SRC_DIR} -o ${.TARGET} ${.IMPSRC}
.endfor

OBJS+=${LUA_LV_SRCS:.c=.o}

# luavgl

LUAVGL_DIR=${.CURDIR}/luavgl
#LUAVGL_SRCDIR=${LUAVGL_DIR}/src

#LUAVGL_SRCS = luavgl.c

#.for S in ${LUAVGL_SRCS}
#${S:T:.c=.o}: ${LUAVGL_SRCDIR}/${S}
#	${LVCOMPILE} ${LUA_CFLAGS} -I${LVGL_SRC_DIR} -I${LUAVGL_SRCDIR} -o ${.TARGET} ${.IMPSRC}
#.endfor

#OBJS+=${LUAVGL_SRCS:T:.c=.o}

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

.if 0
SPNGPKG=spng
SPNG_CFLAGS!=pkg-config --cflags ${SPNGPKG}
SPNG_LDFLAGS!=pkg-config --libs ${SPNGPKG}

SPNG_SRCS=lv_spng.c

${SPNG_SRCS:.c=.o}: ${SPNG_SRCS}
	${LVCOMPILE} ${SPNG_CFLAGS} -I${LVGL_SRC_DIR} -o ${.TARGET} ${.IMPSRC}

OBJS+=${SPNG_SRCS:.c=.o}
LDADD+=${SPNG_LDFLAGS}
.endif

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

CFLAGS+=${LUA_CFLAGS}
LDADD+=-levent ${LUA_LDFLAGS}
DPADD+=${LIBEVENT}

DEBUG=-g

.include <bsd.prog.mk>
