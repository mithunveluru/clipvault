#pragma once

#include "../storage/SettingsRepository.h"

#include <QList>
#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>

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

    bool secretRadarEnabled() const;
    void setSecretRadarEnabled(bool enabled);

    // clamped to the 10 s to 5 min range
    int secretTtlSeconds() const;
    void setSecretTtlSeconds(int seconds);

    bool secretPinnedNeverExpire() const;
    void setSecretPinnedNeverExpire(bool enabled);

    bool secretCopyBackWarning() const;
    void setSecretCopyBackWarning(bool enabled);

    // lowercase app ids or classes
    QStringList secretRiskyApps() const;
    void setSecretRiskyApps(const QStringList& apps);

    bool provenanceEnabled() const;
    void setProvenanceEnabled(bool enabled);

    bool provenanceTitles() const;
    void setProvenanceTitles(bool enabled);

    QStringList provenanceBlocklist() const;
    void setProvenanceBlocklist(const QStringList& apps);

    bool semanticEnabled() const;
    void setSemanticEnabled(bool enabled);

    // folder holding model.onnx and vocab.txt and empty until the user picks one
    QString semanticModelDir() const;
    void setSemanticModelDir(const QString& dir);

    static constexpr int kDefaultSecretTtlSeconds = 45;
    static constexpr int kMinSecretTtlSeconds = 10;
    static constexpr int kMaxSecretTtlSeconds = 5 * 60;
    static constexpr const char* kDefaultSecretRiskyApps = "slack,discord,signal,telegram,teams,zoom,element";
    static constexpr const char* kDefaultProvenanceBlocklist
        = "keepassxc,1password,bitwarden,org.gnome.world.secrets,seahorse";

    static constexpr int kDefaultRetentionSeconds = 7 * 24 * 60 * 60;
    static constexpr int kDefaultMaxItems = 1000;
    static constexpr const char* kDefaultHotkey = "Ctrl+Shift+V";

signals:
    void changed(const QString& key);

private:
    QString read(const QString& key, const QString& fallback) const;
    void write(const QString& key, const QString& value);
    bool readFlag(const char* key, bool fallback) const;
    void writeFlag(const char* key, bool value);
    QStringList readList(const char* key, const char* fallback) const;
    void writeList(const char* key, const QStringList& values);

    SettingsRepository& m_repository;
    mutable QHash<QString, QString> m_cache;
};

} // namespace clipvault
