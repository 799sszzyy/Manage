#include "manage/desktop/main_window.h"

#include "manage/desktop/api_client.h"

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace manage::desktop {
namespace {

constexpr auto kMinimumPasswordLength = 12;

QString friendlyError(const ApiResponse& response) {
    const auto& error = response.error;
    if (error.kind == ApiErrorKind::Network) {
        return QStringLiteral("无法连接本地服务：%1").arg(error.message);
    }
    if (error.kind == ApiErrorKind::InvalidConfiguration) {
        return QStringLiteral("本地服务地址配置不正确：%1").arg(error.message);
    }
    if (error.kind == ApiErrorKind::InvalidResponse) {
        return QStringLiteral("本地服务返回了无法识别的数据，请检查服务版本。");
    }

    if (error.code == QStringLiteral("invalid_credentials")) {
        return QStringLiteral("用户名或密码不正确，请重新输入。");
    }
    if (error.code == QStringLiteral("bootstrap_unavailable")) {
        return QStringLiteral("系统已经完成首次初始化，请切换到“登录”页面。");
    }
    if (error.code == QStringLiteral("password_change_required")) {
        return QStringLiteral("首次登录必须先修改密码。");
    }
    if (error.code == QStringLiteral("forbidden")) {
        return QStringLiteral("当前账号没有权限执行这个操作，登录状态仍然保留。");
    }
    if (error.code == QStringLiteral("unauthorized") ||
        error.code == QStringLiteral("session_expired")) {
        return QStringLiteral("登录状态已经失效，请重新登录。");
    }
    if (error.code == QStringLiteral("account_disabled")) {
        return QStringLiteral("当前账号已停用，请联系管理员。");
    }
    if (error.code == QStringLiteral("repository_failure")) {
        return QStringLiteral("服务器暂时无法读取账号数据，请稍后重试。");
    }
    if (error.code == QStringLiteral("invalid_request")) {
        return QStringLiteral("提交内容不符合要求：%1").arg(error.message);
    }
    if (!error.message.isEmpty()) {
        return QStringLiteral("操作失败：%1").arg(error.message);
    }
    return QStringLiteral("操作失败，请稍后重试。");
}

QString roleName(const QString& role) {
    if (role == QStringLiteral("admin")) {
        return QStringLiteral("管理员");
    }
    if (role == QStringLiteral("quoter")) {
        return QStringLiteral("报价员");
    }
    if (role == QStringLiteral("viewer")) {
        return QStringLiteral("只读查看员");
    }
    return QStringLiteral("未知角色");
}

QLabel* pageTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    auto font = label->font();
    font.setPointSize(20);
    font.setBold(true);
    label->setFont(font);
    return label;
}

bool passwordLooksValid(const QString& password) {
    return password.size() >= kMinimumPasswordLength &&
           !password.trimmed().isEmpty();
}

} // namespace

MainWindow::MainWindow(QUrl apiBaseUrl, QWidget* parent)
    : QMainWindow(parent),
      apiClient_(new ApiClient(std::move(apiBaseUrl), this)) {
    setWindowTitle(QStringLiteral("本地报价管理系统 v0.5.0（Batch 5）"));
    resize(980, 660);

    rootStack_ = new QStackedWidget(this);
    rootStack_->setObjectName(QStringLiteral("rootStack"));
    authenticationPage_ = createAuthenticationPage();
    passwordChangePage_ = createPasswordChangePage();
    mainPage_ = createMainPage();
    rootStack_->addWidget(authenticationPage_);
    rootStack_->addWidget(passwordChangePage_);
    rootStack_->addWidget(mainPage_);
    setCentralWidget(rootStack_);

    connect(
        apiClient_,
        &ApiClient::sessionChanged,
        this,
        [this](bool authenticated) {
            if (!authenticated) {
                showLoginPage(QStringLiteral("登录状态已经失效，请重新登录。"));
            }
        }
    );

    showLoginPage();
}

ApiClient* MainWindow::apiClient() const noexcept {
    return apiClient_;
}

int MainWindow::addModuleTab(const QString& title, QWidget* widget) {
    if (!moduleTabs_ || !widget || title.trimmed().isEmpty()) {
        return -1;
    }
    return moduleTabs_->addTab(widget, title.trimmed());
}

