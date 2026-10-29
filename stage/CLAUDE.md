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
