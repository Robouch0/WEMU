#pragma once

#include <QMap>
#include <QString>

struct InputProfile {
    QString name;
    QMap<QString, QString> bindings;
};
