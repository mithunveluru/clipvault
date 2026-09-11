#pragma once

#include <QSqlDatabase>
#include <QString>

namespace clipvault {

// Owns one SQLite connection and migrates it on open
class Database {
public:
    explicit Database(QString path, QString connectionName = QStringLiteral("clipvault"));
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Fills error with a message fit for the user
    bool open(QString* error);
    void close();

    bool isOpen() const;
    QSqlDatabase handle() const;
    QString path() const { return m_path; }

    static constexpr int kSchemaVersion = 1;

    // Per user data directory never the source tree
    static QString defaultPath();

private:
    bool migrate(QString* error);

    QString m_path;
    QString m_connectionName;
    bool m_open = false;
};

} // namespace clipvault