QWidget* MainWindow::createAuthenticationPage() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("authenticationPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(120, 56, 120, 56);
    layout->setSpacing(18);
    layout->addWidget(pageTitle(QStringLiteral("本地报价管理系统"), page));

    auto* explanation = new QLabel(
        QStringLiteral("账号只用于本机服务。首次使用请先初始化管理员，之后直接登录。"),
        page
    );
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    authenticationTabs_ = new QTabWidget(page);
    authenticationTabs_->setObjectName(QStringLiteral("authenticationTabs"));

    auto* loginTab = new QWidget(authenticationTabs_);
    auto* loginLayout = new QFormLayout(loginTab);
    loginLayout->setContentsMargins(24, 24, 24, 24);
    loginLayout->setSpacing(14);
    loginUsernameEdit_ = new QLineEdit(QStringLiteral("admin"), loginTab);
    loginUsernameEdit_->setObjectName(QStringLiteral("loginUsernameEdit"));
    loginPasswordEdit_ = new QLineEdit(loginTab);
    loginPasswordEdit_->setObjectName(QStringLiteral("loginPasswordEdit"));
    loginPasswordEdit_->setEchoMode(QLineEdit::Password);
    loginPasswordEdit_->setPlaceholderText(QStringLiteral("请输入密码"));
    loginButton_ = new QPushButton(QStringLiteral("登录"), loginTab);
    loginButton_->setObjectName(QStringLiteral("loginButton"));
    loginLayout->addRow(QStringLiteral("用户名"), loginUsernameEdit_);
    loginLayout->addRow(QStringLiteral("密码"), loginPasswordEdit_);
    loginLayout->addRow(QString(), loginButton_);
    authenticationTabs_->addTab(loginTab, QStringLiteral("登录"));

    auto* bootstrapTab = new QWidget(authenticationTabs_);
    auto* bootstrapLayout = new QFormLayout(bootstrapTab);
    bootstrapLayout->setContentsMargins(24, 24, 24, 24);
    bootstrapLayout->setSpacing(14);
    bootstrapDisplayNameEdit_ = new QLineEdit(bootstrapTab);
    bootstrapDisplayNameEdit_->setObjectName(
        QStringLiteral("bootstrapDisplayNameEdit")
    );
    bootstrapDisplayNameEdit_->setPlaceholderText(QStringLiteral("例如：系统管理员"));
    bootstrapPasswordEdit_ = new QLineEdit(bootstrapTab);
    bootstrapPasswordEdit_->setObjectName(QStringLiteral("bootstrapPasswordEdit"));
    bootstrapPasswordEdit_->setEchoMode(QLineEdit::Password);
    bootstrapPasswordEdit_->setPlaceholderText(QStringLiteral("至少 12 个字符"));
    bootstrapConfirmEdit_ = new QLineEdit(bootstrapTab);
    bootstrapConfirmEdit_->setObjectName(QStringLiteral("bootstrapConfirmEdit"));
    bootstrapConfirmEdit_->setEchoMode(QLineEdit::Password);
    bootstrapButton_ = new QPushButton(QStringLiteral("初始化管理员"), bootstrapTab);
    bootstrapButton_->setObjectName(QStringLiteral("bootstrapButton"));
    bootstrapLayout->addRow(QStringLiteral("显示名称"), bootstrapDisplayNameEdit_);
    bootstrapLayout->addRow(QStringLiteral("临时密码"), bootstrapPasswordEdit_);
    bootstrapLayout->addRow(QStringLiteral("确认密码"), bootstrapConfirmEdit_);
    bootstrapLayout->addRow(QString(), bootstrapButton_);
    authenticationTabs_->addTab(bootstrapTab, QStringLiteral("首次初始化"));

    authenticationMessageLabel_ = new QLabel(page);
    authenticationMessageLabel_->setObjectName(
        QStringLiteral("authenticationMessageLabel")
    );
    authenticationMessageLabel_->setWordWrap(true);
    authenticationMessageLabel_->setStyleSheet(
        QStringLiteral("QLabel { color: #b42318; min-height: 28px; }")
    );

    layout->addWidget(authenticationTabs_);
    layout->addWidget(authenticationMessageLabel_);
    layout->addStretch();

    connect(loginButton_, &QPushButton::clicked, this, [this]() { beginLogin(); });
    connect(loginPasswordEdit_, &QLineEdit::returnPressed,
            this, [this]() { beginLogin(); });
    connect(bootstrapButton_, &QPushButton::clicked,
            this, [this]() { beginBootstrap(); });
    connect(bootstrapConfirmEdit_, &QLineEdit::returnPressed,
            this, [this]() { beginBootstrap(); });
    return page;
}

