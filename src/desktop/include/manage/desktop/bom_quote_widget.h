#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace manage::desktop {

class ApiClient;
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
    QPushButton* bomAddItemButton_{};
    QPushButton* bomRemoveItemButton_{};
    QTableWidget* bomItemsTable_{};
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
