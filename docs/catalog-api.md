# 物料与客户接口说明

本模块把一次请求分成三层处理：

1. REST 路由读取 JSON 和网址中的分页参数。
2. `CatalogService` 检查必填字段、长度、价格和 `revision`。
3. `MySqlCatalogRepository` 使用参数绑定的 SQL 读写 MySQL。

金额统一使用整数“分”。例如 `currentUnitPriceCents: 1250` 表示 12.50 元，
不会使用容易产生小数误差的浮点金额。

## 分页返回格式

物料和客户列表都返回：

```json
{
  "items": [],
  "page": 1,
  "pageSize": 20,
  "total": 0,
  "totalPages": 0
}
```

`page` 从 1 开始，`pageSize` 范围为 1 到 100。

## 物料接口

| 方法 | 地址 | 用途 |
| --- | --- | --- |
| GET | `/api/v1/materials?page=1&pageSize=20&search=钢&enabled=true` | 分页和搜索物料 |
| GET | `/api/v1/materials/{id}` | 查询一条物料 |
| POST | `/api/v1/materials` | 新增物料 |
| PUT | `/api/v1/materials/{id}` | 修改物料，必须提供 `revision` |
| PATCH | `/api/v1/materials/{id}/enabled` | 启用或停用物料 |

新增物料示例：

```json
{
  "code": "MAT-001",
  "name": "钢板",
  "specification": "2 mm",
  "unit": "张",
  "category": "金属",
  "currentUnitPriceCents": 12345,
  "isEnabled": true
}
```

停用物料示例：

```json
{
  "revision": 3,
  "isEnabled": false
}
```

首版不对业务暴露删除物料的接口。已被 BOM 或报价引用的物料还受到
MySQL 外键 `ON DELETE RESTRICT` 保护，因此正常处理方式是停用，而不是删除。

## 客户接口

| 方法 | 地址 | 用途 |
| --- | --- | --- |
| GET | `/api/v1/customers?page=1&pageSize=20&search=公司` | 分页和搜索客户 |
| GET | `/api/v1/customers/{id}` | 查询一条客户 |
| POST | `/api/v1/customers` | 新增客户 |
| PUT | `/api/v1/customers/{id}` | 修改客户，必须提供 `revision` |

客户请求示例：

```json
{
  "name": "示例公司",
  "contactName": "张三",
  "phone": "13800000000",
  "address": "示例地址",
  "notes": "重点客户"
}
```

## revision 如何避免覆盖

服务器每次成功修改后都会把 `revision` 加 1。假设甲和乙同时读取到
`revision: 2`，甲先保存后数据库变为 3；乙再用旧的 2 保存时，服务器返回：

```json
{
  "error": "revision_conflict",
  "message": "the record was changed by another request; reload and retry",
  "field": "revision"
}
```

乙需要重新读取最新数据后再决定如何修改。这样可以防止乙在不知情的情况下
覆盖甲已经保存的内容。

## 常见错误状态

| HTTP 状态 | `error` | 含义 |
| --- | --- | --- |
| 400 | `invalid_request` / `invalid_json` | 字段缺失、类型错误或不符合业务范围 |
| 404 | `not_found` | 对应 ID 不存在 |
| 409 | `revision_conflict` | 数据已被其他请求修改 |
| 409 | `duplicate_code` | 物料编码已经存在 |
| 500 | `database_error` | 数据库操作失败 |
