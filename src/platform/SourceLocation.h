#pragma once

#include "FocusTracker.h"

#include <QString>

namespace clipvault::platform {

// best effort path for the source window: terminal cwd from /proc or a file parsed from an
// editor title or empty and never a guessed url
[[nodiscard]] QString resolveSourcePath(const WindowInfo& window);

// opens an http or https url or an existing directory without a shell
bool openSourceLocation(const QString& location);

} // namespace clipvault::platform
