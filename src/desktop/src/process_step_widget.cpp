#include "manage/desktop/process_step_widget.h"

#include "manage/desktop/api_client.h"

#include <QAbstractItemView>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace manage::desktop {
namespace {

constexpr int kPageSize = 20;
constexpr int kIdRole = Qt::UserRole;

template <typename T>
T* named(T* object, const char* name) {
    object->setObjectName(QString::fromLatin1(name));
    return object;
}

QTableWidgetItem* readOnly(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

bool integerText(QString text, int* output) {
    static const QRegularExpression format(QStringLiteral(R"(^\s*(\d+)\s*$)"));
    const auto match = format.match(text);
    if (!match.hasMatch()) {
        return false;
    }
    bool ok = false;
    const auto value = match.captured(1).toInt(&ok);
    if (!ok || value < 0) {
        return false;
    }
    *output = value;
    return true;
}

} // namespace

ProcessStepWidget::ProcessStepWidget(ApiClient* apiClient, QWidget* parent)
    : QWidget(parent), apiClient_(apiClient) {
    setObjectName(QStringLiteral("processStepWidget"));
    buildUi();
    connectUi();
    applySessionState();
    if (apiClient_) {
        connect(apiClient_, &ApiClient::sessionChanged, this, [this](bool) {
            applySessionState();
        });
    }
    QTimer::singleShot(0, this, [this] {
        if (sessionReady()) {
            loadSteps();
        }
    });
}

void ProcessStepWidget::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);

    auto* filters = new QHBoxLayout;
    searchEdit_ = named(new QLineEdit(this), "processStepSearchEdit");
    searchEdit_->setPlaceholderText(QStringLiteral("搜索工序编码或名称"));
    searchButton_ = named(new QPushButton(QStringLiteral("搜索 / 刷新"), this), "processStepSearchButton");
    filters->addWidget(searchEdit_, 1);
    filters->addWidget(searchButton_);
    root->addLayout(filters);

    stepTable_ = named(new QTableWidget(0, 6, this), "processStepTable");
    stepTable_->setHorizontalHeaderLabels({
        QStringLiteral("工序编码"), QStringLiteral("工序名称"), QStringLiteral("单人工时（分钟）"),
        QStringLiteral("说明"), QStringLiteral("状态"), QStringLiteral("版本"),
    });
    stepTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    stepTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    stepTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    stepTable_->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(stepTable_, 1);

    auto* actions = new QHBoxLayout;
    newButton_ = named(new QPushButton(QStringLiteral("新增工序"), this), "processStepNewButton");
    editButton_ = named(new QPushButton(QStringLiteral("编辑工序"), this), "processStepEditButton");
    enableButton_ = named(new QPushButton(QStringLiteral("启用"), this), "processStepEnableButton");
    disableButton_ = named(new QPushButton(QStringLiteral("停用"), this), "processStepDisableButton");
    refreshButton_ = named(new QPushButton(QStringLiteral("刷新列表"), this), "processStepRefreshButton");
    previousButton_ = named(new QPushButton(QStringLiteral("上一页"), this), "processStepPreviousButton");
    nextButton_ = named(new QPushButton(QStringLiteral("下一页"), this), "processStepNextButton");
    pageLabel_ = named(new QLabel(this), "processStepPageLabel");
    actions->addWidget(newButton_);
    actions->addWidget(editButton_);
    actions->addWidget(enableButton_);
    actions->addWidget(disableButton_);
    actions->addWidget(refreshButton_);
    actions->addStretch();
    actions->addWidget(previousButton_);
    actions->addWidget(pageLabel_);
    actions->addWidget(nextButton_);
    root->addLayout(actions);

    auto* editorGroup = named(new QGroupBox(QStringLiteral("工序编辑"), this), "processStepEditorGroup");
    auto* form = new QFormLayout(editorGroup);
    codeEdit_ = named(new QLineEdit(editorGroup), "processStepCodeEdit");
    codeEdit_->setPlaceholderText(QStringLiteral("如 PLATE-01，仅字母数字与 . _ -"));
    nameEdit_ = named(new QLineEdit(editorGroup), "processStepNameEdit");
    laborMinutesEdit_ = named(new QLineEdit(editorGroup), "processStepLaborMinutesEdit");
    laborMinutesEdit_->setPlaceholderText(QStringLiteral("单人完成该工序所需分钟数，0-1440"));
    descriptionEdit_ = named(new QLineEdit(editorGroup), "processStepDescriptionEdit");
    form->addRow(QStringLiteral("工序编码"), codeEdit_);
    form->addRow(QStringLiteral("工序名称"), nameEdit_);
    form->addRow(QStringLiteral("单人工时（分钟）"), laborMinutesEdit_);
    form->addRow(QStringLiteral("说明"), descriptionEdit_);
    auto* editorActions = new QHBoxLayout;
    saveButton_ = named(new QPushButton(QStringLiteral("保存工序"), editorGroup), "processStepSaveButton");
    cancelButton_ = named(new QPushButton(QStringLiteral("取消"), editorGroup), "processStepCancelButton");
    editorActions->addWidget(saveButton_);
    editorActions->addWidget(cancelButton_);
    editorActions->addStretch();
    form->addRow(editorActions);
    root->addWidget(editorGroup);

    statusLabel_ = named(new QLabel(QStringLiteral("请登录后维护工序库。"), this), "processStepStatusLabel");
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);
}

