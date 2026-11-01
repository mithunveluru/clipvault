#pragma once

#include "platform/FocusTracker.h"

namespace clipvault::test {

// tests set the focused window by hand
class FakeFocusTracker final : public platform::FocusTracker {
public:
    using FocusTracker::FocusTracker;

    void setFocused(const platform::WindowInfo& window, bool foreign = true)
    {
        m_current = window;
        if (foreign)
            m_lastForeign = window;
        emit focusChanged(window);
    }

    platform::WindowInfo current() const override { return m_current; }
    platform::WindowInfo lastForeignWindow() const override { return m_lastForeign; }
    bool activate(const platform::WindowInfo& window) override
    {
        m_activated = window;
        return true;
    }
    [[nodiscard]] QString mechanismDescription() const override { return QStringLiteral("fake"); }

    platform::WindowInfo activated() const { return m_activated; }

private:
    platform::WindowInfo m_current;
    platform::WindowInfo m_lastForeign;
    platform::WindowInfo m_activated;
};

} // namespace clipvault::test
