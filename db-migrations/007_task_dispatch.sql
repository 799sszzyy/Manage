-- Manage schema version 7, targeting MySQL 8.4.
--
-- 任务派发系统：报价流程的起点是销售账号向工程师账号派发任务。
--  1. 新增 tasks 表：任务编号、客户(可选)、销售派单人、负责工程师、
--     预期完成时间、状态、标题、备注、关联报价(可空)、revision。
--  2. 状态机：dispatched(已派发) -> in_progress(工程师处理中)
--     -> completed(已完成)；任意非终态可 -> cancelled(已取消)。
--  3. 销售只需填预期时间 + 负责工程师；工程师在任务下构建 BOM/报价，
--     发布报价后回填 quote_id 关联。
--  4. 任务软引用 users 的工程师/销售账号；删除用户受 RESTRICT 保护，
--     避免历史任务失去责任人。

CREATE TABLE tasks (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    task_number VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    customer_id BIGINT UNSIGNED NULL,
    dispatched_by BIGINT UNSIGNED NOT NULL,
    assigned_engineer_id BIGINT UNSIGNED NOT NULL,
    expected_completion_at DATETIME(6) NULL,
    status VARCHAR(32) NOT NULL DEFAULT 'dispatched',
    title VARCHAR(200) NOT NULL DEFAULT '',
    notes VARCHAR(1000) NOT NULL DEFAULT '',
    quote_id BIGINT UNSIGNED NULL,
    revision INT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (id),
    UNIQUE KEY uq_tasks_number (task_number),
    KEY ix_tasks_engineer_status (assigned_engineer_id, status),
    KEY ix_tasks_dispatcher (dispatched_by),
    KEY ix_tasks_status (status),
    CONSTRAINT fk_tasks_customer FOREIGN KEY (customer_id)
        REFERENCES customers (id) ON DELETE SET NULL,
    CONSTRAINT fk_tasks_dispatcher FOREIGN KEY (dispatched_by)
        REFERENCES users (id) ON DELETE RESTRICT,
    CONSTRAINT fk_tasks_engineer FOREIGN KEY (assigned_engineer_id)
        REFERENCES users (id) ON DELETE RESTRICT,
    CONSTRAINT fk_tasks_quote FOREIGN KEY (quote_id)
        REFERENCES quotes (id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
