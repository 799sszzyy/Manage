-- Manage schema version 2: associate saved quotes with an optional BOM template.
-- The relationship is optional because a quote may be entered manually. If a
-- template is later removed, the saved quote and all of its snapshots remain.

ALTER TABLE quotes
    ADD COLUMN bom_template_id BIGINT UNSIGNED NULL AFTER customer_id,
    ADD KEY ix_quotes_bom_template (bom_template_id),
    ADD CONSTRAINT fk_quotes_bom_template FOREIGN KEY (bom_template_id)
        REFERENCES bom_templates (id) ON DELETE SET NULL;
