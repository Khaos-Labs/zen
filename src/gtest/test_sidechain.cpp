#include <gtest/gtest.h>
#include <sc/sidechain.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <consensus/validation.h>
#include <txmempool.h>
#include <undo.h>

class SidechainTestSuite: public ::testing::Test {

public:
    SidechainTestSuite() :
            sidechainManager(Sidechain::ScMgr::instance()), coinViewCache(),
            aBlock(), aTransaction(), anHeight(1789),
            txState(), aFeeRate(), aMemPool(aFeeRate){};

    ~SidechainTestSuite() {
        sidechainManager.reset();
    };

    void SetUp() override {
        SelectBaseParams(CBaseChainParams::REGTEST);
        SelectParams(CBaseChainParams::REGTEST);

        sidechainManager.initialUpdateFromDb(0, true, Sidechain::ScMgr::mock);
    };

    void TearDown() override {};

protected:
    //Subjects under test
    Sidechain::ScMgr&           sidechainManager;
    Sidechain::ScCoinsViewCache coinViewCache;

    //Helpers
    CBlock              aBlock;
    CTransaction        aTransaction;
    int                 anHeight;
    CValidationState    txState;

    CFeeRate   aFeeRate;
    CTxMemPool aMemPool;
    CBlockUndo aBlockUndo;

    void preFillSidechainsCollection();

