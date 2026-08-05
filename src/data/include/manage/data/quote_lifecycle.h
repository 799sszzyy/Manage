#pragma once

#include "manage/data/quote_models.h"

namespace manage::data {

class QuoteLifecycle {
public:
    virtual ~QuoteLifecycle() = default;

    virtual QuoteResult<QuotePage> list(QuoteSearchQuery query) = 0;
    virtual QuoteResult<QuoteDocument> getById(qint64 id) = 0;
    virtual QuoteResult<QuoteDocument> create(CreateQuoteCommand command) = 0;
    virtual QuoteResult<QuoteDocument> update(UpdateQuoteCommand command) = 0;
    virtual QuoteResult<QuoteDocument> changeStatus(
        ChangeQuoteStatusCommand command
    ) = 0;
    virtual QuoteResult<QuoteDocument> clone(CloneQuoteCommand command) = 0;
    virtual QuoteResult<bool> deleteDraft(DeleteDraftQuoteCommand command) = 0;
};

} // namespace manage::data
