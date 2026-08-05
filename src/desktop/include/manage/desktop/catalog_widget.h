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
    void beginNewCustomer();
    void beginEditCustomer();
    void saveCustomer();
    void cancelCustomerEdit();

    [[nodiscard]] int selectedMaterialRow() const;
    [[nodiscard]] int selectedCustomerRow() const;
    [[nodiscard]] QString errorText(const ApiResponse& response) const;
    void setMaterialBusy(bool busy);
    void setCustomerBusy(bool busy);

    ApiClient* apiClient_{};
    QVector<QJsonObject> materials_;
    QVector<QJsonObject> customers_;
    int materialPage_{1};
    int materialTotalPages_{};
    qint64 materialTotal_{};
    int customerPage_{1};
    int customerTotalPages_{};
    qint64 customerTotal_{};
    qint64 editingMaterialId_{};
    qint64 editingMaterialRevision_{};
    qint64 editingCustomerId_{};
    qint64 editingCustomerRevision_{};
    bool materialBusy_{};
    bool customerBusy_{};
    bool materialEditing_{};
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
    QLineEdit* materialPriceEdit_{};
    QCheckBox* materialEnabledCheck_{};
    QPushButton* materialSaveButton_{};
    QPushButton* materialCancelButton_{};

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
