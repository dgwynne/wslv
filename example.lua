local X11Fonts = '/usr/X11R6/lib/X11/fonts/TTF/'
local dejavu = lv.ttf(X11Fonts .. 'DejaVuSans.ttf', 24)

local function tele(topic, payload)
	if wslv.in_cmnd() then
		return
	end

	wslv.tele(topic, payload)
end

local light = { }
local panel_pad = 32

light.panel = lv.obj(lv.scr_act())
light.panel:size(480, 480)
light.panel:set_style('radius', 8)
light.panel:set_style('pad_left', panel_pad)
light.panel:set_style('pad_right', panel_pad)
light.panel:set_style('pad_top', panel_pad)
light.panel:set_style('pad_bottom', panel_pad)
light.panel:center()
light.panel:flag(lv.OBJ_FLAG.SCROLLABLE, false)

light.row = lv.obj(light.panel)
light.row:remove_style_all()

light.label = lv.label(light.row)
light.label:text("Light")
light.label:set_style('text_font', dejavu)
light.label:align(lv.ALIGN.LEFT_MID)

light.power = lv.switch(light.row)
light.power:align(lv.ALIGN.RIGHT_MID)

light.row:size(lv.pct(100), lv.SIZE_CONTENT)
light.row:align(lv.ALIGN.TOP_MID)

light.dimmer = lv.slider(light.panel)
light.dimmer:align(lv.ALIGN.CENTER)
light.dimmer:width(lv.pct(90))
light.dimmer:value(light.dimmer:max())
light.dimmer:set_style('anim_time', 120)

light.power:add_event_cb(lv.EVENT.VALUE_CHANGED, function (obj)
	tele('light/power', obj:state(lv.STATE.CHECKED))
end)

light.dimmer:add_event_cb(lv.EVENT.RELEASED, function (obj)
	tele('light/dimmer', obj:value())
end)

function cmnd(topic, payload)
	if topic == 'light/power' then
		payload = payload:lower()
		ostate = light.power:state(lv.STATE.CHECKED)

		if payload == 'off' or payload == '0' then
			nstate = false
		elseif payload == 'on' or payload == '1' then
			nstate = true
		elseif payload == 'toggle'or payload == '2'  then
			nstate = not ostate
		else
			return
		end

		if ostate ~= nstate then
			light.power:state(lv.STATE.CHECKED, nstate)
			light.power:event_send(lv.EVENT.VALUE_CHANGED)
		end
	elseif topic == 'light/dimmer' then
		if not light.dimmer:is_dragged() then
			light.dimmer:value(payload, true)
		end
	end
end
