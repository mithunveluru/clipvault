#include "ApplicationController.h"

#include "../core/Logging.h"
#include "../platform/SingleInstance.h"
#include "TrayIcon.h"

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QMenu>
#include <QSystemTrayIcon>

namespace clipvault {
namespace {

// Retention sweeps are cheap so a slow timer is enough
constexpr int kMaintenanceIntervalMs = 5 * 60 * 1000;

} // namespace

ApplicationController::ApplicationController(platform::SingleInstance& instance, QObject* parent)
    : QObject(parent)
    , m_instance(instance)
{
}

ApplicationController::~ApplicationController()
{
    shutdown();
}

bool ApplicationController::initialize(const QString& databasePath, QString* error)
{
    m_database = std::make_unique<Database>(databasePath);
    if (!m_database->open(error)) {
        m_database.reset();
        return false;
    }

    m_repository = std::make_unique<ClipboardRepository>(m_database->handle());
    m_settingsRepository = std::make_unique<SettingsRepository>(m_database->handle());
    m_settings = std::make_unique<SettingsService>(*m_settingsRepository);
    m_history = std::make_unique<HistoryService>(*m_repository, *m_settings);
    m_search = std::make_unique<SearchService>(*m_repository);
    m_focus = platform::createFocusTracker();
    m_provenance = std::make_unique<ProvenanceService>(*m_focus, *m_settings, *m_history);
    m_monitor = std::make_unique<ClipboardMonitor>();
    m_hotkey = platform::createGlobalHotkey(m_instance);
    m_copyWarner = std::make_unique<SecretCopyWarner>(*m_focus, *m_settings);
    m_window = std::make_unique<MainWindow>(*m_history, *m_search, *m_settings);

    m_window->setDatabasePath(m_database->path());
    m_window->setProvenanceService(m_provenance.get());
    m_window->setHotkeyNote(m_hotkey->mechanismDescription());

    connect(m_monitor.get(), &ClipboardMonitor::textCaptured, this,
            [this](const QString& text) { m_provenance->capture(text); });
    connect(m_window.get(), &MainWindow::itemCopiedOut, m_provenance.get(),
            &ProvenanceService::noteCopiedBack);

    // expiry clears the clipboard only while it still holds that secret
    m_history->ephemeral().setClipboardClearer(
        [this](const QString& text) { m_monitor->clearIfHolds(text); });
    connect(m_monitor.get(), &ClipboardMonitor::textCaptured, m_copyWarner.get(),
            &SecretCopyWarner::disarm);
    connect(m_window.get(), &MainWindow::secretChosen, this, [this](qint64 id, const QString& kind) {
        const int window = id < 0 ? m_history->ephemeral().remainingSeconds(id)
                                  : m_settings->secretTtlSeconds();
        m_copyWarner->secretWrittenBack(kind, std::chrono::seconds(window));
    });
    connect(m_copyWarner.get(), &SecretCopyWarner::warning, this, [this](const QString& message) {
        m_window->flashStatus(message);
        if (m_tray && m_tray->isVisible())
            m_tray->showMessage(tr("Secret on the clipboard"), message, QSystemTrayIcon::Warning, 8000);
    });

    // Monitor marks the write as ours
    connect(m_window.get(), &MainWindow::itemChosen, this, [this](const QString& content) {
        m_monitor->writeBack(content);
        m_window->hide();
    });

    connect(m_window.get(), &MainWindow::pauseToggleRequested, this,
            &ApplicationController::togglePause);
    connect(m_window.get(), &MainWindow::quitRequested, qApp, &QApplication::quit);

    connect(m_hotkey.get(), &platform::GlobalHotkey::activated, this,
            &ApplicationController::showPopup);
    connect(m_hotkey.get(), &platform::GlobalHotkey::registrationFinished, this,
            &ApplicationController::onHotkeyRegistered);
    connect(&m_instance, &platform::SingleInstance::commandReceived, this,
            [this](const QString& command) {
                if (command == QLatin1String("open"))
                    showWindow();
            });

    connect(m_settings.get(), &SettingsService::changed, this,
            &ApplicationController::onSettingChanged);

    connect(&m_maintenanceTimer, &QTimer::timeout, this, [this] { m_history->runMaintenance(); });
    m_maintenanceTimer.setInterval(kMaintenanceIntervalMs);

    qCInfo(lcApp) << "services initialized";
    return true;
}

void ApplicationController::start(bool background)
{
    m_history->runMaintenance();
    m_maintenanceTimer.start();

    setPaused(m_settings->startPaused());
    m_startedInBackground = background;

    setupTray();
    // Reachability is settled when registration reports back
    registerHotkey();

    if (background) {
        qCInfo(lcApp) << "started in the background";
        return;
    }
    m_window->showNormalWindow();
}

void ApplicationController::registerHotkey()
{
    m_window->setHotkeyNote(m_hotkey->mechanismDescription());
    m_hotkey->registerHotkey(QKeySequence(m_settings->hotkey()));
}

void ApplicationController::onHotkeyRegistered(bool registered)
{
    m_hotkeyRegistered = registered;
    const bool reachable = registered || (m_tray && m_tray->isVisible());
    m_window->setBackgroundReachable(reachable);

    if (!registered)
        m_window->flashStatus(tr("Global shortcut unavailable \u2014 see Settings."));

    if (m_startedInBackground && !reachable) {
        qCWarning(lcApp) << "nothing can reopen the window so showing it now";
        m_window->showNormalWindow();
    }
}

void ApplicationController::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qCWarning(lcApp) << "system tray is not available on this desktop";
        return;
    }

    m_tray = std::make_unique<QSystemTrayIcon>(makeTrayIcon(m_monitor->isPaused()));
    m_tray->setToolTip(QStringLiteral("ClipVault"));

    m_trayMenu = std::make_unique<QMenu>();
    QMenu* menu = m_trayMenu.get();
    menu->addAction(tr("Open ClipVault"), this, &ApplicationController::showWindow);
    m_pauseAction = menu->addAction(tr("Pause Capture"), this, &ApplicationController::togglePause);
    menu->addSeparator();
    menu->addAction(tr("Settings…"), this, [this] {
        showWindow();
        m_window->openSettings();
    });
    menu->addAction(tr("Clear History…"), this, [this] {
        showWindow();
        m_window->confirmClearHistory();
    });
    menu->addSeparator();
    menu->addAction(tr("Quit ClipVault"), qApp, &QApplication::quit);

    m_tray->setContextMenu(menu);

    connect(m_tray.get(), &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    showPopup();
            });

    m_tray->show();
    setPaused(m_monitor->isPaused());
    qCInfo(lcApp) << "tray icon active";
}

