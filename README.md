# Omarchy Pad

Omarchy on a tablet, made to feel like an iPad: the OnePlus Pad 4 first,
anything with a touch screen in principle.

| Project | What it does |
|---|---|
| [floatkey](floatkey/) | The on-screen keyboard: docked, floating, or in a corner; comes up when a text field takes focus and stays away while a hardware keyboard is attached |
| [stage](stage/) | All the touch gestures: drag in from the right or bottom edge to make room beside a window and pick an app for it, pull the scratchpad console down from the top, pull a window down with two fingers to close it, or hold two fingers on it to carry it beside another window or to another workspace, three fingers sideways for workspaces |
| [lock](lock/) | Omarchy's lock screen with floatkey's keyboard, for unlocking with no keyboard attached (prototype) |
| [tablet](tablet/) | The tablet session that ties it together: auto-rotation, starting floatkey and stage |

Each project builds, tests and installs on its own (`bin/build`, `bin/test`,
`bin/install` in its folder), or all at once from here:

```bash
bin/test      # both test suites
bin/install   # both packages (asks for your password once per package)
```

The tablet session is set up once, on the tablet, after the first login:

```bash
tablet/install-tablet-setup.sh
```

Logging in by touch comes with the `omarchy-pad-lock` package
(`lock/pkgbuild/`), which the install image carries: SDDM logs the owner in by
itself, into a session that starts locked, and the password is typed on the
lock screen's keyboard instead of in SDDM's greeter, which has none. See
[lock](lock/README.md) for how it is set up and how to go back.
