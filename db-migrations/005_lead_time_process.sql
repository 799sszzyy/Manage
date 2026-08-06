-- Manage schema version 5, targeting MySQL 8.4.
--
-- 交期与工时模块：
--  1. 每个物料供应商增加交货周期 lead_days（天），用于计算 BOM 交期。
--  2. 新增工序库 process_steps：维护所有生产工序的单人劳动力工时（分钟），
--     工序步骤在订单界面可手动选用，也可在此库维护后修改。
--  3. quotes 增加交期快照：BOM 交期、劳动人数、工序总工时、预计发货交期。
--  4. 新增报价工序明细 quote_process_items：记录每张报价单实际使用的工序步骤。

ALTER TABLE material_suppliers
    ADD COLUMN lead_days INT UNSIGNED NOT NULL DEFAULT 0
    AFTER is_enabled;

CREATE TABLE process_steps (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    code VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    name VARCHAR(200) NOT NULL,
    -- 单人完成该工序所需的工时，单位：分钟。
    labor_minutes INT UNSIGNED NOT NULL DEFAULT 0,
    description VARCHAR(1000) NOT NULL DEFAULT '',
    is_enabled BOOLEAN NOT NULL DEFAULT TRUE,
    revision INT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_process_steps_code (code),
    KEY ix_process_steps_name_enabled (name, is_enabled)
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

ALTER TABLE quotes
    ADD COLUMN bom_lead_days INT UNSIGNED NOT NULL DEFAULT 0
        AFTER bom_quantity_micros,
    ADD COLUMN labor_count INT UNSIGNED NOT NULL DEFAULT 1
        AFTER bom_lead_days,
    ADD COLUMN process_total_minutes INT UNSIGNED NOT NULL DEFAULT 0
        AFTER labor_count,
    ADD COLUMN estimated_delivery_days INT UNSIGNED NOT NULL DEFAULT 0
        AFTER process_total_minutes;

CREATE TABLE quote_process_items (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    quote_id BIGINT UNSIGNED NOT NULL,
    line_no INT UNSIGNED NOT NULL,
    step_name_snapshot VARCHAR(200) NOT NULL,
    labor_minutes_snapshot INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (id),
    UNIQUE KEY uq_quote_process_line (quote_id, line_no),
    CONSTRAINT fk_quote_process_quote FOREIGN KEY (quote_id)
        REFERENCES quotes (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
