# firstboot

Setting up a new Omarchy Pad by touch. On the first boot of an Omarchy image, Omarchy asks for the owner's keyboard
layout, user name, password, hostname and timezone before the login screen appears. That form is text, made to be
answered with a keyboard. With `omarchy-pad-firstboot` installed, the same questions come in a full-screen window
instead, with floatkey's keys (the lock screen's) docked under the field being typed in, and a keyboard cover or a USB
keyboard works too.

The questions, their rules and what happens with the answers stay Omarchy's: the window answers Omarchy's own form.
It only runs while Omarchy's owner setup is still pending (`/var/lib/omarchy/provisioning/pending`), so on a tablet
that is already set up the package does nothing.

## How it works

Omarchy's owner setup (`omarchy-provision-owner`, on tty1) asks every question through `gum` (`choose`, `input`,
`confirm`, `table`), with its rules in `setup-form.sh`.

- **A stand-in `gum`** (`/usr/lib/omarchy-pad/owner-path/gum`, first on the setup's PATH) sends each question to the
  window, as one JSON line over `/run/omarchy-pad/owner-form.sock` (with `omarchy-pad-owner-form ask`), and prints the
  answer with the status Omarchy expects: 0, 1 for back (Esc), 130 for the reboot offer (Ctrl+C). `gum style` and
  anything else go to the real gum, and so does everything when the window is not there, so the text form works as it
  always has.
- **The window** (`omarchy-pad-owner-form`, C++ and Qt Quick in `form/`) runs full screen in a minimal Hyprland on
  tty2 (`omarchy-pad-owner-form.service`, `owner-form/hyprland.lua`, started as SDDM starts its greeter's), as the
  system user `omarchy-pad-setup`, while Omarchy's text form runs unseen on tty1. Its Start button passes Omarchy's
  "Press Return" greeter: the window leaves a file, and a root helper beside it (`owner-form-helper`, in
  `omarchy-pad-owner-form-helper.service`) puts a Return into tty1 with TIOCSTI. The helper is a service of its own
  because systemd kills whatever an `ExecStartPre=` leaves running. Then a page per question: a list (searchable when
  long, such as the timezones), a field, the password and its confirmation on one page (Omarchy's second question is
  answered from it), and the summary as a card with Omarchy's yes and no. It checks the user name and hostname with
  Omarchy's own patterns and messages (read from `setup-form.sh` and Omarchy's text catalog), so Omarchy rarely has to
  ask again; when it does, its message is read off tty1's screen (the helper's copy of `/dev/vcsu1`). Then the setup's progress, from
  Omarchy's state file, until SDDM takes the screen.
- **The keyboard** is floatkey's keys drawn in the window (`../lock/LockKeyboard.qml`, with floatkey's `layouts.js`),
  in the layout picked in the first step.
- **Waiting for it:** the owner setup asks nothing until the window's socket is there (`owner-form-up`, at most
  90 s). The service cannot tell systemd it is ready: its logind session moves its processes out of its own cgroup.
- **The fallback:** if the window does not come up, or goes away during the setup, buffyboard
  (`omarchy-pad-owner-keyboard.service`) draws a touch keyboard under Omarchy's text form, as before 0.4.

## Trying it

- `tests/check` runs the window offscreen against Omarchy's own `setup-form.sh` questions, answers each page with test
  values, checks what Omarchy's prompts got back, and saves a picture of each page.
- `tests/try` opens the window on your desktop with the same questions, to tap through by hand.

Neither needs root or creates a user. What only the tablet shows: the switch to tty2 at the first boot, the Return into
tty1, and the switch back to tty1 before SDDM. On a tablet already set up, the unit can be started without its
condition (a copy of it in `/run/systemd/system`) to see the window come up.

Why Hyprland and not cage: cage's wlroots cannot drive the OnePlus Pad 4's panel (it hangs or loops on its first
modeset, tried 2 Nov 2026 with atomic, legacy and pixman), and Hyprland drives it every day.

## Packages

| Folder | Package | What |
|---|---|---|
| `buffybox/` | `buffybox` 3.6.0 | buffyboard and unl0kr, built from the upstream release (Arch Linux ARM does not package them) |
| `form/` | (in `omarchy-pad-firstboot`) | the window, `omarchy-pad-owner-form` |
| `pkgbuild/` | `omarchy-pad-firstboot` | the window and `omarchy-pad-owner-form.service`, the stand-in `gum`, buffyboard's `omarchy-pad-owner-keyboard.service` and its script, the drop-in that starts and stops them with `omarchy-provision-owner.service`, the form's `setfont`, buffyboard's theme (`/etc/buffyboard.conf.d/50-omarchy-pad.conf`) and `uinput` at boot |

Build each with `makepkg` in its folder. `buffybox` needs `meson` and `scdoc` to build.

## Notes on buffyboard (the fallback)

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
