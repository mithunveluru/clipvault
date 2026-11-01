#pragma once

#include <QString>

namespace clipvault {

// where a clip came from as far as the desktop would tell us
struct Provenance {
    QString app;
    QString windowClass;
    QString title;
    // terminal cwd or editor file or empty
    QString path;

    [[nodiscard]] bool isEmpty() const
    {
        return app.isEmpty() && windowClass.isEmpty() && title.isEmpty() && path.isEmpty();
    }
};

} // namespace clipvault
