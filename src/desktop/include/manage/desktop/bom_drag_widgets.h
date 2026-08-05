#pragma once

#include <QListWidget>
#include <QTableWidget>

#include <memory>

class QMimeData;

namespace manage::desktop {

class MaterialDragList final : public QListWidget {
public:
    explicit MaterialDragList(QWidget* parent = nullptr);

    void addMaterial(qint64 id, const QString& code, const QString& name);

    [[nodiscard]] static QString materialMimeType();
    [[nodiscard]] static std::unique_ptr<QMimeData> materialMimeData(
        qint64 id,
        const QString& code,
        const QString& name
    );

protected:
    void startDrag(Qt::DropActions supportedActions) override;
};

class BomItemsTable final : public QTableWidget {
public:
    explicit BomItemsTable(QWidget* parent = nullptr);

    [[nodiscard]] bool addOrMergeMaterial(
        qint64 materialId,
        const QString& materialCode,
        const QString& materialName,
        qint64 quantityMicros = 1'000'000
    );
    [[nodiscard]] bool moveBomRow(int fromRow, int toRow);
    void renumberLines();
    void setDragEditingEnabled(bool enabled);

    [[nodiscard]] static QString rowMimeType();
    [[nodiscard]] static std::unique_ptr<QMimeData> rowMimeData(int row);

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
