-- Manage schema version 10, targeting MySQL 8.4.
--
-- BOM 供应商+铜价定价（批次9）：
--  1. 构建 BOM 时同一物料需选择「供应商」并输入「当前铜价」，
--     由服务端从 material_supplier_prices 解析真实单价。
--  2. bom_items 新增供应商引用与快照（supplier_name_snapshot）、
--     铜价档快照（copper_price_cents，可为 NULL 表示普通物料）、
--     解析单价快照（unit_price_cents，历史价格不受后续调价影响）。
--  3. quote_items 同步新增供应商引用与名称快照，保证报价行
--     可追溯"用了哪个供应商的价格"；铜价档列已在 009 迁移建立。
--
-- 供应商被删除时 BOM/报价行引用置空，但快照字段保留，
-- 历史 BOM 与报价的显示与金额不受影响。

ALTER TABLE bom_items
    ADD COLUMN material_supplier_id BIGINT UNSIGNED NULL
        AFTER material_id,
    ADD COLUMN supplier_name_snapshot VARCHAR(200) NOT NULL DEFAULT ''
        AFTER material_supplier_id,
    ADD COLUMN copper_price_cents BIGINT NULL
        AFTER supplier_name_snapshot,
    ADD COLUMN unit_price_cents BIGINT UNSIGNED NOT NULL DEFAULT 0
        AFTER copper_price_cents,
    ADD KEY ix_bom_items_supplier (material_supplier_id),
    ADD CONSTRAINT fk_bom_items_supplier FOREIGN KEY (material_supplier_id)
        REFERENCES material_suppliers (id) ON DELETE SET NULL;

ALTER TABLE quote_items
    ADD COLUMN material_supplier_id BIGINT UNSIGNED NULL
        AFTER material_id,
    ADD COLUMN supplier_name_snapshot VARCHAR(200) NOT NULL DEFAULT ''
        AFTER material_supplier_id,
    ADD KEY ix_quote_items_supplier (material_supplier_id),
    ADD CONSTRAINT fk_quote_items_supplier FOREIGN KEY (material_supplier_id)
        REFERENCES material_suppliers (id) ON DELETE SET NULL;