    CTransaction createSidechainTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);
    CTransaction createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);

    CTransaction createEmptyScTx();
    CTransaction createSidechainTxWithNoFwdTransfer(const uint256 & newScId);
    CTransaction createNonScTx(bool ccIsNull = true);
    CTransaction createShieldedTx();
    void         extendTransaction(CTransaction & tx, const uint256 & scId, const CAmount & amount);

    CBlockUndo   createBlockUndoWith(const uint256 & scId, int height, CAmount amount);
    CBlockUndo   createEmptyBlockUndo();
};

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// checkTxSemanticValidity ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, NonSidechain_CcNull_TxsAreSemanticallyValid) {
    aTransaction = createNonScTx();

    //prerequisites
    ASSERT_FALSE(aTransaction.IsScVersion());
    ASSERT_TRUE(aTransaction.ccIsNull());
    ASSERT_TRUE(txState.IsValid());

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, NonSidechain_NonCcNull_TxsAreNotSemanticallyValid) {
    aTransaction = createNonScTx(/*ccIsNull = */false);

    //prerequisites
    ASSERT_FALSE(aTransaction.IsScVersion());
    ASSERT_FALSE(aTransaction.ccIsNull());
    ASSERT_TRUE(txState.IsValid());

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, Sidechain_Shielded_TxsAreNotCurrentlySupported) {
    aTransaction = createShieldedTx();

    //prerequisites
    ASSERT_TRUE(aTransaction.IsScVersion());
    ASSERT_TRUE(aTransaction.vjoinsplit.size() != 0);
    ASSERT_TRUE(txState.IsValid());

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, Sidechain_ccNull_TxsAreSemanticallyValid) {
    aTransaction = createEmptyScTx();

    //prerequisites
    ASSERT_TRUE(aTransaction.IsScVersion());
    ASSERT_TRUE(aTransaction.ccIsNull());
    ASSERT_TRUE(txState.IsValid());

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SidechainCreationsWithoutForwardTransferAreNotSemanticallyValid) {
    //create a sidechain without fwd transfer
    uint256 newScId = uint256S("1492");
    aTransaction = createSidechainTxWithNoFwdTransfer(newScId);

    //prerequisites
    ASSERT_TRUE(aTransaction.IsScVersion());
    ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0);
    ASSERT_FALSE(aTransaction.vft_ccout.size() != 0);
    ASSERT_TRUE(txState.IsValid());

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithPositiveForwardTransferAreSemanticallyValid) {
    //insert a sidechain
    uint256 newScId = uint256S("1492");
    CAmount initialFwdAmount = 1000;
    aTransaction = createSidechainTxWith(newScId, initialFwdAmount);

    //prerequisites
    ASSERT_TRUE(aTransaction.IsScVersion());
    ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0);
    ASSERT_TRUE(aTransaction.vft_ccout.size() != 0);
    ASSERT_TRUE(txState.IsValid());
    ASSERT_TRUE(initialFwdAmount > 0);

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SidechainCreationsWithTooLargePositiveForwardTransferAreNotSemanticallyValid) {
    //insert a sidechain
    uint256 newScId = uint256S("1492");
    CAmount initialFwdAmount = MAX_MONEY +1;
    aTransaction = createSidechainTxWith(newScId, initialFwdAmount);

    //prerequisites
    ASSERT_TRUE(aTransaction.IsScVersion());
    ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0);
    ASSERT_TRUE(aTransaction.vft_ccout.size() != 0);
    ASSERT_TRUE(txState.IsValid());
    ASSERT_TRUE(initialFwdAmount > MAX_MONEY);

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithZeroForwardTransferAreNotSemanticallyValid) {
    //insert a sidechain
    uint256 newScId = uint256S("1492");
    CAmount initialFwdAmount = 0;
    aTransaction = createSidechainTxWith(newScId, initialFwdAmount);

    //prerequisites
    ASSERT_TRUE(aTransaction.IsScVersion());
    ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0);
    ASSERT_TRUE(aTransaction.vft_ccout.size() != 0);
    ASSERT_TRUE(txState.IsValid());
    ASSERT_TRUE(initialFwdAmount == 0);

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithNegativeForwardTransferNotAreSemanticallyValid) {
    //insert a sidechain
    uint256 newScId = uint256S("1492");
    CAmount initialFwdAmount = -1;
    aTransaction = createSidechainTxWith(newScId, initialFwdAmount);

    //prerequisites
    ASSERT_TRUE(aTransaction.IsScVersion());
    ASSERT_TRUE(aTransaction.vsc_ccout.size() != 0);
    ASSERT_TRUE(aTransaction.vft_ccout.size() != 0);
    ASSERT_TRUE(txState.IsValid());
    ASSERT_TRUE(initialFwdAmount < 0);

    //test
    bool res = sidechainManager.checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////// IsTxApplicableToState ////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, EmptyTxsAreApplicableToState) {
    aTransaction = createEmptyScTx();

    //Prerequisite
    ASSERT_TRUE(aTransaction.ccIsNull())<<"Test context: not Sc creation tx, nor forward transfer tx";

    //test
    bool res = sidechainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, ScCreationWithoutForwardTrasferIsApplicableToState) {
    uint256 newScId = uint256S("1492");
    aTransaction = createSidechainTxWithNoFwdTransfer(newScId);

    //Prerequisite
    ASSERT_FALSE(coinViewCache.sidechainExists(newScId))
        <<"Test context: the Sc creation tx to be new in current transaction";

    //test
    bool res = sidechainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, NewScCreationsAreApplicableToState) {
    uint256 newScId = uint256S("1492");
    CAmount initialFwdAmount = 1953;
    aTransaction = createSidechainTxWith(newScId, initialFwdAmount);

    //Prerequisite
    ASSERT_FALSE(coinViewCache.sidechainExists(newScId))
        <<"Test context: the Sc creation tx to be new";

    //test
    bool res = sidechainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, DuplicatedScCreationsAreNotApplicableToState) {
    //insert a sidechain
    uint256 newScId = uint256S("1492");
    CAmount initialFwdAmount = 1953;
    aTransaction = createSidechainTxWith(newScId, initialFwdAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    CAmount anotherFwdTransfer = 1815;
    CTransaction duplicatedTx = createSidechainTxWith(newScId, anotherFwdTransfer);

    //Prerequisite
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))
        <<"Test context: the Sc creation tx to be new";

    //test
    bool res = sidechainManager.IsTxApplicableToState(duplicatedTx, &coinViewCache);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistingSCsAreApplicableToState) {
    //insert a sidechain
    uint256 newScId = uint256S("1492");
    CAmount initialFwdAmount = 1953;
    aTransaction = createSidechainTxWith(newScId, initialFwdAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    CAmount aFwdTransfer = 5;
    aTransaction = createFwdTransferTxWith(newScId, aFwdTransfer);

    //Prerequisite
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))
        <<"Test context: the Sc creation tx to be new";

    //test
    bool res = sidechainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistingSCsAreNotApplicableToState) {
    uint256 nonExistentScId = uint256S("1492");

    CAmount aFwdTransfer = 1815;
    aTransaction = createFwdTransferTxWith(nonExistentScId, aFwdTransfer);

    //Prerequisite
    ASSERT_FALSE(coinViewCache.sidechainExists(nonExistentScId))
        <<"Test context: target sidechain to be non-existent";

    //test
    bool res = sidechainManager.IsTxApplicableToState(aTransaction, &coinViewCache);

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////// IsTxAllowedInMempool /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, EmptyTxsAreAllowedInEmptyMemPool) {
    aTransaction = createEmptyScTx();

    //prerequisites
    ASSERT_TRUE(aMemPool.size() == 0)<<"Test context: empty mempool";
    ASSERT_TRUE(aTransaction.ccIsNull())<<"Test context: not Sc creation tx, nor forward transfer tx";
    ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

    //test
    bool res = sidechainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, EmptyTxsAreAllowedInNonEmptyMemPool) {
    aTransaction = createEmptyScTx();

    CAmount txFee;
    double txPriority = 0.0;

    CTxMemPoolEntry memPoolEntry(aTransaction, txFee, GetTime(), txPriority, anHeight);

    ASSERT_TRUE(aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry))
        <<"Test context: at least a tx in mempool. Could not insert it.";

    //prerequisites
    ASSERT_TRUE(aMemPool.size() != 0)<<"Test context: non-empty mempool";
    ASSERT_TRUE(aTransaction.ccIsNull())<<"Test context: not Sc creation tx, nor forward transfer tx";
    ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

    //test
    bool res = sidechainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, ScCreationTxsAreAllowedInEmptyMemPool) {
    //create a sidechain
    uint256 newScId = uint256S("1492");
    CAmount initialFwdAmount = 1953;
    aTransaction = createSidechainTxWith(newScId, initialFwdAmount);

    //prerequisites
    ASSERT_TRUE(aMemPool.size() == 0)<<"Test context: empty mempool";
    ASSERT_FALSE(aTransaction.ccIsNull())<<"Test context: a Sc creation tx";
    ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

    //test
    bool res = sidechainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, NewScCreationTxsAreAllowedInMemPool) {
    //A Sc tx should be already in mem pool
    uint256 firstScTxId = uint256S("1987");
    CAmount firstScAmount = 1994;
    aTransaction = createSidechainTxWith(firstScTxId, firstScAmount);

    CAmount txFee;
    double txPriority = 0.0;

    CTxMemPoolEntry memPoolEntry(aTransaction, txFee, GetTime(), txPriority, anHeight);
    ASSERT_TRUE(aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry))
        <<"Test context: at least a tx in mempool. Could not insert it.";

    //prerequisites
    ASSERT_TRUE(aMemPool.size() != 0)<<"Test context: non-empty mempool";
    ASSERT_FALSE(aTransaction.ccIsNull())<<"Test context: a Sc creation tx";
    ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

    //Prepare a new Sc tx, with differentId
    uint256 secondScTxId = uint256S("1991");
    CAmount secondScAmount = 5;
    aTransaction = createSidechainTxWith(secondScTxId, secondScAmount);

    //prerequisites
    ASSERT_TRUE(firstScTxId != secondScTxId)<<"Test context: two Sc creation tx with different ids";

    //test
    bool res = sidechainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, DuplicatedScCreationTxsAreNotAllowedInMemPool) {
    //create a sidechain tx and insert in mempool
    uint256 firstScId = uint256S("1987");
    CAmount initialFwdAmount = 1953;
    aTransaction = createSidechainTxWith(firstScId, initialFwdAmount);

    CAmount txFee;
    double txPriority = 0.0;

    CTxMemPoolEntry memPoolEntry(aTransaction, txFee, GetTime(), txPriority, anHeight);
    ASSERT_TRUE(aMemPool.addUnchecked(aTransaction.GetHash(), memPoolEntry))
        <<"Test context: at least a tx in mempool. Could not insert it.";

    //prerequisites
    ASSERT_TRUE(aMemPool.size() != 0)<<"Test context: non-empty mempool";
    ASSERT_FALSE(aTransaction.ccIsNull())<<"Test context: a Sc creation tx";
    ASSERT_TRUE(txState.IsValid())<<"Test require transition state to be valid a-priori";

    //Prepare a new Sc tx, with differentId
    uint256 duplicatedScId = firstScId;
    CAmount anotherAmount = 1492;
    CTransaction duplicatedTx = createSidechainTxWith(duplicatedScId, anotherAmount);

    //prerequisites
    ASSERT_TRUE(duplicatedScId == firstScId)<<"Test context: two Sc creation tx with same ids";

    //test
    bool res = sidechainManager.IsTxAllowedInMempool(aMemPool, aTransaction, txState);

    //check
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////// ApplyMatureBalances /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, CoinsInScCreationDoNotModifyScBalanceBeforeCoinMaturity) {
    //Insert Sc
    uint256 newScId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    int scCreationHeight = 5;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight - 1;

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: existing sc";
    ASSERT_TRUE(lookupBlockHeight < coinMaturityHeight)
        <<"Test context: attempting to mature coins before their maturity height";

    //test
    bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(coinViewCache.getScInfoMap().at(newScId).balance < initialAmount)
        <<"Coins should not alter Sc balance before coin maturity height comes";
}

