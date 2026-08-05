#include "manage/desktop/bom_drag_widgets.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using manage::desktop::BomItemsTable;
using manage::desktop::MaterialDragList;

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

qint64 materialId(const BomItemsTable& table, int row) {
    const auto* item = table.item(row, 1);
    return item ? item->data(Qt::UserRole).toLongLong() : 0;
}

void addMergeAndReorderKeepWholeRows() {
    BomItemsTable table;
    require(table.addOrMergeMaterial(11, QStringLiteral("M-11"),
                                     QStringLiteral("钢板")),
            "first material must be added");
    require(table.addOrMergeMaterial(22, QStringLiteral("M-22"),
                                     QStringLiteral("螺栓"), 2'500'000),
            "second material must be added");
    table.item(0, 4)->setText(QStringLiteral("激光切割"));
    table.item(1, 4)->setText(QStringLiteral("镀锌"));

    require(table.addOrMergeMaterial(11, QStringLiteral("M-11"),
                                     QStringLiteral("钢板"), 250'000),
            "duplicate material must merge");
    require(table.rowCount() == 2,
            "duplicate material must not create another row");
    require(table.item(0, 3)->text() == QStringLiteral("1.250000"),
            "duplicate quantity must be added exactly");
    require(table.item(0, 4)->text() == QStringLiteral("激光切割"),
            "quantity merge must preserve notes");

    require(table.moveBomRow(1, 0), "valid row move must succeed");
    require(table.item(0, 0)->text() == QStringLiteral("10") &&
                table.item(1, 0)->text() == QStringLiteral("20"),
            "moved rows must be renumbered as 10, 20");
    require(materialId(table, 0) == 22 &&
                table.item(0, 3)->text() == QStringLiteral("2.500000") &&
                table.item(0, 4)->text() == QStringLiteral("镀锌"),
            "material id, quantity, and notes must move together");
    require(materialId(table, 1) == 11 &&
                table.item(1, 3)->text() == QStringLiteral("1.250000") &&
                table.item(1, 4)->text() == QStringLiteral("激光切割"),
            "the other complete row must remain aligned");
}

void qtDropEventsAddMergeMoveAndRejectInvalidData() {
    BomItemsTable table;
    table.resize(700, 280);
    table.show();
    QCoreApplication::processEvents();

    auto material = MaterialDragList::materialMimeData(
        31, QStringLiteral("M-31"), QStringLiteral("轴承")
    );
    QDragEnterEvent firstEnter(
        QPoint(20, 20), Qt::CopyAction, material.get(),
        Qt::LeftButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(table.viewport(), &firstEnter);
    QDragMoveEvent firstMove(
        QPoint(20, 20), Qt::CopyAction, material.get(),
        Qt::LeftButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(table.viewport(), &firstMove);
    QDropEvent firstDrop(
        QPointF(20, 20), Qt::CopyAction, material.get(),
        Qt::LeftButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(table.viewport(), &firstDrop);
    require(firstDrop.isAccepted() && table.rowCount() == 1,
            "Qt material drop must add a BOM row");

    QDragEnterEvent duplicateEnter(
        QPoint(20, 20), Qt::CopyAction, material.get(),
        Qt::LeftButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(table.viewport(), &duplicateEnter);
    QDragMoveEvent duplicateMove(
        QPoint(20, 20), Qt::CopyAction, material.get(),
        Qt::LeftButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(table.viewport(), &duplicateMove);
    QDropEvent duplicateDrop(
        QPointF(20, 20), Qt::CopyAction, material.get(),
        Qt::LeftButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(table.viewport(), &duplicateDrop);
    require(duplicateDrop.isAccepted() && table.rowCount() == 1 &&
                table.item(0, 3)->text() == QStringLiteral("2.000000"),
            "repeated Qt material drop must merge quantity");

    require(table.addOrMergeMaterial(32, QStringLiteral("M-32"),
                                     QStringLiteral("端盖"), 3'000'000),
            "second row setup must succeed");
    table.item(0, 4)->setText(QStringLiteral("A note"));
    table.item(1, 4)->setText(QStringLiteral("B note"));
    auto rowMime = BomItemsTable::rowMimeData(1);
    QDragEnterEvent rowEnter(
        QPoint(20, 1), Qt::MoveAction, rowMime.get(),
        Qt::LeftButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(table.viewport(), &rowEnter);
    QDragMoveEvent rowMove(
        QPoint(20, 1), Qt::MoveAction, rowMime.get(),
        Qt::LeftButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(table.viewport(), &rowMove);
    QDropEvent rowDrop(
        QPointF(20, 1), Qt::MoveAction, rowMime.get(),
        Qt::LeftButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(table.viewport(), &rowDrop);
    require(rowDrop.isAccepted() && materialId(table, 0) == 32 &&
                table.item(0, 3)->text() == QStringLiteral("3.000000") &&
                table.item(0, 4)->text() == QStringLiteral("B note"),
            "Qt row drop must move an intact row");

    const auto rowCountBeforeInvalid = table.rowCount();
    const auto firstIdBeforeInvalid = materialId(table, 0);
    QMimeData invalidMime;
    invalidMime.setText(QStringLiteral("not a material"));
    QDropEvent invalidDrop(
        QPointF(20, 20), Qt::CopyAction, &invalidMime,
        Qt::LeftButton, Qt::NoModifier
    );
    QCoreApplication::sendEvent(table.viewport(), &invalidDrop);
    require(!invalidDrop.isAccepted() &&
                table.rowCount() == rowCountBeforeInvalid &&
                materialId(table, 0) == firstIdBeforeInvalid,
            "invalid Qt drop must not change the BOM");

    require(!table.addOrMergeMaterial(
                0, QStringLiteral("BAD"), QStringLiteral("无效")),
            "invalid material id must be rejected");
    require(!table.moveBomRow(-1, 0), "invalid row move must be rejected");
    require(table.rowCount() == rowCountBeforeInvalid,
            "invalid direct operations must not change the BOM");
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    try {
        addMergeAndReorderKeepWholeRows();
        std::cout << "[PASS] merge and intact row reorder\n";
        qtDropEventsAddMergeMoveAndRejectInvalidData();
        std::cout << "[PASS] Qt drag/drop interactions\n";
        std::cout << "2/2 tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