void ProcessStepWidget::connectUi() {
    connect(searchButton_, &QPushButton::clicked, this, &ProcessStepWidget::loadSteps);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &ProcessStepWidget::loadSteps);
    connect(stepTable_, &QTableWidget::itemSelectionChanged, this, &ProcessStepWidget::updateControls);
    connect(newButton_, &QPushButton::clicked, this, &ProcessStepWidget::startCreate);
    connect(editButton_, &QPushButton::clicked, this, &ProcessStepWidget::beginEdit);
    connect(enableButton_, &QPushButton::clicked, this, [this] { toggleEnabled(); });
    connect(disableButton_, &QPushButton::clicked, this, [this] { toggleEnabled(); });
    connect(refreshButton_, &QPushButton::clicked, this, &ProcessStepWidget::refreshList);
    connect(previousButton_, &QPushButton::clicked, this, [this] { --page_; loadSteps(); });
    connect(nextButton_, &QPushButton::clicked, this, [this] { ++page_; loadSteps(); });
    connect(saveButton_, &QPushButton::clicked, this, &ProcessStepWidget::saveStep);
    connect(cancelButton_, &QPushButton::clicked, this, [this] {
        editing_ = false;
        currentId_ = 0;
        currentRevision_ = 0;
        codeEdit_->clear();
        nameEdit_->clear();
        laborMinutesEdit_->clear();
        descriptionEdit_->clear();
        updateControls();
    });
}

void ProcessStepWidget::applySessionState() {
    role_.clear();
    mustChangePassword_ = false;
    if (apiClient_ && apiClient_->isAuthenticated()) {
        const auto user = apiClient_->session().user;
        role_ = user.value(QStringLiteral("role")).toString();
        mustChangePassword_ = user.value(QStringLiteral("mustChangePassword")).toBool(true);
    }
    if (!sessionReady()) {
        steps_ = {};
        stepTable_->setRowCount(0);
        statusLabel_->setText(apiClient_ && apiClient_->isAuthenticated()
            ? QStringLiteral("请先修改临时密码，再维护工序库。")
            : QStringLiteral("请先登录后查看工序库。"));
    } else {
        loadSteps();
    }
    updateControls();
}

bool ProcessStepWidget::sessionReady() const {
    return apiClient_ && apiClient_->isAuthenticated() && !mustChangePassword_;
}

bool ProcessStepWidget::canWrite() const {
    return sessionReady() && (role_ == QStringLiteral("admin") || role_ == QStringLiteral("quoter"));
}

int ProcessStepWidget::selectedRow() const {
    const auto rows = stepTable_->selectionModel()->selectedRows();
    return rows.isEmpty() || rows.first().row() >= steps_.size() ? -1 : rows.first().row();
}