TEST_F(SidechainTestSuite, CoinsInScCreationModifyScBalanceAtCoinMaturity) {
    //Insert Sc
    uint256 newScId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    int scCreationHeight = 7;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight;

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: existing sc";
    ASSERT_TRUE(lookupBlockHeight == coinMaturityHeight)
        <<"Test context: attempting to mature coins at maturity height";

    //test
    bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(coinViewCache.getScInfoMap().at(newScId).balance == initialAmount)
        <<"Current balance is "<<coinViewCache.getScInfoMap().at(newScId).balance
        <<" expected one is "<<initialAmount;
}

TEST_F(SidechainTestSuite, CoinsInScCreationDoNotModifyScBalanceAfterCoinMaturity) {
    //Insert Sc
    uint256 newScId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    int scCreationHeight = 11;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = anHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight + 1;

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: existing sc";
    ASSERT_TRUE(lookupBlockHeight > coinMaturityHeight)
        <<"Test context: attempting to mature coins after their maturity height";

    //test
    bool res = coinViewCache.ApplyMatureBalances(lookupBlockHeight, aBlockUndo);

    //check
    EXPECT_FALSE(res);
    EXPECT_TRUE(coinViewCache.getScInfoMap().at(newScId).balance < initialAmount)
        <<"Current balance is "<<coinViewCache.getScInfoMap().at(newScId).balance
        <<" while initial amount is "<<initialAmount;
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// RestoreImmatureBalances ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, RestoringFromUndoBlockAffectBalance) {
    //insert a sidechain
    uint256 newScId = uint256S("ca1985");
    CAmount initialAmount = 34;
    int scCreationHeight = 71;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //let balance mature
    int maturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    coinViewCache.ApplyMatureBalances(maturityHeight, aBlockUndo);
    CAmount scBalance = coinViewCache.getScInfoMap().at(newScId).balance;

    CAmount amountToUndo = 17;
    aBlockUndo = createBlockUndoWith(newScId,scCreationHeight,amountToUndo);

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exists";
    ASSERT_TRUE(scBalance == initialAmount) <<"Test context: initial coins to have matured";
    ASSERT_TRUE(amountToUndo <= scBalance)
         <<"Test context: not attempting to restore more than initial value";

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);
    CAmount restoredBalance = coinViewCache.getScInfoMap().at(newScId).balance;
    EXPECT_TRUE(restoredBalance == scBalance - amountToUndo)
        <<"balance after restore is "<<restoredBalance<<" instead of"<< scBalance - amountToUndo;
}

