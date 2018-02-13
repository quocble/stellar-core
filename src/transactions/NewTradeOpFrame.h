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

    OfferFrame::pointer mSellSheepOffer;

    bool checkOfferValid(medida::MetricsRegistry& metrics, Database& db,
                         LedgerDelta& delta);

    NewTradeResult&
    innerResult()
    {
        return mResult.tr().newTradeResult();
    }

    NewTradeOp const& mNewtrade;

    // OfferEntry buildOffer(AccountID const& account, ManageOfferOp const& op,
    //                       uint32 flags);

  protected:
    bool mPassive;

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
