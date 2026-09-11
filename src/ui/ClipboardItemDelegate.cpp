#include "ClipboardItemDelegate.h"

#include "../core/ClipboardType.h"
#include "ClipboardListModel.h"

#include <QApplication>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>

namespace clipvault {
namespace {

constexpr int kPadding = 10;
constexpr int kBadgeSize = 28;
constexpr int kBadgeGap = 12;

QString glyphFor(ClipboardType type)
{
    switch (type) {
    case ClipboardType::Url:   return QStringLiteral("\u2197");
    case ClipboardType::Email: return QStringLiteral("@");
    case ClipboardType::Code:  return QStringLiteral("{ }");
    case ClipboardType::Text:  return QStringLiteral("T");
    case ClipboardType::Unknown: break;
    }
    return QStringLiteral("?");
}

QColor blend(const QColor& a, const QColor& b, qreal factor)
{
    return QColor::fromRgbF(a.redF() * (1 - factor) + b.redF() * factor,
                            a.greenF() * (1 - factor) + b.greenF() * factor,
                            a.blueF() * (1 - factor) + b.blueF() * factor);
}

} // namespace

ClipboardItemDelegate::ClipboardItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QDateTime ClipboardItemDelegate::referenceTime() const
{
    if (!m_nowAge.isValid() || m_nowAge.elapsed() > 1000) {
        m_now = QDateTime::currentDateTimeUtc();
        m_nowAge.restart();
    }
    return m_now;
}

QSize ClipboardItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    return { option.rect.width(), kRowHeight };
}

void ClipboardItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    const QPalette& palette = opt.palette;
    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered = opt.state & QStyle::State_MouseOver;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QRect row = opt.rect.adjusted(4, 2, -4, -2);
    if (selected || hovered) {
        QPainterPath path;
        path.addRoundedRect(row, 7, 7);
        painter->fillPath(path, selected ? palette.highlight()
                                         : QBrush(blend(palette.color(QPalette::Base),
                                                        palette.color(QPalette::Text), 0.06)));
    }

    const QColor primary = selected ? palette.color(QPalette::HighlightedText)
                                    : palette.color(QPalette::Text);
    const QColor secondary = selected
        ? blend(palette.color(QPalette::HighlightedText), palette.color(QPalette::Highlight), 0.35)
        : blend(palette.color(QPalette::Text), palette.color(QPalette::Base), 0.45);

    const auto type = static_cast<ClipboardType>(index.data(ClipboardListModel::TypeRole).toInt());
    const bool pinned = index.data(ClipboardListModel::PinnedRole).toBool();

    const QRect badge(row.left() + kPadding, row.top() + (row.height() - kBadgeSize) / 2,
                      kBadgeSize, kBadgeSize);
    QPainterPath badgePath;
    badgePath.addRoundedRect(badge, 6, 6);
    painter->fillPath(badgePath, QBrush(blend(palette.color(QPalette::Base), primary, 0.10)));

    QFont badgeFont = opt.font;
    badgeFont.setPointSizeF(badgeFont.pointSizeF() * 0.82);
    badgeFont.setBold(true);
    painter->setFont(badgeFont);
    painter->setPen(secondary);
    painter->drawText(badge, Qt::AlignCenter, glyphFor(type));

    // Pin marker on the right keeps the baseline stable
    QRect textArea(badge.right() + kBadgeGap, row.top() + 8,
                   row.right() - badge.right() - kBadgeGap - kPadding, row.height() - 16);
    if (pinned) {
        QFont pinFont = opt.font;
        pinFont.setPointSizeF(pinFont.pointSizeF() * 0.9);
        painter->setFont(pinFont);
        const QRect pinRect(row.right() - kPadding - 18, row.top(), 18, row.height());
        painter->setPen(selected ? primary : palette.color(QPalette::Highlight));
        painter->drawText(pinRect, Qt::AlignCenter, QStringLiteral("★"));
        textArea.setRight(pinRect.left() - 6);
    }

    QFont titleFont = opt.font;
    painter->setFont(titleFont);
    painter->setPen(primary);
    const QFontMetrics titleMetrics(titleFont);
    const QRect titleRect(textArea.left(), textArea.top(), textArea.width(), titleMetrics.height());
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                      titleMetrics.elidedText(index.data(ClipboardListModel::PreviewRole).toString(),
                                              Qt::ElideRight, textArea.width()));

    QFont metaFont = opt.font;
    metaFont.setPointSizeF(metaFont.pointSizeF() * 0.85);
    painter->setFont(metaFont);
    painter->setPen(secondary);
    const QDateTime created = index.data(ClipboardListModel::CreatedAtRole).toDateTime();
    const QString meta = QStringLiteral("%1  ·  %2")
                             .arg(relativeTime(created, referenceTime()), toString(type));
    const QFontMetrics metaMetrics(metaFont);
    const QRect metaRect(textArea.left(), titleRect.bottom() + 2, textArea.width(),
                         metaMetrics.height());
    painter->drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter,
                      metaMetrics.elidedText(meta, Qt::ElideRight, textArea.width()));

    painter->restore();
}

} // namespace clipvault
