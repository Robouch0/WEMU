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
    };

    enum Roles {
        NameRole = Qt::UserRole + 1,
        PublisherRole,
        VersionRole,
        IconPathRole,
        RpxPathRole,
    };

    explicit TitleScanner(QObject *parent = nullptr);

    // mandatory overrides for QAbstractListModel
    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void scanDirectory(const QString &path);

    [[nodiscard]] QString searchPath() const { return m_searchPath; }

signals:
    void searchPathChanged();

private:
    QList<GameTitle>     m_titles;
    QString              m_searchPath;
    QFileSystemWatcher  *m_watcher;
};
