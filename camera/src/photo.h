#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QSet>
#include <QString>

// Saving photos: names, the JPEG with its EXIF, and where they go.
namespace Photo {

// The folder photos go to: Camera in the user's Pictures.
QString defaultFolder();

// A free name in `folder` for a photo taken at `when`: IMG_yyyyMMdd_HHmmss.jpg,
// with _2, _3 … for more in the same second. `reserved` holds names handed out
// for photos still being written.
QString nextName(const QString &folder, const QDateTime &when, const QSet<QString> &reserved = {});

// The newest photo in `folder` (by name, which sorts by time), or empty.
QString newest(const QString &folder);

// `image` as a JPEG carrying EXIF: when it was taken, the tablet's maker and
// model when known, and this app as the software.
QByteArray jpeg(const QImage &image, const QDateTime &when, int quality = 92);

// The tablet's maker and model, from the firmware (DMI) or the device tree.
QString maker();
QString model();

} // namespace Photo