TEST_F(SidechainTestSuite, YouCannotRestoreMoreCoinsThanAvailableBalance) {
    //insert a sidechain
    uint256 newScId = uint256S("ca1985");
    CAmount initialAmount = 34;
    int scCreationHeight = 1991;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //let balance mature
    int maturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    coinViewCache.ApplyMatureBalances(maturityHeight, aBlockUndo);
    CAmount scBalance = coinViewCache.getScInfoMap().at(newScId).balance;

    CAmount amountToUndo = 50;
    aBlockUndo = createBlockUndoWith(newScId,scCreationHeight,amountToUndo);

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exists";
    ASSERT_TRUE(scBalance == initialAmount) <<"Test context: initial coins to have matured";
    ASSERT_TRUE(amountToUndo > scBalance)
         <<"Test context: attempting to restore more than initial value";

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    CAmount restoredBalance = coinViewCache.getScInfoMap().at(newScId).balance;
    EXPECT_TRUE(restoredBalance == scBalance)
        <<"balance after restore is "<<restoredBalance<<" instead of"<< scBalance;
}

TEST_F(SidechainTestSuite, RestoringBeforeBalanceMaturesHasNoEffects) {
    //insert a sidechain
    uint256 newScId = uint256S("ca1985");
    CAmount initialAmount = 34;
    int scCreationHeight = 71;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    CAmount scBalance = coinViewCache.getScInfoMap().at(newScId).balance;

    CAmount amountToUndo = 17;
    aBlockUndo = createBlockUndoWith(newScId,scCreationHeight,amountToUndo);

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exists";
    ASSERT_TRUE(scBalance == 0) <<"Test context: initial coins to have not matured";
    ASSERT_TRUE(amountToUndo != 0)
         <<"Test context: attempting to restore some non-zero coins";

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    CAmount restoredBalance = coinViewCache.getScInfoMap().at(newScId).balance;
    EXPECT_TRUE(restoredBalance == 0)
        <<"balance after restore is "<<restoredBalance<<" instead of 0";
}

TEST_F(SidechainTestSuite, RestoringFromEmptyUndoBlockHasEffect) {
    //insert a sidechain
    uint256 newScId = uint256S("ca1985");
    CAmount initialAmount = 34;
    int scCreationHeight = 71;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //let balance mature
    int maturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    coinViewCache.ApplyMatureBalances(maturityHeight, aBlockUndo);
    CAmount scBalance = coinViewCache.getScInfoMap().at(newScId).balance;

    aBlockUndo = createEmptyBlockUndo();

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exists";
    ASSERT_TRUE(scBalance == initialAmount) <<"Test context: initial coins to have matured";
    ASSERT_TRUE(aBlockUndo.msc_iaundo.size() == 0)<<"Test context: an empty undo block";

    //test
    bool res = coinViewCache.RestoreImmatureBalances(anHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);
    CAmount restoredBalance = coinViewCache.getScInfoMap().at(newScId).balance;
    EXPECT_TRUE(restoredBalance == scBalance)
        <<"balance after restore is "<<restoredBalance<<" instead of"<< scBalance;
}

