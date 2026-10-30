#pragma once
#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QString>
#include <QList>
#include <QThreadPool>
#include <QTimer>
#include <atomic>

class TitleScanner : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString searchPath READ searchPath NOTIFY searchPathChanged)

public:
    struct GameTitle {
        QString name;
        QString publisher;
        QString version;
        QString iconPath;
        QString rpxPath;
        QString contentPath;
        QString titleId;
        QStringList sizePaths;
        qint64 sizeBytes = -1; // -1 until the background measurement finishes.
    };

    enum Roles {
        NameRole = Qt::UserRole + 1,
        PublisherRole,
        VersionRole,
        IconPathRole,
        RpxPathRole,
        ContentPathRole,
        TitleIdRole,
        SizeBytesRole,
    };

    explicit TitleScanner(QObject *parent = nullptr);
    ~TitleScanner() override;

    static QString defaultLibraryPath(const QString &applicationDirectory, const QString &workingDirectory);

    // mandatory overrides for QAbstractListModel
    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Command-line/test overrides must not replace the user's saved library.
    Q_INVOKABLE void scanDirectory(const QString &path, bool rememberLibrary = true);
    // The row of the game with this program, or -1.
    Q_INVOKABLE [[nodiscard]] int indexOfGame(const QString &rpxPath) const;
    Q_INVOKABLE void refresh() { scanDirectory(m_searchPath, m_rememberLibrary); }

    [[nodiscard]] QString searchPath() const { return m_searchPath; }

signals:
    void searchPathChanged();

private:
    void directoryChanged(const QString &path);
    void measureSizes();
    void applySize(const QString &key, qint64 bytes);

    QList<GameTitle>     m_titles;
    QString              m_searchPath;
    bool                 m_rememberLibrary = true;
    QFileSystemWatcher  *m_watcher;
    QTimer               m_rescan;
    QHash<QString, qint64> m_sizes;
    QThreadPool          m_sizePool;
    std::atomic_bool     m_stopMeasuring = false;
};
