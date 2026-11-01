#pragma once

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcDb)
Q_DECLARE_LOGGING_CATEGORY(lcClip)
Q_DECLARE_LOGGING_CATEGORY(lcHotkey)
Q_DECLARE_LOGGING_CATEGORY(lcFocus)
Q_DECLARE_LOGGING_CATEGORY(lcSecret)

namespace clipvault {

// Diagnostic stand in for clipboard text which is never logged
QString redact(const QString& content);

// Install the process wide message handler once from main
void installLogHandler();

// Append diagnostics to a size capped owner only file
void enableLogFile(const QString& path);

} // namespace clipvault