QWidget* MainWindow::createPasswordChangePage() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("passwordChangePage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(180, 70, 180, 70);
    layout->setSpacing(18);
    layout->addWidget(pageTitle(QStringLiteral("首次登录必须修改密码"), page));

    auto* explanation = new QLabel(
        QStringLiteral("临时密码不能访问业务数据。请设置一个新的正式密码后继续。"),
        page
    );
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* form = new QFormLayout();
    form->setSpacing(14);
    currentPasswordEdit_ = new QLineEdit(page);
    currentPasswordEdit_->setObjectName(QStringLiteral("currentPasswordEdit"));
    currentPasswordEdit_->setEchoMode(QLineEdit::Password);
    newPasswordEdit_ = new QLineEdit(page);
    newPasswordEdit_->setObjectName(QStringLiteral("newPasswordEdit"));
    newPasswordEdit_->setEchoMode(QLineEdit::Password);
    newPasswordEdit_->setPlaceholderText(QStringLiteral("至少 12 个字符"));
    confirmPasswordEdit_ = new QLineEdit(page);
    confirmPasswordEdit_->setObjectName(QStringLiteral("confirmPasswordEdit"));
    confirmPasswordEdit_->setEchoMode(QLineEdit::Password);
    changePasswordButton_ = new QPushButton(QStringLiteral("修改密码并进入系统"), page);
    changePasswordButton_->setObjectName(QStringLiteral("changePasswordButton"));
    form->addRow(QStringLiteral("当前临时密码"), currentPasswordEdit_);
    form->addRow(QStringLiteral("新密码"), newPasswordEdit_);
    form->addRow(QStringLiteral("确认新密码"), confirmPasswordEdit_);
    form->addRow(QString(), changePasswordButton_);
    layout->addLayout(form);

    passwordChangeMessageLabel_ = new QLabel(page);
    passwordChangeMessageLabel_->setObjectName(
        QStringLiteral("passwordChangeMessageLabel")
    );
    passwordChangeMessageLabel_->setWordWrap(true);
    passwordChangeMessageLabel_->setStyleSheet(
        QStringLiteral("QLabel { color: #b42318; min-height: 28px; }")
    );
    layout->addWidget(passwordChangeMessageLabel_);
    layout->addStretch();

    connect(changePasswordButton_, &QPushButton::clicked,
            this, [this]() { beginPasswordChange(); });
    connect(confirmPasswordEdit_, &QLineEdit::returnPressed,
            this, [this]() { beginPasswordChange(); });
    return page;
}

QWidget* MainWindow::createMainPage() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("mainPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(14);

    auto* header = new QFrame(page);
    header->setFrameShape(QFrame::StyledPanel);
    auto* headerLayout = new QHBoxLayout(header);
    currentUserLabel_ = new QLabel(header);
    currentUserLabel_->setObjectName(QStringLiteral("currentUserLabel"));
    currentRoleLabel_ = new QLabel(header);
    currentRoleLabel_->setObjectName(QStringLiteral("currentRoleLabel"));
    refreshUserButton_ = new QPushButton(QStringLiteral("刷新身份"), header);
    refreshUserButton_->setObjectName(QStringLiteral("refreshUserButton"));
    logoutButton_ = new QPushButton(QStringLiteral("注销"), header);
    logoutButton_->setObjectName(QStringLiteral("logoutButton"));
    headerLayout->addWidget(currentUserLabel_);
    headerLayout->addWidget(currentRoleLabel_);
    headerLayout->addStretch();
    headerLayout->addWidget(refreshUserButton_);
    headerLayout->addWidget(logoutButton_);
    layout->addWidget(header);

    sessionMessageLabel_ = new QLabel(page);
    sessionMessageLabel_->setObjectName(QStringLiteral("sessionMessageLabel"));
    sessionMessageLabel_->setWordWrap(true);
    sessionMessageLabel_->setStyleSheet(
        QStringLiteral("QLabel { color: #b42318; min-height: 24px; }")
    );
    layout->addWidget(sessionMessageLabel_);

    moduleTabs_ = new QTabWidget(page);
    moduleTabs_->setObjectName(QStringLiteral("moduleTabs"));
    auto* overview = new QLabel(
        QStringLiteral(
            "登录和权限底座已经就绪。\n\n"
            "后续物料目录、BOM 与报价页面会作为独立模块加入这里。"
        ),
        moduleTabs_
    );
    overview->setAlignment(Qt::AlignCenter);
    overview->setWordWrap(true);
    moduleTabs_->addTab(overview, QStringLiteral("首页"));
    layout->addWidget(moduleTabs_, 1);

    connect(refreshUserButton_, &QPushButton::clicked,
            this, [this]() { beginRefreshCurrentUser(); });
    connect(logoutButton_, &QPushButton::clicked,
            this, [this]() { beginLogout(); });
    return page;
}

