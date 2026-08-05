#include "manage/desktop/user_management_widget.h"

#include "manage/desktop/api_client.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>

namespace manage::desktop {

UserManagementWidget::UserManagementWidget(ApiClient* apiClient, QWidget* parent)
    : QWidget(parent), apiClient_(apiClient) {
    buildUi();
    connectUi();
    applySessionState();
}

void UserManagementWidget::buildUi() {
    setObjectName(QStringLiteral("userManagementWidget"));
    auto* root = new QVBoxLayout(this);
    auto* searchRow = new QHBoxLayout;
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("userSearchEdit"));
    searchEdit_->setPlaceholderText(QStringLiteral("搜索用户名或显示名称"));
    searchButton_ = new QPushButton(QStringLiteral("查询"), this);
    searchButton_->setObjectName(QStringLiteral("userSearchButton"));
    searchRow->addWidget(searchEdit_, 1);
    searchRow->addWidget(searchButton_);
    root->addLayout(searchRow);

    table_ = new QTableWidget(0, 7, this);
    table_->setObjectName(QStringLiteral("userTable"));
    table_->setHorizontalHeaderLabels({QStringLiteral("用户名"), QStringLiteral("显示名称"),
        QStringLiteral("角色"), QStringLiteral("状态"), QStringLiteral("临时密码"),
        QStringLiteral("版本"), QStringLiteral("编号")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    root->addWidget(table_, 1);

    auto* actions = new QHBoxLayout;
    previousButton_ = new QPushButton(QStringLiteral("上一页"), this);
    nextButton_ = new QPushButton(QStringLiteral("下一页"), this);
    pageLabel_ = new QLabel(this);
    newButton_ = new QPushButton(QStringLiteral("新建用户"), this);
    newButton_->setObjectName(QStringLiteral("newUserButton"));
    editButton_ = new QPushButton(QStringLiteral("编辑"), this);
    enabledButton_ = new QPushButton(QStringLiteral("启用/停用"), this);
    resetButton_ = new QPushButton(QStringLiteral("重置临时密码"), this);
    actions->addWidget(previousButton_);
    actions->addWidget(pageLabel_);
    actions->addWidget(nextButton_);
    actions->addStretch();
    actions->addWidget(newButton_);
    actions->addWidget(editButton_);
    actions->addWidget(enabledButton_);
    actions->addWidget(resetButton_);
    root->addLayout(actions);

    auto* form = new QFormLayout;
    usernameEdit_ = new QLineEdit(this);
    usernameEdit_->setObjectName(QStringLiteral("usernameEdit"));
    displayNameEdit_ = new QLineEdit(this);
    displayNameEdit_->setObjectName(QStringLiteral("userDisplayNameEdit"));
    roleCombo_ = new QComboBox(this);
    roleCombo_->setObjectName(QStringLiteral("userRoleCombo"));
    roleCombo_->addItem(QStringLiteral("管理员"), QStringLiteral("admin"));
    roleCombo_->addItem(QStringLiteral("报价员"), QStringLiteral("quoter"));
    roleCombo_->addItem(QStringLiteral("查看员"), QStringLiteral("viewer"));
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setObjectName(QStringLiteral("temporaryPasswordEdit"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("用户名"), usernameEdit_);
    form->addRow(QStringLiteral("显示名称"), displayNameEdit_);
    form->addRow(QStringLiteral("角色"), roleCombo_);
    form->addRow(QStringLiteral("临时密码"), passwordEdit_);
    root->addLayout(form);

    auto* formActions = new QHBoxLayout;
    saveButton_ = new QPushButton(QStringLiteral("保存"), this);
    saveButton_->setObjectName(QStringLiteral("saveUserButton"));
    cancelButton_ = new QPushButton(QStringLiteral("取消"), this);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("userStatusLabel"));
    formActions->addWidget(saveButton_);
    formActions->addWidget(cancelButton_);
    formActions->addWidget(statusLabel_, 1);
    root->addLayout(formActions);
}

void UserManagementWidget::connectUi() {
    connect(searchButton_, &QPushButton::clicked, this, [this] { page_ = 1; refresh(); });
    connect(searchEdit_, &QLineEdit::returnPressed, searchButton_, &QPushButton::click);
    connect(previousButton_, &QPushButton::clicked, this, [this] { page_ = std::max(1, page_ - 1); refresh(); });
    connect(nextButton_, &QPushButton::clicked, this, [this] { page_ = std::min(totalPages_, page_ + 1); refresh(); });
    connect(newButton_, &QPushButton::clicked, this, &UserManagementWidget::beginCreate);
    connect(editButton_, &QPushButton::clicked, this, &UserManagementWidget::beginEdit);
    connect(enabledButton_, &QPushButton::clicked, this, &UserManagementWidget::toggleEnabled);
    connect(resetButton_, &QPushButton::clicked, this, &UserManagementWidget::resetPassword);
    connect(saveButton_, &QPushButton::clicked, this, &UserManagementWidget::save);
    connect(cancelButton_, &QPushButton::clicked, this, [this] { beginCreate(); });
    if (apiClient_) {
        connect(apiClient_, &ApiClient::sessionChanged, this, [this] { applySessionState(); });
    }
}

void UserManagementWidget::applySessionState() {
    const auto ready = apiClient_ && apiClient_->isAuthenticated() &&
        apiClient_->session().user.value(QStringLiteral("role")).toString() == QStringLiteral("admin") &&
        !apiClient_->session().user.value(QStringLiteral("mustChangePassword")).toBool();
    setEnabled(ready);
    if (ready) {
        beginCreate();
        refresh();
    } else {
        table_->setRowCount(0);
        statusLabel_->setText(QStringLiteral("仅管理员完成临时密码修改后可管理用户"));
    }
}

void UserManagementWidget::refresh() {
    if (!apiClient_ || busy_) return;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("search"), searchEdit_->text().trimmed());
    query.addQueryItem(QStringLiteral("page"), QString::number(page_));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("20"));
    setBusy(true, QStringLiteral("正在读取用户…"));
    apiClient_->get(QStringLiteral("/api/v1/users?") + query.toString(QUrl::FullyEncoded),
        [this](ApiResponse response) { showPage(response); });
}

