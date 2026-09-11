#pragma once

namespace clipvault::platform {

bool isAutostartEnabled();

// False when the entry could not be written or removed
bool setAutostartEnabled(bool enabled);

} // namespace clipvault::platform
