#include "TitleScanner.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <QDebug>
#include <stdexcept>

void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "fixture open failed");
    require(file.write(bytes) == bytes.size(), "fixture write failed");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("WEMU-tests");
    app.setApplicationName("library");
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    try {
        const auto root = temporary.path() + "/library with spaces";
        auto title = [&](const QString &folder, const QString &id, const QString &version, const QString &name) {
            const auto dir = root + "/" + folder;
            for (const auto &part : {"code", "content", "meta"}) require(QDir().mkpath(dir + "/" + part), "mkdir failed");
            write(dir + "/code/program.rpx", "fixture");
            write(dir + "/code/app.xml", ("<app><title_id>" + id + "</title_id><title_version>" + version + "</title_version></app>").toUtf8());
            write(dir + "/meta/meta.xml", ("<menu><title_id>0005000011110001</title_id><longname_en>" + name + "</longname_en></menu>").toUtf8());
            return dir;
        };
        const auto base = title("arbitrary base", "0005000011110001", "0000", "First title");
        title("old update", "0005000e11110001", "00ff", "Wrong display name");
        const auto update = title("renamed update", "0005000E11110001", "0100", "Wrong display name");
        title("orphan", "0005000e11110002", "0001", "Orphan");
        title("dlc", "0005000c11110001", "0001", "DLC");
        title("second region", "0005000011110003", "0000", "Second title");
        const auto broken = title("bad xml", "0005000011110004", "0000", "Broken");
        write(broken + "/code/app.xml", "<app><title_id>bad");
        TitleScanner scanner;
        scanner.scanDirectory(QUrl::fromLocalFile(root).toString());
        require(scanner.rowCount({}) == 2, "base/update deduplication or orphan filtering failed");
        const auto first = scanner.index(0, 0);
        require(scanner.data(first, TitleScanner::NameRole) == "First title", "update replaced display metadata");
        require(scanner.data(first, TitleScanner::RpxPathRole) == update + "/code/program.rpx", "highest numeric update not selected");
        require(scanner.data(first, TitleScanner::ContentPathRole) == base + "/content", "base content overlay lost");
        require(scanner.data(first, TitleScanner::VersionRole) == "256", "hex version incorrectly parsed");
        require(QSettings().value("library/directory") == root, "library was not persisted");
        scanner.refresh();
        require(scanner.rowCount({}) == 2, "refresh duplicated titles");
        scanner.scanDirectory(base, false);
        require(scanner.rowCount({}) == 1, "temporary library override was not scanned");
        require(scanner.searchPath() == base, "temporary library override path incorrect");
        require(QSettings().value("library/directory") == root, "temporary override replaced saved library");
        scanner.refresh();
        require(scanner.rowCount({}) == 1, "temporary library refresh changed its selection");
        require(QSettings().value("library/directory") == root, "refresh persisted temporary library override");
        scanner.scanDirectory(base);
        require(QSettings().value("library/directory") == base, "explicit library selection stopped persisting");
        require(scanner.rowCount({}) == 1, "direct game folder not supported");
        require(scanner.data(scanner.index(0, 0), TitleScanner::RpxPathRole) == base + "/code/program.rpx", "base-only launch wrong");
        scanner.scanDirectory(root + "/orphan");
        require(scanner.rowCount({}) == 0, "orphan update exposed as base game");
        const auto demo = root + "/legacy demo";
        require(QDir().mkpath(demo + "/code") && QDir().mkpath(demo + "/meta"), "demo mkdir failed");
        write(demo + "/code/legacy.rpx", "fixture");
        write(demo + "/code/legacy.elf", "ignored fixture");
        write(demo + "/meta/meta.xml", "<menu><longname_en>Legacy demo</longname_en><title_version>1.0</title_version></menu>");
        write(root + "/bare.rpx", "fixture");
        const auto multiple = root + "/multiple demos";
        require(QDir().mkpath(multiple), "multiple demos mkdir failed");
        write(multiple + "/first.rpx", "fixture");
        write(multiple + "/second.rpx", "fixture");
        const auto emptyAppDemo = root + "/demo without title id";
        require(QDir().mkpath(emptyAppDemo + "/code"), "idless app mkdir failed");
        write(emptyAppDemo + "/code/valid.rpx", "fixture");
        write(emptyAppDemo + "/code/app.xml", "<app><name>homebrew</name></app>");
        title("invalid id", "not-a-title-id", "0000", "Invalid ID");
        const auto missingContent = title("missing commercial content", "0005000011110005", "0000", "No content");
        require(QDir(missingContent + "/content").removeRecursively(), "content removal failed");
        scanner.scanDirectory(root);
        require(scanner.rowCount({}) == 7, "commercial titles and RPX demos were not combined correctly");
        bool foundDemo = false, foundBare = false, foundFirst = false, foundSecond = false, foundIdless = false;
        for (int row = 0; row < scanner.rowCount({}); ++row) {
            const auto item = scanner.index(row, 0);
            const auto name = scanner.data(item, TitleScanner::NameRole).toString();
            if (name == "Legacy demo") {
                foundDemo = true;
                require(scanner.data(item, TitleScanner::RpxPathRole) == demo + "/code/legacy.rpx", "demo executable incorrect");
                require(scanner.data(item, TitleScanner::ContentPathRole) == demo, "demo without content directory cannot launch");
                require(scanner.data(item, TitleScanner::VersionRole) == "1.0", "demo version metadata lost");
            }
            foundBare |= name == "bare";
            foundFirst |= name == "first";
            foundSecond |= name == "second";
            foundIdless |= name == "valid";
        }
        require(foundDemo && foundBare && foundFirst && foundSecond && foundIdless, "demo metadata or filename fallback failed");
        scanner.scanDirectory(demo);
        require(scanner.rowCount({}) == 1, "direct demo folder duplicates code directory entries");
        scanner.scanDirectory(root + "/orphan");
        require(scanner.rowCount({}) == 0, "orphan update was reclassified as a demo");
        scanner.scanDirectory(root + "/dlc");
        require(scanner.rowCount({}) == 0, "DLC was reclassified as a demo");

        const auto checkout = temporary.path() + "/checkout";
        const auto binaryDirectory = checkout + "/build/integration/gui";
        const auto gamesDirectory = checkout + "/games";
        const auto elsewhere = temporary.path() + "/elsewhere";
        require(QDir().mkpath(binaryDirectory) && QDir().mkpath(gamesDirectory) && QDir().mkpath(elsewhere), "discovery fixture failed");
        require(TitleScanner::defaultLibraryPath(binaryDirectory, elsewhere) == gamesDirectory, "nested build cannot find repository games");
        require(TitleScanner::defaultLibraryPath(elsewhere, checkout) == gamesDirectory, "working-directory discovery failed");
        qInfo() << "Library discovery, commercial update pairing, RPX demos, metadata, persistence and refresh passed";
        return 0;
    } catch (const std::exception &e) { qCritical() << e.what(); return 1; }
}
