#include "FocusTracker.h"

#include "../core/Logging.h"
#include "GnomeShellFocusTracker.h"
#ifdef CLIPVAULT_HAVE_XCB
#include "X11FocusTracker.h"
#endif

#include <QGuiApplication>

namespace clipvault::platform {
namespace {

class NullFocusTracker final : public FocusTracker {
public:
    using FocusTracker::FocusTracker;

    [[nodiscard]] WindowInfo current() const override { return { }; }
    [[nodiscard]] WindowInfo lastForeignWindow() const override { return { }; }
    bool activate(const WindowInfo&) override { return false; }
    [[nodiscard]] QString mechanismDescription() const override
    {
        return tr("This session does not tell ClipVault which window is focused, so clips are "
                  "recorded without a source app.");
    }
};

} // namespace

std::unique_ptr<FocusTracker> createFocusTracker(QObject* parent)
{
    if (GnomeShellFocusTracker::isAvailable()) {
        qCInfo(lcFocus) << "focus from the gnome shell extension";
        return std::make_unique<GnomeShellFocusTracker>(parent);
    }
#ifdef CLIPVAULT_HAVE_XCB
    const bool xcb = qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr
        && QGuiApplication::platformName() == QLatin1String("xcb");
    if (xcb) {
        if (auto tracker = X11FocusTracker::create(parent)) {
            qCInfo(lcFocus) << "focus from x11";
            return tracker;
        }
    }
#endif
    qCInfo(lcFocus) << "no focus tracking mechanism available";
    return std::make_unique<NullFocusTracker>(parent);
}

} // namespace clipvault::platform
