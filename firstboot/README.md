# firstboot

Setting up a new Omarchy Pad by touch. On the first boot of an Omarchy image, Omarchy asks for the owner's keyboard
layout, user name, password, hostname and timezone on the screen before the login screen appears. That form is text,
made to be answered with a keyboard. With `omarchy-pad-firstboot` installed, a touch keyboard appears under it at the
bottom of the screen for as long as the form runs, and goes away when it is done.

The form itself is Omarchy's, unchanged. The keyboard is [buffyboard](https://gitlab.postmarketos.org/postmarketOS/buffybox),
postmarketOS's keyboard for the Linux console: it draws on the screen, sends what you tap as key presses, and moves the
console's text up so the form stays above it. It has three layers (letters, capitals, numbers and symbols) and the
arrow keys, Tab and Enter the form uses. A keyboard cover or a USB keyboard works at the same time.

The keyboard only starts while Omarchy's owner setup is still pending
(`/var/lib/omarchy/provisioning/pending`), so on a tablet that is already set up the package does nothing.

## Packages

| Folder | Package | What |
|---|---|---|
| `buffybox/` | `buffybox` 3.6.0 | buffyboard and unl0kr, built from the upstream release (Arch Linux ARM does not package them) |
| `pkgbuild/` | `omarchy-pad-firstboot` | `omarchy-pad-owner-keyboard.service` and its script, the drop-in that starts and stops it with `omarchy-provision-owner.service`, the form's `setfont`, the theme (`/etc/buffyboard.conf.d/50-omarchy-pad.conf`) and `uinput` at boot |

Build each with `makepkg` in its folder. `buffybox` needs `meson` and `scdoc` to build.

## Notes

- buffyboard's keys are a US layout sent as key codes. If the owner picks another keyboard layout in the form, the
  letters after that step follow that layout.
- The keyboard runs as root for the minutes the form runs, because the form itself does (it creates the user).
- The form changes the console font to suit the screen, and a font change would give the console its full height back,
  over the keyboard. While the form runs, its `setfont` is the package's own (`/usr/lib/omarchy-pad/owner-path/setfont`,
  on the form's PATH only), which has the keyboard make room again after each change. Anything else that resizes the
  console, such as the display driver taking over, is caught by `/usr/lib/omarchy-pad/owner-keyboard`.
- The keyboard starts after Omarchy's deferred hardware setup, which comes before the form and takes minutes on a
  first boot, so the boot splash stays up until then. The form waits for the keyboard to be ready before it starts,
  so the first screen already leaves room for it. On a
  small screen (the 800x600 test VM) the summary at the end scrolls the logo off the top; it still reads.
