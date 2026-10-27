#pragma once

#include <QByteArray>
#include <QImage>
#include <memory>
#include <stdexcept>
#include <utility>

namespace WemuSession {
    // Immutable packet storage follows every QImage copy, including the render
    // thread's last reference. Writable QImage access detaches in the usual way.
    inline QImage frameImage(QByteArray body, qsizetype offset, quint32 width, quint32 height)
    {
        if (!width || !height || width > 4096 || height > 4096 || offset < 0 || offset % 4 || offset > body.size() ||
            quint64(width) * height * 4 != quint64(body.size() - offset))
            throw std::runtime_error("Invalid game framebuffer storage");
        auto storage = std::make_unique<QByteArray>(std::move(body));
        // TV/GamePad scan-out is RGBX. Guests commonly leave X at zero;
        // QImage's RGBX format requires 255 there, including the software scene
        // graph path which may otherwise blend successive frames as transparent.
        auto *opaque = reinterpret_cast<unsigned char *>(storage->data() + offset);
        for (quint64 i = 3, bytes = quint64(width) * height * 4; i < bytes; i += 4)
            opaque[i] = 255;
        const auto *pixels = reinterpret_cast<const uchar *>(storage->constData() + offset);
        QImage image(
                pixels, int(width), int(height), int(width * 4), QImage::Format_RGBX8888,
                [](void *owner) { delete static_cast<QByteArray *>(owner); }, storage.get());
        if (image.isNull())
            throw std::runtime_error("Cannot allocate the game display image");
        // The image's cleanup callback now owns the packet.
        [[maybe_unused]] const auto *imageOwner = storage.release();
        return image;
    }
} // namespace WemuSession
