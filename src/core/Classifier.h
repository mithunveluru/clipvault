#pragma once

#include "ClipboardType.h"

#include <QString>

namespace clipvault {

// Offline rules only and conservative by default
ClipboardType classify(const QString& content);

// Exposed for tests
bool looksLikeCode(const QString& content);

} // namespace clipvault
