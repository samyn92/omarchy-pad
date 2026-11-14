#include "photo.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <cstdlib>
#include <cstring>
#include <libexif/exif-data.h>

namespace Photo {

QString defaultFolder()
{
    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty())
        pictures = QDir::homePath() + "/Pictures";
    return pictures + "/Camera";
}

QString nextName(const QString &folder, const QDateTime &when, const QSet<QString> &reserved)
{
    const QString stem = "IMG_" + when.toString("yyyyMMdd_HHmmss");
    const QDir dir(folder);
    for (int n = 1;; ++n) {
        const QString name = n == 1 ? stem + ".jpg" : QStringLiteral("%1_%2.jpg").arg(stem).arg(n);
        if (!reserved.contains(name) && !dir.exists(name))
            return name;
    }
}

QString newest(const QString &folder)
{
    const QStringList photos = QDir(folder).entryList({"IMG_*.jpg"}, QDir::Files, QDir::Name);
    if (photos.isEmpty())
        return {};
    // Names sort by time, except _10 and up within one second, which are
    // only ever a burst; the last whole second is what matters.
    return QDir(folder).filePath(photos.last());
}

static QString firstLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    // Device tree strings end in a NUL.
    return QString::fromUtf8(file.readAll()).section(QChar(0), 0, 0).trimmed();
}

QString maker()
{
    const QString vendor = firstLine("/sys/class/dmi/id/sys_vendor");
    if (!vendor.isEmpty())
        return vendor;
    // Device trees only name the model; its maker is its first word
    // ("OnePlus Pad 4").
    return model().section(' ', 0, 0);
}

QString model()
{
    const QString product = firstLine("/sys/class/dmi/id/product_name");
    return !product.isEmpty() ? product : firstLine("/sys/firmware/devicetree/base/model");
}

static void setText(ExifData *exif, ExifIfd ifd, ExifTag tag, const QByteArray &text)
{
    if (text.isEmpty())
        return;
    ExifEntry *entry = exif_entry_new();
    exif_content_add_entry(exif->ifd[ifd], entry);
    entry->tag = tag;
    entry->format = EXIF_FORMAT_ASCII;
    entry->components = text.size() + 1;
    entry->size = entry->components;
    entry->data = static_cast<unsigned char *>(std::malloc(entry->size));
    std::memcpy(entry->data, text.constData(), text.size() + 1);
    exif_entry_unref(entry);
}

static void setLong(ExifData *exif, ExifIfd ifd, ExifTag tag, uint32_t value)
{
    ExifEntry *entry = exif_entry_new();
    exif_content_add_entry(exif->ifd[ifd], entry);
    exif_entry_initialize(entry, tag);
    if (entry->format == EXIF_FORMAT_SHORT)
        exif_set_short(entry->data, exif_data_get_byte_order(exif), uint16_t(value));
    else
        exif_set_long(entry->data, exif_data_get_byte_order(exif), value);
    exif_entry_unref(entry);
}

static QByteArray exifBlock(const QSize &size, const QDateTime &when)
{
    ExifData *exif = exif_data_new();
    exif_data_set_byte_order(exif, EXIF_BYTE_ORDER_INTEL);
    exif_data_set_data_type(exif, EXIF_DATA_TYPE_COMPRESSED);
    const QByteArray stamp = when.toString("yyyy:MM:dd HH:mm:ss").toLatin1();
    setText(exif, EXIF_IFD_0, EXIF_TAG_MAKE, maker().toUtf8());
    setText(exif, EXIF_IFD_0, EXIF_TAG_MODEL, model().toUtf8());
    setText(exif, EXIF_IFD_0, EXIF_TAG_SOFTWARE, QByteArray("Omarchy Pad Camera " CAMERA_VERSION));
    setText(exif, EXIF_IFD_0, EXIF_TAG_DATE_TIME, stamp);
    setText(exif, EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_ORIGINAL, stamp);
    setText(exif, EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_DIGITIZED, stamp);
    // The pixels are already upright, so viewers mustn't turn them again.
    setLong(exif, EXIF_IFD_0, EXIF_TAG_ORIENTATION, 1);
    setLong(exif, EXIF_IFD_EXIF, EXIF_TAG_PIXEL_X_DIMENSION, uint32_t(size.width()));
    setLong(exif, EXIF_IFD_EXIF, EXIF_TAG_PIXEL_Y_DIMENSION, uint32_t(size.height()));
    unsigned char *data = nullptr;
    unsigned int length = 0;
    exif_data_save_data(exif, &data, &length);
    // libexif's block starts with the "Exif\0\0" an APP1 segment carries.
    QByteArray block(reinterpret_cast<const char *>(data), int(length));
    std::free(data);
    exif_data_unref(exif);
    return block;
}

QByteArray jpeg(const QImage &image, const QDateTime &when, int quality)
{
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", quality) || encoded.size() < 2)
        return {};
    const QByteArray exif = exifBlock(image.size(), when);
    if (exif.isEmpty() || exif.size() > 0xfffd)
        return encoded;
    // The EXIF segment (APP1) goes right after the start-of-image marker.
    QByteArray segment("\xff\xe1", 2);
    const int length = exif.size() + 2;
    segment.append(char(length >> 8)).append(char(length & 0xff)).append(exif);
    encoded.insert(2, segment);
    return encoded;
}

} // namespace Photo
