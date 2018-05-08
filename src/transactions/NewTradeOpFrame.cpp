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
}

bool NewTradeOpFrame::validateAccount(AccountID const&accountId, medida::MetricsRegistry& metrics,
                                    Database& db, LedgerDelta& delta,
                                    TrustFrame::pointer &trustLineSheep,
                                    TrustFrame::pointer &trustLineWheat) {
                                   

    Asset const& sheep = mNewtrade.selling;
    Asset const& wheat = mNewtrade.buying;
    
    if (sheep.type() != ASSET_TYPE_NATIVE)
    {
        auto tlI =
            TrustFrame::loadTrustLineIssuer(accountId, sheep, db, delta);
        trustLineSheep = tlI.first;
        if (!tlI.second)
        {
            metrics
                .NewMeter({"op-new-trade", "invalid", "sell-no-issuer"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_SELL_NO_ISSUER);
            return false;
        }
        if (!trustLineSheep)
        { // we don't have what we are trying to sell
            metrics
                .NewMeter({"op-new-trade", "invalid", "sell-no-trust"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_SELL_NO_TRUST);
            return false;
        }
        // if (trustLineSheep->getBalance() == 0)
        // {
        //     metrics
        //         .NewMeter({"op-new-trade", "invalid", "underfunded"},
        //                   "operation")
        //         .Mark();
        //     innerResult().code(NEW_TRADE_UNDERFUNDED);
        //     return false;
        // }
        if (!trustLineSheep->isAuthorized())
        {
            metrics
                .NewMeter({"op-new-trade", "invalid", "sell-not-authorized"},
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
            TrustFrame::loadTrustLineIssuer(accountId, wheat, db, delta);
        trustLineWheat = tlI.first;
        if (!tlI.second)
        {
            metrics
                .NewMeter({"op-new-trade", "invalid", "buy-no-issuer"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_BUY_NO_ISSUER);
            return false;
        }
        if (!trustLineWheat)
        { // we can't hold what we are trying to buy
            metrics
                .NewMeter({"op-new-trade", "invalid", "buy-no-trust"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_BUY_NO_TRUST);
            return false;
        }
        if (!trustLineWheat->isAuthorized())
        { // we are not authorized to hold what we
            // are trying to buy
            metrics
                .NewMeter({"op-new-trade", "invalid", "buy-not-authorized"},
                          "operation")
                .Mark();
            innerResult().code(NEW_TRADE_BUY_NOT_AUTHORIZED);
            return false;
        }
    }
    
    return true;
}

// make sure these issuers exist and you can hold the ask asset
bool
NewTradeOpFrame::checkOfferValid(medida::MetricsRegistry& metrics,
                                    Database& db, LedgerDelta& delta)
{
    AccountID buyerAccountId = mNewtrade.buyer.tx.sourceAccount;
    AccountID sellerAccountId = mNewtrade.seller.tx.sourceAccount;    

    mAccountA = AccountFrame::loadAccount(delta, buyerAccountId, db); 
    mAccountB = AccountFrame::loadAccount(delta, sellerAccountId, db); 

// We are okay with cross order, just don't send money
//
//    if (buyerAccountId == sellerAccountId) {
//        innerResult().code(NEW_TRADE_CROSS_SELF);        
//        return false;
//    }

    if (!validateAccount(buyerAccountId, metrics, db, delta, mSheepLineA, mWheatLineA)) {
        return false;
    }

    if (!validateAccount(sellerAccountId, metrics, db, delta, mSheepLineB, mWheatLineB)) {
        return false;
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
    Database& db = ledgerManager.getDatabase();

    if (!checkOfferValid(app.getMetrics(), db, delta))
    {
        return false;
    }

    Asset const& sheep = mNewtrade.selling;
    Asset const& wheat = mNewtrade.buying;

    bool creatingNewOffer = true;

    int64_t amount = mNewtrade.amount;
    int64_t maxSheepSend = mNewtrade.amount;
    int64_t maxAmountOfSheepCanSell = mSheepLineA->getBalance();

    soci::transaction sqlTx(db.getSession());

    LedgerEntry le;
    le.data.type(OFFER);
    le.data.offer() = buildOffer(getSourceID(), mNewtrade, 0);
    mTradeOffer = std::make_shared<OfferFrame>(le);

    LedgerDelta tempDelta(delta);
    
    bool crossing = mAccountA->getID() == mAccountB->getID();

    // the maximum is defined by how much wheat it can receive
    int64_t maxWheatCanBuy;
    if (wheat.type() == ASSET_TYPE_NATIVE)
    {
        maxWheatCanBuy = INT64_MAX;
    }
    else
    {
        maxWheatCanBuy = mWheatLineA->getMaxAmountReceive();
        if (maxWheatCanBuy == 0)
        {
            app.getMetrics()
                .NewMeter({"op-new-trade", "invalid", "line-full"},
                            "operation")
                .Mark();
            innerResult().code(NEW_TRADE_LINE_FULL);
            return false;
        }
    }

    Price const& sheepPrice = mNewtrade.price;

    {
        int64_t maxSheepBasedOnWheat;
        if (!bigDivide(maxSheepBasedOnWheat, maxWheatCanBuy, 
                sheepPrice.d, sheepPrice.n, ROUND_DOWN))
        {
            maxSheepBasedOnWheat = INT64_MAX;
        }

        if (maxAmountOfSheepCanSell > maxSheepBasedOnWheat)
        {
            maxAmountOfSheepCanSell = maxSheepBasedOnWheat;
        }
    }
    
    // amount of sheep for sale is the lesser of amount we can sell and
    // amount put in the offer
    if (maxAmountOfSheepCanSell < maxSheepSend)
    {
        maxSheepSend = maxAmountOfSheepCanSell;
    }

    int64_t sheepSent, wheatReceived;
    const Price maxWheatPrice(sheepPrice.d, sheepPrice.n);

    if (!bigDivide(sheepSent, amount, 
            sheepPrice.d, sheepPrice.n, ROUND_DOWN))
    {
        sheepSent = 0;
    }

    if (!bigDivide(wheatReceived, amount, 
            sheepPrice.n, sheepPrice.d, ROUND_DOWN))
    {
        wheatReceived = 0;
    }
    
    if (!crossing) {
        // BUYER => get the wheat, and deduct the sheep
        if (wheat.type() == ASSET_TYPE_NATIVE)
        {
            if (!mAccountA->addBalance(wheatReceived))
            {
                // this would indicate a bug in OfferExchange
                throw std::runtime_error("offer claimed over limit");
            }

            mAccountA->storeChange(tempDelta, db);
        }
        else
        {
            if (!mWheatLineA->addBalance(wheatReceived))
            {
                // this would indicate a bug in OfferExchange
                throw std::runtime_error("offer claimed over limit");
            }

            mWheatLineA->storeChange(tempDelta, db);
        }

        if (sheep.type() == ASSET_TYPE_NATIVE)
        {
            if (!mAccountA->addBalance(-sheepSent))
            {
                // this would indicate a bug in OfferExchange
                throw std::runtime_error("offer sold more than balance");
            }
            mAccountA->storeChange(tempDelta, db);
        }
        else
        {
            if (!mSheepLineA->addBalance(-sheepSent))
            {
                // this would indicate a bug in OfferExchange
                throw std::runtime_error("offer sold more than balance");
            }
            mSheepLineA->storeChange(tempDelta, db);
        }

        //SELLER => get the sheep, and give the wheat
        if (wheat.type() == ASSET_TYPE_NATIVE)
        {
            if (!mAccountB->addBalance(-wheatReceived))
            {
                // this would indicate a bug in OfferExchange
                throw std::runtime_error("offer claimed over limit");
            }

            mAccountB->storeChange(tempDelta, db);
        }
        else
        {
            if (!mWheatLineB->addBalance(-wheatReceived))
            {
                // this would indicate a bug in OfferExchange
                throw std::runtime_error("offer claimed over limit");
            }

            mWheatLineB->storeChange(tempDelta, db);
        }

        if (sheep.type() == ASSET_TYPE_NATIVE)
        {
            if (!mAccountB->addBalance(sheepSent))
            {
                // this would indicate a bug in OfferExchange
                throw std::runtime_error("offer sold more than balance");
            }
            mAccountB->storeChange(tempDelta, db);
        }
        else
        {
            if (!mSheepLineB->addBalance(sheepSent))
            {
                // this would indicate a bug in OfferExchange
                throw std::runtime_error("offer sold more than balance");
            }
            mSheepLineB->storeChange(tempDelta, db);
        }

    }
    
    // create the offer in the db
    mTradeOffer->mEntry.data.offer().offerID = tempDelta.getHeaderFrame().generateID();
    innerResult().code(NEW_TRADE_SUCCESS);
    innerResult().success().offer.effect(NEW_TRADE_CREATED);
    mTradeOffer->storeAdd(tempDelta, db);
    mSourceAccount->storeChange(tempDelta, db);
    
    
    ClaimOfferAtom atom(mAccountA->getID(), mTradeOffer->getOfferID(), wheat,
                   wheatReceived, sheep, sheepSent);
    innerResult().success().offer.offer() = mTradeOffer->getOffer();
    innerResult().success().offersClaimed.push_back(atom);

    sqlTx.commit();
    tempDelta.commit();

    app.getMetrics()
        .NewMeter({"op-new-trade", "success", "apply"}, "operation")
        .Mark();
    return true;
}

// makes sure the currencies are different
bool
NewTradeOpFrame::doCheckValid(Application& app)
{
    Asset const& sheep = mNewtrade.selling;
    Asset const& wheat = mNewtrade.buying;

    if (!isAssetValid(sheep) || !isAssetValid(wheat))
    {
        app.getMetrics()
            .NewMeter({"op-new-trade", "invalid", "invalid-asset"},
                      "operation")
            .Mark();
        innerResult().code(NEW_TRADE_MALFORMED);
        return false;
    }
    if (compareAsset(sheep, wheat))
    {
        app.getMetrics()
            .NewMeter({"op-new-trade", "invalid", "equal-currencies"},
                      "operation")
            .Mark();
        innerResult().code(NEW_TRADE_MALFORMED);
        return false;
    }
    if (mNewtrade.amount < 0 || mNewtrade.price.d <= 0 ||
        mNewtrade.price.n <= 0)
    {
        app.getMetrics()
            .NewMeter({"op-new-trade", "invalid", "negative-or-zero-values"},
                      "operation")
            .Mark();
        innerResult().code(NEW_TRADE_MALFORMED);
        return false;
    }

    return true;
}

OfferEntry
NewTradeOpFrame::buildOffer(AccountID const& account,
                               NewTradeOp const& op, uint32 flags)
{
    OfferEntry o;
    o.sellerID = account;
    o.offerID = 0;
    o.amount = op.amount;
    o.price = op.price;
    o.selling = op.selling;
    o.buying = op.buying;
    o.flags = flags;
    return o;
}

}

