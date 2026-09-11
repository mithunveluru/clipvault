#pragma once

#include "GlobalHotkey.h"

namespace clipvault::platform {

class SingleInstance;

// Registers a desktop keybinding through gsettings
class GnomeGlobalHotkey : public GlobalHotkey {
    Q_OBJECT
public:
    explicit GnomeGlobalHotkey(SingleInstance& instance, QObject* parent = nullptr);
    ~GnomeGlobalHotkey() override;

    void registerHotkey(const QKeySequence& sequence) override;
    void unregisterHotkey() override;
    QString mechanismDescription() const override;

    // True when gsettings and the media keys schema exist
    static bool isSupported();

    // Maps Ctrl+Shift+V to <Control><Shift>v
    static QString toGnomeBinding(const QKeySequence& sequence);

private:
    SingleInstance& m_instance;
    bool m_registered = false;
};

} // namespace clipvault::platform
