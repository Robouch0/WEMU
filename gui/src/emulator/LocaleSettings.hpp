#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

#include "hle/SystemConfig.hpp"

inline unsigned titleRegionForLaunch(const QString &rpxPath)
{
    QFile file(QFileInfo(rpxPath).absoluteDir().filePath("../meta/meta.xml"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return 4;
    QXmlStreamReader xml(&file);
    unsigned mask{};
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("region")) {
            bool valid{};
            mask = xml.readElementText().trimmed().toUInt(&valid, 16);
            if (!valid)
                return 4;
        }
    }
    return xml.hasError() ? 4 : Core::SystemConfig::chooseTitleRegion(mask);
}
