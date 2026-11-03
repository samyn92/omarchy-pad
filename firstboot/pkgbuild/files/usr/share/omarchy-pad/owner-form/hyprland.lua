-- Omarchy Pad: the Hyprland of the first-boot touch form (/usr/lib/omarchy-pad/owner-form-compositor), as minimal as
-- the one SDDM's greeter runs (/usr/share/sddm/hyprland.lua): the form, full screen, and nothing else. The form draws
-- its own keyboard and asks for the keyboard layout itself, so none is set here.

hl.config({
  misc = {
    disable_hyprland_logo = true,
    disable_splash_rendering = true,
    background_color = "rgb(1a1b26)",
    force_default_wallpaper = 0,
  },

  animations = {
    enabled = false,
  },

  -- A first boot shows no "Hyprland updated" window over the form.
  ecosystem = {
    no_update_news = true,
  },
})

hl.on("hyprland.start", function()
  hl.exec_cmd("/usr/lib/omarchy-pad/owner-form-session")
end)
