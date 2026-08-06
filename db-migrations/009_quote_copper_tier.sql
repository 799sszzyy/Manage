-- Manage schema version 9, targeting MySQL 8.4.
--
-- 报价行铜价档：电线类物料按铜价区分价格分支（catalog 的
-- material_prices.copper_price_cents 仅用于物料目录的价格管理）。
-- 报价单明细行在此新增铜价档快照（可选）：
--   - NULL        ：普通物料，无铜价档（默认）。
--   - 非空整数    ：电线类物料报价时选定的铜价档（元/吨，精确到分）。
-- 业务含义：报价输出按 Bshine .xls 模板展开为海外客户成果单时，
-- 电线类物料可"按铜价档展开多行"——同一物料每个铜价档一行，
-- 每行展示该档对应的报价快照单价，供客户按铜价区间对照。

ALTER TABLE quote_items
    ADD COLUMN copper_price_cents BIGINT NULL
        AFTER unit_price_cents;
