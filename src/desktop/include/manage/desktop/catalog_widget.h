#pragma once

#include <QJsonObject>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace manage::desktop {

class ApiClient;
struct ApiResponse;

class CatalogWidget final : public QWidget {
    Q_OBJECT

public:
    explicit CatalogWidget(ApiClient* apiClient, QWidget* parent = nullptr);

    void refreshMaterials();
    void refreshCustomers();

private:
    QWidget* createMaterialsPage();
    QWidget* createCustomersPage();
    QWidget* createMaterialEditor();
    QWidget* createCustomerEditor();
    void connectActions();
    void applySessionState();
    void updateWriteAccess();
    [[nodiscard]] bool sessionReady() const;
    [[nodiscard]] bool canWrite() const;

    void loadMaterials();
    void loadCustomers();
    void showMaterials(const ApiResponse& response);
    void showCustomers(const ApiResponse& response);
    void updateMaterialPaging();
    void updateCustomerPaging();

    void beginNewMaterial();
    void beginEditMaterial();
    void saveMaterial();
    void cancelMaterialEdit();
    void toggleSelectedMaterial();

    // 供应商分支
    void loadSuppliers();
    void showSuppliers(const ApiResponse& response);
    void beginNewSupplier();
    void beginEditSupplier();
    void saveSupplier();
    void cancelSupplierEdit();
    void toggleSelectedSupplier();

    // 价格分支（电线类按铜价区分）
    void loadPrices();
    void showPrices(const ApiResponse& response);
    void beginNewPrice();
    void beginEditPrice();
    void savePrice();
    void cancelPriceEdit();
    void toggleSelectedPrice();

    void applyCopperVisibility();
    void updateBranchAccess();
    [[nodiscard]] bool branchesReady() const;

    void beginNewCustomer();
    void beginEditCustomer();
    void saveCustomer();
    void cancelCustomerEdit();

    [[nodiscard]] int selectedMaterialRow() const;
    [[nodiscard]] int selectedSupplierRow() const;
    [[nodiscard]] int selectedPriceRow() const;
    [[nodiscard]] int selectedCustomerRow() const;
    [[nodiscard]] QString errorText(const ApiResponse& response) const;
    void setMaterialBusy(bool busy);
    void setCustomerBusy(bool busy);
    void setSupplierBusy(bool busy);
    void setPriceBusy(bool busy);

    ApiClient* apiClient_{};
    QVector<QJsonObject> materials_;
    QVector<QJsonObject> customers_;
    QVector<QJsonObject> suppliers_;
    QVector<QJsonObject> prices_;
    int materialPage_{1};
    int materialTotalPages_{};
    qint64 materialTotal_{};
    int customerPage_{1};
    int customerTotalPages_{};
    qint64 customerTotal_{};
    qint64 editingMaterialId_{};
    qint64 editingMaterialRevision_{};
    qint64 editingSupplierId_{};
    qint64 editingSupplierRevision_{};
    qint64 editingPriceId_{};
    qint64 editingPriceRevision_{};
    qint64 editingCustomerId_{};
    qint64 editingCustomerRevision_{};
    bool materialBusy_{};
    bool customerBusy_{};
    bool supplierBusy_{};
    bool priceBusy_{};
    bool materialEditing_{};
    bool supplierEditing_{};
    bool priceEditing_{};
    bool customerEditing_{};

    QLineEdit* materialsSearchEdit_{};
    QPushButton* materialsSearchButton_{};
    QPushButton* materialsRefreshButton_{};
    QPushButton* materialsPreviousButton_{};
    QPushButton* materialsNextButton_{};
    QLabel* materialsPageLabel_{};
    QLabel* materialsStatusLabel_{};
    QTableWidget* materialsTable_{};
    QPushButton* materialAddButton_{};
    QPushButton* materialEditButton_{};
    QPushButton* materialToggleButton_{};
    QGroupBox* materialEditor_{};
    QLineEdit* materialCodeEdit_{};
    QLineEdit* materialNameEdit_{};
    QLineEdit* materialSpecificationEdit_{};
    QLineEdit* materialUnitEdit_{};
    QLineEdit* materialCategoryEdit_{};
    QCheckBox* materialCopperCheck_{};
    QLineEdit* materialPriceEdit_{};
    QCheckBox* materialEnabledCheck_{};
    QPushButton* materialSaveButton_{};
    QPushButton* materialCancelButton_{};

    QGroupBox* supplierGroupBox_{};
    QTableWidget* suppliersTable_{};
    QPushButton* supplierAddButton_{};
    QPushButton* supplierEditButton_{};
    QPushButton* supplierToggleButton_{};
    QGroupBox* supplierEditor_{};
    QLineEdit* supplierNameEdit_{};
    QLineEdit* supplierContactEdit_{};
    QLineEdit* supplierPhoneEdit_{};
    // 供货周期（天）：该供应商对当前物料的交货周期，用于交期计算。
    QLineEdit* supplierLeadDaysEdit_{};
    QCheckBox* supplierDefaultCheck_{};
    QPushButton* supplierSaveButton_{};
    QPushButton* supplierCancelButton_{};
    QLabel* suppliersStatusLabel_{};

    QGroupBox* priceGroupBox_{};
    QTableWidget* pricesTable_{};
    QPushButton* priceAddButton_{};
    QPushButton* priceEditButton_{};
    QPushButton* priceToggleButton_{};
    QGroupBox* priceEditor_{};
    QLineEdit* priceCopperEdit_{};
    QLineEdit* priceUnitEdit_{};
    QCheckBox* priceDefaultCheck_{};
    QPushButton* priceSaveButton_{};
    QPushButton* priceCancelButton_{};
    QLabel* pricesStatusLabel_{};

    QLineEdit* customersSearchEdit_{};
    QPushButton* customersSearchButton_{};
    QPushButton* customersRefreshButton_{};
    QPushButton* customersPreviousButton_{};
    QPushButton* customersNextButton_{};
    QLabel* customersPageLabel_{};
    QLabel* customersStatusLabel_{};
    QTableWidget* customersTable_{};
    QPushButton* customerAddButton_{};
    QPushButton* customerEditButton_{};
    QGroupBox* customerEditor_{};
    QLineEdit* customerNameEdit_{};
    QLineEdit* customerContactEdit_{};
    QLineEdit* customerPhoneEdit_{};
    QLineEdit* customerAddressEdit_{};
    QLineEdit* customerNotesEdit_{};
    QPushButton* customerSaveButton_{};
    QPushButton* customerCancelButton_{};
};

} // namespace manage::desktop
