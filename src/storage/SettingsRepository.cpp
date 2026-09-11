#include "SettingsRepository.h"

#include "../core/Logging.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace clipvault {

SettingsRepository::SettingsRepository(QSqlDatabase db)
    : m_db(std::move(db))
{
}

SettingsRepository::Lookup SettingsRepository::value(const QString& key, QString* out) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec()) {
        qCCritical(lcDb) << "settings read failed:" << query.lastError().text();
        return Lookup::Failed;
    }
    if (!query.next())
        return Lookup::Missing;
    if (out)
        *out = query.value(0).toString();
    return Lookup::Found;
}

bool SettingsRepository::setValue(const QString& key, const QString& value)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO settings (key, value) VALUES (?, ?)"
        " ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(key);
    query.addBindValue(value);
    if (!query.exec()) {
        qCCritical(lcDb) << "settings write failed:" << query.lastError().text();
        return false;
    }
    return true;
}

} // namespace clipvault
