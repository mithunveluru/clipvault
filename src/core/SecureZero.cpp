#include "SecureZero.h"

#include <string.h>

namespace clipvault {

void secureZero(QString& text)
{
    // a shared buffer belongs to someone else so only the detached copy is ours to wipe
    if (!text.isEmpty() && !text.isDetached()) {
        text = QString();
        return;
    }
    if (!text.isEmpty())
        explicit_bzero(text.data(), static_cast<size_t>(text.size()) * sizeof(QChar));
    text = QString();
}

void secureZero(QByteArray& bytes)
{
    if (!bytes.isEmpty() && !bytes.isDetached()) {
        bytes = QByteArray();
        return;
    }
    if (!bytes.isEmpty())
        explicit_bzero(bytes.data(), static_cast<size_t>(bytes.size()));
    bytes = QByteArray();
}

} // namespace clipvault
