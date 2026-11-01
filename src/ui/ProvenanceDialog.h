#pragma once

#include "../core/ClipboardItem.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>

#include <array>

namespace clipvault {

class ProvenanceService;

// every recorded source field of one item plus ways back to it
class ProvenanceDialog : public QDialog {
    Q_OBJECT
public:
    ProvenanceDialog(const ClipboardItem& item, ProvenanceService& provenance, QWidget* parent = nullptr);

signals:
    // content free message for the main window footer
    void statusMessage(const QString& message);

private:
    // members rather than heap children and the layout is declared first so it goes last
    QFormLayout m_form;
    std::array<QLabel, 6> m_fields;
    QDialogButtonBox m_buttons;
};

} // namespace clipvault
