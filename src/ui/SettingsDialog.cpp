#include "SettingsDialog.h"

#include "../services/SettingsService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
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

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);
}

} // namespace clipvault
