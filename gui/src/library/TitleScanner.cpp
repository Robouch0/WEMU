#include "TitleScanner.hpp"
#include <QDir>
#include <QDirIterator>
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
constexpr int rescanDelayMs = 500;

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

qint64 diskSize(const QStringList &paths, const std::atomic_bool &stop)
{
    qint64 total = 0;
    for (const auto &path : paths) {
        const QFileInfo info(path);
        if (info.isFile()) total += info.size();
        QDirIterator files(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (files.hasNext() && !stop) {
            files.next();
            total += files.fileInfo().size();
        }
    }
    return total;
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
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &TitleScanner::directoryChanged);
    // Copying a game touches its folders many times; rescan once it settles.
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(rescanDelayMs);
    connect(&m_rescan, &QTimer::timeout, this, &TitleScanner::refresh);
    m_sizePool.setMaxThreadCount(1);
}

TitleScanner::~TitleScanner()
{
    m_stopMeasuring = true;
    m_sizePool.clear();
    m_sizePool.waitForDone();
}

void TitleScanner::scanDirectory(const QString &path, bool rememberLibrary)
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
                // Several programs can share a folder, and a loose RPX sits in the library root itself.
                title.sizePaths = {executables.size() == 1 && dir != rootPath ? dir : canonical};
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
        title.sizePaths = {dir};
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
                base.title.sizePaths = QStringList{base.title.sizePaths.first(), update.title.sizePaths.first()};
            }
        }
        titles.append(base.title);
    }
    std::sort(titles.begin(), titles.end(), [](const GameTitle &a, const GameTitle &b) { return a.name < b.name; });
    for (auto &title : titles) title.sizeBytes = m_sizes.value(title.sizePaths.join('\n'), -1);
    beginResetModel();
    m_titles = titles;
    const bool changed = m_searchPath != rootPath;
    m_searchPath = rootPath;
    m_rememberLibrary = rememberLibrary;
    endResetModel();
    if (changed) emit searchPathChanged();
    if (rememberLibrary) QSettings().setValue("library/directory", rootPath);
    if (!m_watcher->directories().isEmpty()) m_watcher->removePaths(m_watcher->directories());
    // A game only becomes valid once code/ and meta/ are complete, so those need watching too.
    QStringList watched = roots;
    for (const auto &dir : roots) {
        for (const auto &part : {"/code", "/meta"})
            if (QDir(dir + part).exists()) watched.append(dir + part);
    }
    m_watcher->addPaths(watched);
    measureSizes();
}

void TitleScanner::directoryChanged(const QString &path)
{
    for (auto size = m_sizes.begin(); size != m_sizes.end();) {
        const auto paths = size.key().split('\n');
        const bool stale = std::any_of(paths.begin(), paths.end(), [&](const QString &root) { return path.startsWith(root); });
        size = stale ? m_sizes.erase(size) : std::next(size);
    }
    m_rescan.start();
}

void TitleScanner::measureSizes()
{
    m_sizePool.clear();
    for (const auto &title : std::as_const(m_titles)) {
        if (title.sizeBytes >= 0) continue;
        m_sizePool.start([this, paths = title.sizePaths] {
            const auto bytes = diskSize(paths, m_stopMeasuring);
            if (m_stopMeasuring) return;
            QMetaObject::invokeMethod(this, [this, key = paths.join('\n'), bytes] { applySize(key, bytes); }, Qt::QueuedConnection);
        });
    }
}

void TitleScanner::applySize(const QString &key, qint64 bytes)
{
    m_sizes.insert(key, bytes);
    for (int row = 0; row < m_titles.size(); ++row) {
        if (m_titles[row].sizePaths.join('\n') != key) continue;
        m_titles[row].sizeBytes = bytes;
        const auto changed = index(row);
        emit dataChanged(changed, changed, {SizeBytesRole});
    }
}

int TitleScanner::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_titles.size(); }
QHash<int, QByteArray> TitleScanner::roleNames() const
{
    return {{NameRole, "name"}, {PublisherRole, "publisher"}, {VersionRole, "version"},
            {IconPathRole, "iconPath"}, {RpxPathRole, "rpxPath"}, {ContentPathRole, "contentPath"}, {TitleIdRole, "titleId"},
            {SizeBytesRole, "sizeBytes"}};
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
        case SizeBytesRole: return title.sizeBytes;
        default: return {};
    }
}
