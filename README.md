# LVGL x wscons(4) on OpenBSD

This implements LVGL drivers for the OpenBSD wsdisplay(4) or drm(4)
frame buffer devices, and wsmouse(4) devices as a pointer. It's
Good Enough(tm) to support the LVGL demo applications.

## But why?

I have a long term plan to get something like a Sonoff NSPanel Pro
or a Tuya T6E based board running OpenBSD instead of the crippled
and likely to be an abandonware version of Android that they ship
with. wslv would be the basis for the user interface they provide.

Alternative, a cheap mini PC and a touch screen mounted on the wall
would also work.

While LVGL and wscons provide the user interface, MQTT will allow
it to interact with other hardware or visa versa.

While it is possible to run a desktop environment and browser in
kios mode to provide a user interface, integrating with the devices
directly allows for precise (and remote) control of functionality
like suspending and resuming the display. It should also be portable
to resource constrained systems that would not be able to support
a full browser environment.

Also, I'm an idiot and make up work for myself.

## How?

- Install OpenBSD on supported hardware

Anything that provides a framebuffer and a mouse/touchscreen should
be Good Enough(tm).

- Get the source code

`git clone` and an update of the submodules is probably the easiest way

- Run `make obj` and then `make`

- Run `./obj/wslv` on the console.

`wslv` needs permission to open the display and input devices. By
default it tries to open /dev/ttyC0 or /dev/dri/card0 for the
display, and /dev/wsmouse0 for a pointer. When you log in on the
console the system changes the ownership of these devices so you
can use them as a non-privileged user.

## To Do

- integrate MQTT again

## Ideas

- integrate [LVGL + Berry](https://github.com/lvgl/lv_binding_berry)

The goal would be to allow reuse of Berry code between Tasmota and
wslv for implementing a user interface that communicates using MQTT
to control stuff.
