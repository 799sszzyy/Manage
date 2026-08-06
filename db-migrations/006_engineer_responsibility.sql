-- Manage schema version 6, targeting MySQL 8.4.
--
-- 工程师责任制：
--  1. quotes 增加指派工程师账号、销售预测的 BOM 构建完成时间、
--     工程师实际提交报价时间三个字段。
--  2. 业务流程：销售录入客户需求后，在报价草稿上指派一位工程师账号，
--     并给出预测的 BOM 构建完成时间（deadline）；工程师负责在截止时间前
--     构建 BOM 并发布报价单；发布（issue）时自动记录工程师提交时间。
--  3. 统计分析按「工程师账号 + 期间（月/季/年）」查询，以预测完成时间
--     归属期间，计算是否准时完成（提交时间 <= 预测时间即为准时）。

ALTER TABLE quotes
    ADD COLUMN assigned_engineer_id BIGINT UNSIGNED NULL
        AFTER source_quote_id,
    ADD COLUMN expected_completion_at DATETIME(6) NULL
        AFTER assigned_engineer_id,
    ADD COLUMN engineer_submitted_at DATETIME(6) NULL
        AFTER expected_completion_at;

ALTER TABLE quotes
    ADD CONSTRAINT fk_quotes_assigned_engineer FOREIGN KEY (assigned_engineer_id)
        REFERENCES users (id) ON DELETE RESTRICT;

-- 统计常用路径：按工程师 + 预测完成时间期间检索。
CREATE INDEX ix_quotes_engineer_expected
    ON quotes (assigned_engineer_id, expected_completion_at);
