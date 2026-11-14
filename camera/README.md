# Camera

Take photos with the tablet's cameras, by touch. It opens on the back camera,
with the whole picture the photo will have.

| Do | What happens |
|---|---|
| Tap the **shutter** (the big white button) | Saves what you see as a photo, at the camera's full size (13 MP on the back of the OnePlus Pad 4, 8 MP on the front) |
| Tap the **picture** | Focuses on that spot (cameras with a focus motor; a square shows where) |
| Tap the **turning arrows** | Switches between the back and front camera |
| Tap the **last photo** | Opens it in your image viewer |

The controls sit along the right edge when the tablet is held sideways and
along the bottom when it's upright. The front camera shows you as a mirror
would, and saves the photo the way others see you.

Photos go to `Camera` in your Pictures folder (`~/Pictures/Camera`), named by
when they were taken (`IMG_20261113_190501.jpg`), upright, with the date and
the tablet's model in the photo's details.

While the camera app is in the background for a while, it lets the camera go,
so a browser or a call can use it, and takes it back when you return. If
another app has the camera when you open it, it says so: tap to try again.

## What it needs

Any camera libcamera drives. On the OnePlus Pad 4, the device package
(`iceland-device`) brings the kernel side and the cameras' settings.

```bash
bin/build     # build/omarchy-pad-camera
bin/test      # the tests (no camera needed)
bin/install   # the omarchy-pad-camera package
```

`omarchy-pad-camera --version` prints the version and the commit it was built
from. `QT_LOGGING_RULES=camera.debug=true` logs the stream, its frame rate and
focusing.
