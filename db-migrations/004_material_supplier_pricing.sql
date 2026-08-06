-- Manage schema version 4, targeting MySQL 8.4.
--
-- 物料库分支扩展：
--  1. 同一物料（相同编号）可维护多个供应商，每个供应商可有一个或多个价格分支。
--  2. 电线类物料（is_copper_based = TRUE）在供应商下按铜价分支维护不同价格：
--     material -> supplier -> copper_price -> unit_price。
--  普通物料（is_copper_based = FALSE）的价格分支铜价列为 NULL，
--  同一供应商下仅允许一条普通价格。

ALTER TABLE materials
    ADD COLUMN is_copper_based BOOLEAN NOT NULL DEFAULT FALSE
    AFTER category;

CREATE TABLE material_suppliers (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    material_id BIGINT UNSIGNED NOT NULL,
    supplier_name VARCHAR(200) NOT NULL,
    contact_name VARCHAR(100) NOT NULL DEFAULT '',
    phone VARCHAR(64) NOT NULL DEFAULT '',
    is_default BOOLEAN NOT NULL DEFAULT FALSE,
    is_enabled BOOLEAN NOT NULL DEFAULT TRUE,
    revision INT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_material_supplier (material_id, supplier_name),
    KEY ix_material_suppliers_material (material_id),
    CONSTRAINT fk_material_suppliers_material FOREIGN KEY (material_id)
        REFERENCES materials (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE material_supplier_prices (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    material_supplier_id BIGINT UNSIGNED NOT NULL,
    copper_price_cents BIGINT UNSIGNED NULL,
    unit_price_cents BIGINT UNSIGNED NOT NULL,
    is_default BOOLEAN NOT NULL DEFAULT FALSE,
    is_enabled BOOLEAN NOT NULL DEFAULT TRUE,
    revision INT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_supplier_copper_price (material_supplier_id, copper_price_cents),
    KEY ix_supplier_prices_supplier (material_supplier_id),
    CONSTRAINT fk_supplier_prices_supplier FOREIGN KEY (material_supplier_id)
        REFERENCES material_suppliers (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
