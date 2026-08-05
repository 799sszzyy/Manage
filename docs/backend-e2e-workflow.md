# 第二批：后端黑盒 E2E 工作流

`manage_backend_e2e` 从 HTTP 接口外部观察 `manage-server`，不调用服务器内部类，也不直接读写业务表。它用于回答“管理员从第一次启动到创建并持久化业务数据，整个后端流程是否真的连通”。

## 验收流程

完整的受管模式按以下顺序执行：

1. 检查安全开关和测试数据库名称。
2. 运行 `manage-server --migrate-only`，再启动服务并等待健康检查。
3. 初始化 `admin`，使用临时密码登录，并确认必须修改密码。
4. 严格模式确认匿名请求返回 `401`，临时密码会话访问业务接口返回 `403 password_change_required`。
5. 修改正式密码并重新登录。
6. 使用唯一 `E2E-...` 前缀创建并修改客户、物料和 BOM。
7. 替换 BOM 明细，停用 BOM 和物料。
8. 注销并确认旧令牌不能再次访问用户或业务接口。
9. 停止并重启服务器，重新登录后读取客户、物料和 BOM，确认数据库持久化和停用状态。

工具不会清空数据库、删除旧测试记录或修改迁移文件。每次运行会生成新的唯一前缀，只操作本次创建的记录。由于管理员初始化只能执行一次，完整流程应使用一个尚未初始化管理员的独立 `_test` 数据库；需要重复执行时，请新建另一个测试数据库，不要让工具重建或清空旧数据库。

## 安全条件

真实运行必须同时满足：

- 显式设置 `MANAGE_E2E=1`。
- `MANAGE_DB_NAME` 必须以 `_test` 结尾。
- 目标只能是本机 `http://127.0.0.1:<端口>` 或 `http://localhost:<端口>`。
- 测试数据前缀必须以 `E2E-` 开头。

密码由工具根据唯一前缀临时生成，不会输出到终端，也不会写入仓库。

## 构建和自检

```powershell
F:\Qt\6.8.3\msvc2022_64\bin\qt-cmake.bat -S . -B build-local -DBUILD_TESTING=ON
cmake --build build-local --config Release
ctest --test-dir build-local -C Release --output-on-failure
```

只运行不接触数据库的工具自检：

```powershell
.\build-local\Release\manage_backend_e2e.exe --self-test
```

只检查真实运行的安全条件：

```powershell
$env:MANAGE_E2E = '1'
$env:MANAGE_DB_NAME = 'manage_batch2_test'
.\build-local\Release\manage_backend_e2e.exe --preflight-only
```

## 完整受管运行

先在 MySQL 中人工创建一个空的、名称以 `_test` 结尾的数据库和仅能访问该库的测试账号。以下值仅为示意，不要把真实密码提交到 Git：

```powershell
$env:MANAGE_E2E = '1'
$env:MANAGE_DB_HOST = '127.0.0.1'
$env:MANAGE_DB_PORT = '3306'
$env:MANAGE_DB_NAME = 'manage_batch2_test'
$env:MANAGE_DB_USER = 'manage_e2e'
$env:MANAGE_DB_PASSWORD = '在本机填写测试密码'

.\build-local\Release\manage_backend_e2e.exe `
    --base-url http://127.0.0.1:18082 `
    --server-executable .\build-local\Release\manage-server.exe `
    --strict-authorization
```

默认就是严格权限模式，命令中的 `--strict-authorization` 用来让 CI 和人工验收意图更加醒目。当前第二批的 `business-authorization` 分支尚未集成之前，严格检查预期会失败；这不是降低测试标准，而是准确标记集成依赖。

若只想在合并前诊断当前“业务路由未保护”的旧服务器，可以显式加上 `--legacy-unprotected-routes`。该结果不能作为最终安全验收结果。

## 外部服务模式

工具也能针对由人工或 CI 启动的本机服务运行：

```powershell
.\build-local\Release\manage_backend_e2e.exe `
    --base-url http://127.0.0.1:18082 `
    --external-no-restart
```

外部模式无法控制迁移、启动和重启，因此会明确输出重启步骤被跳过。最终完整验收应使用 `--server-executable` 受管模式。

## 输入、处理和输出

- 输入：本机测试 MySQL 配置、服务器程序路径和可选的 HTTP 地址。
- 处理：工具启动服务器，通过真实 HTTP 请求串联认证、客户、物料和 BOM，并重启服务器再次读取。
- 输出：逐步的 `[PASS]`、明确的失败步骤和最终退出码；业务记录保存在指定测试库中，且带唯一 `E2E-...` 前缀。

本工具只覆盖后端 HTTP 和 MySQL 联合流程，不覆盖 Qt 桌面界面的点击操作，也不负责创建或删除 MySQL 数据库。