void ProcessStepWidget::updateControls() {
    const auto ready = sessionReady();
    const auto write = canWrite();
    const auto row = selectedRow();
    const auto selectedEnabled = row >= 0
        ? steps_.at(row).toObject().value(QStringLiteral("isEnabled")).toBool()
        : false;
    searchEdit_->setEnabled(ready && !busy_);
    searchButton_->setEnabled(ready && !busy_);
    refreshButton_->setEnabled(ready && !busy_);
    newButton_->setEnabled(write && !busy_);
    editButton_->setEnabled(write && !busy_ && row >= 0);
    enableButton_->setEnabled(write && !busy_ && row >= 0 && !selectedEnabled);
    disableButton_->setEnabled(write && !busy_ && row >= 0 && selectedEnabled);
    previousButton_->setEnabled(ready && !busy_ && page_ > 1);
    nextButton_->setEnabled(ready && !busy_ && page_ < totalPages_);
    codeEdit_->setEnabled(write && editing_ && !busy_);
    nameEdit_->setEnabled(write && editing_ && !busy_);
    laborMinutesEdit_->setEnabled(write && editing_ && !busy_);
    descriptionEdit_->setEnabled(write && editing_ && !busy_);
    saveButton_->setEnabled(write && editing_ && !busy_);
    cancelButton_->setEnabled(editing_ && !busy_);
}

QString ProcessStepWidget::errorText(const ApiResponse& response) const {
    if (response.error.kind == ApiErrorKind::Network) return QStringLiteral("网络连接失败，请确认本地服务已启动。");
    if (response.httpStatus == 401) return QStringLiteral("登录已失效，请重新登录。");
    if (response.httpStatus == 403) return QStringLiteral("当前账号没有工序维护权限。");
    if (response.httpStatus == 409) return QStringLiteral("操作冲突：工序可能已被修改，请刷新后重试。");
    if (response.httpStatus == 404) return QStringLiteral("工序不存在或已被删除，请刷新列表。");
    return QStringLiteral("操作失败（%1）：%2").arg(
        response.error.code.isEmpty() ? QStringLiteral("unknown") : response.error.code,
        response.error.message.isEmpty() ? QStringLiteral("未知错误") : response.error.message
    );
}

void ProcessStepWidget::loadSteps() {
    if (!sessionReady() || busy_) return;
    busy_ = true;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(page_));
    query.addQueryItem(QStringLiteral("pageSize"), QString::number(kPageSize));
    if (!searchEdit_->text().trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("search"), searchEdit_->text().trimmed());
    }
    apiClient_->get(QStringLiteral("/api/v1/process-steps?%1").arg(query.toString(QUrl::FullyEncoded)),
        [self = QPointer<ProcessStepWidget>(this)](ApiResponse response) {
            if (self) {
                self->busy_ = false;
                self->showSteps(response);
            }
        });
}

void ProcessStepWidget::showSteps(const ApiResponse& response) {
    if (!sessionReady()) return;
    if (!response.succeeded()) {
        statusLabel_->setText(errorText(response));
        updateControls();
        return;
    }
    const auto values = response.body.value(QStringLiteral("items"));
    if (!values.isArray()) {
        statusLabel_->setText(QStringLiteral("服务器返回的工序列表格式不正确。"));
        updateControls();
        return;
    }
    steps_ = values.toArray();
    stepTable_->setRowCount(0);
    for (const auto& value : steps_) {
        const auto step = value.toObject();
        const auto row = stepTable_->rowCount();
        stepTable_->insertRow(row);
        stepTable_->setItem(row, 0, readOnly(step.value(QStringLiteral("code")).toString()));
        stepTable_->setItem(row, 1, readOnly(step.value(QStringLiteral("name")).toString()));
        stepTable_->setItem(row, 2, readOnly(QString::number(step.value(QStringLiteral("laborMinutes")).toInteger())));
        stepTable_->setItem(row, 3, readOnly(step.value(QStringLiteral("description")).toString()));
        stepTable_->setItem(row, 4, readOnly(step.value(QStringLiteral("isEnabled")).toBool() ? QStringLiteral("启用") : QStringLiteral("停用")));
        stepTable_->setItem(row, 5, readOnly(QString::number(step.value(QStringLiteral("revision")).toInteger())));
    }
    total_ = qMax<qint64>(0, response.body.value(QStringLiteral("total")).toInteger());
    page_ = qMax(1, response.body.value(QStringLiteral("page")).toInt(page_));
    const auto pageSize = qMax(1, response.body.value(QStringLiteral("pageSize")).toInt(kPageSize));
    totalPages_ = total_ == 0 ? 1 : static_cast<int>((total_ + pageSize - 1) / pageSize);
    pageLabel_->setText(QStringLiteral("%1 / %2 页，共 %3 条").arg(page_).arg(totalPages_).arg(total_));
    statusLabel_->setText(QStringLiteral("工序列表已加载。工时单位：单人完成该工序所需分钟数。"));
    updateControls();
}

