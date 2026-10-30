# stage

iPad-style multitasking for Omarchy on a tablet. Drag in from the right edge
and the window under your finger gets smaller: the room it gives up becomes the
launcher, and the app you tap opens there. Drag up from the bottom edge to do
the same the other way. Pull the scratchpad console down from the top.

![Dragging in from the right edge](docs/dragging.png)

![Let go: the window keeps its share, the launcher takes the rest](docs/split.png)

![Up from the bottom edge, the same the other way](docs/bottom.png)

## Gestures

| Gesture | What it does |
|---|---|
| One finger in from the **right edge** | Shrinks the window beside your finger sideways; the launcher fills the room |
| One finger up from the **bottom edge** | Shrinks the window above your finger upward; the launcher fills the room |
| One finger down from the middle of the **bar** (or the top edge without Omarchy Pad's bar) | Pulls Omarchy's scratchpad console down with the finger; an empty one offers the launcher |
| Push up on the dimmed screen or from the bottom edge, with the console down | Takes the console up with the finger; a short push puts it back. A tap on the dimmed screen sends it up too |
| Hold the **line between two windows** (0.2 s, it lights up), then drag | Both windows follow the line, resized as it moves (or as frosted pictures, see below); let go and it lands on a third, a half or two thirds. Pushed to the screen's edge, a single window leaves for an empty workspace of its own |
| Two fingers pulled **down** on a window | The window follows them and, let go far enough or flicked, slides off and closes; a short pull puts it back |
| Two fingers **held still** on a window (half a second) | The window lifts and follows your fingers. Put it on a side of another window to share that window's place (its left or right third for beside, the middle for above or below), or on a workspace in the strip that comes down along the top (the last one, +, is a new one) to move it there. Anywhere else it goes back |
| Three fingers sideways | Slides this workspace aside for the next or previous one, following your fingers; a quarter of the way or a flick switches, less goes back. Past the last workspace with windows there is one empty one, to start something new on |
| Three fingers up | Maximizes the focused window |
| Three fingers down | Pulls the window down, as with two |
| Three fingers **pinched in** | Copies, as on an iPad (floatkey sends the keys, with shift in a terminal) |
| Three fingers **spread out** | Pastes |

Whenever anything is copied, by a pinch, a key or an app's menu, a small
"Copied" pill shows under the bar for a moment. Two fingers pinching are left
to the apps, for zooming.

While you drag, the window keeps its border at your finger, the wallpaper
shows through the gap, and the launcher arrives as a window of its own, with
Hyprland's gaps, border colors and rounding, so it already looks like the
layout it will become. When you let go, the window keeps just the share your
finger left it, from a quarter of the room to four fifths (the launcher needs
at least a fifth). A short, quick flick makes halves; a short or slow pull
springs back. Tap an app and its window opens where the launcher was, at that
size; tap beside the launcher to put things back.

The launcher shows the apps in rows, one kind of app a row, each scrolling
sideways; the rows scroll up and down. Until you arrange them they are made
up from the apps: Omarchy's dock favourites on top, then AI apps, Omarchy's
web apps, Internet, Office, Media, Development, Games and System by the
categories the apps give, and Other for the rest. Hold an app to arrange
them: the icons wiggle, and the app follows your finger into another row,
between two apps, or onto "New row" at the end (a row scrolls along when you
take an app to its edge). The arrows beside a row's name move the row up or
down, and Done ends it. Your rows are kept in `~/.config/stage/launcher.json`;
an app installed later goes into the row of its kind, or Other, and a row
emptied goes away.

The icons are the icon theme's that Omarchy's theme picks, followed when the
theme changes. When it names one that isn't installed (Omarchy's Vantablack
and White name a Yaru-gray that Yaru doesn't have), the nearest installed one
stands in: Yaru-gray's is Yaru, and hicolor, where every app keeps its own
icon, after that.

Two and three fingers come straight from the touch screen (stage opens the
devices udev tags as touch screens with libinput, so the user has to be in the
`input` group), because fingers on a window go to that window, not to the
overlay. A pulled window is a picture of it, and the real window is hidden
while the picture is out, so whatever is behind it shows: the wallpaper, other
windows, the dimmed screen around the console. Any window can be pulled away
like that, floating ones and the console's too. Once Hyprland has closed it,
the others move in; a short pull puts the picture back and the window with it.
The app under your fingers still sees them, so a page may scroll a little
while you pull.
Try it without touching: `stage simulate pull 1150 300 1150 650`, then
`stage simulate release`.

Carrying a window: once two fingers have rested on it for half a second
(moving first is a pull, which closes), the window becomes a picture with a
shadow, a little smaller, and its place stays empty while you carry it. Over
another window, the half it would take lights up in the theme's accent and the
picture shrinks so you can see it; let go and dwindle splits that window there,
so two windows put on two halves make quarters. Over the strip of workspaces
the picture becomes a thumbnail below your fingers and the tile under them
lights up; let go and the window moves to that workspace and you stay where you
are. Any window can be carried; a floating or maximized one only to another
workspace. Try it without touching: `stage simulate grab X Y X2 Y2` rests two
fingers at X Y long enough to lift the window there and carries it to X2 Y2,
then `stage simulate release`.

Holding a line between windows: stage keeps a grip about 36 px wide along
every line where tiled windows meet, reaching a little into the windows on
both sides, so the apps never get that touch (a browser would open its menu
first). The light gathers at the finger in the theme's accent while it's held,
and bursts when it's grabbed. Try it without touching: `stage simulate press X
Y` on a line, `stage simulate move X Y`, then `stage simulate release`.

Once grabbed, the windows follow the line in one of two ways:

- **Live** (the default): the windows themselves are resized as the line
  moves, up to about 30 times a second, and Hyprland animates them onto the
  third or half it lands on. You see the real apps; one that redraws slower
  than that falls a little behind the line.
- **With pictures,** as iPadOS does: frosted pictures of the windows, with
  their apps' icons, follow the finger at the screen's full rate, and the
  windows themselves take their new sizes once it lets go.

```bash
stage resize pictures   # or: stage resize live; remembered from then on
stage resize            # which one it does now
```

Omarchy Pad's bar (`../shell/bar`) hands a pull from its middle to stage over
the command socket: `stage top begin X Y`, `stage top follow DISTANCE`,
`stage top end 1|0`. It stays connected and says `bar here`, and while it is
connected stage leaves the top edge to the bar, so a pull from the top always
does what the bar's third under the finger does.

A workspace holds at most four windows (two each way), the console two. Past
that, or splitting a window that already shares its width or height, the
picture only gives a little and springs back, so you can feel there's no more
room. While a docked keyboard owns the bottom edge, only the right and top work.

On an empty workspace the right or bottom edge opens the launcher on the whole screen.

![The scratchpad console coming down](docs/console.png)

## Commands

```bash
stage                          # run (the tablet session starts it)
stage status                   # console=closed apps=55
stage launch foot right 0.66   # what the launcher does: foot beside the focused window
stage launch firefox console   # ...or in the console
stage console open             # bring the console down; also close
stage simulate drag 1693 600 1200 600   # a finger without a hand, still held;
stage simulate release                  # then release, pick APP or cancel
stage simulate pinch in                 # three fingers copying; out pastes
stage simulate grab 400 500 1500 500    # two fingers lift a window and carry it;
                                        # then release
stage quit
```

## How it works

stage is one transparent layer-shell overlay over the whole screen that only
takes touches in strips along the edges, so a finger landing there reaches
stage instead of the app underneath. The real window never resizes while the
finger moves, which is what keeps it smooth: stage draws over the part it
gives up, with the wallpaper as Omarchy shows it and a new window frame, at
the places dwindle will put the two windows (the split line, minus half a gap
and a border on each side). The console's windows are hidden until it comes
down, so for those stage takes pictures (ext-image-copy-capture) to move.

Pictures are copied on the GPU when Hyprland and the driver allow it (they
do on the OnePlus Pad): Hyprland copies the window into a GPU buffer stage
made, and stage draws that buffer as it is. One is ready in a few
milliseconds, and nothing stalls. Otherwise they come through memory, which
takes Hyprland about 60 ms for a full-screen window, during which it draws
nothing else. To hide the real window, stage registers a window rule that
makes windows tagged `stage-hidden` see-through and tags the one it pulls;
taking the tag off gives the window back its own opacity.

Letting go on an app tells Hyprland, over its socket, to focus the window that
made room, put the next window beside or under it (dwindle's preselect),
launch the app through `uwsm-app` the way Omarchy does, and once its window is
up, set the split to where your finger left it. The console comes down without
Hyprland sliding it a second time: its `specialWorkspaceIn` animation is
switched off for that one toggle and put back exactly as it was.

## Install

```bash
bin/install
```

On the OnePlus Pad tablet setup (`../tablet`), the tablet session starts
stage.
Keep Hyprland from animating the overlay:

```lua
hl.layer_rule({ match = { namespace = "^stage$" }, no_anim = true, animation = "none" })
```

## Development

```bash
bin/build     # build/stage
bin/test      # unit tests, and the gestures on the real QML, offscreen
QT_LOGGING_RULES="stage.debug=true" build/stage   # what each gesture sees
```
