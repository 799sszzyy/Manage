-- Manage schema version 8, targeting MySQL 8.4.
--
-- 交期日期：在已有的 estimated_delivery_days（天数）基础上，
-- 新增 estimated_delivery_at（具体日期），由 created_at + estimated_delivery_days 计算。
-- 业务含义：交期日期 = 报价创建日期 + BOM 最长物料供货周期 + 工时/劳动力折算天数。

ALTER TABLE quotes
    ADD COLUMN estimated_delivery_at DATETIME(6) NULL
        AFTER estimated_delivery_days;
