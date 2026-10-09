#include "TitleScanner.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QUrl>
#include <QXmlStreamReader>
#include <algorithm>
#include <QCryptographicHash>
#include <QDateTime>
#include <QImage>
#include <QStandardPaths>
#include <SDL2/SDL_image.h>

namespace {
QString displayIcon(const QString &path)
{
    if (!path.endsWith(".tga", Qt::CaseInsensitive)) return QUrl::fromLocalFile(path).toString();
    const QFileInfo info(path);
    const auto key = QCryptographicHash::hash((path + QString::number(info.lastModified().toMSecsSinceEpoch())).toUtf8(),
                                             QCryptographicHash::Sha256).toHex();
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/icons";
    const auto output = cache + "/" + key + ".png";
    if (!QFile::exists(output)) {
        SDL_Surface *source = IMG_Load(path.toUtf8().constData());
        if (!source) return {};
        SDL_Surface *rgba = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(source);
        if (!rgba) return {};
        QImage image(static_cast<const uchar *>(rgba->pixels), rgba->w, rgba->h, rgba->pitch, QImage::Format_RGBA8888);
        const bool saved = QDir().mkpath(cache) && image.save(output, "PNG");
        SDL_FreeSurface(rgba);
        if (!saved) return {};
    }
    return QUrl::fromLocalFile(output).toString();
}

QMap<QString, QString> metadata(const QString &path, bool *wellFormed = nullptr)
{
    if (wellFormed) *wellFormed = false;
    QFile file(path);
    QMap<QString, QString> result;
    if (!file.open(QIODevice::ReadOnly)) return result;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const auto name = xml.name().toString();
        if (name == "title_id" || name == "title_version" || name == "longname_en"
            || name == "longname_ja" || name == "publisher_en")
            result[name] = xml.readElementText().trimmed();
    }
    if (wellFormed) *wellFormed = !xml.hasError();
    return xml.hasError() ? QMap<QString, QString>{} : result;
}
}

QString TitleScanner::defaultLibraryPath(const QString &applicationDirectory, const QString &workingDirectory)
{
    for (const auto &start : {applicationDirectory, workingDirectory}) {
        QDir directory(start);
        do {
            const QFileInfo games(directory.filePath("games"));
            if (games.isDir()) return games.canonicalFilePath();
        } while (directory.cdUp());
    }
    return {};
}

TitleScanner::TitleScanner(QObject *parent)
    : QAbstractListModel(parent), m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { refresh(); });
}

