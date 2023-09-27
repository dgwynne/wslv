local max = 'max'
local min = 'min'

local X11Fonts = '/usr/X11R6/lib/X11/fonts/TTF/'

local function tele(topic, payload)
	if wslv.in_cmnd() then
		return
	end

	wslv.tele(topic, tostring(payload))
end

local light = { }

light.panel = lvgl.Object()
light.panel:set {
	w = 480,
	h = 480,
	radius = 8,
	pad_all = 32,
	align = lvgl.ALIGN.CENTER,
}
light.panel:clear_flag(lvgl.FLAG.SROLLABLE)

light.row = light.panel:Object()
light.row:remove_style_all()

light.label = light.row:Label {
	text = "Light",
	text_font = lvgl.Font(X11Fonts .. 'DejaVuSans.ttf', 24),
	align = lvgl.ALIGN.LEFT_MID,
}
light.power = light.row:Switch {
	align = lvgl.ALIGN.RIGHT_MID,
}

light.row:set {
	height = lvgl.SIZE_CONTENT,
	width = lvgl.PCT(100),
}
light.row:set { align = lvgl.ALIGN.TOP_MID }

light.dimmer = light.panel:Slider {
	align = lvgl.ALIGN.CENTER,
	w = lvgl.PCT(90),
	value = max,
}

light.power:onevent(lvgl.EVENT.VALUE_CHANGED, function (obj)
	tele('light/power', obj:has_state(lvgl.STATE.CHECKED))
end)

light.dimmer:onevent(lvgl.EVENT.RELEASED, function (obj)
	tele('light/dimmer', obj:get_value())
end)

function cmnd(topic, payload)
	if topic == 'light/power' then
		payload = payload:lower()
		ostate = light.power:has_state(lvgl.STATE.CHECKED)

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
			if nstate then
				light.power:add_state(lvgl.STATE.CHECKED)
			else
				light.power:clear_state(lvgl.STATE.CHECKED)
			end
			light.power:valueChanged()
		end
	elseif topic == 'light/dimmer' then
		if not light.dimmer:is_dragged() then
			light.dimmer:value(payload, true)
		end
	end
end