void MainWindow::beginLogin() {
    const auto username = loginUsernameEdit_->text().trimmed();
    const auto password = loginPasswordEdit_->text();
    if (username.isEmpty() || password.isEmpty()) {
        authenticationMessageLabel_->setText(
            QStringLiteral("请输入用户名和密码。")
        );
        return;
    }

    setAuthenticationBusy(true);
    authenticationMessageLabel_->setText(QStringLiteral("正在登录…"));
    apiClient_->login(
        username,
        password,
        [this](ApiResponse response) {
            setAuthenticationBusy(false);
            handleLoginResponse(std::move(response));
        }
    );
}

void MainWindow::beginBootstrap() {
    const auto displayName = bootstrapDisplayNameEdit_->text().trimmed();
    const auto password = bootstrapPasswordEdit_->text();
    if (displayName.isEmpty()) {
        authenticationMessageLabel_->setText(QStringLiteral("请输入管理员显示名称。"));
        return;
    }
    if (!passwordLooksValid(password)) {
        authenticationMessageLabel_->setText(
            QStringLiteral("密码至少需要 12 个字符，且不能全部为空格。")
        );
        return;
    }
    if (password != bootstrapConfirmEdit_->text()) {
        authenticationMessageLabel_->setText(QStringLiteral("两次输入的密码不一致。"));
        return;
    }

    setAuthenticationBusy(true);
    authenticationMessageLabel_->setText(QStringLiteral("正在初始化管理员…"));
    apiClient_->bootstrap(
        password,
        displayName,
        [this, password](ApiResponse response) {
            if (!response.succeeded()) {
                setAuthenticationBusy(false);
                authenticationMessageLabel_->setText(friendlyError(response));
                if (response.error.code == QStringLiteral("bootstrap_unavailable")) {
                    authenticationTabs_->setCurrentIndex(0);
                }
                return;
            }

            authenticationMessageLabel_->setText(
                QStringLiteral("初始化成功，正在使用管理员账号登录…")
            );
            bootstrapPasswordEdit_->clear();
            bootstrapConfirmEdit_->clear();
            apiClient_->login(
                QStringLiteral("admin"),
                password,
                [this](ApiResponse loginResponse) {
                    setAuthenticationBusy(false);
                    handleLoginResponse(std::move(loginResponse));
                }
            );
        }
    );
}

void MainWindow::beginPasswordChange() {
    const auto currentPassword = currentPasswordEdit_->text();
    const auto newPassword = newPasswordEdit_->text();
    if (currentPassword.isEmpty()) {
        passwordChangeMessageLabel_->setText(QStringLiteral("请输入当前临时密码。"));
        return;
    }
    if (!passwordLooksValid(newPassword)) {
        passwordChangeMessageLabel_->setText(
            QStringLiteral("新密码至少需要 12 个字符，且不能全部为空格。")
        );
        return;
    }
    if (newPassword != confirmPasswordEdit_->text()) {
        passwordChangeMessageLabel_->setText(QStringLiteral("两次输入的新密码不一致。"));
        return;
    }
    if (newPassword == currentPassword) {
        passwordChangeMessageLabel_->setText(QStringLiteral("新密码不能与临时密码相同。"));
        return;
    }

    setPasswordChangeBusy(true);
    passwordChangeMessageLabel_->setText(QStringLiteral("正在修改密码…"));
    apiClient_->changePassword(
        currentPassword,
        newPassword,
        [this](ApiResponse response) {
            setPasswordChangeBusy(false);
            if (!response.succeeded()) {
                passwordChangeMessageLabel_->setText(friendlyError(response));
                return;
            }
            currentPasswordEdit_->clear();
            newPasswordEdit_->clear();
            confirmPasswordEdit_->clear();
            showMainPage();
        }
    );
}

