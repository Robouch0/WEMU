#include "TitleScanner.hpp"

#include <QDir>
#include <QFile>
#include <QXmlStreamReader>

TitleScanner::TitleScanner(QObject *parent)
    : QAbstractListModel(parent)
{
}

void TitleScanner::scanDirectory(const QString &path)
{
    beginResetModel();
    m_titles.clear();

    if (m_searchPath != path) {
        m_searchPath = path;
        emit searchPathChanged();
    }

    QDir root(path);
    for (const QString &entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        GameTitle title;
        QString gameDir = root.filePath(entry);
        QString metaXml = gameDir + "/meta/meta.xml";
        QString codeDir = gameDir + "/code";

        if (!QFile::exists(metaXml))
            continue;

        const QStringList rpxFiles = QDir(codeDir).entryList({"*.rpx"}, QDir::Files);
        if (rpxFiles.isEmpty())
            continue;
        title.rpxPath = codeDir + "/" + rpxFiles.first();

        QFile file(metaXml);
        if (file.open(QIODevice::ReadOnly)) {
            QXmlStreamReader xml(&file);
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement()) {
                    if (xml.name() == u"longname_en")
                        title.name = xml.readElementText();
                    else if (xml.name() == u"publisher_en")
                        title.publisher = xml.readElementText();
                    else if (xml.name() == u"title_version")
                        title.version = xml.readElementText();
                }
            }
        }

        m_titles.append(title);
    }

    endResetModel();
}


int TitleScanner::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_titles.size());
}

QHash<int, QByteArray> TitleScanner::roleNames() const
{
    return {
        { NameRole, "name"},
        { PublisherRole, "publisher"},
        { VersionRole, "version"},
        { IconPathRole, "iconPath"},
        { RpxPathRole, "rpxPath"},
    };
}

QVariant TitleScanner::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_titles.size())
        return {};

    const GameTitle &title = m_titles.at(index.row());
    switch (role) {
        case NameRole:      return title.name;
        case PublisherRole: return title.publisher;
        case VersionRole:   return title.version;
        case IconPathRole:  return title.iconPath;
        case RpxPathRole:   return title.rpxPath;
        default:            return {};
    }
}
