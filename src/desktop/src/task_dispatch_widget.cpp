#include "manage/desktop/task_dispatch_widget.h"

#include "manage/desktop/api_client.h"

#include <QDateTime>
#include <QDateTimeEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace manage::desktop {
namespace {

QString statusText(const QString& code) {
    if (code == QStringLiteral("dispatched")) return QStringLiteral("已派发");
    if (code == QStringLiteral("in_progress")) return QStringLiteral("处理中");
    if (code == QStringLiteral("completed")) return QStringLiteral("已完成");
    if (code == QStringLiteral("cancelled")) return QStringLiteral("已取消");
    return code;
}

QString optionalDateTimeText(const QJsonValue& value) {
    if (value.isNull() || !value.isString()) {
        return QStringLiteral("—");
    }
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs)
        .toLocalTime()
        .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

} // namespace

TaskDispatchWidget::TaskDispatchWidget(ApiClient* apiClient, QWidget* parent)
    : QWidget(parent), apiClient_(apiClient) {
    buildUi();
    connectUi();
    applySessionState();
}

void TaskDispatchWidget::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 16);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("任务派发（销售 → 工程师）"), this);
    auto font = title->font();
    font.setPointSize(16);
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);

    auto* hint = new QLabel(
        QStringLiteral("销售只需填负责工程师与预期完成时间；工程师在任务下做 BOM/报价并推进状态。"),
        this
    );
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* searchBar = new QHBoxLayout();
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(QStringLiteral("按任务编号或标题搜索"));
    searchButton_ = new QPushButton(QStringLiteral("搜索"), this);
    refreshButton_ = new QPushButton(QStringLiteral("刷新"), this);
    searchBar->addWidget(searchEdit_, 1);
    searchBar->addWidget(searchButton_);
    searchBar->addWidget(refreshButton_);
    layout->addLayout(searchBar);

    taskTable_ = new QTableWidget(0, 6, this);
    taskTable_->setHorizontalHeaderLabels({
        QStringLiteral("任务编号"),
        QStringLiteral("标题"),
        QStringLiteral("工程师ID"),
        QStringLiteral("状态"),
        QStringLiteral("预期完成"),
        QStringLiteral("关联报价ID"),
    });
    taskTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    taskTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    taskTable_->horizontalHeader()->setStretchLastSection(true);
    taskTable_->verticalHeader()->setVisible(false);
    layout->addWidget(taskTable_, 1);

    auto* pager = new QHBoxLayout();
    previousButton_ = new QPushButton(QStringLiteral("上一页"), this);
    nextButton_ = new QPushButton(QStringLiteral("下一页"), this);
    pageLabel_ = new QLabel(this);
    pager->addWidget(previousButton_);
    pager->addWidget(pageLabel_);
    pager->addWidget(nextButton_);
    pager->addStretch();
    layout->addLayout(pager);

    auto* formGroup = new QFormLayout();
    formGroup->setSpacing(8);
    engineerIdEdit_ = new QLineEdit(this);
    engineerIdEdit_->setPlaceholderText(QStringLiteral("工程师账号 ID（数字）"));
    customerIdEdit_ = new QLineEdit(this);
    customerIdEdit_->setPlaceholderText(QStringLiteral("可选，客户 ID（数字）"));
    titleEdit_ = new QLineEdit(this);
    titleEdit_->setPlaceholderText(QStringLiteral("任务标题，如：亚马逊 UF02 报价"));
    notesEdit_ = new QLineEdit(this);
    notesEdit_->setPlaceholderText(QStringLiteral("备注"));
    expectedEdit_ = new QDateTimeEdit(
        QDateTime::currentDateTime().addDays(7), this
    );
    expectedEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    expectedEdit_->setCalendarPopup(true);
    saveButton_ = new QPushButton(QStringLiteral("派发任务"), this);
    cancelButton_ = new QPushButton(QStringLiteral("取消"), this);
    formGroup->addRow(QStringLiteral("负责工程师ID"), engineerIdEdit_);
    formGroup->addRow(QStringLiteral("客户ID（可选）"), customerIdEdit_);
    formGroup->addRow(QStringLiteral("标题"), titleEdit_);
    formGroup->addRow(QStringLiteral("备注"), notesEdit_);
    formGroup->addRow(QStringLiteral("预期完成时间"), expectedEdit_);
    auto* formActions = new QHBoxLayout();
    formActions->addWidget(saveButton_);
    formActions->addWidget(cancelButton_);
    formActions->addStretch();
    formGroup->addRow(QString(), formActions);
    layout->addLayout(formGroup);

    auto* taskActions = new QHBoxLayout();
    advanceButton_ = new QPushButton(QStringLiteral("推进状态"), this);
    cancelTaskButton_ = new QPushButton(QStringLiteral("取消任务"), this);
    newButton_ = new QPushButton(QStringLiteral("新建任务"), this);
    taskActions->addWidget(newButton_);
    taskActions->addWidget(advanceButton_);
    taskActions->addWidget(cancelTaskButton_);
    taskActions->addStretch();
    layout->addLayout(taskActions);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(QStringLiteral("QLabel { color: #b42318; min-height: 24px; }"));
    layout->addWidget(statusLabel_);
}

