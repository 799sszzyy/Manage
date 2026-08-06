-- Manage schema version 3: preserve how many units of the selected BOM are sold.
-- Existing quotes remain unchanged because one BOM is the default quantity.

ALTER TABLE quotes
    ADD COLUMN bom_quantity_micros BIGINT UNSIGNED NOT NULL DEFAULT 1000000
        AFTER bom_template_id,
    ADD CONSTRAINT ck_quotes_bom_quantity CHECK (bom_quantity_micros > 0);
