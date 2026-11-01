#include "FocusTracker.h"

#include "../core/Logging.h"

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
    qCInfo(lcFocus) << "no focus tracking mechanism available";
    return std::make_unique<NullFocusTracker>(parent);
}

} // namespace clipvault::platform