void UserManagementWidget::showPage(const ApiResponse& response) {
    setBusy(false);
    if (!response.succeeded()) {
        statusLabel_->setText(response.error.message);
        return;
    }
    users_ = response.body.value(QStringLiteral("items")).toArray();
    page_ = response.body.value(QStringLiteral("page")).toInt(1);
    const auto total = response.body.value(QStringLiteral("total")).toInteger();
    totalPages_ = std::max(1, static_cast<int>((total + 19) / 20));
    table_->setRowCount(users_.size());
    for (qsizetype row = 0; row < users_.size(); ++row) {
        const auto user = users_.at(row).toObject();
        const auto role = user.value(QStringLiteral("role")).toString();
        const auto roleText = role == QStringLiteral("admin") ? QStringLiteral("管理员")
            : role == QStringLiteral("quoter") ? QStringLiteral("报价员") : QStringLiteral("查看员");
        const QStringList values{
            user.value(QStringLiteral("username")).toString(),
            user.value(QStringLiteral("displayName")).toString(), roleText,
            user.value(QStringLiteral("enabled")).toBool() ? QStringLiteral("启用") : QStringLiteral("停用"),
            user.value(QStringLiteral("mustChangePassword")).toBool() ? QStringLiteral("待修改") : QStringLiteral("正常"),
            QString::number(user.value(QStringLiteral("revision")).toInt()),
            QString::number(user.value(QStringLiteral("id")).toInteger()),
        };
        for (int column = 0; column < values.size(); ++column)
            table_->setItem(row, column, new QTableWidgetItem(values.at(column)));
    }
    pageLabel_->setText(QStringLiteral("第 %1/%2 页，共 %3 个").arg(page_).arg(totalPages_).arg(total));
    previousButton_->setEnabled(page_ > 1);
    nextButton_->setEnabled(page_ < totalPages_);
    statusLabel_->setText(QStringLiteral("用户列表已更新"));
}

void UserManagementWidget::beginCreate() {
    editingExisting_ = false;
    usernameEdit_->clear();
    usernameEdit_->setReadOnly(false);
    displayNameEdit_->clear();
    roleCombo_->setCurrentIndex(2);
    passwordEdit_->clear();
    passwordEdit_->setEnabled(true);
}

