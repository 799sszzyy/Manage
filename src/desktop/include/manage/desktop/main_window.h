#pragma once

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QUrl>

class QLabel;
class QPushButton;

namespace manage::desktop {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QUrl apiBaseUrl, QWidget* parent = nullptr);

private:
    void refreshServerStatus();

    QUrl apiBaseUrl_;
    QNetworkAccessManager networkManager_;
    QLabel* statusLabel_{};
    QPushButton* retryButton_{};
};

} // namespace manage::desktop
