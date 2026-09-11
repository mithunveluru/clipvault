#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QStyledItemDelegate>

namespace clipvault {

// Two line row painted from the widget palette
class ClipboardItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ClipboardItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    static constexpr int kRowHeight = 56;

private:
    // Relative ages change by the second so cache it
    QDateTime referenceTime() const;

    mutable QDateTime m_now;
    mutable QElapsedTimer m_nowAge;
};

} // namespace clipvault
