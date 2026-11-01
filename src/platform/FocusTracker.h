#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace clipvault::platform {

struct WindowInfo {
    // desktop file id or wm class instance shown to the user
    QString app;
    QString windowClass;
    // only filled when the tracker can see it and callers decide whether to keep it
    QString title;
    // transient and never persisted
    qint64 pid = 0;
    // tracker specific handle for activate and zero when unknown
    quint64 windowId = 0;

    [[nodiscard]] bool isValid() const { return !app.isEmpty() || !windowClass.isEmpty(); }
};

// reports the focused top level window with the mechanism kept behind this interface
class FocusTracker : public QObject {
    Q_OBJECT
public:
    explicit FocusTracker(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~FocusTracker() override = default;

    [[nodiscard]] virtual WindowInfo current() const = 0;
    // most recent focused window that is not clipvault itself
    [[nodiscard]] virtual WindowInfo lastForeignWindow() const = 0;
    // raises the window and returns false when the mechanism cannot
    virtual bool activate(const WindowInfo& window) = 0;
    // one line for the settings dialog saying how and how well focus is seen
    [[nodiscard]] virtual QString mechanismDescription() const = 0;

signals:
    void focusChanged(const clipvault::platform::WindowInfo& window);
};

// extension over dbus then x11 then null
std::unique_ptr<FocusTracker> createFocusTracker(QObject* parent = nullptr);

} // namespace clipvault::platform
