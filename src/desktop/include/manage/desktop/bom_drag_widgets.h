#pragma once

#include <QListWidget>
#include <QTableWidget>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QMimeData;

namespace manage::desktop {

// 左侧物料源：拖入 BOM 明细时携带物料 id/编码/名称/电线类标志。
class MaterialDragList final : public QListWidget {
public:
    explicit MaterialDragList(QWidget* parent = nullptr);

    void addMaterial(
        qint64 id,
        const QString& code,
        const QString& name,
        bool isCopperBased = false
    );

    [[nodiscard]] static QString materialMimeType();
    [[nodiscard]] static std::unique_ptr<QMimeData> materialMimeData(
        qint64 id,
        const QString& code,
        const QString& name,
        bool isCopperBased = false
    );

protected:
    void startDrag(Qt::DropActions supportedActions) override;
};

// BOM 明细表：行号 / 物料编码 / 物料名称 / 供应商 / 铜价档 / 数量 / 单价 / 备注。
// 供应商列为每行一个下拉框，由外层组件（BomQuoteWidget）加载物料供应商后填充。
class BomItemsTable final : public QTableWidget {
    Q_OBJECT

public:
    enum Column {
        Line = 0,
        Code = 1,
        Name = 2,
        Supplier = 3,
        Copper = 4,
        Quantity = 5,
        UnitPrice = 6,
        Notes = 7,
        ColumnCount = 8,
    };

    explicit BomItemsTable(QWidget* parent = nullptr);

    [[nodiscard]] bool addOrMergeMaterial(
        qint64 materialId,
        const QString& materialCode,
        const QString& materialName,
        qint64 quantityMicros = 1'000'000,
        bool isCopperBased = false
    );
    [[nodiscard]] bool moveBomRow(int fromRow, int toRow);
    void renumberLines();
    void setDragEditingEnabled(bool enabled);

    // 填充某行的供应商下拉（suppliers 为 (id, 名称) 列表）。
    // selectedSupplierId > 0 时选中该供应商（回显已保存的 BOM）。
    void setRowSuppliers(
        int row,
        const std::vector<std::pair<qint64, QString>>& suppliers,
        qint64 selectedSupplierId = 0
    );
    // 当前行选择的供应商 id（0 = 未选择）。
    [[nodiscard]] qint64 rowSupplierId(int row) const;
    // 当前行是否已选择供应商。
    [[nodiscard]] bool rowHasSupplier(int row) const;
    // 当前行的物料 id（无则返回 0）。
    [[nodiscard]] qint64 rowMaterialId(int row) const;

    [[nodiscard]] static QString rowMimeType();
    [[nodiscard]] static std::unique_ptr<QMimeData> rowMimeData(int row);

signals:
    // 通过拖入或添加按钮新增了一个物料行（合并数量时不发出）。
    void rowAdded(int row);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    [[nodiscard]] int insertionRow(const QPoint& position) const;
    [[nodiscard]] bool dropMaterial(const QMimeData* mimeData);
    [[nodiscard]] bool dropRow(const QMimeData* mimeData, int insertionRow);

    bool dragEditingEnabled_{true};
};

} // namespace manage::desktop
