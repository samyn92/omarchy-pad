# Omarchy Pad

Omarchy on a tablet, made to feel like an iPad: the OnePlus Pad 4 first,
anything with a touch screen in principle.

| Project | What it does |
|---|---|
| [floatkey](floatkey/) | The on-screen keyboard: docked, floating, or in a corner; comes up when a text field takes focus and stays away while a hardware keyboard is attached |
| [stage](stage/) | All the touch gestures: drag in from the right or bottom edge to make room beside a window and pick an app for it, pull the scratchpad console down from the top, pull a window down with two fingers to close it, or hold two fingers on it to carry it beside another window or to another workspace, three fingers sideways for workspaces |
| [lock](lock/) | Omarchy's lock screen with floatkey's keyboard, for unlocking with no keyboard attached (prototype) |
| [tablet](tablet/) | The tablet session that ties it together: auto-rotation, starting floatkey and stage |
| [shell](shell/) | Omarchy shell plugins for touch: the tablet bar, pull-down drawers, a picker you can swipe |

Omarchy Pad is meant for any touch tablet. What one tablet's hardware needs
(its kernel, firmware, sound, charging, the panel's scale) comes from that
tablet's device package, such as the OnePlus Pad 4's `iceland-device`.

## Installing

Everything comes as packages, and the `omarchy-pad` package pulls in all of
them. Each one sets itself up for every user at their next login, so there is
nothing to run after installing. To build them all into a folder (for an
install image or a package repository):

```bash
bin/packages OUTDIR
```

While working on one project, each builds, tests and installs on its own
(`bin/build`, `bin/test`, `bin/install` in its folder), or floatkey and stage
at once from here:

```bash
bin/test      # both test suites
bin/install   # both packages (asks for your password once per package)
```

Logging in by touch comes with the `omarchy-pad-lock` package
(`lock/pkgbuild/`): SDDM logs the owner in by itself, into a session that
starts locked, and the password is typed on the lock screen's keyboard instead
of in SDDM's greeter, which has none. See [lock](lock/README.md) for how it is
set up and how to go back.
