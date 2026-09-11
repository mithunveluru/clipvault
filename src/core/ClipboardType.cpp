#include "ClipboardType.h"

namespace clipvault {

QString toString(ClipboardType type)
{
    switch (type) {
    case ClipboardType::Text:  return QStringLiteral("Text");
    case ClipboardType::Url:   return QStringLiteral("URL");
    case ClipboardType::Email: return QStringLiteral("Email");
    case ClipboardType::Code:  return QStringLiteral("Code");
    case ClipboardType::Unknown: break;
    }
    return QStringLiteral("Unknown");
}

ClipboardType clipboardTypeFromString(QStringView text)
{
    if (text.compare(u"Text", Qt::CaseInsensitive) == 0)  return ClipboardType::Text;
    if (text.compare(u"URL", Qt::CaseInsensitive) == 0)   return ClipboardType::Url;
    if (text.compare(u"Email", Qt::CaseInsensitive) == 0) return ClipboardType::Email;
    if (text.compare(u"Code", Qt::CaseInsensitive) == 0)  return ClipboardType::Code;
    return ClipboardType::Unknown;
}

} // namespace clipvault
