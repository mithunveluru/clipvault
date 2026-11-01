#pragma once

#include "../clipboard/ClipboardMonitor.h"
#include "../platform/FocusTracker.h"
#include "../platform/GlobalHotkey.h"
#include "../services/HistoryService.h"
#include "../services/SecretCopyWarner.h"
#include "../services/ProvenanceService.h"
#include "../services/SearchService.h"
#include "../services/SettingsService.h"
#include "../storage/ClipboardRepository.h"
#include "../storage/Database.h"
#include "../storage/SettingsRepository.h"
#include "../ui/MainWindow.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace clipvault {

namespace platform {
class SingleInstance;
}

// Owns the object graph declared in dependency order
class ApplicationController : public QObject {
    Q_OBJECT
public:
    explicit ApplicationController(platform::SingleInstance& instance, QObject* parent = nullptr);
    ~ApplicationController() override;

    // Fills error with a message fit for the user
    bool initialize(const QString& databasePath, QString* error);

    // Background keeps the window hidden on launch
    void start(bool background);

    void shutdown();

    void showPopup();
    void showWindow();

private:
    void setupTray();
    void togglePause();
    void setPaused(bool paused);
    void onSettingChanged(const QString& key);
    void registerHotkey();
    void onHotkeyRegistered(bool registered);

    platform::SingleInstance& m_instance;

    std::unique_ptr<Database> m_database;
    std::unique_ptr<ClipboardRepository> m_repository;
    std::unique_ptr<SettingsRepository> m_settingsRepository;
    std::unique_ptr<SettingsService> m_settings;
    std::unique_ptr<HistoryService> m_history;
    std::unique_ptr<SearchService> m_search;
    std::unique_ptr<platform::FocusTracker> m_focus;
    std::unique_ptr<ProvenanceService> m_provenance;
    std::unique_ptr<ClipboardMonitor> m_monitor;
    std::unique_ptr<platform::GlobalHotkey> m_hotkey;
    std::unique_ptr<SecretCopyWarner> m_copyWarner;
    std::unique_ptr<MainWindow> m_window;
    std::unique_ptr<QSystemTrayIcon> m_tray;
    // Unparented or a widget parent lays it out inline
    std::unique_ptr<QMenu> m_trayMenu;

    QAction* m_pauseAction = nullptr;
    QTimer m_maintenanceTimer;
    bool m_hotkeyRegistered = false;
    bool m_startedInBackground = false;
    bool m_shutdownDone = false;
};

} // namespace clipvault
