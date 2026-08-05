# 本地与局域网报价管理系统

Manage v0.7.0 是一套面向 Windows 的 Qt/C++ 报价管理系统。它可以在一台电脑上运行，也可以让同一可信局域网内的多台 Windows 客户端连接一台服务器。MySQL 始终只供服务端使用，客户端只访问受账号权限保护的 HTTP API。

## 已完成范围

- 客户和物料维护，物料支持批量 Excel 导入、导出与停用。
- 平铺 BOM 编辑、物料拖入、重复项合并和整行排序。
- 报价核算、保存、查询、复制、发布和作废，保存客户与物料价格快照。
- 管理员、报价员、查看员三种角色及账号管理。
- 按日期、客户、状态、月份和物料类别统计并导出 Excel。
- 单机默认启动、显式局域网模式、Windows 运行包、数据库备份与人工确认恢复。

报价金额使用整数定点数：金额单位为分，数量精确到百万分之一，费率使用基点。报价状态固定为 `draft -> issued -> void`，不包含审批流和层级 BOM。

## 代码结构

- `src/domain`：报价公式与数值边界，不依赖 Qt。
- `src/auth`：PBKDF2 密码派生、会话和角色授权。
- `src/data`：MySQL 连接、版本迁移、目录/BOM/报价/统计仓储及批量事务。
- `src/server`：Qt HttpServer REST API 和应用组合入口。
- `src/desktop`：Qt Widgets 登录框架与六个业务页面。
- `src/excel`：基于 QXlsx 的 `.xlsx` 导入导出。
- `db-migrations`：可校验的版本化数据库结构。
- `deploy/windows`：免 PowerShell 策略的 `.cmd` 启动入口，以及配置、打包、备份和恢复工具。
- `tests`：领域、服务、桌面、数据库和真实后端流程测试。

## 开发构建

在 Visual Studio 2022 Developer PowerShell 中执行：

```powershell
F:\Qt\6.8.3\msvc2022_64\bin\qt-cmake.bat -S . -B build-final -DBUILD_TESTING=ON
cmake --build build-final --config Release
ctest --test-dir build-final -C Release --output-on-failure
```

开发程序默认使用 `127.0.0.1:18080`。单机运行不需要开放防火墙端口。

## 单机与局域网

服务端默认只能在本机访问。局域网模式必须同时提供监听地址和明确授权：

```powershell
.\build-final\Release\manage-server.exe `
    --listen-address 0.0.0.0 `
    --port 18080 `
    --allow-lan
```

客户端使用服务器的局域网 IP：

```powershell
.\build-final\Release\manage-desktop.exe `
    --api-url http://192.168.1.20:18080
```

局域网 HTTP 不提供 TLS，只能用于可信的公司/家庭内网，不能把端口直接映射到互联网。

## Windows 运行包

`deploy/windows/Package-Manage.ps1` 会生成带 Qt、QMYSQL 和 `libmysql.dll` 的时间戳目录，不覆盖旧包。运行包内：

1. 默认数据库为 `127.0.0.1:3306/manage`，用户为 `manage_app`；如有不同，编辑 `Start-ManageServer.cmd` 中的 `--db-*` 参数。
2. 双击 `Start-ManageServer.cmd`，在可见控制台中输入数据库密码并保持窗口开启；输入不会回显。
3. 双击 `Start-ManageDesktop.cmd` 打开桌面端。
4. 备份/恢复工具使用 `manage.settings.psd1.example` 的副本。若系统禁止 `.ps1`，不要使用 `ExecutionPolicy Bypass`；应采用签名脚本策略或 MySQL 官方工具。

完整操作和代码映射见 `docs/本地报价管理系统-用户说明书.docx` 与 `docs/本地报价管理系统-开发说明书.docx`。

## 安全约束

- 数据库密码不写入仓库或配置样例，只从当前环境或安全输入框取得。
- 生产库不会被自动测试；真实集成测试只接受名称以 `_test` 结尾的独立数据库。
- 已被报价引用的物料只能停用，历史报价快照不会随物料调价改变。
- 报价和批量导入使用事务；并发修改通过 `revision` 冲突保护。
