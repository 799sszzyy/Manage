-- Manage schema version 1, targeting MySQL 8.4.

CREATE TABLE roles (
    id TINYINT UNSIGNED NOT NULL,
    code VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    display_name VARCHAR(64) NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uq_roles_code (code)
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

INSERT INTO roles (id, code, display_name) VALUES
    (1, 'admin', '管理员'),
    (2, 'quoter', '报价员'),
    (3, 'viewer', '只读用户');

CREATE TABLE users (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    display_name VARCHAR(100) NOT NULL,
    role_id TINYINT UNSIGNED NOT NULL,
    password_algorithm VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NULL,
    password_hash VARBINARY(255) NULL,
    password_salt VARBINARY(64) NULL,
    password_iterations INT UNSIGNED NULL,
    must_change_password BOOLEAN NOT NULL DEFAULT TRUE,
    is_enabled BOOLEAN NOT NULL DEFAULT TRUE,
    revision INT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_users_username (username),
    KEY ix_users_role_enabled (role_id, is_enabled),
    CONSTRAINT fk_users_role FOREIGN KEY (role_id) REFERENCES roles (id),
    CONSTRAINT ck_users_password_material CHECK (
        (password_algorithm IS NULL AND password_hash IS NULL
            AND password_salt IS NULL AND password_iterations IS NULL
            AND is_enabled = FALSE)
        OR
        (password_algorithm IS NOT NULL AND password_hash IS NOT NULL
            AND password_salt IS NOT NULL AND password_iterations >= 100000)
    )
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

-- The authentication module will securely set this bootstrap account's
-- password before enabling it. No default password is stored in source code.
INSERT INTO users (
    id, username, display_name, role_id, must_change_password, is_enabled
) VALUES (1, 'admin', '初始管理员', 1, TRUE, FALSE);

CREATE TABLE materials (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    code VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    name VARCHAR(200) NOT NULL,
    specification VARCHAR(500) NOT NULL DEFAULT '',
    unit VARCHAR(32) NOT NULL,
    category VARCHAR(100) NOT NULL DEFAULT '',
    current_unit_price_cents BIGINT UNSIGNED NOT NULL,
    is_enabled BOOLEAN NOT NULL DEFAULT TRUE,
    revision INT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_materials_code (code),
    KEY ix_materials_name (name),
    KEY ix_materials_category_enabled (category, is_enabled)
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE customers (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    name VARCHAR(200) NOT NULL,
    contact_name VARCHAR(100) NOT NULL DEFAULT '',
    phone VARCHAR(64) NOT NULL DEFAULT '',
    address VARCHAR(500) NOT NULL DEFAULT '',
    notes TEXT NOT NULL,
    revision INT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    KEY ix_customers_name (name)
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE bom_templates (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    code VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    name VARCHAR(200) NOT NULL,
    description VARCHAR(1000) NOT NULL DEFAULT '',
    is_enabled BOOLEAN NOT NULL DEFAULT TRUE,
    revision INT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_bom_templates_code (code),
    KEY ix_bom_templates_name_enabled (name, is_enabled)
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE bom_items (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    bom_template_id BIGINT UNSIGNED NOT NULL,
    line_no INT UNSIGNED NOT NULL,
    material_id BIGINT UNSIGNED NOT NULL,
    quantity_micros BIGINT UNSIGNED NOT NULL,
    notes VARCHAR(500) NOT NULL DEFAULT '',
    PRIMARY KEY (id),
    UNIQUE KEY uq_bom_items_line (bom_template_id, line_no),
    KEY ix_bom_items_material (material_id),
    CONSTRAINT fk_bom_items_template FOREIGN KEY (bom_template_id)
        REFERENCES bom_templates (id) ON DELETE CASCADE,
    CONSTRAINT fk_bom_items_material FOREIGN KEY (material_id)
        REFERENCES materials (id) ON DELETE RESTRICT,
    CONSTRAINT ck_bom_items_quantity CHECK (quantity_micros > 0)
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE quotes (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    quote_number VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    customer_id BIGINT UNSIGNED NOT NULL,
    customer_name_snapshot VARCHAR(200) NOT NULL,
    customer_contact_snapshot VARCHAR(100) NOT NULL DEFAULT '',
    customer_phone_snapshot VARCHAR(64) NOT NULL DEFAULT '',
    customer_address_snapshot VARCHAR(500) NOT NULL DEFAULT '',
    status VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL
        DEFAULT 'draft',
    material_cost_cents BIGINT UNSIGNED NOT NULL DEFAULT 0,
    freight_cents BIGINT UNSIGNED NOT NULL DEFAULT 0,
    other_fees_cents BIGINT UNSIGNED NOT NULL DEFAULT 0,
    markup_basis_points INT UNSIGNED NOT NULL DEFAULT 0,
    markup_amount_cents BIGINT UNSIGNED NOT NULL DEFAULT 0,
    price_before_tax_cents BIGINT UNSIGNED NOT NULL DEFAULT 0,
    tax_basis_points INT UNSIGNED NOT NULL DEFAULT 0,
    tax_amount_cents BIGINT UNSIGNED NOT NULL DEFAULT 0,
    price_with_tax_cents BIGINT UNSIGNED NOT NULL DEFAULT 0,
    notes TEXT NOT NULL,
    source_quote_id BIGINT UNSIGNED NULL,
    created_by BIGINT UNSIGNED NOT NULL,
    updated_by BIGINT UNSIGNED NOT NULL,
    issued_at DATETIME(6) NULL,
    voided_at DATETIME(6) NULL,
    revision INT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_quotes_number (quote_number),
    KEY ix_quotes_customer_created (customer_id, created_at),
    KEY ix_quotes_status_created (status, created_at),
    KEY ix_quotes_source (source_quote_id),
    CONSTRAINT fk_quotes_customer FOREIGN KEY (customer_id)
        REFERENCES customers (id) ON DELETE RESTRICT,
    CONSTRAINT fk_quotes_source FOREIGN KEY (source_quote_id)
        REFERENCES quotes (id) ON DELETE SET NULL,
    CONSTRAINT fk_quotes_created_by FOREIGN KEY (created_by)
        REFERENCES users (id) ON DELETE RESTRICT,
    CONSTRAINT fk_quotes_updated_by FOREIGN KEY (updated_by)
        REFERENCES users (id) ON DELETE RESTRICT,
    CONSTRAINT ck_quotes_status CHECK (status IN ('draft', 'issued', 'void')),
    CONSTRAINT ck_quotes_markup_rate CHECK (markup_basis_points <= 10000),
    CONSTRAINT ck_quotes_tax_rate CHECK (tax_basis_points <= 10000),
    CONSTRAINT ck_quotes_status_timestamps CHECK (
        (status = 'draft' AND issued_at IS NULL AND voided_at IS NULL)
        OR (status = 'issued' AND issued_at IS NOT NULL AND voided_at IS NULL)
        OR (status = 'void' AND issued_at IS NOT NULL AND voided_at IS NOT NULL)
    )
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

CREATE TABLE quote_items (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    quote_id BIGINT UNSIGNED NOT NULL,
    line_no INT UNSIGNED NOT NULL,
    material_id BIGINT UNSIGNED NOT NULL,
    material_code_snapshot VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    material_name_snapshot VARCHAR(200) NOT NULL,
    specification_snapshot VARCHAR(500) NOT NULL DEFAULT '',
    unit_snapshot VARCHAR(32) NOT NULL,
    quantity_micros BIGINT UNSIGNED NOT NULL,
    unit_price_cents BIGINT UNSIGNED NOT NULL,
    subtotal_cents BIGINT UNSIGNED NOT NULL,
    notes VARCHAR(500) NOT NULL DEFAULT '',
    PRIMARY KEY (id),
    UNIQUE KEY uq_quote_items_line (quote_id, line_no),
    KEY ix_quote_items_material (material_id),
    CONSTRAINT fk_quote_items_quote FOREIGN KEY (quote_id)
        REFERENCES quotes (id) ON DELETE CASCADE,
    CONSTRAINT fk_quote_items_material FOREIGN KEY (material_id)
        REFERENCES materials (id) ON DELETE RESTRICT,
    CONSTRAINT ck_quote_items_quantity CHECK (quantity_micros > 0)
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
