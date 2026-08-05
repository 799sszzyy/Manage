#include "manage/desktop/bom_drag_widgets.h"

#include <QAbstractItemView>
#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

#include <limits>
#include <utility>
#include <vector>

namespace manage::desktop {
namespace {

constexpr auto kIdRole = Qt::UserRole;
constexpr auto kMaterialCodeRole = Qt::UserRole + 1;
constexpr auto kMaterialNameRole = Qt::UserRole + 2;
constexpr auto kMaterialMime = "application/x-manage-material-v1";
constexpr auto kBomRowMime = "application/x-manage-bom-row-v1";

QTableWidgetItem* readOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QString quantityText(qint64 micros) {
    return QStringLiteral("%1.%2")
        .arg(micros / 1'000'000)
        .arg(micros % 1'000'000, 6, 10, QLatin1Char('0'));
}

bool parseQuantityMicros(const QString& text, qint64* result) {
    const auto parts = text.trimmed().split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.size() > 2 || parts.first().isEmpty()) {
        return false;
    }
    auto fraction = parts.size() == 2 ? parts.at(1) : QString{};
    if (fraction.size() > 6) {
        return false;
    }
    for (const auto character : parts.first() + fraction) {
        if (!character.isDigit()) {
            return false;
        }
    }
    while (fraction.size() < 6) {
        fraction.append(QLatin1Char('0'));
    }
    bool wholeOk = false;
    bool fractionOk = fraction.isEmpty();
    const auto whole = parts.first().toLongLong(&wholeOk);
    const auto fractionValue = fraction.isEmpty()
                                   ? 0
                                   : fraction.toLongLong(&fractionOk);
    if (!wholeOk || !fractionOk ||
        whole > (std::numeric_limits<qint64>::max() - fractionValue) /
                    1'000'000) {
        return false;
    }
    *result = whole * 1'000'000 + fractionValue;
    return *result > 0;
}

} // namespace

MaterialDragList::MaterialDragList(QWidget* parent) : QListWidget(parent) {
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::CopyAction);
}

void MaterialDragList::addMaterial(
    qint64 id,
    const QString& code,
    const QString& name
) {
    if (id <= 0 || code.trimmed().isEmpty()) {
        return;
    }
    auto* item = new QListWidgetItem(
        QStringLiteral("%1 — %2").arg(code, name), this
    );
    item->setData(kIdRole, id);
    item->setData(kMaterialCodeRole, code);
    item->setData(kMaterialNameRole, name);
    item->setToolTip(QStringLiteral("拖入右侧 BOM 明细"));
}

QString MaterialDragList::materialMimeType() {
    return QString::fromLatin1(kMaterialMime);
}

std::unique_ptr<QMimeData> MaterialDragList::materialMimeData(
    qint64 id,
    const QString& code,
    const QString& name
) {
    auto result = std::make_unique<QMimeData>();
    if (id <= 0 || code.trimmed().isEmpty()) {
        return result;
    }
    const QJsonObject material{
        {QStringLiteral("id"), id},
        {QStringLiteral("code"), code},
        {QStringLiteral("name"), name},
    };
    result->setData(
        materialMimeType(),
        QJsonDocument(material).toJson(QJsonDocument::Compact)
    );
    return result;
}

void MaterialDragList::startDrag(Qt::DropActions supportedActions) {
    Q_UNUSED(supportedActions);
    const auto* selected = currentItem();
    if (!selected) {
        return;
    }
    auto mimeData = materialMimeData(
        selected->data(kIdRole).toLongLong(),
        selected->data(kMaterialCodeRole).toString(),
        selected->data(kMaterialNameRole).toString()
    );
    if (!mimeData->hasFormat(materialMimeType())) {
        return;
    }
    auto* drag = new QDrag(this);
    drag->setMimeData(mimeData.release());
    drag->exec(Qt::CopyAction, Qt::CopyAction);
}

BomItemsTable::BomItemsTable(QWidget* parent) : QTableWidget(0, 5, parent) {
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setHorizontalHeaderLabels({
        QStringLiteral("行号"), QStringLiteral("物料编码"),
        QStringLiteral("物料名称"), QStringLiteral("数量"), QStringLiteral("备注"),
    });
}

bool BomItemsTable::addOrMergeMaterial(
    qint64 materialId,
    const QString& materialCode,
    const QString& materialName,
    qint64 quantityMicros
) {
    if (materialId <= 0 || materialCode.trimmed().isEmpty() ||
        quantityMicros <= 0) {
        return false;
    }
    for (int row = 0; row < rowCount(); ++row) {
        const auto* codeItem = item(row, 1);
        if (!codeItem || codeItem->data(kIdRole).toLongLong() != materialId) {
            continue;
        }
        auto* quantityItem = item(row, 3);
        qint64 existing{};
        if (!quantityItem || !parseQuantityMicros(quantityItem->text(), &existing) ||
            existing > std::numeric_limits<qint64>::max() - quantityMicros) {
            return false;
        }
        quantityItem->setText(quantityText(existing + quantityMicros));
        setCurrentCell(row, 0);
        return true;
    }

    const auto row = rowCount();
    insertRow(row);
    setItem(row, 0, readOnlyItem(QString::number((row + 1) * 10)));
    auto* code = readOnlyItem(materialCode.trimmed());
    code->setData(kIdRole, materialId);
    setItem(row, 1, code);
    setItem(row, 2, readOnlyItem(materialName.trimmed()));
    setItem(row, 3, new QTableWidgetItem(quantityText(quantityMicros)));
    setItem(row, 4, new QTableWidgetItem);
    setCurrentCell(row, 0);
    renumberLines();
    return true;
}

