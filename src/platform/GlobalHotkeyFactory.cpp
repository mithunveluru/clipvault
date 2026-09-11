#include "GlobalHotkey.h"

#include "../core/Logging.h"
#include "GnomeGlobalHotkey.h"
#include "SingleInstance.h"

#include <QGuiApplication>

namespace clipvault::platform {
namespace {

// Used when the session offers no shortcut mechanism
class NullGlobalHotkey final : public GlobalHotkey {
public:
    explicit NullGlobalHotkey(SingleInstance& instance, QObject* parent = nullptr)
        : GlobalHotkey(parent)
    {
        // A user bound clipvault --show still arrives here
        connect(&instance, &SingleInstance::commandReceived, this, [this](const QString& command) {
            if (command == QLatin1String("show"))
                emit activated();
        });
    }

    void registerHotkey(const QKeySequence&) override { emit registrationFinished(false); }
    void unregisterHotkey() override {}

    QString mechanismDescription() const override
    {
        return tr("This desktop session does not expose a way to register a global "
                  "shortcut. Open ClipVault from the tray icon, or bind a shortcut "
                  "yourself to: clipvault --show");
    }
};

} // namespace

std::unique_ptr<GlobalHotkey> createGlobalHotkey(SingleInstance& instance, QObject* parent)
{
    if (GnomeGlobalHotkey::isSupported()) {
        qCInfo(lcHotkey) << "using the GNOME desktop shortcut mechanism";
        return std::make_unique<GnomeGlobalHotkey>(instance, parent);
    }

    qCWarning(lcHotkey) << "no global shortcut mechanism available for platform"
                        << QGuiApplication::platformName();
    return std::make_unique<NullGlobalHotkey>(instance, parent);
}

} // namespace clipvault::platform
