# LVGL x wscons(4) x Lua on OpenBSD

This integrates LVGL, MQTT, Lua, and OpenBSD to provide a platform
for building touch screen interfaces.

This implements LVGL drivers for the OpenBSD wsdisplay(4) or drm(4)
frame buffer devices, and wsmouse(4) devices as a pointer. The
program connects to an MQTT server, and provides integration between
MQTT and LVGL via Lua.

## But why?

I have a long term plan to get something like a Sonoff NSPanel Pro
or a Tuya T6E based board running OpenBSD instead of the crippled
and likely to be an abandonware version of Android that they ship
with. wslv would be the basis for the user interface they provide.

Alternatively, a cheap mini PC and a touch screen mounted on the wall
would also work.

While LVGL and wscons provide the user interface, MQTT allows it
to interact with other hardware or visa versa. Lua provides a
relatively easy scripting language to build an interface out of
LVGL.

While it is possible to run a desktop environment and browser in
kiosk mode to provide a user interface, integrating with the OpenBSD
wscons devices directly allows for precise control of the supported
peripherals. It should also be portable to resource constrained
systems that are not able to support a full browser environment.

Also, I'm an idiot and make up work for myself.

## Todo

- Learn LVGL

## How?

- Install OpenBSD on supported hardware

Anything that provides a framebuffer and a mouse/touchscreen should
be Good Enough(tm).

- Install Lua 5.3, libspng, and git via `pkg_add`

- Get the source code

`git clone` and an update of the submodules is probably the easiest way

- Run `make obj` and then `make`

- Run `./obj/wslv` on the console.

wslv needs permission to open the display and input devices. By
default it tries to open /dev/ttyC0 or /dev/dri/card0 for the
display, and /dev/wsmouse0 for a pointer. When you log in on the
console the system changes the ownership of these devices so you
can use them as a non-privileged user.

## Usage

wslv requires an MQTT server and a Lua script. These can be
specified with the `-h` and `-l` command line options respectively.

The full set of command line options are:

- `-4`

Force wslv to use IPv4 for the MQTT server connection.

- `-6`

Force wslv to use IPv6 for the MQTT server connection.

- `-d devname`

Use `devname` as the name of the device in MQTT topics. By default
wslv uses the short host name of the local system.

- `-h mqtthost`

The host name for the MQTT server.

- `-i idletime`

The number of seconds of idle time before the screen will blank.
By default the idle time is 2 minutes.

- `-l script.lua`

The Lua script used to control LVGL on the display.

- `-M wsmouse`

A `wsmouse(4)` device to use as a LVGL pointer. If no pointers are
specified wslv will use /dev/wsmouse0 by default. Multiple pointers
may be specified.

- `-p mqttport`

The MQTT server port to connect to. wslv will default to port 1883.

- `-W wsdisplay`

The `wsdisplay(4)` device to use as the LVGL display. wslv will
default to /dev/ttyC0. If the display appears to support DRM and
Kernel Mode Setting, wslv will close the `wsdisplay(4)` device
and try opening `/dev/dri/card0` instead.

- `-r`

Display a Reload button on the screen that triggers a reload of the
Lua script used to control the interface.

## MQTT

The MQTT messages that wslv sends and receives are inspired by
Tasmota and OpenHASP.

wslv uses `tele/DEVNAME/LWT` as the topic for "Last Will and
Testament" messages. Once connected it publishes `Online`. If it's
disconnected the MQTT server should publish `Offline`.

The state of the screen is published as part of `tele/DEVNAME/STATUS`.
The screen can be manually controlled by sending `ON` (or `1`),
`OFF` (or `0`), or `TOGGLE` (or `2`) to `cmnd/DEVNAME/screen`.

Lua scripts can use `wslv.tele(topic, payload)` to publish messages.
The topic argument to the `wslv.tele()` method is added to the end
of `tele/DEVNAME/` before being sent. ie, `wslv.tele('foo', 'bar')`
will send `bar` to `tele/DEVNAME/foo`.

MQTT messages sent to topics starting with `cmnd/DEVNAME/` will be
given to the Lua script to handle if it provides a
`cmnd(topic, payload)` function for wslv to call. The
`cmnd/DEVNAME/` prefix is removed from the topic before the Lua
`cmnd()` handler is called.

Lua scripts can also publish or subscribe to any MQTT topic they
want with `wslv.publish()` and `wslv.subscribe()` respectively.
MQTT messages can be received by an `mqtt_message()` function in
the Lua script.
