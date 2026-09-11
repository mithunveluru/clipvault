#pragma once

#include "../storage/SettingsRepository.h"

#include <QList>
#include <QObject>
#include <QHash>
#include <QString>

namespace clipvault {

// Typed cached access to the settings table
class SettingsService : public QObject {
    Q_OBJECT
public:
    struct RetentionOption {
        QString label;
        int seconds; // 0 means never
    };

    explicit SettingsService(SettingsRepository& repository, QObject* parent = nullptr);

    static QList<RetentionOption> retentionOptions();

    int retentionSeconds() const;
    void setRetentionSeconds(int seconds);

    int maxItems() const;
    void setMaxItems(int maxItems);

    bool startPaused() const;
    void setStartPaused(bool paused);

    QString hotkey() const;
    void setHotkey(const QString& sequence);

    bool launchOnStartup() const;
    void setLaunchOnStartup(bool enabled);

    static constexpr int kDefaultRetentionSeconds = 7 * 24 * 60 * 60;
    static constexpr int kDefaultMaxItems = 1000;
    static constexpr const char* kDefaultHotkey = "Ctrl+Shift+V";

signals:
    void changed(const QString& key);

private:
    QString read(const QString& key, const QString& fallback) const;
    void write(const QString& key, const QString& value);

    SettingsRepository& m_repository;
    mutable QHash<QString, QString> m_cache;
};

} // namespace clipvault
