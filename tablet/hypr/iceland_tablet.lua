-- OnePlus Pad 4 tablet settings for Omarchy (omarchy-pad/tablet). Loaded from ~/.config/hypr/hyprland.lua
-- after your own overrides; install-tablet-setup.sh writes the panel name and scale below.

local panel = "@PANEL@"
local scale = @SCALE@

-- The 13.2" panel at 3392x2400, up to 144 Hz. The kernel drops a static screen to 30 Hz on its own.
-- Scale 2 gives a 1696x1200 desktop that is easy to touch; 1.6 gives 2120x1500.
hl.monitor({ output = panel, mode = "preferred", position = "0x0", scale = scale })

-- Touch and pen follow the panel (iceland-tablet-session turns all three together).
hl.config({
  input = {
    touchdevice = { output = panel },
    tablet = { output = panel },
  },
})

-- Auto-rotate, on-screen keyboard and gestures.
o.launch_on_start("iceland-tablet-session")

-- Keys Omarchy 4.5 leaves free (Super+Ctrl+R / K / Alt+R are its reminders and Herdr bindings).
o.bind("SUPER + CTRL + SHIFT + R", "Lock or unlock screen rotation", "iceland-rotate-lock toggle")
o.bind("SUPER + CTRL + SHIFT + K", "On-screen keyboard", "iceland-osk toggle")

-- floatkey slides and fades itself in and out, over the whole screen; a compositor animation on top would fade the
-- entire overlay.
hl.layer_rule({ match = { namespace = "^floatkey$" }, no_anim = true, animation = "none" })