void ApplicationController::togglePause()
{
    setPaused(!m_monitor->isPaused());
}

void ApplicationController::setPaused(bool paused)
{
    m_monitor->setPaused(paused);
    m_window->setCapturePaused(paused);
    if (m_pauseAction)
        m_pauseAction->setText(paused ? tr("Resume Capture") : tr("Pause Capture"));
    if (m_tray) {
        m_tray->setIcon(makeTrayIcon(paused));
        m_tray->setToolTip(paused ? tr("ClipVault — paused") : tr("ClipVault — capturing"));
    }
}

void ApplicationController::onSettingChanged(const QString& key)
{
    if (key == QLatin1String("retention_seconds")) {
        m_history->applyRetentionChange();
    } else if (key == QLatin1String("max_items")) {
        m_history->runMaintenance();
    } else if (key == QLatin1String("hotkey")) {
        // Rewrites the existing binding so no removal step is needed
        registerHotkey();
    }
}

void ApplicationController::showPopup()
{
    if (m_window)
        m_window->showAsPopup();
}

void ApplicationController::showWindow()
{
    if (m_window)
        m_window->showNormalWindow();
}

void ApplicationController::shutdown()
{
    if (m_shutdownDone)
        return;
    m_shutdownDone = true;

    m_maintenanceTimer.stop();
    // Shortcut released by the hotkey destructor not unregistered
    if (m_monitor)
        m_monitor->setPaused(true);
    if (m_tray)
        m_tray->hide();

    // Reverse dependency order UI platform services storage
    m_tray.reset();
    m_trayMenu.reset();
    m_window.reset();
    m_hotkey.reset();
    m_copyWarner.reset();
    m_monitor.reset();
    m_provenance.reset();
    m_focus.reset();
    m_search.reset();
    m_history.reset();
    m_settings.reset();
    m_settingsRepository.reset();
    m_repository.reset();
    if (m_database)
        m_database->close();
    m_database.reset();

    qCInfo(lcApp) << "shutdown complete";
}

} // namespace clipvault
