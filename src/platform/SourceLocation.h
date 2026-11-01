#pragma once

#include "FocusTracker.h"

#include <QString>

namespace clipvault::platform {

// best effort path for the source window from a terminal cwd in proc or a file named in an
// editor title and never a guessed url
[[nodiscard]] QString resolveSourcePath(const WindowInfo& window);

// opens an existing local directory or plain text file without a shell
// and shows the folder of any other file
bool openSourceLocation(const QString& location);

// pieces of the two calls above kept separate so tests can reach them

// where the file name sits in the title of a known editor
enum class EditorKind { None, FileFirst, ProjectFirst };

[[nodiscard]] bool isTerminal(const WindowInfo& window);
[[nodiscard]] EditorKind editorKind(const WindowInfo& window);
// vim and neovim name the open file in the terminal title
[[nodiscard]] bool isVimTitle(const QString& title);
// an existing file named by the title or empty and the base dir only helps when it matches the project
[[nodiscard]] QString pathFromEditorTitle(const QString& title, EditorKind kind, const QString& baseDir);
[[nodiscard]] QString processWorkingDirectory(qint64 pid);
// cwd of the newest descendant so a terminal yields its shell or the program running in it
[[nodiscard]] QString foregroundWorkingDirectory(qint64 pid);
// what open source location hands to the desktop or empty when it refuses
[[nodiscard]] QString openableLocation(const QString& location);

} // namespace clipvault::platform
