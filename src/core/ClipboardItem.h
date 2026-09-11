#pragma once

#include "ClipboardType.h"

#include <QDateTime>
#include <QString>

#include <optional>

namespace clipvault {

// Timestamps are UTC and persist as Unix seconds
struct ClipboardItem {
    qint64 id = -1;
    QString content;
    ClipboardType type = ClipboardType::Unknown;
    bool pinned = false;
    QDateTime createdAt;
    std::optional<QDateTime> expiresAt;
    QString contentHash;

    bool isValid() const { return id >= 0; }
};

} // namespace clipvault
