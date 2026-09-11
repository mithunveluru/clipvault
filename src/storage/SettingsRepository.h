#pragma once

#include <QSqlDatabase>
#include <QString>

namespace clipvault {

// Key value persistence for the settings table
class SettingsRepository {
public:
    // A failed query must never look like an unset key
    enum class Lookup { Found, Missing, Failed };

    explicit SettingsRepository(QSqlDatabase db);

    Lookup value(const QString& key, QString* out) const;
    bool setValue(const QString& key, const QString& value);

private:
    QSqlDatabase m_db;
};

} // namespace clipvault
