#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/OfferFrame.h"
#include "ledger/TrustFrame.h"
#include "transactions/OperationFrame.h"

namespace stellar
{
class NewTradeOpFrame : public OperationFrame
{
    TrustFrame::pointer mSheepLineA;
    TrustFrame::pointer mWheatLineA;

    TrustFrame::pointer mSheepLineB;
    TrustFrame::pointer mWheatLineB;

    AccountFrame::pointer mAccountA;
    AccountFrame::pointer mAccountB;

    OfferFrame::pointer mTradeOffer;

    bool validateAccount(AccountID const&accountId, medida::MetricsRegistry& metrics,
                                    Database& db, LedgerDelta& delta,
                                    TrustFrame::pointer &trustLineWheat,
                                    TrustFrame::pointer &trustLineSheep);

    bool checkOfferValid(medida::MetricsRegistry& metrics, Database& db,
                         LedgerDelta& delta);

    NewTradeResult&
    innerResult()
    {
        return mResult.tr().newTradeResult();
    }

    NewTradeOp const& mNewtrade;
    OfferEntry buildOffer(AccountID const& account,
                               NewTradeOp const& op, uint32 flags);

  public:
    NewTradeOpFrame(Operation const& op, OperationResult& res,
                       TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static ManageOfferResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageOfferResult().code();
    }
    
};
}
