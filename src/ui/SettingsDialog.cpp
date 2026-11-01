#include "SettingsDialog.h"

#include "../services/SettingsService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace clipvault {

SettingsDialog::SettingsDialog(SettingsService& settings, QString hotkeyNote, QString databasePath,
                               bool capturePaused, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("ClipVault Settings"));
    setModal(true);
    buildUi(hotkeyNote, databasePath, capturePaused);
}

void SettingsDialog::buildUi(const QString& hotkeyNote, const QString& databasePath,
                             bool capturePaused)
{
    auto* root = new QVBoxLayout(this);

    auto* general = new QGroupBox(tr("General"), this);
    auto* generalForm = new QFormLayout(general);

    m_launchOnStartup = new QCheckBox(tr("Start ClipVault when I log in"), general);
    m_launchOnStartup->setChecked(m_settings.launchOnStartup());
    connect(m_launchOnStartup, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setLaunchOnStartup(on); });
    generalForm->addRow(m_launchOnStartup);

    m_hotkeyEdit = new QKeySequenceEdit(QKeySequence(m_settings.hotkey()), general);
    connect(m_hotkeyEdit, &QKeySequenceEdit::editingFinished, this, [this] {
        const QKeySequence sequence = m_hotkeyEdit->keySequence();
        if (sequence.isEmpty())
            return;
        // Only the first chord is meaningful for a global shortcut
        const QKeySequence single(sequence[0]);
        m_hotkeyEdit->setKeySequence(single);
        m_settings.setHotkey(single.toString(QKeySequence::PortableText));
    });
    generalForm->addRow(tr("Global shortcut"), m_hotkeyEdit);

    auto* hotkeyLabel = new QLabel(hotkeyNote, general);
    hotkeyLabel->setWordWrap(true);
    hotkeyLabel->setEnabled(false);
    generalForm->addRow(QString(), hotkeyLabel);

    m_retention = new QComboBox(general);
    for (const auto& option : SettingsService::retentionOptions())
        m_retention->addItem(option.label, option.seconds);
    const int retentionIndex = m_retention->findData(m_settings.retentionSeconds());
    m_retention->setCurrentIndex(retentionIndex >= 0 ? retentionIndex : 2);
    connect(m_retention, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_settings.setRetentionSeconds(m_retention->itemData(index).toInt());
    });
    generalForm->addRow(tr("Keep history for"), m_retention);

    m_maxItems = new QSpinBox(general);
    m_maxItems->setRange(10, 100000);
    m_maxItems->setSingleStep(100);
    m_maxItems->setValue(m_settings.maxItems());
    m_maxItems->setToolTip(tr("Pinned entries are never removed by this limit."));
    connect(m_maxItems, &QSpinBox::valueChanged, this,
            [this](int value) { m_settings.setMaxItems(value); });
    generalForm->addRow(tr("Maximum history items"), m_maxItems);

    m_startPaused = new QCheckBox(tr("Start with capture paused"), general);
    m_startPaused->setChecked(m_settings.startPaused());
    connect(m_startPaused, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setStartPaused(on); });
    generalForm->addRow(m_startPaused);

    root->addWidget(general);

    auto* privacy = new QGroupBox(tr("Privacy"), this);
    auto* privacyLayout = new QVBoxLayout(privacy);

    m_pauseCapture = new QCheckBox(tr("Pause clipboard capture"), privacy);
    m_pauseCapture->setChecked(capturePaused);
    connect(m_pauseCapture, &QCheckBox::toggled, this, &SettingsDialog::pauseToggleRequested);
    privacyLayout->addWidget(m_pauseCapture);

    auto* clearButton = new QPushButton(tr("Clear history…"), privacy);
    connect(clearButton, &QPushButton::clicked, this, &SettingsDialog::clearHistoryRequested);
    privacyLayout->addWidget(clearButton, 0, Qt::AlignLeft);

    auto* privacyNote = new QLabel(
        tr("Clipboard history is stored only on this computer. ClipVault has no accounts, "
           "no telemetry and makes no network connections. Clipboard contents are never "
           "written to the log."),
        privacy);
    privacyNote->setWordWrap(true);
    privacyNote->setEnabled(false);
    privacyLayout->addWidget(privacyNote);

    auto* pathLabel = new QLabel(tr("Database: %1").arg(databasePath), privacy);
    pathLabel->setWordWrap(true);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setEnabled(false);
    privacyLayout->addWidget(pathLabel);

    root->addWidget(privacy);

    auto* radar = new QGroupBox(tr("Secret radar"), this);
    auto* radarForm = new QFormLayout(radar);

    m_secretRadar = new QCheckBox(tr("Keep copied passwords and keys in memory only"), radar);
    m_secretRadar->setChecked(m_settings.secretRadarEnabled());
    connect(m_secretRadar, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setSecretRadarEnabled(on); });
    radarForm->addRow(m_secretRadar);

    m_secretTtl = new QSpinBox(radar);
    m_secretTtl->setRange(SettingsService::kMinSecretTtlSeconds, SettingsService::kMaxSecretTtlSeconds);
    m_secretTtl->setSuffix(tr(" s"));
    m_secretTtl->setValue(m_settings.secretTtlSeconds());
    m_secretTtl->setToolTip(tr("When the time runs out the secret is wiped from memory and, if it is "
                               "still on the clipboard, the clipboard is cleared."));
    connect(m_secretTtl, &QSpinBox::valueChanged, this,
            [this](int value) { m_settings.setSecretTtlSeconds(value); });
    radarForm->addRow(tr("Forget secrets after"), m_secretTtl);

    m_secretPinnedNeverExpire = new QCheckBox(tr("Pinned secrets never expire"), radar);
    m_secretPinnedNeverExpire->setChecked(m_settings.secretPinnedNeverExpire());
    m_secretPinnedNeverExpire->setToolTip(tr("Pinning a secret saves it to disk. Without this option it is "
                                             "still removed when the history retention period ends."));
    connect(m_secretPinnedNeverExpire, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setSecretPinnedNeverExpire(on); });
    radarForm->addRow(m_secretPinnedNeverExpire);

    m_secretCopyBackWarning = new QCheckBox(tr("Warn when a chat app is focused after copying a secret"),
                                            radar);
    m_secretCopyBackWarning->setChecked(m_settings.secretCopyBackWarning());
    connect(m_secretCopyBackWarning, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setSecretCopyBackWarning(on); });
    radarForm->addRow(m_secretCopyBackWarning);

    m_secretRiskyApps = new QLineEdit(m_settings.secretRiskyApps().join(QStringLiteral(", ")), radar);
    m_secretRiskyApps->setPlaceholderText(tr("Comma separated app names"));
    connect(m_secretRiskyApps, &QLineEdit::editingFinished, this, [this] {
        QStringList apps;
        for (const QString& part : m_secretRiskyApps->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString app = part.trimmed().toLower();
            if (!app.isEmpty())
                apps << app;
        }
        m_settings.setSecretRiskyApps(apps);
    });
    radarForm->addRow(tr("Chat apps"), m_secretRiskyApps);

    auto* radarNote = new QLabel(
        tr("Detection is a best guess from the text's shape and can miss secrets or flag ordinary text. "
           "The warning is best effort: ClipVault cannot see when or where you paste, only which "
           "window gets focus afterwards, and only on desktops that report focus."),
        radar);
    radarNote->setWordWrap(true);
    radarNote->setEnabled(false);
    radarForm->addRow(radarNote);

    root->addWidget(radar);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);
}

} // namespace clipvault