bool BomItemsTable::moveBomRow(int fromRow, int toRow) {
    if (fromRow < 0 || fromRow >= rowCount() || toRow < 0 ||
        toRow >= rowCount() || fromRow == toRow) {
        return false;
    }
    std::vector<QTableWidgetItem*> cells;
    cells.reserve(static_cast<std::size_t>(columnCount()));
    for (int column = 0; column < columnCount(); ++column) {
        cells.push_back(takeItem(fromRow, column));
    }
    removeRow(fromRow);
    insertRow(toRow);
    for (int column = 0; column < columnCount(); ++column) {
        setItem(toRow, column, cells.at(static_cast<std::size_t>(column)));
    }
    renumberLines();
    setCurrentCell(toRow, 0);
    return true;
}

void BomItemsTable::renumberLines() {
    for (int row = 0; row < rowCount(); ++row) {
        auto* line = item(row, 0);
        if (!line) {
            line = readOnlyItem({});
            setItem(row, 0, line);
        }
        line->setText(QString::number((row + 1) * 10));
        line->setFlags(line->flags() & ~Qt::ItemIsEditable);
    }
}

void BomItemsTable::setDragEditingEnabled(bool enabled) {
    dragEditingEnabled_ = enabled;
    setDragEnabled(enabled);
    setAcceptDrops(enabled);
    viewport()->setAcceptDrops(enabled);
    setDropIndicatorShown(enabled);
}

QString BomItemsTable::rowMimeType() {
    return QString::fromLatin1(kBomRowMime);
}

std::unique_ptr<QMimeData> BomItemsTable::rowMimeData(int row) {
    auto result = std::make_unique<QMimeData>();
    if (row < 0) {
        return result;
    }
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << row;
    result->setData(rowMimeType(), bytes);
    return result;
}

void BomItemsTable::startDrag(Qt::DropActions supportedActions) {
    Q_UNUSED(supportedActions);
    if (!dragEditingEnabled_ || currentRow() < 0) {
        return;
    }
    auto mimeData = rowMimeData(currentRow());
    auto* drag = new QDrag(this);
    drag->setMimeData(mimeData.release());
    drag->exec(Qt::MoveAction, Qt::MoveAction);
}

void BomItemsTable::dragEnterEvent(QDragEnterEvent* event) {
    if (dragEditingEnabled_ && event->mimeData() &&
        (event->mimeData()->hasFormat(MaterialDragList::materialMimeType()) ||
         event->mimeData()->hasFormat(rowMimeType()))) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void BomItemsTable::dragMoveEvent(QDragMoveEvent* event) {
    if (dragEditingEnabled_ && event->mimeData() &&
        (event->mimeData()->hasFormat(MaterialDragList::materialMimeType()) ||
         event->mimeData()->hasFormat(rowMimeType()))) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void BomItemsTable::dropEvent(QDropEvent* event) {
    if (!dragEditingEnabled_ || !event->mimeData()) {
        event->ignore();
        return;
    }
    bool changed = false;
    if (event->mimeData()->hasFormat(MaterialDragList::materialMimeType())) {
        changed = dropMaterial(event->mimeData());
    } else if (event->mimeData()->hasFormat(rowMimeType())) {
        changed = dropRow(
            event->mimeData(), insertionRow(event->position().toPoint())
        );
    }
    if (changed) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

int BomItemsTable::insertionRow(const QPoint& position) const {
    const auto index = indexAt(position);
    if (!index.isValid()) {
        return rowCount();
    }
    const auto rect = visualRect(index);
    return index.row() + (position.y() >= rect.center().y() ? 1 : 0);
}

bool BomItemsTable::dropMaterial(const QMimeData* mimeData) {
    const auto document = QJsonDocument::fromJson(
        mimeData->data(MaterialDragList::materialMimeType())
    );
    if (!document.isObject()) {
        return false;
    }
    const auto material = document.object();
    return addOrMergeMaterial(
        material.value(QStringLiteral("id")).toInteger(),
        material.value(QStringLiteral("code")).toString(),
        material.value(QStringLiteral("name")).toString()
    );
}

bool BomItemsTable::dropRow(const QMimeData* mimeData, int targetInsertionRow) {
    QByteArray bytes = mimeData->data(rowMimeType());
    QDataStream stream(&bytes, QIODevice::ReadOnly);
    int sourceRow = -1;
    stream >> sourceRow;
    if (stream.status() != QDataStream::Ok || sourceRow < 0 ||
        sourceRow >= rowCount() || targetInsertionRow < 0 ||
        targetInsertionRow > rowCount()) {
        return false;
    }
    auto destinationRow = targetInsertionRow;
    if (destinationRow > sourceRow) {
        --destinationRow;
    }
    if (destinationRow == sourceRow) {
        return false;
    }
    return moveBomRow(sourceRow, destinationRow);
}

} // namespace manage::desktop
