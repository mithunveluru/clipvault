#include "TrayIcon.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace clipvault {

QIcon makeTrayIcon(bool paused)
{
    QIcon icon;
    for (const int size : { 16, 22, 24, 32, 48, 64 }) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const qreal margin = size * 0.10;
        const QRectF body(margin, margin, size - 2 * margin, size - 2 * margin);
        QPainterPath path;
        path.addRoundedRect(body, size * 0.22, size * 0.22);

        const QColor fill = paused ? QColor(0x8a, 0x8a, 0x8a) : QColor(0x2f, 0x6f, 0xed);
        painter.fillPath(path, fill);

        // Clip tab keeps it legible at 16px
        painter.setBrush(QColor(255, 255, 255, 235));
        painter.setPen(Qt::NoPen);
        const QRectF tab(size * 0.32, size * 0.06, size * 0.36, size * 0.20);
        painter.drawRoundedRect(tab, size * 0.07, size * 0.07);

        QPen pen(QColor(255, 255, 255, 235));
        pen.setWidthF(qMax(1.4, size * 0.085));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        if (paused) {
            const qreal x1 = size * 0.41;
            const qreal x2 = size * 0.59;
            painter.drawLine(QPointF(x1, size * 0.40), QPointF(x1, size * 0.76));
            painter.drawLine(QPointF(x2, size * 0.40), QPointF(x2, size * 0.76));
        } else {
            painter.drawLine(QPointF(size * 0.33, size * 0.44), QPointF(size * 0.50, size * 0.74));
            painter.drawLine(QPointF(size * 0.50, size * 0.74), QPointF(size * 0.67, size * 0.44));
        }
        painter.end();

        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace clipvault
