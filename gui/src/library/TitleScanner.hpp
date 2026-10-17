#pragma once
#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QString>
#include <QList>

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
    };

    enum Roles {
        NameRole = Qt::UserRole + 1,
        PublisherRole,
        VersionRole,
        IconPathRole,
        RpxPathRole,
        ContentPathRole,
        TitleIdRole,
    };

    explicit TitleScanner(QObject *parent = nullptr);

    static QString defaultLibraryPath(const QString &applicationDirectory, const QString &workingDirectory);

    // mandatory overrides for QAbstractListModel
    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Command-line/test overrides must not replace the user's saved library.
    Q_INVOKABLE void scanDirectory(const QString &path, bool rememberLibrary = true);
    Q_INVOKABLE void refresh() { scanDirectory(m_searchPath, m_rememberLibrary); }

    [[nodiscard]] QString searchPath() const { return m_searchPath; }

signals:
    void searchPathChanged();

private:
    QList<GameTitle>     m_titles;
    QString              m_searchPath;
    bool                 m_rememberLibrary = true;
    QFileSystemWatcher  *m_watcher;
};
