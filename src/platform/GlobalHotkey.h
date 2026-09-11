#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>

#include <memory>

namespace clipvault::platform {

class SingleInstance;

// Desktop wide shortcut with the mechanism kept behind this interface
class GlobalHotkey : public QObject {
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject* parent = nullptr) : QObject(parent) {}
    ~GlobalHotkey() override = default;

    // Asynchronous because a desktop may take seconds to answer
    virtual void registerHotkey(const QKeySequence& sequence) = 0;
    virtual void unregisterHotkey() = 0;

    // One line for the settings dialog
    virtual QString mechanismDescription() const = 0;

signals:
    void activated();
    void registrationFinished(bool registered);
};

// Picks the implementation that works on the running session
std::unique_ptr<GlobalHotkey> createGlobalHotkey(SingleInstance& instance, QObject* parent = nullptr);

} // namespace clipvault::platform