TEST_F(SidechainTestSuite, YouCannotRestoreCoinsFromInexistentSc) {
    //insert a sidechain
    uint256 inexistentScId = uint256S("ca1985");
    int scCreationHeight = 71;

    CAmount amountToUndo = 10;
    aBlockUndo = createBlockUndoWith(inexistentScId,scCreationHeight,amountToUndo);

    //prerequisites
    ASSERT_FALSE(coinViewCache.sidechainExists(inexistentScId))<<"Test context: sc to be missing";

    //test
    bool res = coinViewCache.RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// RevertTxOutputs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, RevertingScCreationTxRemovesTheSc) {
    //create sidechain to be rollbacked and register it
    uint256 newScId = uint256S("a1b2");
    CAmount initialAmount = 1;
    int scCreationHeight = 1;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //create fwd transaction to be rollbacked
    int initialAmountMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    Sidechain::ScInfo viewInfo = coinViewCache.getScInfoMap().at(newScId);

    int revertHeight = scCreationHeight;

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exist";
    ASSERT_TRUE(revertHeight == scCreationHeight)
        <<"Test context: attempting a revert on the height where sc creation tx was stored";
    ASSERT_TRUE(viewInfo.mImmatureAmounts.at(initialAmountMaturityHeight) == initialAmount)
        <<"Test context: an initial amount amenable to be reverted";

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, revertHeight);

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(coinViewCache.sidechainExists(newScId));
}

TEST_F(SidechainTestSuite, RevertingFwdTransferRemovesCoinsFromImmatureBalance) {
    //insert sidechain
    uint256 newScId = uint256S("a1b2");
    CAmount initialAmount = 1;
    int scCreationHeight = 1;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //create fwd transaction to be rollbacked
    CAmount fwdAmount = 7;
    int fwdTxHeight = 5;
    int fwdTxMaturityHeight = fwdTxHeight + Params().ScCoinsMaturity();
    aTransaction = createFwdTransferTxWith(newScId, fwdAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeight);
    Sidechain::ScInfo viewInfo = coinViewCache.getScInfoMap().at(newScId);

    int revertHeight = fwdTxHeight;

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exist";
    ASSERT_TRUE(revertHeight == fwdTxHeight)
        <<"Test context: attempting a revert on the height where fwd tx was stored";
    ASSERT_TRUE(viewInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdAmount)
        <<"Test context: a fwd amount amenable to be reverted";

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, revertHeight);

    //checks
    EXPECT_TRUE(res);
    viewInfo = coinViewCache.getScInfoMap().at(newScId);
    EXPECT_TRUE(viewInfo.mImmatureAmounts.count(fwdTxMaturityHeight) == 0);
}

TEST_F(SidechainTestSuite, FwdTransferTxToUnexistingScCannotBeReverted) {
    uint256 unexistingScId = uint256S("a1b2");

    //create fwd transaction to be reverted
    CAmount fwdAmount = 999;
    aTransaction = createFwdTransferTxWith(unexistingScId, fwdAmount);

    //prerequisites
    ASSERT_FALSE(coinViewCache.sidechainExists(unexistingScId))
        <<"Test context: unexisting sideChain";

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, anHeight);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, ScCreationTxCannotBeRevertedIfScIsNotPreviouslyCreated) {
    uint256 unexistingScId = uint256S("a1b2");

    //create Sc transaction to be reverted
    aTransaction = createSidechainTxWithNoFwdTransfer(unexistingScId);

    //prerequisites
    ASSERT_FALSE(coinViewCache.sidechainExists(unexistingScId))
        <<"Test context: unexisint sideChain";

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, anHeight);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, RevertingAFwdTransferOnTheWrongHeightHasNoEffect) {
    //insert sidechain
    uint256 newScId = uint256S("a1b2");
    CAmount initialAmount = 1;
    int scCreationHeight = 1;
    aTransaction = createSidechainTxWith(newScId, initialAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //create fwd transaction to be rollbacked
    CAmount fwdAmount = 7;
    int fwdTxHeight = 5;
    int fwdTxMaturityHeight = fwdTxHeight + Params().ScCoinsMaturity();
    aTransaction = createFwdTransferTxWith(newScId, fwdAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeight);
    Sidechain::ScInfo viewInfo = coinViewCache.getScInfoMap().at(newScId);

    int revertHeight = fwdTxHeight -1;

    //prerequisites
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))<<"Test context: sc to exist";
    ASSERT_TRUE(revertHeight != fwdTxHeight)
        <<"Test context: attempting a revert on the height where fwd tx was stored";
    ASSERT_TRUE(viewInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdAmount)
        <<"Test context: a fwd amount amenable to be reverted";

    //test
    bool res = coinViewCache.RevertTxOutputs(aTransaction, revertHeight);

    //checks
    EXPECT_FALSE(res);
    viewInfo = coinViewCache.getScInfoMap().at(newScId);
    EXPECT_TRUE(viewInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdAmount)
        <<"Immature amount is "<<viewInfo.mImmatureAmounts.at(fwdTxMaturityHeight)
        <<"instead of "<<fwdAmount;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateScInfo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, EmptyTxsAreProcessedButNotRegistered) {
    //Prerequisite
    aTransaction = createEmptyScTx();
    ASSERT_TRUE(aTransaction.ccIsNull())<<"Test context: not Sc creation tx, nor forward transfer tx";

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //check
    EXPECT_TRUE(res) << "Empty tx should be processed"; //How to check for no side-effects (i.e. no register)
}

