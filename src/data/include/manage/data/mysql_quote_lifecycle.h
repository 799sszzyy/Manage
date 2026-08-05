#pragma once

#include "manage/data/quote_lifecycle.h"

#include <QSqlDatabase>

namespace manage::data {

// Persists the complete quote aggregate. Every mutating operation owns one
// database transaction so the quote header and its snapshot lines cannot drift
// apart when an error occurs.
class MySqlQuoteLifecycle final : public QuoteLifecycle {
public:
    explicit MySqlQuoteLifecycle(QSqlDatabase database);

    QuoteResult<QuotePage> list(QuoteSearchQuery query) override;
    QuoteResult<QuoteDocument> getById(qint64 id) override;
    QuoteResult<QuoteDocument> create(CreateQuoteCommand command) override;
    QuoteResult<QuoteDocument> update(UpdateQuoteCommand command) override;
    QuoteResult<QuoteDocument> changeStatus(
        ChangeQuoteStatusCommand command
    ) override;
    QuoteResult<QuoteDocument> clone(CloneQuoteCommand command) override;
    QuoteResult<bool> deleteDraft(DeleteDraftQuoteCommand command) override;

private:
    QSqlDatabase database_;
};

} // namespace manage::data
