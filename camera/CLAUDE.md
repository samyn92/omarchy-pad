# camera

The camera app: libcamera straight from C++ (`src/cameras.*`), a viewfinder
that draws the camera's dmabufs as GL textures (`src/viewfinder.*`), photos
saved as JPEG with EXIF (`src/photo.*`), and tap to focus driven by the app
(`src/focus.*`).

- **Version:** `VERSION` in `camera.pro`. The PKGBUILD reads it from there;
  `omarchy-pad-camera --version` and the photos' EXIF carry it.
- **Running a build in place:** `QT_IM_MODULE=none setsid -f
  build/omarchy-pad-camera >/dev/null 2>&1 </dev/null`. It opens a window on
  the current workspace and holds the camera until closed
  (`pkill -x omarchy-pad-cam`; never `pkill -f`, which matches the shell
  running it). Photos taken go to the real `~/Pictures/Camera`.
- **Logs:** `QT_LOGGING_RULES="camera.debug=true"` shows the stream it picked,
  fps every 90 frames, the lens and each focus step.
  `LIBCAMERA_LOG_LEVELS='*:ERROR'` quiets libcamera.
- **Why not Qt Multimedia or PipeWire:** Qt Multimedia only sees V4L2 nodes,
  and Qualcomm's CAMSS nodes are useless without libcamera. PipeWire gives no
  lens control and no full-size stills.
- **One stream at full size:** on the Pad 4 the software ISP keeps 30 fps at
  13 MP, so the viewfinder streams the sensor's whole size and the shutter
  saves the current frame. The simple pipeline has one stream per camera, so
  a smaller preview would mean switching modes for each photo.
- **Frames and buffers:** a `Frame` holds a camera buffer until let go; the
  newest waits in `Cameras::m_latest`, the viewfinder keeps the one it shows
  and the one before (the GPU may still be drawing it). Before a stream stops,
  `Cameras::stopping` makes everyone let go: libcamera frees the buffers before
  releasing the camera, and refuses to release it otherwise.
- **Focus:** the simple pipeline exposes no lens control, so the app finds the
  lens through the sensor's ancillary media link and moves `FOCUS_ABSOLUTE`
  itself. `OMARCHY_PAD_CAMERA_LENS=/dev/v4l-subdevN` names one by hand.
- **Orientation:** libcamera's `Rotation` (as Android's SENSOR_ORIENTATION)
  turned back by the screen's rotation; see `src/orientation.h`. The QML turns
  `Screen.orientation` into the screen's clockwise rotation.
- `src/theme.*` is copied from stage (follows the Omarchy theme); keep fixes
  in step.
- The tests never open a real camera: `appStartsWithNoCamera` sets
  `LIBCAMERA_PIPELINES_MATCH_LIST` to a pipeline that doesn't exist.