TEST_F(SidechainTestSuite, NewSCsAreRegisteredById) {
    uint256 newScId = uint256S("1492");
    CAmount initialFwdTxAmount = 1;
    aTransaction = createSidechainTxWith(newScId, initialFwdTxAmount);

    //Prerequisite
    ASSERT_FALSE(coinViewCache.sidechainExists(newScId))
            << "Test context: that sidechain is not registered";

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //check
    EXPECT_TRUE(res) << "New sidechain creation txs should be processed";
    EXPECT_TRUE(coinViewCache.sidechainExists(newScId))
            << "New sidechain creation txs should be cached";
}

TEST_F(SidechainTestSuite, ScDoubleInsertionIsRejected) {
    //first,valid sideChain transaction
    uint256 newScId = uint256S("1492");
    CAmount initialFwdTxAmount = 1;
    aTransaction = createSidechainTxWith(newScId, initialFwdTxAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //second, id-duplicated, sideChain transaction
    CAmount aFwdTxAmount = 999;
    CTransaction duplicatedTx = createSidechainTxWith(newScId, aFwdTxAmount);

    //prerequisites
    ASSERT_TRUE(aTransaction.vsc_ccout[0].scId == duplicatedTx.vsc_ccout[0].scId)
        <<"Test context: two SC Tx with same id";
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))
        <<"Test context: first Sc to be successfully registered";

    //test
    bool res = coinViewCache.UpdateScInfo(duplicatedTx, aBlock, anHeight);

    //check
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, NoRollbackIsPerformedOnceInvalidTransactionIsEncountered) {
    uint256 firstScId = uint256S("1492");
    CAmount firstScAmount = 10;
    aTransaction = createSidechainTxWith(firstScId, firstScAmount);

    uint256 duplicatedScId = uint256S("1492");
    CAmount duplicatedAmount = 100;
    extendTransaction(aTransaction, duplicatedScId, duplicatedAmount);

    uint256 anotherScId = uint256S("1912");
    CAmount anotherScAmount = 2;
    extendTransaction(aTransaction, anotherScId, anotherScAmount);

    //prerequisites
    ASSERT_TRUE(firstScId == duplicatedScId)<<"Test context: second tx to be a duplicate";
    ASSERT_TRUE(firstScId != anotherScId)<<"Test context: third tx to be a valid one";
    EXPECT_FALSE(coinViewCache.sidechainExists(firstScId))
        << "Test context: first sc not to be already created";
    EXPECT_FALSE(coinViewCache.sidechainExists(anotherScId))
        << "Test context: second sc not to be already created";

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //check
    EXPECT_FALSE(res);
    EXPECT_TRUE(coinViewCache.sidechainExists(firstScId))
        << "First, valid sidechain creation txs should be cached";
    EXPECT_FALSE(coinViewCache.sidechainExists(anotherScId))
        << "third, valid sidechain creation txs is currently not cached";
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistentScAreRejected) {
    uint256 nonExistentId = uint256S("1492");
    CAmount initialFwdAmount = 1987;
    aTransaction = createFwdTransferTxWith(nonExistentId, initialFwdAmount);

    //Prerequisite
    ASSERT_FALSE(coinViewCache.sidechainExists(nonExistentId))
        <<"Test context: target sidechain to be non-existent";

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //check
    EXPECT_FALSE(res);
    EXPECT_FALSE(coinViewCache.sidechainExists(nonExistentId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistentSCsAreRegistered) {
    //insert the sidechain
    uint256 newScId = uint256S("1492");
    CAmount initialFwdAmount = 1953;
    aTransaction = createSidechainTxWith(newScId, initialFwdAmount);

    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //create forward transfer
    CAmount anotherFwdAmount = 1987;
    aTransaction = createFwdTransferTxWith(newScId, anotherFwdAmount);

    //Prerequisite
    ASSERT_TRUE(coinViewCache.sidechainExists(newScId))
        <<"Test context: Sc to exist before attempting the forward transfer tx";

    //test
    bool res = coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //check
    EXPECT_TRUE(res);
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// Flush /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, FlushAlignsPersistedTxsWithViewOnes) {
    uint256 newScId = uint256S("a1b2");
    CAmount initialFwdTxAmount = 1;
    int scCreationHeight = 10;
    aTransaction = createSidechainTxWith(newScId, initialFwdTxAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //prerequisites
    ASSERT_TRUE(sidechainManager.sidechainExists(newScId,&coinViewCache))
        << "Test context: a tx to be ready to be persisted";

    //test
    bool res = coinViewCache.Flush();

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainManager.getScInfoMap() == coinViewCache.getScInfoMap())
        <<"flush should align txs in view with persisted ones";
}

TEST_F(SidechainTestSuite, UponViewCreationAllPersistedTxsAreLoaded) {
    //prerequisites
    preFillSidechainsCollection();
    ASSERT_TRUE(sidechainManager.getScInfoMap().size() != 0)<<"Test context: some sidechains initially";

    //test
    Sidechain::ScCoinsViewCache newView;

    //check
    EXPECT_TRUE(sidechainManager.getScInfoMap() == newView.getScInfoMap())
        <<"when new coinViewCache is create, it should be aligned with sidechain manager";
}

TEST_F(SidechainTestSuite, FlushPersistsNewSidechains) {
    //create the sidechain
    uint256 newScId = uint256S("a1b2");
    CAmount fwdTransfer = 1000;
    aTransaction = createSidechainTxWith(newScId, fwdTransfer);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, anHeight);

    //Prerequisite
    ASSERT_TRUE(sidechainManager.sidechainExists(newScId,&coinViewCache))
        << "Test context: new sidechain to be ready to be persisted";

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainManager.sidechainExists(newScId));
}

