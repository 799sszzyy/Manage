#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace manage::desktop {

class ApiClient;
class BomItemsTable;
class MaterialDragList;
struct ApiResponse;

class BomQuoteWidget final : public QWidget {
    Q_OBJECT

public:
    explicit BomQuoteWidget(ApiClient* apiClient, QWidget* parent = nullptr);

private:
    void buildUi();
    QWidget* buildBomPage();
    QWidget* buildQuotePage();
    void connectUi();
    void syncSessionRole();
    void updateControlState();
    void beginRequest(const QString& message, QLabel* statusLabel);
    void endRequest();

    void refreshBoms();
    void refreshMaterials();
    void showSelectedBom();
    void loadBom(qint64 id);
    void applyBom(const QJsonObject& object);
    void startNewBom();
    void saveBom();
    void replaceBomItems();
    void toggleBomEnabled();
    void addBomItem();
    void removeBomItem();
    [[nodiscard]] QJsonArray bomItemsPayload(bool* ok, QString* error) const;
    [[nodiscard]] QJsonObject bomMetadataPayload() const;

    // 批次9：按（供应商, 当前铜价）解析真实单价。
    // 加载某行的供应商下拉（selectedSupplierId > 0 时回显已保存供应商）。
    void loadSuppliersForRow(int row, std::optional<qint64> selectedSupplierId);
    void resolveRowPrice(int row);
    void onRowSupplierChanged(int row);
    void onBomItemsChanged(QTableWidgetItem* item);

    void addQuoteLine();
    void removeQuoteLine();
    void calculateQuote();

    [[nodiscard]] bool isAdmin() const;
    [[nodiscard]] bool canCalculate() const;
    [[nodiscard]] bool sessionReady() const;
    [[nodiscard]] QString responseError(const ApiResponse& response) const;
    void showBomError(const ApiResponse& response, const QString& operation);
    void showQuoteError(const ApiResponse& response);

    ApiClient* apiClient_{};
    QString role_;
    bool mustChangePassword_{};
    int pendingRequests_{};
    // 供应商下拉异步加载的代数保护：BOM 表格重置时递增，丢弃过期响应。
    quint64 supplierLoadGeneration_{};
    qint64 currentBomId_{};
    int currentBomRevision_{};
    bool currentBomEnabled_{true};

    QLabel* roleLabel_{};
    QLineEdit* materialSearchEdit_{};
    QPushButton* materialRefreshButton_{};
    QLabel* materialSearchStatusLabel_{};
    QLineEdit* bomSearchEdit_{};
    QPushButton* bomRefreshButton_{};
    QTableWidget* bomListTable_{};
    QPushButton* bomViewButton_{};
    QPushButton* bomNewButton_{};
    QLineEdit* bomCodeEdit_{};
    QLineEdit* bomNameEdit_{};
    QLineEdit* bomDescriptionEdit_{};
    QLabel* bomRevisionLabel_{};
    QComboBox* bomMaterialCombo_{};
    MaterialDragList* bomMaterialList_{};
    QPushButton* bomAddItemButton_{};
    QPushButton* bomRemoveItemButton_{};
    BomItemsTable* bomItemsTable_{};
    QPushButton* bomSaveButton_{};
    QPushButton* bomReplaceItemsButton_{};
    QPushButton* bomToggleEnabledButton_{};
    QLabel* bomStatusLabel_{};

    QComboBox* quoteMaterialCombo_{};
    QPushButton* quoteAddLineButton_{};
    QPushButton* quoteRemoveLineButton_{};
    QTableWidget* quoteLinesTable_{};
    QDoubleSpinBox* freightSpin_{};
    QDoubleSpinBox* otherFeesSpin_{};
    QDoubleSpinBox* markupSpin_{};
    QDoubleSpinBox* taxSpin_{};
    QPushButton* calculateButton_{};
    QLabel* materialCostLabel_{};
    QLabel* priceBeforeTaxLabel_{};
    QLabel* priceWithTaxLabel_{};
    QLabel* quoteStatusLabel_{};
};

} // namespace manage::desktop
