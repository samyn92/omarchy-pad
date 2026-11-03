-- Omarchy Pad's tablet settings for Omarchy's Hyprland. Written by omarchy-pad-tablet-setup from
-- /usr/share/omarchy-pad/tablet/omarchy_pad_tablet.lua and loaded from ~/.config/hypr/hyprland.lua after your own
-- overrides. The setup writes it again when the package brings a new version, so change the panel and scale in
-- ~/.config/omarchy-pad/tablet.conf (then run omarchy-pad-tablet-setup) rather than here.

local panel = "@PANEL@"
local scale = @SCALE@

-- The built-in screen at its preferred mode. The device layer sets the scale that suits it (the OnePlus Pad 4's
-- 3392x2400 panel: 2, a 1696x1200 desktop that is easy to touch). The kernel drops a static screen's refresh rate
-- on its own where the panel can.
hl.monitor({ output = panel, mode = "preferred", position = "0x0", scale = scale })

-- Touch and pen follow the panel (omarchy-pad-tablet-session turns all three together).
hl.config({
  input = {
    touchdevice = { output = panel },
    tablet = { output = panel },
  },
})

-- Auto-rotate, on-screen keyboard and gestures.
o.launch_on_start("omarchy-pad-tablet-session")

-- Keys Omarchy 4.5 leaves free (Super+Ctrl+R / K / Alt+R are its reminders and Herdr bindings).
o.bind("SUPER + CTRL + SHIFT + R", "Lock or unlock screen rotation", "omarchy-pad-rotate-lock toggle")
o.bind("SUPER + CTRL + SHIFT + K", "On-screen keyboard", "omarchy-pad-osk toggle")

-- The power button as on a phone or tablet, in place of Omarchy's power menu (omarchy-pad-power): a short press
-- sleeps or wakes, holding it opens the power screen, and with Volume Down it takes a screenshot. Also on the lock
-- screen. omarchy-pad-tablet-session tells the system that the session handles the key (a handle-power-key
-- inhibitor), so a device layer that also watches it leaves it alone.
hl.unbind("XF86PowerOff")
o.bind_hold("XF86PowerOff", "Power button", "omarchy-pad-power press", "Power button released",
  "omarchy-pad-power release", { locked = true })
o.rebind("XF86AudioLowerVolume", "Volume down, or a screenshot with the power button", "omarchy-pad-power volume-down",
  { locked = true, repeating = true })

-- floatkey slides and fades itself in and out, over the whole screen; a compositor animation on top would fade the
-- entire overlay.
hl.layer_rule({ match = { namespace = "^floatkey$" }, no_anim = true, animation = "none" })
hl.layer_rule({ match = { namespace = "^stage$" }, no_anim = true, animation = "none" })
-- The pull-down drawers (shell/drawers) follow the finger themselves, on a surface that stays mapped.
hl.layer_rule({ match = { namespace = "^omarchy-pad-drawers$" }, no_anim = true, animation = "none" })
