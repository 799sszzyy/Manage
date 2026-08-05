#pragma once

#include <QJsonArray>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace manage::desktop {

class ApiClient;
struct ApiResponse;

class UserManagementWidget final : public QWidget {
    Q_OBJECT

public:
    explicit UserManagementWidget(ApiClient* apiClient, QWidget* parent = nullptr);

private:
    void buildUi();
    void connectUi();
    void applySessionState();
    void refresh();
    void showPage(const ApiResponse& response);
    void beginCreate();
    void beginEdit();
    void save();
    void toggleEnabled();
    void resetPassword();
    void setBusy(bool busy, const QString& message = {});
    int selectedRow() const;
    QJsonObject selectedUser() const;

    ApiClient* apiClient_{};
    bool busy_{};
    bool editingExisting_{};
    int page_{1};
    int totalPages_{1};
    QJsonArray users_;

    QLineEdit* searchEdit_{};
    QPushButton* searchButton_{};
    QTableWidget* table_{};
    QPushButton* previousButton_{};
    QPushButton* nextButton_{};
    QLabel* pageLabel_{};
    QPushButton* newButton_{};
    QPushButton* editButton_{};
    QPushButton* enabledButton_{};
    QPushButton* resetButton_{};
    QLineEdit* usernameEdit_{};
    QLineEdit* displayNameEdit_{};
    QComboBox* roleCombo_{};
    QLineEdit* passwordEdit_{};
    QPushButton* saveButton_{};
    QPushButton* cancelButton_{};
    QLabel* statusLabel_{};
};

} // namespace manage::desktop
