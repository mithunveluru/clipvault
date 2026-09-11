#pragma once

#include <QString>
#include <QStringView>

namespace clipvault {

enum class ClipboardType { Text, Url, Email, Code, Unknown };

QString toString(ClipboardType type);
ClipboardType clipboardTypeFromString(QStringView text);

} // namespace clipvault
