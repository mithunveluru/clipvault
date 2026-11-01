#pragma once

#include <QDateTime>
#include <QString>

#include <optional>

namespace clipvault {

// parsed search box query and every field is optional
struct SearchFilter {
    QString text;
    // matched against source app and window class
    QString app;
    QString title;
    std::optional<QDateTime> since;
    std::optional<QDateTime> until;

    [[nodiscard]] bool isEmpty() const
    {
        return text.isEmpty() && app.isEmpty() && title.isEmpty() && !since && !until;
    }
};

} // namespace clipvault
