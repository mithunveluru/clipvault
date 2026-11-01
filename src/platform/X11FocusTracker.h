#pragma once

#include "FocusTracker.h"

#include <cstdint>
#include <memory>

struct xcb_connection_t;
class QSocketNotifier;

namespace clipvault::platform {

// follows the active window through ewmh on its own x connection so the root event mask qt
// selected on its connection stays untouched
class X11FocusTracker final : public FocusTracker {
public:
    struct ConnectionDeleter {
        void operator()(xcb_connection_t* connection) const;
    };
    using Connection = std::unique_ptr<xcb_connection_t, ConnectionDeleter>;

    // null when no x server is reachable
    [[nodiscard]] static std::unique_ptr<X11FocusTracker> create(QObject* parent = nullptr);

    X11FocusTracker(Connection connection, int screen, QObject* parent = nullptr);
    ~X11FocusTracker() override;

    [[nodiscard]] WindowInfo current() const override { return m_current; }
    [[nodiscard]] WindowInfo lastForeignWindow() const override { return m_lastForeign; }
    bool activate(const WindowInfo& window) override;
    [[nodiscard]] QString mechanismDescription() const override;

private:
    struct Drain {
        bool changed = false;
        bool more = false;
    };
    [[nodiscard]] Drain drainEvents();
    void scheduleDrain();
    void scheduleRefresh();
    void readEvents();
    void refresh();
    [[nodiscard]] std::uint32_t activeWindow() const;
    [[nodiscard]] WindowInfo describe(std::uint32_t window) const;
    void watch(std::uint32_t window);

    Connection m_connection;
    // declared after the connection so it goes away before the socket closes
    std::unique_ptr<QSocketNotifier> m_notifier;
    bool m_drainQueued = false;
    bool m_refreshQueued = false;
    std::uint32_t m_root = 0;
    std::uint32_t m_watched = 0;
    std::uint32_t m_netActiveWindow = 0;
    std::uint32_t m_netWmName = 0;
    std::uint32_t m_netWmPid = 0;
    std::uint32_t m_utf8String = 0;
    WindowInfo m_current;
    WindowInfo m_lastForeign;
};

} // namespace clipvault::platform
