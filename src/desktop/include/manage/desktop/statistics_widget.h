#pragma once

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
    void setBusy(bool busy);

    ApiClient* apiClient_{};
    QDateEdit* startDateEdit_{};
    QDateEdit* endDateEdit_{};
    QLineEdit* customerIdEdit_{};
    QComboBox* statusCombo_{};
    QPushButton* refreshButton_{};
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
};

} // namespace manage::desktop
