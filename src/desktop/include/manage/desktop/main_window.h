#pragma once

#include <QMainWindow>
#include <QString>
#include <QUrl>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QWidget;

namespace manage::desktop {

class ApiClient;
struct ApiResponse;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QUrl apiBaseUrl, QWidget* parent = nullptr);

    // 后续业务页面通过这个入口接入，共享同一个已登录主框架。
    [[nodiscard]] ApiClient* apiClient() const noexcept;
    int addModuleTab(const QString& title, QWidget* widget);

private:
    QWidget* createAuthenticationPage();
    QWidget* createPasswordChangePage();
    QWidget* createMainPage();

    void beginLogin();
    void beginBootstrap();
    void beginPasswordChange();
    void beginRefreshCurrentUser();
    void beginLogout();
    void handleLoginResponse(ApiResponse response);

    void showLoginPage(const QString& message = {});
    void showPasswordChangePage();
    void showMainPage();
    void updateCurrentUserPresentation();
    void setAuthenticationBusy(bool busy);
    void setPasswordChangeBusy(bool busy);
    void setMainBusy(bool busy);

    ApiClient* apiClient_{};
    QStackedWidget* rootStack_{};
    QWidget* authenticationPage_{};
    QWidget* passwordChangePage_{};
    QWidget* mainPage_{};

    QTabWidget* authenticationTabs_{};
    QLineEdit* loginUsernameEdit_{};
    QLineEdit* loginPasswordEdit_{};
    QPushButton* loginButton_{};
    QLineEdit* bootstrapDisplayNameEdit_{};
    QLineEdit* bootstrapPasswordEdit_{};
    QLineEdit* bootstrapConfirmEdit_{};
    QPushButton* bootstrapButton_{};
    QLabel* authenticationMessageLabel_{};

    QLineEdit* currentPasswordEdit_{};
    QLineEdit* newPasswordEdit_{};
    QLineEdit* confirmPasswordEdit_{};
    QPushButton* changePasswordButton_{};
    QLabel* passwordChangeMessageLabel_{};

    QLabel* currentUserLabel_{};
    QLabel* currentRoleLabel_{};
    QLabel* sessionMessageLabel_{};
    QPushButton* refreshUserButton_{};
    QPushButton* logoutButton_{};
    QTabWidget* moduleTabs_{};
};

} // namespace manage::desktop
