# 本地单机报价管理系统

这是一个面向 Windows 单机部署的报价管理系统。MySQL、C++ 本地 API 与 Qt Widgets 客户端最终均运行在同一台电脑上，并只通过 `localhost` 通信。

## 当前完成范围

工程已经建立 Qt 6 / CMake / C++17 分层结构，并完成领域核算与首版数据库底层模块。

- `src/domain`：不依赖 Qt 的领域核心，负责金额、数量、费率校验和报价计算。
- `src/data`：数据库配置、`QMYSQL` 连接、版本化迁移和迁移完整性检查。
- `src/server`：仅监听 `127.0.0.1` 的 Qt HttpServer；正常启动前会自动迁移数据库。
- `src/desktop`：可启动的 Qt Widgets 主窗口，通过 HTTP 检查本地服务状态。
- `db-migrations`：MySQL 8.4 表结构、索引、初始角色以及建库账号模板。
- `tests`：领域、迁移、服务接口和应用冒烟测试。

核算使用整数定点数，避免浮点金额误差：金额单位为分、数量精确到百万分之一，加价率和税率使用基点（100 基点 = 1%）。每条物料小计以及每次百分比计算均四舍五入到分。

## 构建与测试

本机使用 Qt 6.8.3 MSVC 2022 64 位 SDK：

```powershell
F:\Qt\6.8.3\msvc2022_64\bin\qt-cmake.bat -S . -B build-local -DBUILD_TESTING=ON
cmake --build build-local --config Release
ctest --test-dir build-local -C Release --output-on-failure
```

`--smoke-test` 不连接数据库，适合在数据库尚未配置时检查程序能否启动：

```powershell
.\build-local\Release\manage-server.exe --smoke-test
.\build-local\Release\manage-desktop.exe --smoke-test
```

## MySQL 首次配置

Qt 的 `QMYSQL` 驱动已在当前开发机的 Qt 6.8.3 MSVC2022 64 位环境中配置。MySQL 服务仍需先由 MySQL Configurator 建立并启动。

1. 复制账号模板，并把两处占位密码换成同一个强密码。`.local.sql` 文件已被 Git 忽略。

```powershell
Copy-Item .\db-migrations\provision-mysql.sql.example .\db-migrations\provision-mysql.local.sql
```

2. 用 MySQL 管理员执行该脚本，创建 `manage` 数据库及仅拥有该库所需权限的 `manage_app` 账号。

```powershell
Get-Content -Raw .\db-migrations\provision-mysql.local.sql |
    & 'C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe' -u root -p
```

3. 在当前 PowerShell 设置连接参数并单独执行迁移。

```powershell
$env:MANAGE_DB_HOST = '127.0.0.1'
$env:MANAGE_DB_PORT = '3306'
$env:MANAGE_DB_NAME = 'manage'
$env:MANAGE_DB_USER = 'manage_app'
$env:MANAGE_DB_PASSWORD = '这里填写刚才设置的密码'
& .\build-local\Release\manage-server.exe --migrate-only
```

之后正常运行 `manage-server.exe` 时也会自动检查并迁移。迁移使用 MySQL advisory lock 防止并发执行，并校验历史脚本的 SHA-256；失败或被修改的迁移会阻止服务继续启动。

首版迁移会创建账号/角色、物料、客户、BOM、报价单及报价明细快照。初始 `admin` 记录不含默认密码且处于禁用状态；后续身份认证模块负责安全设置密码、启用账号并强制首次登录改密。

需要运行真实 MySQL 集成测试时，请使用名称以 `_test` 结尾的独立测试库，再设置 `MANAGE_TEST_MYSQL=1` 后运行 CTest。

## 当前 REST API

- `GET /api/v1/health`
- `POST /api/v1/quotes/calculate`

核算接口使用 `quantityMicros`、`unitPriceCents`、`freightCents`、`otherFeesCents`、`markupBasisPoints` 和 `taxBasisPoints` 等整数 JSON 字段。
