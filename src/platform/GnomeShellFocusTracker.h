#pragma once

#include "FocusTracker.h"

#include <memory>

class QDBusError;
class QDBusServiceWatcher;

namespace clipvault::platform {

// talks to the clipvault focus gnome shell extension over the session bus and sees native
// wayland windows that x11 cannot
class GnomeShellFocusTracker final : public FocusTracker {
    Q_OBJECT
public:
    static constexpr auto kService = "io.github.mithunveluru.ClipVault.Focus";
    static constexpr auto kPath = "/io/github/mithunveluru/ClipVault/Focus";
    static constexpr auto kInterface = "io.github.mithunveluru.ClipVault.Focus";

    // true when the extension currently owns its name on the session bus
    [[nodiscard]] static bool isAvailable();

    explicit GnomeShellFocusTracker(QObject* parent = nullptr);
    ~GnomeShellFocusTracker() override;

    [[nodiscard]] WindowInfo current() const override { return m_current; }
    [[nodiscard]] WindowInfo lastForeignWindow() const override { return m_lastForeign; }
    bool activate(const WindowInfo& window) override;
    [[nodiscard]] QString mechanismDescription() const override;

private slots:
    void onFocusChanged(const QString& appId, const QString& wmClass, const QString& title, qlonglong pid,
        qulonglong windowId);
    void onActivated(bool activated);
    void onCallFailed(const QDBusError& error);

private:
    void query();
    void update(const WindowInfo& window);

    std::unique_ptr<QDBusServiceWatcher> m_watcher;
    WindowInfo m_current;
    WindowInfo m_lastForeign;
};

} // namespace clipvault::platform
