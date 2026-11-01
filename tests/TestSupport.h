#pragma once

#include "services/HistoryService.h"
#include "services/SearchService.h"
#include "services/SettingsService.h"
#include "storage/ClipboardRepository.h"
#include "storage/Database.h"
#include "storage/SettingsRepository.h"

#include <QTemporaryDir>
#include <QUuid>

#include <memory>

namespace clipvault::test {

// Throwaway stack that never touches the real history
class Fixture {
public:
    Fixture()
        : m_connectionName(QStringLiteral("clipvault-test-%1")
                               .arg(QUuid::createUuid().toString(QUuid::Id128)))
    {
        m_database = std::make_unique<Database>(databasePath(), m_connectionName);
        QString error;
        m_opened = m_database->open(&error);
        m_openError = error;
        if (!m_opened)
            return;
        build();
    }

    // Proves data survives a restart
    bool reopen()
    {
        m_search.reset();
        m_history.reset();
        m_settings.reset();
        m_settingsRepository.reset();
        m_repository.reset();
        m_database->close();
        m_database = std::make_unique<Database>(databasePath(), m_connectionName);
        QString error;
        if (!m_database->open(&error))
            return false;
        build();
        return true;
    }

    bool isOpen() const { return m_opened; }
    QString openError() const { return m_openError; }
    QString databasePath() const { return m_dir.filePath(QStringLiteral("clipvault-test.db")); }

    Database& database() { return *m_database; }
    ClipboardRepository& repository() { return *m_repository; }
    SettingsService& settings() { return *m_settings; }
    HistoryService& history() { return *m_history; }
    SearchService& search() { return *m_search; }

    // Bypasses the service so tests control timestamps
    qint64 insertRaw(const QString& content, const QDateTime& createdAt, bool pinned = false,
                     std::optional<QDateTime> expiresAt = std::nullopt)
    {
        ClipboardItem item;
        item.content = content;
        item.type = ClipboardType::Text;
        item.pinned = pinned;
        item.createdAt = createdAt;
        item.expiresAt = expiresAt;
        item.contentHash = HistoryService::hashOf(content);
        const auto stored = m_repository->upsert(item);
        return stored ? stored->id : -1;
    }

private:
    void build()
    {
        m_repository = std::make_unique<ClipboardRepository>(m_database->handle());
        m_settingsRepository = std::make_unique<SettingsRepository>(m_database->handle());
        m_settings = std::make_unique<SettingsService>(*m_settingsRepository);
        m_history = std::make_unique<HistoryService>(*m_repository, *m_settings);
        m_search = std::make_unique<SearchService>(*m_repository);
    }

    QTemporaryDir m_dir;
    QString m_connectionName;
    bool m_opened = false;
    QString m_openError;

    std::unique_ptr<Database> m_database;
    std::unique_ptr<ClipboardRepository> m_repository;
    std::unique_ptr<SettingsRepository> m_settingsRepository;
    std::unique_ptr<SettingsService> m_settings;
    std::unique_ptr<HistoryService> m_history;
    std::unique_ptr<SearchService> m_search;
};

} // namespace clipvault::test
