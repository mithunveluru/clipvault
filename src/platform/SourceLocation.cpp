#include "SourceLocation.h"

namespace clipvault::platform {

// stubs until the provenance platform work lands
QString resolveSourcePath(const WindowInfo&)
{
    return { };
}

bool openSourceLocation(const QString&)
{
    return false;
}

} // namespace clipvault::platform
