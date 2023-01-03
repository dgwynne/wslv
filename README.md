# LVGL x wscons(4) on OpenBSD

This implements LVGL drivers for the OpenBSD wsdisplay(4) frame
buffer, and wsmouse(4) as a pointer. It's Good Enough(tm) to support
the LVGL demo applications.

This code is complicated by the addition of MQTT support, allowing
encoder and keypad events to be sent to the program as if they were
from a hardware driver. This doesn't work as well in practice as
I'd hoped, as the LVGL encoder/keypad processing is too low level
to meaningfully handle MQTT input from devices that already implement
their own interpretation of short and long presses, etc. It would
probably make more sense to have MQTT inject LVGL events directly
rather than pretend to be hardware peripherals.

## But why?

I have a long term plan to get something like a Sonoff NSPanel Pro
or a Tuya T6E based board running OpenBSD instead of the crippled
and likely to be an abandonware version of Android that they ship
with. wslv would be the basis for the user interface they provide.

While LVGL and wscons provide the user interface, MQTT will allow
it to interact with other hardware or visa versa. eg, pressing a
button widget on an LVGL interface could send an MQTT message to
tell another device or Home Assistatn to do something, or a ZigBee
remote (like the IKEA 5 button remote) could be used as an encoder
to nagivate an LVGL based interface.

Also, I'm an idiot and make up work for myself.

## To Do

- map wskbd key codes before sending them to LVGL
- factor out common wsmouse and wskbd code
- make MQTT optional
- let MQTT control/monitor wsdisplay brightness and blanking
- audio?
