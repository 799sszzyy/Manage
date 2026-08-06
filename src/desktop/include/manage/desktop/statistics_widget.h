#pragma once

#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace manage::desktop {

class ApiClient;
struct ApiResponse;

class StatisticsWidget final : public QWidget {
public:
    explicit StatisticsWidget(ApiClient* apiClient, QWidget* parent = nullptr);

    void refresh();

private:
    void showReport(const ApiResponse& response);
    void exportReport();
    void setBusy(bool busy);

    // 工程师责任制：按工程师 + 期间（月/季/年）统计任务完成情况。
    void loadEngineers();
    void refreshEngineer();
    void showEngineerReport(const ApiResponse& response);
    [[nodiscard]] bool validEngineerPeriod(QString* message) const;

    ApiClient* apiClient_{};
    QDateEdit* startDateEdit_{};
    QDateEdit* endDateEdit_{};
    QLineEdit* customerIdEdit_{};
    QComboBox* statusCombo_{};
    QPushButton* refreshButton_{};
    QPushButton* exportButton_{};
    QLabel* messageLabel_{};
    QLabel* quoteCountLabel_{};
    QLabel* totalAmountLabel_{};
    QLabel* averageAmountLabel_{};
    QLabel* issuedCountLabel_{};
    QLabel* voidCountLabel_{};
    QLabel* publishedRateLabel_{};
    QTableWidget* monthTable_{};
    QTableWidget* customerTable_{};
    QTableWidget* categoryTable_{};
    QJsonObject currentReport_;

    QComboBox* engineerCombo_{};
    QComboBox* periodTypeCombo_{};
    QLineEdit* periodEdit_{};
    QPushButton* engineerRefreshButton_{};
    QLabel* engineerMessageLabel_{};
    QLabel* engineerAssignedLabel_{};
    QLabel* engineerSubmittedLabel_{};
    QLabel* engineerUnsubmittedLabel_{};
    QLabel* engineerOnTimeLabel_{};
    QLabel* engineerLateLabel_{};
    QLabel* engineerRateLabel_{};
    QLabel* engineerDeviationLabel_{};
    QTableWidget* engineerGroupTable_{};
    QTableWidget* engineerTaskTable_{};
    QJsonObject currentEngineerReport_;
};

} // namespace manage::desktop