TEST_F(SidechainTestSuite, FlushPersistsForwardTransfersToo) {
    //create and persist the sidechain
    uint256 newScId = uint256S("a1b2");
    CAmount initialFwdTxAmount = 1;
    int scCreationHeight = 1;
    aTransaction = createSidechainTxWith(newScId, initialFwdTxAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    coinViewCache.Flush();

    //create forward transfer
    CAmount fwdTxAmount = 1000;
    int fwdTxHeght = scCreationHeight + 10;
    int fwdTxMaturityHeight = fwdTxHeght + Params().ScCoinsMaturity();
    aTransaction = createFwdTransferTxWith(newScId, fwdTxAmount);
    coinViewCache.UpdateScInfo(aTransaction, aBlock, fwdTxHeght);

    //prerequisites
    ASSERT_TRUE(sidechainManager.sidechainExists(newScId))
        << "Test context: new sidechain to be already persisted";

    Sidechain::ScInfo infoInView = coinViewCache.getScInfoMap().at(newScId);
    ASSERT_TRUE(infoInView.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdTxAmount)
        <<"Test context: fwd amount to be ready to be flushed";

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_TRUE(res);

    Sidechain::ScInfo persistedInfo = sidechainManager.getScInfoMap().at(newScId);
    ASSERT_TRUE(persistedInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdTxAmount)
        <<"Following flush, persisted fwd amount should equal the one in view";
}

TEST_F(SidechainTestSuite, EmptyFlushDoesNotPersistNewSidechain) {
    const Sidechain::ScInfoMap & initialScCollection = sidechainManager.getScInfoMap();

    //prerequisites
    ASSERT_TRUE(coinViewCache.getScInfoMap().size() == 0)<<"There should be no new txs to persist";
    ASSERT_TRUE(initialScCollection.size() == 0)<<"Test context: no sidechains initially";

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_TRUE(res);

    const Sidechain::ScInfoMap & finalScCollection = sidechainManager.getScInfoMap();
    EXPECT_TRUE(finalScCollection == initialScCollection)
        <<"Sidechains collection should not have changed with empty flush";
}

TEST_F(SidechainTestSuite, EmptyFlushDoesNotAlterExistingSidechainsCollection) {
    //prerequisites
    preFillSidechainsCollection();

    const Sidechain::ScInfoMap & initialScCollection = sidechainManager.getScInfoMap();

    ASSERT_TRUE(coinViewCache.getScInfoMap().size() == 0)<<"There should be no new txs to persist";
    ASSERT_TRUE(initialScCollection.size() != 0)<<"Test context: some sidechains initially";

    //test
    bool res = coinViewCache.Flush();

    //checks
    EXPECT_TRUE(res);

    const Sidechain::ScInfoMap & finalScCollection = sidechainManager.getScInfoMap();
    EXPECT_TRUE(finalScCollection == initialScCollection)
        <<"Sidechains collection should not have changed with empty flush";
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Structural UTs ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, ManagerIsSingleton) {
    //test
    Sidechain::ScMgr& rAnotherScMgrInstance = Sidechain::ScMgr::instance();

    //check
    EXPECT_TRUE(&sidechainManager == &rAnotherScMgrInstance)
            << "ScManager Instances have different address:"
            << &sidechainManager << " and " << &rAnotherScMgrInstance;
}

TEST_F(SidechainTestSuite, ManagerDoubleInitializationIsForbidden) {
    size_t cacheSize(0);
    bool fWipe(false);

    //prerequisites: first initialization happens in fixture's setup

    //test
    bool res = sidechainManager.initialUpdateFromDb(cacheSize, fWipe, Sidechain::ScMgr::mock);

    //Checks
    EXPECT_FALSE(res) << "Db double initialization should be forbidden";
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// Test Fixture definitions ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SidechainTestSuite::preFillSidechainsCollection() {
    //force access to manager in-memory data structure to fill it up for testing purposes

    Sidechain::ScInfoMap & rManagerInternalMap
        = const_cast<Sidechain::ScInfoMap&>(sidechainManager.getScInfoMap());

    //create a couple of ScInfo to fill data struct
    Sidechain::ScInfo info;
    uint256 scId;

    scId = uint256S("a123");
    info.creationBlockHash = uint256S("aaaa");
    info.creationBlockHeight = 1992;
    info.creationTxHash = uint256S("bbbb");
    rManagerInternalMap[scId] = info;

    scId = uint256S("b987");
    info.creationBlockHash = uint256S("1111");
    info.creationBlockHeight = 1993;
    info.creationTxHash = uint256S("2222");
    rManagerInternalMap[scId] = info;
}

CTransaction SidechainTestSuite::createSidechainTxWith(const uint256 & newScId, const CAmount & fwdTxAmount)
{
    CMutableTransaction aMutableTransaction;
    aMutableTransaction.nVersion = SC_TX_VERSION;

    CTxScCreationOut aSidechainCreationTx;
    aSidechainCreationTx.scId = newScId;
    aMutableTransaction.vsc_ccout.push_back(aSidechainCreationTx);

    CTxForwardTransferOut aForwardTransferTx;
    aForwardTransferTx.scId = aSidechainCreationTx.scId;
    aForwardTransferTx.nValue = fwdTxAmount;
    aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);

    return CTransaction(aMutableTransaction);
}

