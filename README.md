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

Also, I'm an idiot and make up work for myself.

## To Do

- integrate MQTT again
- integrate [LVGL + Berry](https://github.com/lvgl/lv_binding_berry)

The goal would be to allow reuse of Berry code between Tasmota and
wslv for implementing a user interface that communicates using MQTT
to control stuff.