void TitleScanner::scanDirectory(const QString &path)
{
    const QUrl url(path);
    const QString local = url.isLocalFile() ? url.toLocalFile() : path;
    if (local.isEmpty() || !QDir(local).exists()) return;
    const QString rootPath = QFileInfo(local).canonicalFilePath();
    struct Candidate { GameTitle title; quint64 id; unsigned version; };
    QList<Candidate> bases, updates;
    QList<GameTitle> demos;
    QSet<QString> seenDemos;
    QStringList roots{rootPath};
    const QDir root(rootPath);
    const bool structuredRoot = QDir(root.filePath("code")).exists();
    for (const auto &entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!structuredRoot || (entry != "code" && entry != "content" && entry != "meta")) roots.append(root.filePath(entry));
    }
    for (const auto &dir : roots) {
        const auto rpx = QDir(dir + "/code").entryList({"*.rpx"}, QDir::Files, QDir::Name);
        bool metaValid = false, appValid = false;
        const auto meta = metadata(dir + "/meta/meta.xml", &metaValid);
        const auto app = metadata(dir + "/code/app.xml", &appValid);
        if (!appValid && QFile::exists(dir + "/code/app.xml")) continue;
        // Updates often retain the base title ID in meta.xml. app.xml is authoritative.
        const auto idString = app.value("title_id", meta.value("title_id"));
        if (idString.isEmpty()) {
            if (!metaValid && QFile::exists(dir + "/meta/meta.xml")) continue;
            QStringList executables;
            for (const auto &name : rpx) executables.append(dir + "/code/" + name);
            for (const auto &name : QDir(dir).entryList({"*.rpx"}, QDir::Files, QDir::Name))
                executables.append(dir + "/" + name);
            for (const auto &executable : executables) {
                const auto canonical = QFileInfo(executable).canonicalFilePath();
                if (seenDemos.contains(canonical)) continue;
                seenDemos.insert(canonical);
                GameTitle title;
                const auto fallback = QFileInfo(executable).completeBaseName();
                title.name = executables.size() == 1 ? meta.value("longname_en", meta.value("longname_ja", fallback)) : fallback;
                if (title.name.isEmpty()) title.name = fallback;
                title.name.replace('\n', ' ');
                title.publisher = meta.value("publisher_en");
                title.version = meta.value("title_version");
                title.rpxPath = canonical;
                title.contentPath = QDir(dir + "/content").exists() ? dir + "/content" : dir;
                title.titleId = "rpx:" + canonical;
                for (const auto &icon : {dir + "/meta/cover.png", dir + "/cover.png", dir + "/meta/iconTex.tga"}) {
                    if (!QFile::exists(icon)) continue;
                    title.iconPath = displayIcon(icon);
                    if (!title.iconPath.isEmpty()) break;
                }
                demos.append(title);
            }
            continue;
        }
        if (rpx.size() != 1 || !QDir(dir + "/content").exists()) continue;
        bool valid = false;
        const quint64 id = idString.toULongLong(&valid, 16);
        if (!valid || idString.size() != 16) continue;
        const auto kind = id >> 32;
        if (kind != 0x00050000 && kind != 0x0005000e) continue;
        GameTitle title;
        title.name = meta.value("longname_en", meta.value("longname_ja", QFileInfo(dir).fileName()));
        title.name.replace('\n', ' ');
        title.publisher = meta.value("publisher_en");
        title.titleId = QString::number(id & 0xffffffffULL, 16).rightJustified(8, '0');
        const unsigned version = app.contains("title_version") ? app.value("title_version").toUInt(nullptr, 16)
                                                               : meta.value("title_version").toUInt();
        title.version = QString::number(version);
        title.rpxPath = dir + "/code/" + rpx.first();
        title.contentPath = dir + "/content";
        for (const auto &icon : {dir + "/meta/cover.png", dir + "/cover.png", dir + "/meta/iconTex.tga"}) {
            if (QFile::exists(icon)) {
                title.iconPath = displayIcon(icon);
                if (!title.iconPath.isEmpty()) break;
            }
        }
        (kind == 0x0005000e ? updates : bases).append({title, id, version});
    }
    QList<GameTitle> titles = demos;
    QSet<quint64> seen;
    for (auto base : bases) {
        if (seen.contains(base.id)) continue;
        seen.insert(base.id);
        for (const auto &update : updates) {
            if ((update.id & 0xffffffffULL) == (base.id & 0xffffffffULL) && update.version > base.version) {
                base.title.rpxPath = update.title.rpxPath;
                base.title.version = update.title.version;
                base.version = update.version;
            }
        }
        titles.append(base.title);
    }
    std::sort(titles.begin(), titles.end(), [](const GameTitle &a, const GameTitle &b) { return a.name < b.name; });
    beginResetModel();
    m_titles = titles;
    const bool changed = m_searchPath != rootPath;
    m_searchPath = rootPath;
    endResetModel();
    if (changed) emit searchPathChanged();
    QSettings().setValue("library/directory", rootPath);
    if (!m_watcher->directories().isEmpty()) m_watcher->removePaths(m_watcher->directories());
    m_watcher->addPaths(roots);
}

int TitleScanner::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_titles.size(); }
QHash<int, QByteArray> TitleScanner::roleNames() const
{
    return {{NameRole, "name"}, {PublisherRole, "publisher"}, {VersionRole, "version"},
            {IconPathRole, "iconPath"}, {RpxPathRole, "rpxPath"}, {ContentPathRole, "contentPath"}, {TitleIdRole, "titleId"}};
}
QVariant TitleScanner::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_titles.size()) return {};
    const auto &title = m_titles[index.row()];
    switch (role) {
        case NameRole: return title.name;
        case PublisherRole: return title.publisher;
        case VersionRole: return title.version;
        case IconPathRole: return title.iconPath;
        case RpxPathRole: return title.rpxPath;
        case ContentPathRole: return title.contentPath;
        case TitleIdRole: return title.titleId;
        default: return {};
    }
}
