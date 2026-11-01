#pragma once

#include <QByteArray>
#include <QString>

namespace clipvault {

// overwrites the buffer in place then empties it and the compiler cannot drop the write
void secureZero(QString& text);
void secureZero(QByteArray& bytes);

} // namespace clipvault
