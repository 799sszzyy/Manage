# Batch 5 统计分析模块

## 已实现边界

- 只读接口：`GET /api/v1/statistics`。
- 必填筛选：`startDate`、`endDate`，格式为 `YYYY-MM-DD`。
- 可选筛选：`customerId`、`status`（`draft`、`issued`、`void`）。
- `admin`、`quoter`、`viewer` 均可查询；临时密码账号仍需先修改密码。
- 汇总：报价数、总金额、平均金额、当前已发布数、作废数、发布率。
- 维度：月份、客户、物料类别。
- JSON 金额字段均为整数分，发布率字段为整数基点。

发布率计算为 `(当前已发布数 + 已作废数) / 报价数`。作废报价曾经发布过，
所以包含在发布率中。发布率只是流程状态指标，不是成交成功率。

月份和客户金额使用报价的 `price_with_tax_cents`；物料类别金额使用报价行的
`subtotal_cents`，不把运费、其他费用、加价和税费强行分摊到物料类别。日期筛选
按报价 `created_at` 计算，开始和结束日期都包含在查询范围内。

当前 `quote_items` 没有类别快照字段，因此类别维度读取物料表的当前类别；查询
不会修改报价及其客户、物料名称、单价或金额快照。本模块未新增数据库迁移。

## 整合接线

整合分支在数据库连接建立后创建：

```cpp
manage::data::MySqlStatisticsRepository statisticsRepository(database);
```

并通过新增的第五个参数把它交给 `ApiServer`。桌面端创建
`manage::desktop::StatisticsWidget`，传入主窗口共享的 `ApiClient`，再使用
`addModuleTab` 加入主窗口。统计模块本身不修改 `server/app/main.cpp` 或
`desktop/app/main.cpp`。

统计 Excel 导出由整合分支调用 Excel 模块完成；本分支只负责生成和显示统计数据。
