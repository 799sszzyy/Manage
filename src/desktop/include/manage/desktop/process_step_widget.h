#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace manage::desktop {

class ApiClient;
struct ApiResponse;

// 工序与工时管理页：维护工序库（工序编码、名称、单人工时、启停），
// 供报价订单界面手动选用工序步骤。
class ProcessStepWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ProcessStepWidget(ApiClient* apiClient, QWidget* parent = nullptr);

private:
    void buildUi();
    void connectUi();
    void applySessionState();
    void updateControls();
    [[nodiscard]] bool sessionReady() const;
    [[nodiscard]] bool canWrite() const;
    [[nodiscard]] int selectedRow() const;
    [[nodiscard]] QString errorText(const ApiResponse& response) const;

    void loadSteps();
    void showSteps(const ApiResponse& response);
    void startCreate();
    void beginEdit();
    void saveStep();
    void toggleEnabled();
    void refreshList();

    ApiClient* apiClient_{};
    QString role_;
    bool mustChangePassword_{};
    bool busy_{};
    int page_{1};
    int totalPages_{1};
    qint64 total_{};
    qint64 currentId_{};
    int currentRevision_{};
    QJsonArray steps_;
    bool editing_{};

    QLineEdit* searchEdit_{};
    QPushButton* searchButton_{};
    QTableWidget* stepTable_{};
    QPushButton* previousButton_{};
    QPushButton* nextButton_{};
    QLabel* pageLabel_{};
    QPushButton* newButton_{};
    QPushButton* editButton_{};
    QPushButton* enableButton_{};
    QPushButton* disableButton_{};
    QPushButton* refreshButton_{};

    QLineEdit* codeEdit_{};
    QLineEdit* nameEdit_{};
    QLineEdit* laborMinutesEdit_{};
    QLineEdit* descriptionEdit_{};
    QPushButton* saveButton_{};
    QPushButton* cancelButton_{};
    QLabel* statusLabel_{};
};

} // namespace manage::desktop