void ProcessStepWidget::startCreate() {
    if (!canWrite() || busy_) return;
    editing_ = true;
    currentId_ = 0;
    currentRevision_ = 0;
    codeEdit_->clear();
    nameEdit_->clear();
    laborMinutesEdit_->clear();
    descriptionEdit_->clear();
    statusLabel_->setText(QStringLiteral("请填写新工序信息后保存。"));
    updateControls();
}

void ProcessStepWidget::beginEdit() {
    if (!canWrite() || busy_) return;
    const auto row = selectedRow();
    if (row < 0) return;
    const auto step = steps_.at(row).toObject();
    editing_ = true;
    currentId_ = step.value(QStringLiteral("id")).toInteger();
    currentRevision_ = step.value(QStringLiteral("revision")).toInteger();
    codeEdit_->setText(step.value(QStringLiteral("code")).toString());
    nameEdit_->setText(step.value(QStringLiteral("name")).toString());
    laborMinutesEdit_->setText(QString::number(step.value(QStringLiteral("laborMinutes")).toInteger()));
    descriptionEdit_->setText(step.value(QStringLiteral("description")).toString());
    statusLabel_->setText(QStringLiteral("请修改工序信息后保存。"));
    updateControls();
}

void ProcessStepWidget::saveStep() {
    if (!canWrite() || !editing_ || busy_) return;
    const auto code = codeEdit_->text().trimmed();
    const auto name = nameEdit_->text().trimmed();
    const auto description = descriptionEdit_->text().trimmed();
    int laborMinutes = 0;
    if (code.isEmpty() || name.isEmpty() || !integerText(laborMinutesEdit_->text(), &laborMinutes)) {
        statusLabel_->setText(QStringLiteral("工序编码与名称不能为空，单人工时须为 0-1440 的整数分钟。"));
        return;
    }
    const auto updating = currentId_ > 0;
    QJsonObject body{
        {QStringLiteral("code"), code},
        {QStringLiteral("name"), name},
        {QStringLiteral("laborMinutes"), laborMinutes},
        {QStringLiteral("description"), description},
        {QStringLiteral("isEnabled"), true},
    };
    if (updating) body.insert(QStringLiteral("revision"), currentRevision_);
    busy_ = true;
    const auto callback = [self = QPointer<ProcessStepWidget>(this), updating](ApiResponse response) {
        if (!self) return;
        self->busy_ = false;
        if (!response.succeeded()) {
            self->statusLabel_->setText(self->errorText(response));
            self->updateControls();
            return;
        }
        self->editing_ = false;
        self->currentId_ = 0;
        self->currentRevision_ = 0;
        self->codeEdit_->clear();
        self->nameEdit_->clear();
        self->laborMinutesEdit_->clear();
        self->descriptionEdit_->clear();
        self->statusLabel_->setText(updating
            ? QStringLiteral("工序已保存。")
            : QStringLiteral("工序已新增。"));
        self->loadSteps();
    };
    if (updating) {
        apiClient_->put(QStringLiteral("/api/v1/process-steps/%1").arg(currentId_), body, callback);
    } else {
        apiClient_->post(QStringLiteral("/api/v1/process-steps"), body, callback);
    }
}

void ProcessStepWidget::toggleEnabled() {
    if (!canWrite() || busy_) return;
    const auto row = selectedRow();
    if (row < 0) return;
    const auto step = steps_.at(row).toObject();
    const auto enabled = step.value(QStringLiteral("isEnabled")).toBool();
    const auto target = !enabled;
    busy_ = true;
    apiClient_->patch(QStringLiteral("/api/v1/process-steps/%1/enabled").arg(step.value(QStringLiteral("id")).toInteger()),
        QJsonObject{
            {QStringLiteral("enabled"), target},
            {QStringLiteral("revision"), step.value(QStringLiteral("revision")).toInteger()},
        },
        [self = QPointer<ProcessStepWidget>(this)](ApiResponse response) {
            if (!self) return;
            self->busy_ = false;
            if (!response.succeeded()) {
                self->statusLabel_->setText(self->errorText(response));
                self->updateControls();
                return;
            }
            self->statusLabel_->setText(QStringLiteral("工序启用状态已更新。"));
            self->loadSteps();
        });
}

void ProcessStepWidget::refreshList() {
    page_ = 1;
    loadSteps();
}

} // namespace manage::desktop
