// Copyright (c) 2012 The LuckChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITBET_H
#define BITBET_H

#ifdef WIN32
#include <windows.h>
#endif
#include <stdint.h>
#include <string>
#include "base58.h"
#include "key.h"
#include "init.h"
#include "bignum.h"
#include "sync.h"
#include "net.h"
#include "main.h"
#include "script.h"
#include "scrypt.h"
#include <list>
#include <fstream>

#ifdef QT_GUI
    #include "sqlite3/sqlite3.h"
#else
    #ifdef WIN32
        #include "sqlite3/sqlite3.h"
    #else
        #include <sqlite3.h>
    #endif
#endif

using namespace std;
using namespace boost;

// Bet Magic | Lottery ID | Opcode (Genesis = 1, Bet = 2, Cash = 3) | Bet Type | Bet Amount | Mini Bet Amount | Start block | Target block 
//                 | Guess HASH Length | Bet Text | Lottery wallet address | Lottery wallet Private Key 
//                 | Bettor's default Wallet Address | Lottery Tx ID | Sign message¡£
// Bet Type: 0 = Lucky 16,  1 = Lucky Odd-Even, 2 = Lucky Big-Small, 3 = Lucky Boss, 4 = Lucky Lotto, 5 = Bet RealEvents
//typedef 
struct BitBetPack
{
   //string betMagic;
   int id;
   int opCode;
   int betType;
   uint64_t u6BetAmount;
   uint64_t u6MiniBetAmount;
   uint64_t u6StartBlock;
   uint64_t u6TargetBlock, u6BetCount;
   int betLen, betStartNum;
   string betNum;
   string bettor;
   string genBet;
   string referee;
   string betTitle, refereeNick, ex2, ex3;
   
   string betNum2;
   string tx;
   string refereeDecideTx;
   uint64_t u6BlockSpace;
   int confirmed, payIndex;
   uint64_t u6MaxBetCoins, u6Time, u6TotalBetAmount;
   int done;

   int paramCount;
};//BitBetPack, *PBitBetPack;

// BitBetCmd: | Referee Order | Reg(Enb, Dis) | 
struct BitBetCommand{
   string cmdName;
   string p1, p2, p3, p4, p5, p6, p7, p8, p9, pa;
   int paramCount;
};

struct dbOneResultCallbackPack
{
   int vType;  // 1 uint64_t,  2 string
   int fID;    int fDone;
   uint64_t u6Rzt;
   string sFieldName;     string sRzt;
};

struct txOutPairPack{
   int idx;   uint64_t v_nValue;
   string sAddr;
};

struct dbBitBetTotalAmountAndWinnerPack{
   uint64_t u6RecordCount, u6AllBetCoins,  u6WinnerCount, u6AllWinerBetCoins;
   std::vector<txOutPairPack > allWiners;
};

#define Total_Blocks_of_20Days 28800
#define Big_Target_Block_Min_Bet_Amount 1000000
#define Mini_Banker_Bet_Amount 100000
#define New_Rules_161013_Active_Block 21000
#define New_LuckyBossRule_Active_Block 33000
#define Max_Alive_Launch_BitBets 2000
#define Max_One_Bettor_Alive_Launch_BitBets 32
#define BitBet_Launch_MultiBet_MinAmount 1000
#define BitBet_Launch_MultiBet_Amount 1000
#define BitBet_Launch_EventBet_Amount 5000
#define BitBet_Launch_LottoBet_Amount 10000
#define BitBet_RewardMiner_Rate 0.001
#define BitBet_Lucky16_Max_Bet_Count 15
#define BitBet_Lucky16_Max_Reward_Times 15
#define BitBet_Standard_Confirms 10
#define BitBet_Lucky_Lotto_ReBet_Confirms 20
#define BitBet_MAX_Confirms 60
#define BitBet_Real_Event_Confirms 36 * 60
#define MAX_BitBet_Str_Param_Len 1024
#define OneResultPack_U64_TYPE  1
#define OneResultPack_STR_TYPE 2
#define Referees_coinAddress_idx 2
#define Referees_fee_idx 4
#define Referees_maxcoins_idx 5

#define AllBets_id_idx 0
#define AllBets_opcode_idx 1
#define AllBets_bet_type_idx 2
#define AllBets_bet_amount_idx 3
#define AllBets_min_amount_idx 4
#define AllBets_sblock_idx 5
#define AllBets_blockspace_idx 6
#define AllBets_tblock_idx 7
#define AllBets_bet_len_idx 8
#define AllBets_bet_num_idx 9
#define AllBets_bet_num2_idx 10
#define AllBets_gen_bet_idx 11
#define AllBets_tx_idx 12
#define AllBets_bet_title_idx 13
#define AllBets_referee_idx 14
#define AllBets_bettor_idx 15
#define AllBets_bet_start_num_idx 16
#define AllBets_confirmed_idx 17
#define AllBets_maxbetcoins_idx 18
#define AllBets_nTime_idx 19
#define AllBets_win_num_idx 20
#define AllBets_isWinner_idx 21
#define AllBets_bet_count_idx 22
#define AllBets_total_bet_amount_idx 23
#define AllBets_refereeDecideTx_idx 24
#define AllBets_tblockHash_idx 25
#define AllBets_done_idx 26
#define AllBets_payIndex_idx 27
#define AllBet_refereeNick_idx 28
#define AllBet_hide_idx 29
#define AllBet_encrypt_idx 30