int UserManagementWidget::selectedRow() const {
    return table_->currentRow();
}

QJsonObject UserManagementWidget::selectedUser() const {
    const auto row = selectedRow();
    return row >= 0 && row < users_.size() ? users_.at(row).toObject() : QJsonObject{};
}

void UserManagementWidget::beginEdit() {
    const auto user = selectedUser();
    if (user.isEmpty()) return;
    editingExisting_ = true;
    usernameEdit_->setText(user.value(QStringLiteral("username")).toString());
    usernameEdit_->setReadOnly(true);
    displayNameEdit_->setText(user.value(QStringLiteral("displayName")).toString());
    roleCombo_->setCurrentIndex(roleCombo_->findData(user.value(QStringLiteral("role")).toString()));
    passwordEdit_->clear();
    passwordEdit_->setEnabled(false);
}

void UserManagementWidget::save() {
    if (!apiClient_ || busy_) return;
    QJsonObject body{{QStringLiteral("displayName"), displayNameEdit_->text().trimmed()},
                     {QStringLiteral("role"), roleCombo_->currentData().toString()}};
    QString path = QStringLiteral("/api/v1/users");
    if (editingExisting_) {
        const auto user = selectedUser();
        if (user.isEmpty()) return;
        path += QStringLiteral("/") + QString::number(user.value(QStringLiteral("id")).toInteger());
        body.insert(QStringLiteral("revision"), user.value(QStringLiteral("revision")));
    } else {
        body.insert(QStringLiteral("username"), usernameEdit_->text().trimmed());
        body.insert(QStringLiteral("temporaryPassword"), passwordEdit_->text());
    }
    setBusy(true, QStringLiteral("正在保存…"));
    const auto callback = [this](ApiResponse response) {
        setBusy(false);
        if (!response.succeeded()) { statusLabel_->setText(response.error.message); return; }
        beginCreate();
        refresh();
    };
    if (editingExisting_) apiClient_->put(path, body, callback);
    else apiClient_->post(path, body, callback);
}

void UserManagementWidget::toggleEnabled() {
    const auto user = selectedUser();
    if (!apiClient_ || busy_ || user.isEmpty()) return;
    QJsonObject body{{QStringLiteral("enabled"), !user.value(QStringLiteral("enabled")).toBool()},
                     {QStringLiteral("revision"), user.value(QStringLiteral("revision"))}};
    setBusy(true, QStringLiteral("正在更新状态…"));
    apiClient_->patch(QStringLiteral("/api/v1/users/%1/enabled").arg(user.value(QStringLiteral("id")).toInteger()), body,
        [this](ApiResponse response) {
            setBusy(false);
            if (!response.succeeded()) { statusLabel_->setText(response.error.message); return; }
            refresh();
        });
}

void UserManagementWidget::resetPassword() {
    const auto user = selectedUser();
    if (!apiClient_ || busy_ || user.isEmpty()) return;
    passwordEdit_->setEnabled(true);
    const auto password = passwordEdit_->text();
    if (password.isEmpty()) {
        statusLabel_->setText(QStringLiteral("请在临时密码框输入新密码后再重置"));
        return;
    }
    QJsonObject body{{QStringLiteral("temporaryPassword"), password},
                     {QStringLiteral("revision"), user.value(QStringLiteral("revision"))}};
    setBusy(true, QStringLiteral("正在重置密码…"));
    apiClient_->post(QStringLiteral("/api/v1/users/%1/reset-password").arg(user.value(QStringLiteral("id")).toInteger()), body,
        [this](ApiResponse response) {
            setBusy(false);
            passwordEdit_->clear();
            if (!response.succeeded()) { statusLabel_->setText(response.error.message); return; }
            refresh();
        });
}

void UserManagementWidget::setBusy(bool busy, const QString& message) {
    busy_ = busy;
    searchButton_->setEnabled(!busy);
    newButton_->setEnabled(!busy);
    editButton_->setEnabled(!busy);
    enabledButton_->setEnabled(!busy);
    resetButton_->setEnabled(!busy);
    saveButton_->setEnabled(!busy);
    if (!message.isEmpty()) statusLabel_->setText(message);
}

} // namespace manage::desktop
