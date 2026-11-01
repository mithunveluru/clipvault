#include "SourceLocation.h"

namespace clipvault::platform {

// ponytail: stubs until the provenance platform work lands
QString resolveSourcePath(const WindowInfo&)
{
    return { };
}

bool openSourceLocation(const QString&)
{
    return false;
}

} // namespace clipvault::platform
