// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "transactions/NewTradeOpFrame.h"
#include "OfferExchange.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/OfferFrame.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "util/Logging.h"
#include "util/types.h"

// convert from sheep to wheat
// selling sheep
// buying wheat

namespace stellar
{

using namespace std;
using xdr::operator==;

NewTradeOpFrame::NewTradeOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mNewtrade(mOperation.body.newTradeOp())
{
    mPassive = false;
}

// make sure these issuers exist and you can hold the ask asset
bool
NewTradeOpFrame::checkOfferValid(medida::MetricsRegistry& metrics,
                                    Database& db, LedgerDelta& delta)
{
    Asset const& sheep = mNewtrade.selling;
    Asset const& wheat = mNewtrade.buying;

    if (mNewtrade.amount == 0)
    {
        // don't bother loading trust lines as we're deleting the offer
        return true;
    }

    if (sheep.type() != ASSET_TYPE_NATIVE)
    {
        auto tlI =
            TrustFrame::loadTrustLineIssuer(getSourceID(), sheep, db, delta);
        mSheepLineA = tlI.first;
        if (!tlI.second)
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "sell-no-issuer"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_SELL_NO_ISSUER);
            return false;
        }
        if (!mSheepLineA)
        { // we don't have what we are trying to sell
            metrics
                .NewMeter({"op-manage-offer", "invalid", "sell-no-trust"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_SELL_NO_TRUST);
            return false;
        }
        if (mSheepLineA->getBalance() == 0)
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "underfunded"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_UNDERFUNDED);
            return false;
        }
        if (!mSheepLineA->isAuthorized())
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "sell-not-authorized"},
                          "operation")
                .Mark();
            // we are not authorized to sell
            innerResult().code(NEW_TRADE_SELL_NOT_AUTHORIZED);
            return false;
        }
    }

    if (wheat.type() != ASSET_TYPE_NATIVE)
    {
        auto tlI =
            TrustFrame::loadTrustLineIssuer(getSourceID(), wheat, db, delta);
        mWheatLineA = tlI.first;
        if (!tlI.second)
        {
            metrics
                .NewMeter({"op-manage-offer", "invalid", "buy-no-issuer"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_BUY_NO_ISSUER);
            return false;
        }
        if (!mWheatLineA)
        { // we can't hold what we are trying to buy
            metrics
                .NewMeter({"op-manage-offer", "invalid", "buy-no-trust"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_BUY_NO_TRUST);
            return false;
        }
        if (!mWheatLineA->isAuthorized())
        { // we are not authorized to hold what we
            // are trying to buy
            metrics
                .NewMeter({"op-manage-offer", "invalid", "buy-not-authorized"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_BUY_NOT_AUTHORIZED);
            return false;
        }
    }
    return true;
}

// you are selling sheep for wheat
// need to check the counter offers selling wheat for sheep
// see if this is modifying an old offer
// see if this offer crosses any existing offers
bool
NewTradeOpFrame::doApply(Application& app, LedgerDelta& delta,
                            LedgerManager& ledgerManager)
{
    printf("applying new trade op");
    return true;
}

// makes sure the currencies are different
bool
NewTradeOpFrame::doCheckValid(Application& app)
{
    Asset const& sheep = mNewtrade.selling;
    Asset const& wheat = mNewtrade.buying;

    return true;
}

}
