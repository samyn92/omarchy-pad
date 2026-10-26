# Omarchy Pad

Omarchy on a tablet, made to feel like an iPad: the OnePlus Pad 4 first,
anything with a touch screen in principle.

| Project | What it does |
|---|---|
| [floatkey](floatkey/) | The on-screen keyboard: docked, floating, or in a corner; comes up when a text field takes focus and stays away while a hardware keyboard is attached |
| [stage](stage/) | Multitasking gestures: drag in from the right or bottom edge to make room beside a window and pick an app for it, pull the scratchpad console down from the top |
| [tablet](tablet/) | The tablet session that ties it together: auto-rotation, starting floatkey and stage, and lisgd's multi-finger gestures (three fingers sideways for workspaces, two or three fingers down to close a window) |

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