void MainWindow::beginRefreshCurrentUser() {
    setMainBusy(true);
    sessionMessageLabel_->setText(QStringLiteral("正在刷新当前用户…"));
    apiClient_->me([this](ApiResponse response) {
        setMainBusy(false);
        if (!response.succeeded()) {
            sessionMessageLabel_->setText(friendlyError(response));
            return;
        }
        sessionMessageLabel_->setText(QStringLiteral("当前用户信息已刷新。"));
        updateCurrentUserPresentation();
    });
}

void MainWindow::beginLogout() {
    setMainBusy(true);
    sessionMessageLabel_->setText(QStringLiteral("正在注销…"));
    apiClient_->logout([this](ApiResponse response) {
        setMainBusy(false);
        if (!response.succeeded()) {
            sessionMessageLabel_->setText(friendlyError(response));
            return;
        }
        loginPasswordEdit_->clear();
        showLoginPage(QStringLiteral("已安全注销。"));
    });
}

void MainWindow::handleLoginResponse(ApiResponse response) {
    loginPasswordEdit_->clear();
    if (!response.succeeded()) {
        authenticationMessageLabel_->setText(friendlyError(response));
        return;
    }

    authenticationMessageLabel_->clear();
    const auto mustChangePassword = apiClient_->session()
                                        .user
                                        .value(QStringLiteral("mustChangePassword"))
                                        .toBool(false);
    if (mustChangePassword) {
        showPasswordChangePage();
        return;
    }
    showMainPage();
}

void MainWindow::showLoginPage(const QString& message) {
    if (!rootStack_ || !authenticationPage_) {
        return;
    }
    rootStack_->setCurrentWidget(authenticationPage_);
    authenticationTabs_->setCurrentIndex(0);
    authenticationMessageLabel_->setText(message);
    loginPasswordEdit_->clear();
    loginUsernameEdit_->setFocus();
}

void MainWindow::showPasswordChangePage() {
    rootStack_->setCurrentWidget(passwordChangePage_);
    passwordChangeMessageLabel_->setText(
        QStringLiteral("请完成密码修改后再进入业务模块。")
    );
    currentPasswordEdit_->clear();
    newPasswordEdit_->clear();
    confirmPasswordEdit_->clear();
    currentPasswordEdit_->setFocus();
}

void MainWindow::showMainPage() {
    if (!apiClient_->isAuthenticated()) {
        showLoginPage(QStringLiteral("请先登录。"));
        return;
    }
    rootStack_->setCurrentWidget(mainPage_);
    sessionMessageLabel_->clear();
    updateCurrentUserPresentation();
}

void MainWindow::updateCurrentUserPresentation() {
    const auto user = apiClient_->session().user;
    const auto username = user.value(QStringLiteral("username"))
                              .toString(QStringLiteral("未知用户"));
    const auto displayName = user.value(QStringLiteral("displayName")).toString();
    currentUserLabel_->setText(
        displayName.isEmpty()
            ? QStringLiteral("当前用户：%1").arg(username)
            : QStringLiteral("当前用户：%1（%2）").arg(displayName, username)
    );
    const auto role = user.value(QStringLiteral("role")).toString();
    currentRoleLabel_->setText(
        QStringLiteral("角色：%1").arg(roleName(role))
    );
}

void MainWindow::setAuthenticationBusy(bool busy) {
    authenticationTabs_->setEnabled(!busy);
}

void MainWindow::setPasswordChangeBusy(bool busy) {
    currentPasswordEdit_->setEnabled(!busy);
    newPasswordEdit_->setEnabled(!busy);
    confirmPasswordEdit_->setEnabled(!busy);
    changePasswordButton_->setEnabled(!busy);
}

void MainWindow::setMainBusy(bool busy) {
    refreshUserButton_->setEnabled(!busy);
    logoutButton_->setEnabled(!busy);
}

} // namespace manage::desktop
