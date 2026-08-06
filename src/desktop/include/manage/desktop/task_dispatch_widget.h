#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace manage::desktop {

class ApiClient;
struct ApiResponse;

// 任务派发页：销售账号向工程师账号派发任务（报价流程起点）。
// 销售只需填负责工程师、预期完成时间、标题/备注；工程师在任务下做 BOM/报价，
// 并可推进状态 dispatched → in_progress → completed / cancelled。
class TaskDispatchWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TaskDispatchWidget(ApiClient* apiClient, QWidget* parent = nullptr);

private:
    void buildUi();
    void connectUi();
    void applySessionState();
    void updateControls();
    [[nodiscard]] bool sessionReady() const;
    [[nodiscard]] bool canWrite() const;
    [[nodiscard]] int selectedRow() const;
    [[nodiscard]] QString errorText(const ApiResponse& response) const;
    [[nodiscard]] qint64 currentUserId() const;

    void loadTasks();
    void showTasks(const ApiResponse& response);
    void startCreate();
    void saveTask();
    void cancelEdit();
    void advanceStatus();
    void cancelTask();
    void refreshList();

    ApiClient* apiClient_{};
    QString role_;
    qint64 userId_{};
    bool mustChangePassword_{};
    bool busy_{};
    int page_{1};
    int totalPages_{1};
    qint64 total_{};
    qint64 currentId_{};
    int currentRevision_{};
    QString currentStatus_;
    QJsonArray tasks_;
    bool creating_{};

    QLineEdit* searchEdit_{};
    QPushButton* searchButton_{};
    QTableWidget* taskTable_{};
    QPushButton* previousButton_{};
    QPushButton* nextButton_{};
    QLabel* pageLabel_{};
    QPushButton* refreshButton_{};
    QPushButton* newButton_{};

    QLineEdit* engineerIdEdit_{};
    QLineEdit* customerIdEdit_{};
    QLineEdit* titleEdit_{};
    QLineEdit* notesEdit_{};
    QDateTimeEdit* expectedEdit_{};
    QPushButton* saveButton_{};
    QPushButton* cancelButton_{};
    QPushButton* advanceButton_{};
    QPushButton* cancelTaskButton_{};
    QLabel* statusLabel_{};
};

} // namespace manage::desktop