void TaskDispatchWidget::connectUi() {
    connect(searchButton_, &QPushButton::clicked, this, [this]() {
        page_ = 1;
        loadTasks();
    });
    connect(refreshButton_, &QPushButton::clicked, this, [this]() { loadTasks(); });
    connect(previousButton_, &QPushButton::clicked, this, [this]() {
        if (page_ > 1) { --page_; loadTasks(); }
    });
    connect(nextButton_, &QPushButton::clicked, this, [this]() {
        if (page_ < totalPages_) { ++page_; loadTasks(); }
    });
    connect(newButton_, &QPushButton::clicked, this, [this]() { startCreate(); });
    connect(saveButton_, &QPushButton::clicked, this, [this]() { saveTask(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this]() { cancelEdit(); });
    connect(advanceButton_, &QPushButton::clicked, this, [this]() { advanceStatus(); });
    connect(cancelTaskButton_, &QPushButton::clicked, this, [this]() { cancelTask(); });
    connect(taskTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto row = selectedRow();
        if (row < 0 || row >= tasks_.size()) {
            currentId_ = 0;
            currentRevision_ = 0;
            currentStatus_.clear();
            updateControls();
            return;
        }
        const auto task = tasks_.at(row).toObject();
        currentId_ = task.value(QStringLiteral("id")).toVariant().toLongLong();
        currentRevision_ = task.value(QStringLiteral("revision")).toInt();
        currentStatus_ = task.value(QStringLiteral("status")).toString();
        updateControls();
    });

    if (apiClient_ != nullptr) {
        connect(apiClient_, &ApiClient::sessionChanged, this, [this]() {
            applySessionState();
        });
    }
}

void TaskDispatchWidget::applySessionState() {
    if (apiClient_ == nullptr) {
        return;
    }
    const auto& session = apiClient_->session();
    if (session.authenticated()) {
        userId_ = session.user.value(QStringLiteral("id")).toVariant().toLongLong();
        role_ = session.user.value(QStringLiteral("role")).toString();
        mustChangePassword_ = session.user
            .value(QStringLiteral("mustChangePassword"), false).toBool();
    } else {
        userId_ = 0;
        role_.clear();
        mustChangePassword_ = false;
    }
    updateControls();
    if (sessionReady()) {
        loadTasks();
    }
}

void TaskDispatchWidget::updateControls() {
    const auto ready = sessionReady();
    const auto writable = ready && canWrite();
    searchButton_->setEnabled(ready);
    refreshButton_->setEnabled(ready);
    taskTable_->setEnabled(ready);
    newButton_->setEnabled(writable);
    saveButton_->setEnabled(writable && creating_);
    cancelButton_->setEnabled(creating_);
    engineerIdEdit_->setEnabled(writable && creating_);
    customerIdEdit_->setEnabled(writable && creating_);
    titleEdit_->setEnabled(writable && creating_);
    notesEdit_->setEnabled(writable && creating_);
    expectedEdit_->setEnabled(writable && creating_);

    const bool selectable = ready && currentId_ > 0;
    const bool active = selectable &&
        (currentStatus_ == QStringLiteral("dispatched") ||
         currentStatus_ == QStringLiteral("in_progress"));
    advanceButton_->setEnabled(writable && active);
    cancelTaskButton_->setEnabled(writable && active);
}

