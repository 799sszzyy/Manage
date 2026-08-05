# 第四批（Batch 4）报价生命周期共同契约

## 固定标号与目标

- 批次：Batch 4
- 软件版本：v0.4.0
- 并行验收端口：18084
- 正式默认端口：18080
- 整合分支：`codex/batch4-quote-integration-v0.4.0-p18084`
- 目标：完成报价保存、列表/详情查询、草稿修改、发布、作废、复制和草稿删除。

本文件是数据、HTTP API 和桌面三个永久工作树的共同边界。三个模块不得自行更改字段名称或状态规则；需要调整时先在整合分支统一修改本文件。

## 状态规则

报价状态固定为：

- `draft`：可修改、可发布、可复制、可删除。
- `issued`：内容冻结，可作废、可复制，不可修改或删除。
- `void`：内容冻结，只可查询和复制。

合法流转只有 `draft -> issued -> void`。复制任何状态的报价都会生成新的 `draft`，使用新的报价编号，并记录 `sourceQuoteId`。已发布与已作废报价必须保留客户和物料快照。

## 权限

- `admin`：查询、创建、修改、发布、作废、复制和删除草稿。
- `quoter`：与管理员相同的报价业务权限。
- `viewer`：只能查询列表和详情。
- 尚未修改临时密码的账号不能访问报价业务接口。

## HTTP API

| 方法 | 路径 | 作用 |
| --- | --- | --- |
| `GET` | `/api/v1/quotes` | 分页查询；参数为 `page`、`pageSize`、`search`、`status`、`customerId`。 |
| `GET` | `/api/v1/quotes/{id}` | 读取报价详情和全部快照明细。 |
| `POST` | `/api/v1/quotes` | 新建并保存草稿，返回 HTTP 201。 |
| `PUT` | `/api/v1/quotes/{id}` | 按 `revision` 修改草稿。 |
| `PATCH` | `/api/v1/quotes/{id}/status` | 按 `revision` 发布或作废，正文为 `{ "status": "issued|void", "revision": n }`。 |
| `POST` | `/api/v1/quotes/{id}/clone` | 复制为新草稿，返回 HTTP 201。 |
| `DELETE` | `/api/v1/quotes/{id}?revision=n` | 只允许删除草稿，成功返回 HTTP 204。 |

列表响应字段固定为 `items`、`total`、`page`、`pageSize`。报价 JSON 使用 camelCase，对应 `quote_models.h`；金额单位为分，数量单位为百万分之一，费率单位为基点。

新建与修改正文固定包含：

```json
{
  "customerId": 1,
  "bomTemplateId": 1,
  "freightCents": 1000,
  "otherFeesCents": 200,
  "markupBasisPoints": 2000,
  "taxBasisPoints": 1300,
  "notes": "",
  "items": [
    {
      "materialId": 1,
      "quantityMicros": 2500000,
      "unitPriceCents": 1234,
      "notes": ""
    }
  ],
  "revision": 1
}
```

`POST` 不发送 `revision`；`PUT` 必须发送。服务端忽略客户端提供的名称、规格、单位、小计和总额，全部从数据库和领域计算模块重新生成，防止伪造快照或金额。

## 数据完整性

- 报价编号由服务端生成且唯一，建议格式 `Q-YYYYMMDD-{id}`。
- 新建或修改必须在单个数据库事务中完成主表与明细。
- 客户、物料、BOM 和操作人必须存在。
- 每次保存重新读取客户与物料资料，生成名称、联系方式、规格、单位和单价快照。
- 报价总额必须复用 `Manage::Domain` 的整数定点核算，禁止在数据层或桌面端另写浮点公式。
- 乐观锁冲突返回 HTTP 409；不存在返回 404；非法状态流转返回 409；校验失败返回 400。
- 数据库迁移只允许新增 `002_*.sql`，不得修改已经发布的 `001_initial_schema.sql`。

## 模块所有权

- `quote-data`：`quote_models.h`、`quote_lifecycle.h` 的具体 MySQL 实现、002 迁移和数据/服务测试。
- `quote-api`：报价路由、JSON 映射、权限与路由测试；只依赖 `QuoteLifecycle` 抽象。
- `quote-desktop`：报价管理 Widget 和模拟 HTTP 测试；只依赖现有 `ApiClient` 与本文件规定的 JSON。
- `quote-integration`：把具体 MySQL 生命周期注入服务端，把报价管理页接入主窗口，并负责真实 MySQL 端到端测试。

## 第四批验收线

- 三个功能分支各自 Release 编译和测试通过。
- 合并后原有 18 项测试全部保持通过，并增加报价数据、路由和桌面测试。
- 使用名称以 `_test` 结尾的新数据库，在 `18084` 完成：初始化管理员、改密、创建客户/物料/BOM、保存草稿、重启服务、查询、修改、发布、复制、作废和草稿删除。
- 发布后的客户与物料快照在原资料修改后保持不变。
- 第四批不实现 Excel 导出、用户账号管理和统计分析；这些属于 Batch 5。
