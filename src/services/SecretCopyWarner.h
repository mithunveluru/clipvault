#pragma once

#include "../platform/FocusTracker.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>

namespace clipvault {

class SettingsService;

// best effort only because the desktop never says when a paste happens so focus changes stand in for it
class SecretCopyWarner : public QObject {
    Q_OBJECT
public:
    SecretCopyWarner(
        platform::FocusTracker& focus, const SettingsService& settings, QObject* parent = nullptr);

    // a secret was just written to the clipboard and stays risky for the given window
    void secretWrittenBack(const QString& kind, std::chrono::seconds window);
    // the clipboard moved on so focus changes no longer matter
    void disarm();
    [[nodiscard]] bool isArmed() const { return m_armed; }

signals:
    // content free text for a non modal status message
    void warning(const QString& message);

private:
    void onFocusChanged(const platform::WindowInfo& window);
    [[nodiscard]] bool isRisky(const platform::WindowInfo& window) const;

    const SettingsService& m_settings;
    QTimer m_window;
    QString m_kind;
    bool m_armed = false;
};

} // namespace clipvault
