#pragma once

#include "FocusTracker.h"

#include <QString>

namespace clipvault::platform {

// best effort path for the source window from a terminal cwd in proc or a file named in an
// editor title and never a guessed url
[[nodiscard]] QString resolveSourcePath(const WindowInfo& window);

// opens an http or https url or an existing directory without a shell
bool openSourceLocation(const QString& location);

} // namespace clipvault::platform
