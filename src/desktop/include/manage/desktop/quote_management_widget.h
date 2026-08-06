#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;

namespace manage::desktop {

class ApiClient;
struct ApiResponse;

class QuoteManagementWidget final : public QWidget {
    Q_OBJECT

public:
    explicit QuoteManagementWidget(ApiClient* apiClient, QWidget* parent = nullptr);

private:
    void buildUi();
    void connectUi();
    void applySessionState();
    void updateControls();
    [[nodiscard]] bool sessionReady() const;
    [[nodiscard]] bool canWrite() const;
    [[nodiscard]] int selectedQuoteRow() const;
    [[nodiscard]] QString errorText(const ApiResponse& response) const;

    void refreshQuotes();
    void loadQuotes();
    void showQuotes(const ApiResponse& response);
    void loadSelectedQuote();
    void loadQuote(qint64 id);
    void showQuoteDetail(const ApiResponse& response);
    void loadLookups();
    void loadCustomers();
    void loadBoms();
    void loadMaterials();
    void loadSelectedBom(bool forceReload = false);
    void loadNextBomMaterial();
    void addMaterialRow(const QJsonObject& material, qint64 quantityMicros, const QString& notes);

    void startNewQuote();
    void beginEditQuote();
    void addItem();
    void removeItem();
    void saveQuote();
    void changeStatus(const QString& status);
    void cloneQuote();
    void deleteDraft();
    [[nodiscard]] QJsonObject editorPayload(bool includeRevision, bool* ok) const;
    void applyDetail(const QJsonObject& quote);
    void clearEditor();
    void setBusy(bool busy, const QString& message = {});

    ApiClient* apiClient_{};
    QString role_;
    bool mustChangePassword_{};
    bool busy_{};
    int page_{1};
    int totalPages_{1};
    qint64 total_{};
    qint64 currentId_{};
    qint64 currentRevision_{};
    qint64 loadedBomId_{};
    qint64 loadedBomQuantityMicros_{1'000'000};
    QString currentStatus_;
    bool editing_{};
    QJsonArray quotes_;
    QJsonObject pendingBom_;
    QJsonArray pendingBomItems_;
    QJsonArray pendingBomMaterials_;
    int pendingBomIndex_{};
    int bomLoadGeneration_{};
    bool loadingBom_{};

    QLineEdit* searchEdit_{};
    QComboBox* statusFilter_{};
    QPushButton* searchButton_{};
    QTableWidget* quoteTable_{};
    QPushButton* previousButton_{};
    QPushButton* nextButton_{};
    QLabel* pageLabel_{};
    QPushButton* viewButton_{};
    QPushButton* newButton_{};
    QPushButton* editButton_{};
    QPushButton* issueButton_{};
    QPushButton* voidButton_{};
    QPushButton* cloneButton_{};
    QPushButton* deleteButton_{};

    QGroupBox* editorGroup_{};
    QLabel* numberLabel_{};
    QLabel* stateLabel_{};
    QLabel* revisionLabel_{};
    QComboBox* customerCombo_{};
    QComboBox* bomCombo_{};
    QDoubleSpinBox* bomQuantitySpin_{};
    QLineEdit* materialSearchEdit_{};
    QPushButton* materialSearchButton_{};
    QComboBox* materialCombo_{};
    QPushButton* addItemButton_{};
    QPushButton* removeItemButton_{};
    QTableWidget* itemsTable_{};
    QDoubleSpinBox* freightSpin_{};
    QDoubleSpinBox* otherFeesSpin_{};
    QDoubleSpinBox* markupSpin_{};
    QDoubleSpinBox* taxSpin_{};
    QTextEdit* notesEdit_{};
    QLabel* materialCostLabel_{};
    QLabel* beforeTaxLabel_{};
    QLabel* withTaxLabel_{};
    QPushButton* saveButton_{};
    QPushButton* cancelButton_{};
    QLabel* statusLabel_{};
};

} // namespace manage::desktop
