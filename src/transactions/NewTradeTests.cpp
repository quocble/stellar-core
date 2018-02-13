// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "database/Database.h"
#include "ledger/LedgerManager.h"
#include "lib/util/uint128_t.h"
#include "main/Application.h"
#include "main/Config.h"
#include "test/TestAccount.h"
#include "test/TestExceptions.h"
#include "test/TestMarket.h"
#include "test/TestUtils.h"
#include "test/TxTests.h"
#include "test/test.h"
#include "transactions/OfferExchange.h"
#include "util/Logging.h"
#include "util/Timer.h"
#include "util/format.h"
#include "util/make_unique.h"

using namespace stellar;
using namespace stellar::txtest;

// Offer that takes multiple other offers and remains
// Offer selling XLM
// Offer buying XLM
// Offer with transfer rate
// Offer for more than you have
// Offer for something you can't hold
// Offer with line full (both accounts)

TEST_CASE("new trade", "")
{
    Config const& cfg = getTestConfig();

    VirtualClock clock;
    auto app = createTestApplication(clock, cfg);
    app->start();

    // set up world
    auto root = TestAccount::createRoot(*app);

    int64_t trustLineBalance = 100000;
    int64_t trustLineLimit = trustLineBalance * 10;

    int64_t txfee = app->getLedgerManager().getTxFee();

    // minimum balance necessary to hold 2 trust lines
    const int64_t minBalance2 =
        app->getLedgerManager().getMinBalance(2) + 20 * txfee;

    // sets up issuer account
    auto issuer = root.create("issuer", minBalance2 * 10);
    auto xlm = makeNativeAsset();
    auto idr = issuer.asset("IDR");
    auto usd = issuer.asset("USD");

    const Price oneone(1, 1);


    SECTION("setup trust")
    {
        auto a1 = root.create("A", minBalance2 * 2);
        auto b1 = root.create("B", minBalance2 * 2);

        a1.changeTrust(idr, trustLineLimit);
        a1.changeTrust(usd, trustLineLimit);
        b1.changeTrust(idr, trustLineLimit);
        b1.changeTrust(usd, trustLineLimit);

        issuer.pay(a1, idr, trustLineBalance);
        issuer.pay(b1, usd, trustLineBalance);

        auto market = TestMarket{*app};
        auto firstOffer = market.newTrade(issuer, {idr, usd, oneone, 100});;        
    }
}
