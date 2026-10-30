# stage

All the touch gestures, in one transparent layer-shell overlay that takes
touches only in strips along the edges. It talks to Hyprland over its sockets
(`src/hyprland.*`) and reads the touch screen straight from libinput
(`src/touchscreen.*`).

- **Version:** `VERSION` in `stage.pro`. The PKGBUILD reads it from there, and
  `stage --version` prints it with the commit.
- **Running a build in place:** `stage quit`, then
  `setsid -f build/stage >/dev/null 2>&1 </dev/null`. Use `/usr/bin/stage` to
  go back to the installed one. `stage status` and `stage simulate …` (see the
  README) drive it without a finger.
- **Logs:** `QT_LOGGING_RULES="stage.debug=true"` shows what each gesture sees.
  Logs go to the user journal unless you set `QT_FORCE_STDERR_LOGGING=1`.
- `src/fingers.cpp` (`Fingers::map`) maps panel points to the screen for each
  rotation. floatkey's tap rule copies it, so keep the two in step.
- stage itself changes Hyprland's animations for a single toggle and puts
  them back. That is app code over the socket, and fine. Claude's own
  experiments with the live config are not (see the root CLAUDE.md).
- Window pictures go through `src/gpupicture.*` (a dmabuf Hyprland copies
  into, drawn as a GL texture) and fall back to shared memory in
  `src/capture.cpp` when either side refuses. With debug logging, each
  pull or slide ends with a `frames: … | moves: …` line: the usual step
  between frames and between finger moves, the longest, and the late ones.
- A window being pulled is hidden with the `stage-hidden` tag and its
  `no_anim` (so it goes at once); stage registers the tag's rule itself and
  takes leftover tags off when it starts.
- `stage resize live|pictures` is kept with QSettings in
  `~/.config/stage/stage.conf`. A stage tried in a nested Hyprland should get
  an `XDG_CONFIG_HOME` of its own, or switching it switches the real one.
- A window carried by two fingers and put beside another goes out to the
  `special:stagegrab` workspace and back in, with the target focused and a
  dwindle `preselect`. That gives the side asked for only with
  `dwindle.preserve_split` on, as Omarchy has it; a nested Hyprland trying it
  needs that too.
- The launcher's rows come from `src/shelves.*` (made up from the apps until
  the user moves one, then kept in `~/.config/stage/launcher.json`); the same
  rule as `stage.conf` about a nested stage's `XDG_CONFIG_HOME` applies.
