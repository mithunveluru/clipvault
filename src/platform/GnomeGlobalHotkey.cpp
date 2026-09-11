#include "GnomeGlobalHotkey.h"

#include "../core/Logging.h"
#include "SingleInstance.h"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QProcess>
#include <QtConcurrent>
#include <QStringList>
#include <QRegularExpression>
#include <QStandardPaths>

namespace clipvault::platform {
namespace {

constexpr auto kMediaKeysSchema = "org.gnome.settings-daemon.plugins.media-keys";
constexpr auto kBindingSchema = "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding";
// Runs off the GUI thread so stalls cannot block
constexpr int kGsettingsTimeoutMs = 1500;

constexpr auto kBindingPath = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/clipvault/";

QString gsettings(const QStringList& arguments, bool* ok = nullptr)
{
    QProcess process;
    process.start(QStringLiteral("gsettings"), arguments);
    const bool finished = process.waitForFinished(kGsettingsTimeoutMs);
    const bool success = finished && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
    if (ok)
        *ok = success;
    if (!success && finished)
        qCWarning(lcHotkey) << "gsettings" << arguments.value(0) << "failed:"
                            << process.readAllStandardError().trimmed();
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QString bindingTarget()
{
    return QStringLiteral("%1:%2").arg(QLatin1String(kBindingSchema), QLatin1String(kBindingPath));
}

// Preserves keybindings belonging to other applications
QStringList currentPaths()
{
    bool ok = false;
    const QString raw = gsettings({ QStringLiteral("get"), QLatin1String(kMediaKeysSchema),
                                    QStringLiteral("custom-keybindings") },
                                 &ok);
    QStringList paths;
    if (!ok)
        return paths;
    static const QRegularExpression re(QStringLiteral("'([^']+)'"));
    auto it = re.globalMatch(raw);
    while (it.hasNext())
        paths << it.next().captured(1);
    return paths;
}

bool writePaths(const QStringList& paths)
{
    QStringList quoted;
    quoted.reserve(paths.size());
    for (const QString& path : paths)
        quoted << QStringLiteral("'%1'").arg(path);
    bool ok = false;
    gsettings({ QStringLiteral("set"), QLatin1String(kMediaKeysSchema),
                QStringLiteral("custom-keybindings"),
                QStringLiteral("[%1]").arg(quoted.join(QStringLiteral(", "))) },
              &ok);
    return ok;
}

bool applyBinding(const QString& binding, const QString& command)
{
    QStringList paths = currentPaths();
    if (!paths.contains(QLatin1String(kBindingPath))) {
        paths << QLatin1String(kBindingPath);
        if (!writePaths(paths))
            return false;
    }

    bool ok = true;
    bool step = false;
    gsettings({ QStringLiteral("set"), bindingTarget(), QStringLiteral("name"),
                QStringLiteral("ClipVault") }, &step);
    ok = ok && step;
    gsettings({ QStringLiteral("set"), bindingTarget(), QStringLiteral("command"), command }, &step);
    ok = ok && step;
    gsettings({ QStringLiteral("set"), bindingTarget(), QStringLiteral("binding"), binding }, &step);
    return ok && step;
}

void removeBinding()
{
    QStringList paths = currentPaths();
    if (paths.removeAll(QLatin1String(kBindingPath)) > 0)
        writePaths(paths);

    bool ok = false;
    for (const char* key : { "name", "command", "binding" })
        gsettings({ QStringLiteral("reset"), bindingTarget(), QLatin1String(key) }, &ok);
}

} // namespace

GnomeGlobalHotkey::GnomeGlobalHotkey(SingleInstance& instance, QObject* parent)
    : GlobalHotkey(parent)
    , m_instance(instance)
{
    connect(&m_instance, &SingleInstance::commandReceived, this, [this](const QString& command) {
        if (command == QLatin1String("show"))
            emit activated();
    });
}

GnomeGlobalHotkey::~GnomeGlobalHotkey()
{
    // Binding deliberately survives process exit
}

bool GnomeGlobalHotkey::isSupported()
{
    if (QStandardPaths::findExecutable(QStringLiteral("gsettings")).isEmpty())
        return false;
    bool ok = false;
    gsettings({ QStringLiteral("get"), QLatin1String(kMediaKeysSchema),
                QStringLiteral("custom-keybindings") },
              &ok);
    return ok;
}

QString GnomeGlobalHotkey::toGnomeBinding(const QKeySequence& sequence)
{
    if (sequence.isEmpty())
        return {};

    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();

    QString binding;
    if (modifiers & Qt::ControlModifier)
        binding += QStringLiteral("<Control>");
    if (modifiers & Qt::AltModifier)
        binding += QStringLiteral("<Alt>");
    if (modifiers & Qt::ShiftModifier)
        binding += QStringLiteral("<Shift>");
    if (modifiers & Qt::MetaModifier)
        binding += QStringLiteral("<Super>");

    QString key = QKeySequence(QKeyCombination(Qt::NoModifier, combination.key())).toString();
    // GDK names are case sensitive so lowercase plain letters only
    if (key.size() == 1 && key.at(0).isLetter())
        key = key.toLower();
    return binding + key;
}

void GnomeGlobalHotkey::registerHotkey(const QKeySequence& sequence)
{
    const QString binding = toGnomeBinding(sequence);
    if (binding.isEmpty()) {
        qCWarning(lcHotkey) << "empty shortcut, nothing registered";
        emit registrationFinished(false);
        return;
    }

    const QString command = QStringLiteral("%1 --show").arg(QCoreApplication::applicationFilePath());

    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, binding] {
        m_registered = watcher->result();
        if (m_registered)
            qCInfo(lcHotkey) << "global shortcut registered with the desktop as" << binding;
        else
            qCWarning(lcHotkey) << "the desktop rejected the global shortcut";
        emit registrationFinished(m_registered);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(applyBinding, binding, command));
}

void GnomeGlobalHotkey::unregisterHotkey()
{
    if (!m_registered)
        return;
    m_registered = false;

    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [watcher] {
        qCInfo(lcHotkey) << "global shortcut removed from the desktop";
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(removeBinding));
}

QString GnomeGlobalHotkey::mechanismDescription() const
{
    return tr("Registered with GNOME as a custom shortcut. Wayland does not let "
              "applications grab keys directly, so the desktop delivers the shortcut "
              "to ClipVault.");
}

} // namespace clipvault::platform