CTransaction SidechainTestSuite::createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount)
{
    CMutableTransaction aMutableTransaction;
    aMutableTransaction.nVersion = SC_TX_VERSION;

    CTxForwardTransferOut aForwardTransferTx;
    aForwardTransferTx.scId = newScId;
    aForwardTransferTx.nValue = fwdTxAmount;
    aMutableTransaction.vft_ccout.push_back(aForwardTransferTx);

    return CTransaction(aMutableTransaction);
}

CTransaction SidechainTestSuite::createEmptyScTx() {
    CMutableTransaction aMutableTransaction;
    aMutableTransaction.nVersion = SC_TX_VERSION;

    return CTransaction(aMutableTransaction);
}

CTransaction SidechainTestSuite::createSidechainTxWithNoFwdTransfer(const uint256 & newScId)
{
    CMutableTransaction aMutableTransaction;
    aMutableTransaction.nVersion = SC_TX_VERSION;

    CTxScCreationOut aSidechainCreationTx;
    aSidechainCreationTx.scId = newScId;
    aMutableTransaction.vsc_ccout.push_back(aSidechainCreationTx);

    return CTransaction(aMutableTransaction);
}

CTransaction SidechainTestSuite::createNonScTx(bool ccIsNull) {
    CMutableTransaction aMutableTransaction;
    aMutableTransaction.nVersion = TRANSPARENT_TX_VERSION;

    if (!ccIsNull)
    {
        CTxScCreationOut aSidechainCreationTx;
        aSidechainCreationTx.scId = uint256S("1492");
        aMutableTransaction.vsc_ccout.push_back(aSidechainCreationTx);
    }

    return CTransaction(aMutableTransaction);
}

CTransaction SidechainTestSuite::createShieldedTx()
{
    CMutableTransaction aMutableTransaction;
    aMutableTransaction.nVersion = SC_TX_VERSION;
    JSDescription  aShieldedTx;
    aMutableTransaction.vjoinsplit.push_back(aShieldedTx);

    return CTransaction(aMutableTransaction);
}

void  SidechainTestSuite::extendTransaction(CTransaction & tx, const uint256 & scId, const CAmount & amount) {
    CMutableTransaction mutableTx = tx;

    mutableTx.nVersion = SC_TX_VERSION;

    CTxScCreationOut aSidechainCreationTx;
    aSidechainCreationTx.scId = scId;
    mutableTx.vsc_ccout.push_back(aSidechainCreationTx);

    CTxForwardTransferOut aForwardTransferTx;
    aForwardTransferTx.scId = aSidechainCreationTx.scId;
    aForwardTransferTx.nValue = amount;
    mutableTx.vft_ccout.push_back(aForwardTransferTx);

    tx = mutableTx;
    return;
}

CBlockUndo SidechainTestSuite::createBlockUndoWith(const uint256 & scId, int height, CAmount amount)
{
    CBlockUndo retVal;
    std::map<int, CAmount> AmountPerHeight;
    AmountPerHeight[height] = amount;
    retVal.msc_iaundo[scId] = AmountPerHeight;

    return retVal;
}

CBlockUndo SidechainTestSuite::createEmptyBlockUndo()
{
    return CBlockUndo();
}