bool TaskDispatchWidget::sessionReady() const {
    return apiClient_ != nullptr && apiClient_->isAuthenticated() &&
        !mustChangePassword_;
}

bool TaskDispatchWidget::canWrite() const {
    return role_ == QStringLiteral("admin") || role_ == QStringLiteral("quoter");
}

int TaskDispatchWidget::selectedRow() const {
    const auto rows = taskTable_->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

qint64 TaskDispatchWidget::currentUserId() const {
    return userId_;
}

QString TaskDispatchWidget::errorText(const ApiResponse& response) const {
    const auto& error = response.error;
    if (error.kind == ApiErrorKind::Network) {
        return QStringLiteral("无法连接本地服务：%1").arg(error.message);
    }
    if (error.kind == ApiErrorKind::InvalidResponse) {
        return QStringLiteral("本地服务返回了无法识别的数据。");
    }
    if (!error.message.isEmpty()) {
        return QStringLiteral("操作失败：%1").arg(error.message);
    }
    return QStringLiteral("操作失败，请稍后重试。");
}

void TaskDispatchWidget::loadTasks() {
    if (!sessionReady() || apiClient_ == nullptr) {
        return;
    }
    busy_ = true;
    updateControls();
    statusLabel_->setText(QStringLiteral("正在加载任务…"));
    QString path = QStringLiteral("/api/v1/tasks?page=%1&pageSize=%2")
        .arg(page_)
        .arg(20);
    const auto search = searchEdit_->text().trimmed();
    if (!search.isEmpty()) {
        path += QStringLiteral("&search=%1").arg(search);
    }
    apiClient_->get(path, [this](ApiResponse response) {
        busy_ = false;
        showTasks(response);
        updateControls();
    });
}

void TaskDispatchWidget::showTasks(const ApiResponse& response) {
    if (!response.succeeded()) {
        statusLabel_->setText(errorText(response));
        return;
    }
    statusLabel_->clear();
    const auto body = response.body;
    tasks_ = body.value(QStringLiteral("items")).toArray();
    total_ = body.value(QStringLiteral("total")).toVariant().toLongLong();
    totalPages_ = body.value(QStringLiteral("totalPages")).toInt();
    if (totalPages_ < 1) totalPages_ = 1;

    taskTable_->setRowCount(static_cast<int>(tasks_.size()));
    for (int i = 0; i < tasks_.size(); ++i) {
        const auto task = tasks_.at(i).toObject();
        taskTable_->setItem(i, 0, new QTableWidgetItem(
            task.value(QStringLiteral("taskNumber")).toString()
        ));
        taskTable_->setItem(i, 1, new QTableWidgetItem(
            task.value(QStringLiteral("title")).toString()
        ));
        taskTable_->setItem(i, 2, new QTableWidgetItem(
            task.value(QStringLiteral("assignedEngineerId")).toVariant().toString()
        ));
        taskTable_->setItem(i, 3, new QTableWidgetItem(
            statusText(task.value(QStringLiteral("status")).toString())
        ));
        taskTable_->setItem(i, 4, new QTableWidgetItem(
            optionalDateTimeText(task.value(QStringLiteral("expectedCompletionAt")))
        ));
        const auto quoteId = task.value(QStringLiteral("quoteId"));
        taskTable_->setItem(i, 5, new QTableWidgetItem(
            quoteId.isNull() ? QStringLiteral("—")
                             : quoteId.toVariant().toString()
        ));
    }
    pageLabel_->setText(QStringLiteral("第 %1 / %2 页（共 %3 条）")
        .arg(page_).arg(totalPages_).arg(total_));
}

void TaskDispatchWidget::startCreate() {
    creating_ = true;
    engineerIdEdit_->clear();
    customerIdEdit_->clear();
    titleEdit_->clear();
    notesEdit_->clear();
    expectedEdit_->setDateTime(QDateTime::currentDateTime().addDays(7));
    statusLabel_->clear();
    updateControls();
    engineerIdEdit_->setFocus();
}

void TaskDispatchWidget::saveTask() {
    if (!sessionReady() || apiClient_ == nullptr) {
        return;
    }
    bool engineerOk = false;
    const auto engineerId = engineerIdEdit_->text().trimmed().toLongLong(&engineerOk);
    if (!engineerOk || engineerId <= 0) {
        statusLabel_->setText(QStringLiteral("请填写有效的负责工程师 ID。"));
        return;
    }
    if (engineerId == currentUserId()) {
        statusLabel_->setText(QStringLiteral("负责工程师不能与派单人（当前销售）相同。"));
        return;
    }
    if (titleEdit_->text().trimmed().isEmpty()) {
        statusLabel_->setText(QStringLiteral("请填写任务标题。"));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("dispatchedBy"),
        QJsonValue(static_cast<qint64>(currentUserId())));
    body.insert(QStringLiteral("assignedEngineerId"),
        QJsonValue(static_cast<qint64>(engineerId)));
    bool customerOk = false;
    const auto customerId =
        customerIdEdit_->text().trimmed().toLongLong(&customerOk);
    if (customerOk && customerId > 0) {
        body.insert(QStringLiteral("customerId"),
            QJsonValue(static_cast<qint64>(customerId)));
    }
    body.insert(QStringLiteral("title"), titleEdit_->text().trimmed());
    body.insert(QStringLiteral("notes"), notesEdit_->text().trimmed());
    body.insert(QStringLiteral("expectedCompletionAt"),
        expectedEdit_->dateTime().toUTC().toString(Qt::ISODateWithMs));

    busy_ = true;
    updateControls();
    statusLabel_->setText(QStringLiteral("正在派发任务…"));
    apiClient_->post(QStringLiteral("/api/v1/tasks"), body, [this](ApiResponse response) {
        busy_ = false;
        if (!response.succeeded()) {
            statusLabel_->setText(errorText(response));
            updateControls();
            return;
        }
        creating_ = false;
        updateControls();
        loadTasks();
    });
}

void TaskDispatchWidget::cancelEdit() {
    creating_ = false;
    updateControls();
}

void TaskDispatchWidget::advanceStatus() {
    if (currentId_ <= 0 || apiClient_ == nullptr) {
        return;
    }
    QString next;
    if (currentStatus_ == QStringLiteral("dispatched")) {
        next = QStringLiteral("in_progress");
    } else if (currentStatus_ == QStringLiteral("in_progress")) {
        next = QStringLiteral("completed");
    } else {
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("revision"), QJsonValue(static_cast<qint64>(currentRevision_)));
    body.insert(QStringLiteral("status"), next);
    busy_ = true;
    updateControls();
    statusLabel_->setText(QStringLiteral("正在推进状态…"));
    apiClient_->patch(
        QStringLiteral("/api/v1/tasks/%1/status").arg(currentId_),
        body,
        [this](ApiResponse response) {
            busy_ = false;
            if (!response.succeeded()) {
                statusLabel_->setText(errorText(response));
                updateControls();
                return;
            }
            loadTasks();
        }
    );
}

void TaskDispatchWidget::cancelTask() {
    if (currentId_ <= 0 || apiClient_ == nullptr) {
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("revision"), QJsonValue(static_cast<qint64>(currentRevision_)));
    body.insert(QStringLiteral("status"), QStringLiteral("cancelled"));
    busy_ = true;
    updateControls();
    statusLabel_->setText(QStringLiteral("正在取消任务…"));
    apiClient_->patch(
        QStringLiteral("/api/v1/tasks/%1/status").arg(currentId_),
        body,
        [this](ApiResponse response) {
            busy_ = false;
            if (!response.succeeded()) {
                statusLabel_->setText(errorText(response));
                updateControls();
                return;
            }
            loadTasks();
        }
    );
}

} // namespace manage::desktop