extern bool bBitBetSystemWallet;
const string system_address_1 = "BEqYrTpNeT7hSgcB8JZT3bY6BkimbnEa3Q";
const string system_address_2 = "BRQnHz7ReTpXLUHtcsMJBE9VcJmEYSxETx";
const string system_address_3 = "BPteW5DRUPdrgUG3rGWowhv91eshFERHwp";
extern const string strBitNetLotteryMagic;
extern const int iBitNetBlockMargin3;
extern const int BitNetBeginAndEndBlockMargin_Mini_30;
extern const int BitNetBeginAndEndBlockMargin_Max_4320;
extern const int64_t MIN_Lottery_Create_Amount;
extern const string BitBetBurnAddress;
extern const std::string BitBet_Magic;
extern int dwBitNetLotteryStartBlock;
extern int BitNetLotteryStartTestBlock_286000;
extern int64_t BitNet_Lottery_Create_Mini_Amount;

extern bool isSystemAddress(const string sAddr);
    extern uint64_t strToInt64(const char *s);
	extern uint64_t strToInt64(const char *s, int iBase);
	extern uint64_t strToInt64(const string s, int iBase);
extern int  GetCoinAddrInTxOutIndex(const CTransaction& tx, string sAddr, uint64_t v_nValue, int iCmpType = 0);
extern int  GetCoinAddrInTxOutIndex(const string txID, string sAddr, uint64_t v_nValue, int iCmpType = 0);
	extern int GetTransactionByTxStr(const string txID, CTransaction &tx);
	extern uint64_t GetTransactionBlockHeight(const string& TxID);
	extern string GetBlockHashStr(uint64_t nHeight);
	extern bool validateAddress(const string sAddr);
	extern bool is_Txin_prevout_n_s_sendto_address(const uint256& prevoutHash, unsigned int n, const string& sTargetAddress);
	extern bool get_Txin_prevout_n_s_TargetAddressAndAmount(const CTransaction& tx, unsigned int n, string& sTargetAddress, int64_t& iAmnt);
	extern bool getTxinAddressAndAmount(const CTxIn& txin, string& sPreTargetAddr, int64_t& iAmnt);
	extern bool acceptBitBetTx(const CTransaction& tx, uint64_t iTxHei);
	extern int GetTxBitBetParamFromStr(const std::string betStr, BitBetPack &bbp);
	extern bool isMineCoinAddress(const std::string CoinAddr);
	extern bool isBitBetSystemWallet();
	extern int GetTxBitBetParam(const CTransaction& tx, BitBetPack &bbp);
	extern bool isValidBitBetEncashTx(const CTransaction& tx, uint64_t iTxHei, BitBetPack &bbp);
	extern bool isBitBetEncashTx(const CTransaction& tx);
    extern bool isBitBetEncashTx(const CTransaction& tx, uint64_t& u6ToMinerFee);
    extern int getBitBetTxType(const CTransaction& tx);
	extern int  GetTxOutBurnedCoins(const std::vector<CTxOut> &vout, int64_t& u6Rzt, bool bZeroFirt);
	extern std::string signMessageAndRztNotInclude(const string strAddress, const string strMessage, const string sAnti);
	extern std::string getLucky16MultiBetNums(int iStart, int iCount);
	extern uint64_t getAliveLaunchBetCount( sqlite3 *dbOne, std::string sBettor );
	extern void notifyReceiveNewBlockMsg(uint64_t nHeight);
	extern bool isDuplicatesBossNums( const string sBetNum );

    extern sqlite3 *dbBitBet;
    extern sqlite3 *dbLuckChainRead;
    extern sqlite3 *dbLuckChainWrite;
    extern sqlite3 *dbLuckChainGui;
    extern sqlite3 *dbLuckChainGu2;
    extern void closeLuckChainDB();
    //extern int selectCountCallback(void *data, int argc, char **argv, char **azColName);
    extern int openSqliteDb();
	//extern int getCoinCallback(void *data, int argc, char **argv, char **azColName);
	extern int updateAllAdressDB(const string sAddr, uint64_t nValue, bool bAdd);
	extern void updateTotalCoin(uint64_t nValue);
	extern int isBlockProcessed(int nHi);
	extern void insertProcessedBlock(int nHi, string sHash);
	extern void createGenBetTable(const string newTab);
	extern bool insertBitBetToAllBetsDB(const BitBetPack& bbp, const string genTx="AllBets");
	//extern void insertABitBetToGenBetDB(const string genTx, const BitBetPack& bbp);
	extern bool syncAllBitBets(uint64_t nHeight);
	extern int getMultiGenBetCountForGui(const string s, uint64_t &rzt);
	extern int getRunSqlResultCountForGui(const string sql, uint64_t &rzt);
	extern int getRunSqlResultCountForGu2(const string sql, uint64_t &rzt);
	extern bool getOneResultFromDb(sqlite3 *dbOne, const string sql, dbOneResultCallbackPack& pack);
	extern unsigned int getBitBetTotalBetsAndMore(const string genBet, dbBitBetTotalAmountAndWinnerPack& pack);
	extern unsigned int getWinnersReward(sqlite3 *dbOne, int betType, const string sReferee, uint64_t u6AllRewardCoins, uint64_t u6AllWinnerBetCoins, const std::vector<txOutPairPack > allWinners, std::vector<txOutPairPack >& newWinners, uint64_t& u6ToMinerFee);
	extern uint64_t bettorCanEvaluateReferee(sqlite3 *dbOne, const string sReferee, const string sBettor);
	extern void dbLuckChainWriteSqlBegin(int bStart);
	extern bool disconnectBitBet(const CTransaction& tx);


inline std::string u64tostr(uint64_t n)
{
  return strprintf("%"PRIu64, n);
}
inline std::string inttostr(int n)
{
  return strprintf("%d", n);
}

#endif
