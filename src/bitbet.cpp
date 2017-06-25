// Copyright (c) 2012 The LuckChain developers
#include <algorithm>
#include <boost/algorithm/string/replace.hpp>
#include <string>
#include "bitbet.h"
#include <stdio.h>
#include <inttypes.h>
#include "wallet.h"
#include "walletdb.h"
#include "bitcoinrpc.h"
#include "init.h"
#include "base58.h"
#include "lz4/lz4.h"
#include "lzma/LzmaLib.h"
#include <fstream>
#include "db.h"
#include "txdb.h"

#ifdef QT_GUI
    #include "sqlite3/sqlite3.h"
#else
    #ifdef WIN32
        #include "sqlite3/sqlite3.h"
    #else
        #include <sqlite3.h>
    #endif
#endif

#include "coincontrol.h"
#include <iostream>
#include <iterator>
#include <vector>

using namespace json_spirit;
using namespace std;
using namespace boost;

#define BLOCK_NO_12W 120000
const std::string BitBet_Magic("BitBet:");
const std::string BitBet_CMD_Magic("BitBetCMD:");
const string BitBetBurnAddress("B4T5ciTCkWauSqVAcVKy88ofjcSasUkSYU");
const string strBitNetLotteryMagic = "BitLottery:";
const int BitBetBeginEndBlockSpace_10 = 10;
const int64_t BitBet_Mini_Amount = 10;  //MIN_TXOUT_AMOUNT;   // 100 * COIN
const int64_t  Balanced_Mining_Amount = 5000000 * COIN;
const int iBitNetBlockMargin3 = 3;
const int BitNetBeginAndEndBlockMargin_Mini_30 = 10;
const int BitNetBeginAndEndBlockMargin_Max_4320 = 4320;
int dwBitNetLotteryStartBlock = 320;
int BitNetLotteryStartTestBlock_286000 = 123;
int64_t BitNet_Lottery_Create_Mini_Amount = 10 * COIN;   //MIN_TXOUT_AMOUNT;
int64_t MIN_Lottery_Create_Amount = 10 * COIN;   //MIN_TXOUT_AMOUNT;
bool bBitBetSystemWallet = false, bLuckChainRollbacking=false, bSystemNodeWallet=false;
int iRecordPlayerInfo = 0;   // 2016.11.23 add
CCriticalSection cs_bitbet;  // 2017.04.30 add
CCriticalSection cs_queue_mining;  // 2017.05.30 add
int nLockQueueNodeCoinTime = 60 * 24;  // 24 hours
extern string s_Current_Dir;

extern bool verifyMessage(const string strAddress, const string strSign, const string strMessage);
extern string signMessage(const string strAddress, const string strMessage);
bool isSendCoinFromBurnAddr(const CTransaction& tx);
bool processBitBetCmdTx(const CTransaction& tx, uint64_t iTxHei);
bool insertBitBetEvaluate(const BitBetCommand& bbc);
bool updateBitBetRefereeEvaluate(const string sReferee, const string sEva);
bool getOneResultFromDb(sqlite3 *dbOne, const string sql, dbOneResultCallbackPack& pack);
int GetTxBitBetParam(const CTransaction& tx, BitBetPack &bbp);
int deleteBitBetTx(const string tx);
int  GetTxOutDetails(const std::vector<CTxOut> &vout, std::vector<txOutPairPack > &outPair);
static int synLuckyBossCallback(void *data, int argc, char **argv, char **azColName);
bool isDuplicatesOXNums( const string sBetNum );
string explainOXNums(int iBetLen, const string sBetNum, const string sBlkHash);

boost::signals2::signal<void (uint64_t nBlockNum, uint64_t nTime)> NotifyReceiveNewBlockMsg;
boost::signals2::signal<void (const BitBetPack& bbp)> NotifyReceiveNewBitBetMsg;
boost::signals2::signal<void (int opc, const std::string nickName, const std::string coinAddr, const std::string sFee, const std::string maxBetCoin)> NotifyaddLuckChainRefereeMsg5Param;
boost::signals2::signal<void (int opcode, const std::string sTx)> NotifyQueueNodeMsg;

std::string GetNewCoinAddress(const string strAccount)
{
    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    // Generate a new key that is added to wallet
    CPubKey newKey;
    if (!pwalletMain->GetKeyFromPool(newKey, false)) return "";   //throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    CKeyID keyID = newKey.GetID();
    if( strAccount.length() > 0 ){ pwalletMain->SetAddressBookName(keyID, strAccount); }
    return CBitcoinAddress(keyID).ToString();
}

bool isSystemAddress(const string sAddr)
{
   return (sAddr == system_address_1) || (sAddr == system_address_2) || (sAddr == system_address_3);
}
bool isMineCoinAddress(const std::string CoinAddr)
{
	bool rzt = false;
	CBitcoinAddress address(CoinAddr);
	if( address.IsValid() )
	{
		CTxDestination dest = address.Get();
		rzt = IsMine(*pwalletMain, dest);
	}	
	return rzt;
}

bool isBitBetSystemWallet()
{
	bool rzt = isMineCoinAddress(system_address_1) || isMineCoinAddress(system_address_2) || isMineCoinAddress(system_address_3); 
	return rzt;
}

std::string signMessageAndRztNotInclude(const string strAddress, const string strMessage, const string sAnti)
{
	string rzt = signMessage(strAddress, strMessage);
	while( rzt.find(sAnti) != string::npos ){ rzt = signMessage(strAddress, strMessage); }
	return rzt;
}

int strToInt(const char *s, int iBase=10)
{
   return strtol(s, NULL, iBase);	
}
int strToInt(const string s, int iBase=10)
{
   return strtol(s.c_str(), NULL, iBase);
}

uint64_t strToInt64(const char *s, int iBase)
{
   return strtoll(s, NULL, iBase);
}
uint64_t strToInt64(const string s, int iBase)
{
   return strtoll(s.c_str(), NULL, iBase);
}
uint64_t strToInt64(const char *s)
{
  uint64_t i=0;      char c ;
  if( !s ){ return i; }
  int scanned = sscanf(s, "%" SCNu64 "%c", &i, &c);
  if (scanned == 1){ return i; }
  if (scanned > 1) { return i; }  // TBD about extra data found
  return 0;  // TBD failed to scan;  
}

struct BitBetBossPack{
   int b1, b2, b3, b4, b5, b6;
   int paramCount;
};
int getBetBossNums(const std::string betStr, BitBetBossPack& bbbp)
{
	int rzt = 0;
	string stxData = "";
	int iLen = betStr.length();   if( iLen > MAX_BitBet_Str_Param_Len ){ return rzt; }
	if( betStr.length() > 0 ){ stxData = betStr.c_str(); }
	if( stxData.length() > 0 )
	{
		int i = 0;
		try{
		char *delim = ",";
					
		char * pp = (char *)stxData.c_str();
        char *reserve;
		char *pch = strtok_r(pp, delim, &reserve);
		while (pch != NULL)
		{
			i++;
			if( i == 1 ){ bbbp.b1 = atoi(pch); }
			else if( i == 2 ){ bbbp.b2 = atoi(pch); }
			else if( i == 3 ){ bbbp.b3 = atoi(pch); }
			else if( i == 4 ){ bbbp.b4 = atoi(pch); }
			else if( i == 5 ){ bbbp.b5 = atoi(pch); }
			else if( i == 6 ){ bbbp.b6 = atoi(pch);   break; }
			//if( i >= betLen ){ break; }
			pch = strtok_r(NULL, delim, &reserve);
		}
		}catch (std::exception &e) {
			printf("getBetBossNums:: err [%s]\n", e.what());
		}
		rzt = i;
	}
	bbbp.paramCount = rzt;    return rzt;
}

int GetTransactionByTxStr(const string txID, CTransaction &tx, uint256 &hashBlock)
{
	int rzt = 0;
	if( txID.length() > 34 )
	{
		uint256 hash;
		hash.SetHex(txID);
		hashBlock = 0;
		if (!GetTransaction(hash, tx, hashBlock))
			return rzt;
		if( hashBlock > 0 ){ rzt++; }
	}
	return rzt;
}

int GetTransactionByTxStr(const string txID, CTransaction &tx)
{
	uint256 hashBlock;
	return GetTransactionByTxStr(txID, tx, hashBlock);
}

string GetBlockHashStr(int64_t nHeight)
{
    string rzt = "";
	if (nHeight < 0 || nHeight > nBestHeight)
	{
		if( fDebug ){ printf("GetBlockHashStr() failed, [%s] < 0 or > [%s] \n", i64tostr(nHeight).c_str(), i64tostr(nBestHeight).c_str()); }
		return rzt;
	}
    try{
        CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
	    if( pblockindex != NULL ){  return pblockindex->phashBlock->GetHex();  }
		else if( fDebug ){ printf("GetBlockHashStr(%s) failed \n", i64tostr(nHeight).c_str()); }
	}catch (std::exception &e) {
		printf("GetBlockHashStr:: err [%s] \n", e.what());
	} catch (...) {
        //PrintExceptionContinue(NULL, "GetBlockHashStr()");
    }
	return rzt;
}

uint64_t getTxBlockHeightBy_hashBlock( const uint256 hashBlock )
{
   uint64_t rzt = 0;
   if( hashBlock > 0 )
   {
      map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
      if (mi != mapBlockIndex.end() && (*mi).second)
      {
         CBlockIndex* pindex = (*mi).second;
         if( pindex->IsInMainChain() ){ rzt = pindex->nHeight; }
      }
   }
   return rzt;
}

uint64_t GetTransactionBlockHeight(const string& TxID)
{
    uint64_t rzt = 0;
	if( TxID.length() < 34 ){ return rzt; }
	
    uint256 hash;
    hash.SetHex(TxID);	//params[0].get_str());
	
        CTransaction tx;
        uint256 hashBlock = 0;
        if( GetTransaction(hash, tx, hashBlock) )
        {
		   rzt = getTxBlockHeightBy_hashBlock( hashBlock );
            /*if( hashBlock > 0 )
            {
                map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
                if (mi != mapBlockIndex.end() && (*mi).second)
                {
                    CBlockIndex* pindex = (*mi).second;
                    if( pindex->IsInMainChain() ){ rzt = pindex->nHeight; }
                        //entry.push_back(Pair("confirmations", 1 + nBestHeight - pindex->nHeight));
                }
            }*/
        }

    return rzt;
}

bool validateAddress(const string sAddr)
{
	bool rzt = false;
	CBitcoinAddress address(sAddr);
	rzt = address.IsValid();
	return rzt;
}
//string getTxin_prevout_n_s_sendto_address(const CTransaction& tx, unsigned int n)
bool is_Txin_prevout_n_s_sendto_address(const uint256& prevoutHash, unsigned int n, const string& sTargetAddress)
{
	bool rzt = false;
	bool bValid = validateAddress(sTargetAddress);
	string txID = prevoutHash.GetHex();
	if( fDebug ){ printf("is_Txin_prevout_n_s_sendto_address: Tag Address [%s] Valid = [%u], n = [%u], tx id = [%s] \n", sTargetAddress.c_str(), bValid, n, txID.c_str()); }
	if( !bValid ){ return rzt; }
	
	CTransaction tx;
	if( GetTransactionByTxStr(txID, tx) > 0 )
	{	
		if( fDebug ){ printf("is_Txin_prevout_n_s_sendto_address: tx.vout.size() = [%u] : [%u] \n", tx.vout.size(), n); }
		if( tx.vout.size() > n )
		{
			const CTxOut& txout = tx.vout[n];
			txnouttype type;
			vector<CTxDestination> addresses;
			int nRequired;

			if( ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired) )
			{
				BOOST_FOREACH(const CTxDestination& addr, addresses)
				{
					//rzt = strprintf("%s|%s", rzt.c_str(), CBitcoinAddress(addr).ToString().c_str());
					string sAa = CBitcoinAddress(addr).ToString();
					if( fDebug ){ printf("is_Txin_prevout_n_s_sendto_address: txout[%u].scriptPubKey's address = [%s] \n", n, sAa.c_str()); }
					if( sAa == sTargetAddress )
					{
						if( fDebug ){ printf("is_Txin_prevout_n_s_sendto_address: Yes :) \n"); }
						return true;
					}
				}
			}			
		}
	}
	return rzt;
} //is_Txin_prevout_n_s_sendto_address

//bool get_Txin_prevout_n_s_TargetAddressAndAmount(const uint256& prevoutHash, unsigned int n, string& sTargetAddress, int64_t& iAmnt)
bool get_Txin_prevout_n_s_TargetAddressAndAmount(const CTransaction& tx, unsigned int n, string& sTargetAddress, int64_t& iAmnt)
{
	bool rzt = false;
	sTargetAddress = "";  iAmnt = 0;
	string txID = tx.GetHash().ToString();  //prevoutHash.GetHex();
	//if( fDebug ){ printf("get_Txin_prevout_n_s_TargetAddressAndAmount: n = [%d], tx id = [%s] \n", n, txID.c_str()); }
	
	//CTransaction tx;
	//if( GetTransactionByTxStr(txID, tx) > 0 )
	{	
		//if( fDebug ){ printf("get_Txin_prevout_n_s_TargetAddressAndAmount: tx.vout.size() = [%u] : [%u] \n", tx.vout.size(), n); }
		if( tx.vout.size() > n )
		{
			const CTxOut& txout = tx.vout[n];
			txnouttype type;
			vector<CTxDestination> addresses;
			int nRequired;

			if( ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired) )
			{
				BOOST_FOREACH(const CTxDestination& addr, addresses)
				{
					//rzt = strprintf("%s|%s", rzt.c_str(), CBitcoinAddress(addr).ToString().c_str());
					string sAa = CBitcoinAddress(addr).ToString();
					//if( fDebug ){ printf("get_Txin_prevout_n_s_TargetAddressAndAmount: txout[%u].scriptPubKey's address = [%s] \n", n, sAa.c_str()); }
					if( sAa.length() > 30 )
					{
						sTargetAddress = sAa.c_str();   iAmnt = txout.nValue;
						if( fDebug ){ printf("get_Txin_prevout_n_s_TargetAddressAndAmount: [%s] [%I64u] :) \n", sTargetAddress.c_str(), iAmnt); }
						return true;
					}
				}
			}			
		}
	}
	return rzt;
}  //get_Txin_prevout_n_s_TargetAddressAndAmount

bool getTxinAddressAndAmount(const CTxIn& txin, string& sPreTargetAddr, int64_t& iAmnt)
{
	bool rzt = false;
	if( txin.prevout.IsNull() ){ return rzt; }

    uint256 hashBlock = 0;
    CTransaction txPrev;
    //if( fDebug ){ printf( "getTxinAddressAndAmount: txin.prevout.n = [%u], \n", txin.prevout.n ); }
	if( GetTransaction(txin.prevout.hash, txPrev, hashBlock) )	// get the vin's previous transaction
	{
		sPreTargetAddr = "";
		iAmnt = 0;
		rzt = get_Txin_prevout_n_s_TargetAddressAndAmount(txPrev, txin.prevout.n, sPreTargetAddr, iAmnt);
	}
	return rzt;
}


sqlite3 *dbAllAddress = NULL;
sqlite3 *dbBitBet = NULL;
sqlite3 *dbLuckChainRead = NULL;
sqlite3 *dbLuckChainRead2 = NULL;
sqlite3 *dbLuckChainWrite = NULL;
sqlite3 *dbLuckChainGui = NULL;
sqlite3 *dbLuckChainGu2 = NULL;
sqlite3 *dbQueueMining = NULL;

void closeLuckChainDB()
{
    if( dbAllAddress ) sqlite3_close(dbAllAddress);
    if( dbBitBet ) sqlite3_close(dbBitBet);
    if( dbLuckChainRead ) sqlite3_close(dbLuckChainRead);
    if( dbLuckChainRead2 ) sqlite3_close(dbLuckChainRead2);
    if( dbLuckChainWrite ) sqlite3_close(dbLuckChainWrite);
    if( dbLuckChainGui ) sqlite3_close(dbLuckChainGui);
	if( dbLuckChainGu2 ) sqlite3_close(dbLuckChainGu2);
	if( dbQueueMining ) sqlite3_close(dbQueueMining);
}

static int selectCallback(void *data, int argc, char **argv, char **azColName)
{
   //fprintf(stderr, "%s: ", (const char*)data);
   //(int *)data^ = 
   int i;
   for(i=0; i<argc; i++){
      printf("%s :: %s = %s\n", (const char*)data, azColName[i], argv[i] ? argv[i] : "NULL");   // Callback function called :: Count(*) = 10
   }
   //printf("\n");
   return 0;
}

static int sqliteCallback(void *NotUsed, int argc, char **argv, char **azColName){
   int i;
   for(i=0; i<argc; i++){
      printf("sqlite callback %s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   printf("\n");
   return 0;
}

static int selectCountCallback(void *data, int argc, char **argv, char **azColName)
{
   //fprintf(stderr, "%s: ", (const char*)data);
   if( (data!=NULL) && (argc > 0) )
   {
      *(uint64_t *)data = atoi64(argv[0]);  //strToInt64
   }
   return 0;
}

std::string getDbNameByPtr(void *ptr)
{
	string rzt=" ";
	if( ptr == dbLuckChainRead ){ rzt = "dbLuckChainRead"; }
	else if( ptr == dbLuckChainRead2 ){ rzt = "dbLuckChainRead2"; }
	else if( ptr == dbLuckChainWrite ){ rzt = "dbLuckChainWrite"; }
	else if( ptr == dbLuckChainGui ){ rzt = "dbLuckChainGui"; }
	else if( ptr == dbLuckChainGu2 ){ rzt = "dbLuckChainGu2"; }
	else if( ptr == dbQueueMining ){ rzt = "dbQueueMining"; }
	return rzt;
}
int dbBusyCallback(void *ptr, int count)    //  Database connection,      Number of times table has been busy
{    
    MilliSleep(234);  // wait 0.5 s   //sqlite3 *db = (sqlite3 *)ptr;   
    if( fDebug ){ string s = getDbNameByPtr(ptr);   printf("[%s] is locking [%d] now, can not write/read.\n", s.c_str(), count); }
	if( (ptr==dbLuckChainGui) || (ptr==dbLuckChainGu2) ){  MilliSleep(235);  }
	if( count > 3 ){
		if( ptr == dbLuckChainGu2 ){ return 0; }
	}
	else if( count > 5 ){ if( ptr == dbLuckChainGui ){ return 0; } }
    return 1;    // must return 1
}

/*void createGenBetTable(const string newTab)
{
   //****** Create table, table name can't start with digit number ******
   string sql = "Create TABLE " + newTab + "([id] integer PRIMARY KEY AUTOINCREMENT"
                     ",[opcode] int"
                     ",[bet_type] int NOT NULL"
                     ",[bet_amount] bigint"
                     ",[min_amount] bigint"
                     ",[sblock] bigint DEFAULT 0"
                     ",[blockspace] bigint DEFAULT 0"
                     ",[tblock] bigint DEFAULT 0"
                     ",[bet_len] int"
                     ",[bet_num] varchar"
                     ",[bet_num2] varchar"
                     ",[gen_bet] varchar(64)"
                     ",[tx] varchar(64)"
                     ",[bet_title] varchar"
                     ",[referee] varchar"
                     ",[bettor] varchar(34)"
                     ",[confirmed] int DEFAULT 0"
                     ",[maxbetcoins] bigint DEFAULT 0"
                     ",[nTime] bigint"
                     ",[win_num] varchar"
                     ",[bet_count] bigint DEFAULT 1"
                     ",[total_bet_amount] bigint DEFAULT 0"
                     ",[refereeDecideTx] varchar"
                     ",[done] int DEFAULT 0"
                     ");";
   char* zErrMsg = 0;
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &zErrMsg);
   printf("createGenBetTable:: [%s] \n [%d] [%s]\n", sql.c_str(), rc, zErrMsg);
   if( zErrMsg ){ sqlite3_free(zErrMsg); }
}*/

void createAllBetsIndex()
{
   //-- 2016.10.27 add index for speed
   string sql = "create index idxOpcDone on AllBets(opcode, done);";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
   sql = "create index idxBetType on AllBets(bet_type);";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
   sql = "create index idxGenBet on AllBets(gen_bet);";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
   sql = "create index idxTx on AllBets(tx);";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
   sql = "create index idxBettor on AllBets(bettor);";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
   sql = "create index idxCoinAddr on Users(coinAddr);";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
}

string buildCreatAllBetsTableStr(string sName)
{
   string sql = "Create TABLE " + sName +"([id] integer PRIMARY KEY AUTOINCREMENT"
					",[opcode] int"
					",[bet_type] int NOT NULL"
					",[bet_amount] bigint"
					",[min_amount] bigint"
					",[sblock] bigint"
					",[blockspace] bigint"
					",[tblock] bigint"
					",[bet_len] int"
					",[bet_num] varchar COLLATE NOCASE"
					",[bet_num2] bigint"
					",[gen_bet] varchar(64)"
					",[tx] varchar(64) UNIQUE NOT NULL"
					",[bet_title] varchar"
					",[referee] varchar"
					",[bettor] varchar(34) NOT NULL"
					",[bet_start_num] int"
					",[confirmed] int"
					",[maxbetcoins] bigint"
					",[nTime] bigint"
					",[win_num] varchar"
					",[isWinner] int DEFAULT 0"
					",[bet_count] bigint DEFAULT 1"
					",[total_bet_amount] bigint DEFAULT 0"
					",[refereeDecideTx] varchar(64) DEFAULT z"
					",[tblockHash] varchar(64) DEFAULT z"
					",[done] int DEFAULT 0"
					",[payIndex] integer"
					",[refereeNick] varchar"
					",[hide] int DEFAULT 0"
					",[encrypt] int DEFAULT 0"
					",[encashTx] varchar(64)"
					",[rounds] bigint DEFAULT 0"
					",[new_bet_count] bigint DEFAULT 0"
					",[max_bet_count] integer DEFAULT 0"
					",[one_addr_once] int DEFAULT 0"
					",[est_bet_count] int DEFAULT 0"
					",[firstOfThisAddr] int DEFAULT 0, [totalOfThisAddrBetCount] int DEFAULT 0, [totalOfThisAddrCoins] bigint DEFAULT 0"
					",[winCoins] bigint DEFAULT 0, [winCount] int DEFAULT 0"
					",[oneAddrMaxBetAmount] bigint DEFAULT 0, [enCashFlag] int DEFAULT 0, [uniqueNumber] int DEFAULT 0"
                     ");";	
	return sql;
}
void buildNodesDb()
{
   string sql = "Create TABLE Nodes ([id] integer PRIMARY KEY AUTOINCREMENT"
					",[nick] varchar(32) UNIQUE NOT NULL"
					",[inblock] bigint DEFAULT 0"
					",[coinaddr] varchar(34) UNIQUE NOT NULL"
					",[tx] varchar(64) UNIQUE NOT NULL"
					",[payid] int DEFAULT 0"
					",[gotblks] bigint DEFAULT 0"
					",[lost] bigint DEFAULT 0"
					",[clrlost] bigint DEFAULT 0"
					",[lockdays] int DEFAULT 0"
					",[unlockblk] bigint DEFAULT 0"
					",[lastblktm] bigint DEFAULT 0"
					",[confirm] int DEFAULT 0"
					",[regtm] bigint DEFAULT 0);";
   char* pe = 0;
   int rc = sqlite3_exec(dbQueueMining, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildNodesDb :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }
}
void buildLuckChainDb()
{
   string sql = buildCreatAllBetsTableStr("AllBets");  /* "Create TABLE AllBets([id] integer PRIMARY KEY AUTOINCREMENT"
					",[opcode] int"
					",[bet_type] int NOT NULL"
					",[bet_amount] bigint"
					",[min_amount] bigint"
					",[sblock] bigint"
					",[blockspace] bigint"
					",[tblock] bigint"
					",[bet_len] int"
					",[bet_num] varchar COLLATE NOCASE"
					",[bet_num2] bigint"
					",[gen_bet] varchar(64)"
					",[tx] varchar(64) UNIQUE NOT NULL"
					",[bet_title] varchar"
					",[referee] varchar"
					",[bettor] varchar(34) NOT NULL"
					",[bet_start_num] int"
					",[confirmed] int"
					",[maxbetcoins] bigint"
					",[nTime] bigint"
					",[win_num] varchar"
					",[isWinner] int DEFAULT 0"
					",[bet_count] bigint DEFAULT 1"
					",[total_bet_amount] bigint DEFAULT 0"
					",[refereeDecideTx] varchar(64) DEFAULT z"
					",[tblockHash] varchar(64) DEFAULT z"
					",[done] int DEFAULT 0"
					",[payIndex] integer"
					",[refereeNick] varchar"
					",[hide] int DEFAULT 0"
					",[encrypt] int DEFAULT 0"
					",[encashTx] varchar(64)"
					",[rounds] bigint DEFAULT 0"
					",[new_bet_count] bigint DEFAULT 0"
					",[max_bet_count] integer DEFAULT 0"
					",[one_addr_once] int DEFAULT 0"
					",[est_bet_count] int DEFAULT 0"
					",[firstOfThisAddr] int DEFAULT 0, [totalOfThisAddrBetCount] int DEFAULT 0, [totalOfThisAddrCoins] bigint DEFAULT 0"
					",[winCoins] bigint DEFAULT 0, [winCount] int DEFAULT 0"
					",[oneAddrMaxBetAmount] bigint DEFAULT 0, [enCashFlag] int DEFAULT 0, [uniqueNumber] int DEFAULT 0"
                     ");"; */
   char* pe = 0;
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: AllBets :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }
   createAllBetsIndex();   //-- 2016.10.27 add index for speed
   
   sql = buildCreatAllBetsTableStr("AllBetsTmp");
   rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: AllBetsTmp :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }   


   sql = "Create TABLE Addresss([id] integer PRIMARY KEY AUTOINCREMENT"
			",[addr] varchar(34) UNIQUE NOT NULL"
			",[coin] bigint DEFAULT 0"
            ");";
   pe = 0;      rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: Addresss :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }


   sql = "Create TABLE Comment([id] integer PRIMARY KEY AUTOINCREMENT"
			",[user] varchar(34)"
			",[target] varchar(34)"
			",[content] varchar"
			",[ntime] bigint DEFAULT 0"
			",[sign] varchar"
            ");";
   pe = 0;      rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: Comment :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }

   
   sql = "Create TABLE Identity([id] integer PRIMARY KEY AUTOINCREMENT"
			",[nickName] varchar UNIQUE"
			",[coinAddr] varchar(34) UNIQUE"
			",[local] varchar(100)"
			",[sign] varchar(100) UNIQUE"
            ");";
   pe = 0;
   rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: Identity :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }

   
   sql = "Create TABLE Referees([id] integer PRIMARY KEY AUTOINCREMENT"
			",[nickName] varchar UNIQUE NOT NULL"
			",[coinAddress] varchar(34) UNIQUE NOT NULL"
			",[local] varchar"
			",[fee] varchar"
			",[maxcoins] bigint"
			",[remark] varchar"
			",[good] bigint DEFAULT 0"
			",[bad] bigint DEFAULT 0"
			",[unlockTime] bigint DEFAULT 0"
			",[power] integer DEFAULT 1"
			",[decideCount] bigint DEFAULT 0"
			",[takeFees] bigint DEFAULT 0"
			",[msgForSign] varchar"
			",[systemSign] varchar"
			",[systemAddr] varchar(34) NOT NULL"
			",[valid] int DEFAULT 1"
			",[nTime] bigint"
            ");";
   pe = 0;
   rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: Referees :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }

   
   sql = "Create TABLE Settings([Address_for_referee] varchar(34)"  //sql = "Create TABLE Settings([Address_for_referee] varchar(34) DEFAULT BQFZPLSdySUxpMTAdpF5uhXVKU9vQLxKtx"
			",[db_ver] varchar, [best_blknum] bigint);";
   rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
//sql = "insert Settings set Address_for_referee='BQFZPLSdySUxpMTAdpF5uhXVKU9vQLxKtx', db_ver='1.1012';";
   sql = "INSERT INTO Settings (Address_for_referee, db_ver, best_blknum) VALUES ('BQFZPLSdySUxpMTAdpF5uhXVKU9vQLxKtx', '1.1211', 0);"; 
   pe = 0;      rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: Settings :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }

   
   sql = "Create TABLE SpentBets([id] integer PRIMARY KEY AUTOINCREMENT"
			",[tx] varchar(64) UNIQUE NOT NULL"
			",[idx] integer DEFAULT 0"
            ");";
   pe = 0;      rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: SpentBets :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }
   
   sql = "Create TABLE SpentSign([id] integer PRIMARY KEY AUTOINCREMENT"
			",[sign] varchar UNIQUE NOT NULL);";
   pe = 0;      rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: SpentSign :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }

   
   sql = "Create TABLE total([totalcoin] bigint DEFAULT 0"
			",[totaladdr] bigint DEFAULT 0"
			",[totaltx] bigint DEFAULT 0"
			",[totalburn] bigint DEFAULT 0"
            ");";
   pe = 0;      rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: total :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }
   
   sql = "Create TABLE Users([id] integer PRIMARY KEY AUTOINCREMENT"   // 2016.11.23 add
			",[coinAddr] varchar(34) UNIQUE NOT NULL"
			",[nickName] varchar(32)"
			",[totalBetCoins] bigint DEFAULT 0"
			",[totalWinCoins] bigint DEFAULT 0"
			",[totalBetCount] bigint DEFAULT 0"
			",[totalWinCount] bigint DEFAULT 0, [nTime] bigint DEFAULT 0, [sign] varchar, [weight] integer default 3"
			",[flag] integer default 0, [remarks] varchar default '-'"
            ");";
   pe = 0;
   rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("buildLuckChainDb :: Users :: [%s] \n [%d] [%s]\n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe);    pe=0; }
}

bool isTableExists(const string sTab, sqlite3 *oneDb)
{
   bool rzt = false;
   string sql = "select count(*)  from sqlite_master where type='table' and name like '%" + sTab + "%';";
   int64_t icnt = 0;   //char* pe = 0;
   int rc = sqlite3_exec(oneDb, sql.c_str(), selectCountCallback, (void*)&icnt, NULL);
   rzt = icnt  > 0;
   if( fDebug ){ printf("isTableExists:: rzt=[%d], rc=[%d], [%s] \n", rzt, rc, sql.c_str()); }
   //if( pe ){ sqlite3_free(pe); }
   return rzt;
}

int64_t i6BackupLuckChainDb_BlkNum=0;                                                        // 2017.04.12 add
string sBackupLuckChainDbFn="", sDataDbDir="", sLuckChainDbFn="";      // 2017.04.12 add
bool ReadOrWriteLuckChainBackupInfoToDB(bool bRead)
{
	bool rzt=false;   CTxDB txdb;
	if( bRead ){ rzt = txdb.ReadLuckChainBackupInfo(i6BackupLuckChainDb_BlkNum); }
	else{ rzt = txdb.WriteLuckChainBackupInfo(i6BackupLuckChainDb_BlkNum); }
	return rzt;
}

const string BitBetBurnAddressTest("mkBRk7uf2EfEBFweiij3Gf9NqEBqT7k8Tb");
int openSqliteDb()
{
   if( fTestNet )
   {
		char* p = (char*)BitBetBurnAddress.c_str();      memcpy(p, BitBetBurnAddressTest.c_str(), BitBetBurnAddress.length());   //BitNetLotteryStartTestBlock_286000 = 99;
	}
   int  rc;
   //char *sql;
   int rcSafe = sqlite3_threadsafe();

#ifdef QT_GUI
    iRecordPlayerInfo = GetArg("-recordplayerinfo", 1);
#else
    iRecordPlayerInfo = GetArg("-recordplayerinfo", 0);
#endif
    sDataDbDir = GetDataDir().string();
#ifdef WIN32
   string sLuckChainDb = sDataDbDir + "\\luckchain.db",  sAllAddressDb = GetDataDir().string() + "\\alladdress.db", sNodesDb="nodes.db";
   std::replace( sLuckChainDb.begin(), sLuckChainDb.end(), '\\', '\x2f'); // replace all '\' to '/'
#else
   string sLuckChainDb = sDataDbDir + "/luckchain.db",  sAllAddressDb = sDataDbDir + "/alladdress.db", sNodesDb = sDataDbDir + "/nodes.db";
#endif

    if( ReadOrWriteLuckChainBackupInfoToDB(true) )   // 2017.04.12 add
	{
		sBackupLuckChainDbFn = strprintf("%s/%s.db", sDataDbDir.c_str(), i64tostr(i6BackupLuckChainDb_BlkNum).c_str());
	}

    const char* pLuckChainDb = sLuckChainDb.c_str();      string sLuckChainDbInAppDir = "luckchain.db";   	filesystem::path pathLuckChainDbOrg = sLuckChainDbInAppDir;
	if( filesystem::exists(pathLuckChainDbOrg) )
	{
		pLuckChainDb = sLuckChainDbInAppDir.c_str();
	}else{
		filesystem::path pathLuckChainDb = sLuckChainDb;
		if( !filesystem::exists(pathLuckChainDb) )
		{
			if( GetArg("-defdbinappdir", 1) )
			{
				pLuckChainDb = sLuckChainDbInAppDir.c_str();
			}else{
				pathLuckChainDb = sLuckChainDbInAppDir;   //sLuckChainDb = "luckchain.db";
				if( filesystem::exists(pathLuckChainDb) ){ pLuckChainDb = sLuckChainDbInAppDir.c_str(); }
			}
		}
	}
	sLuckChainDbFn = pLuckChainDb;

//if( fDebug ){ printf("---> openSqliteDb, thread safe = [%d] [%s], bAllBetsExist=[%d]  \n", rcSafe, pLuckChainDb, bAllBetsExist); }
   /* Open database */
    rc = sqlite3_open(sNodesDb.c_str(), &dbQueueMining);
	bool bAllBetsExist = isTableExists("Nodes", dbQueueMining);
	if( !bAllBetsExist ){ buildNodesDb(); }

    rc = sqlite3_open(pLuckChainDb, &dbLuckChainWrite);
	bAllBetsExist = isTableExists("AllBets", dbLuckChainWrite);
	if( !bAllBetsExist )
	{
		buildLuckChainDb();
	}
    if( fDebug ){ printf("---> openSqliteDb, thread safe = [%d] [%s], bAllBetsExist=[%d]  BitBetBurnAddress=[%s] \n", rcSafe, pLuckChainDb, bAllBetsExist, BitBetBurnAddress.c_str()); }
	if( fDebug ){ printf("i6BackupLuckChainDb_BlkNum=[%s], sBackupLuckChainDbFn=[%s] \n", i64tostr(i6BackupLuckChainDb_BlkNum).c_str(), sBackupLuckChainDbFn.c_str()); }

   string sql = "SELECT * from Settings;";
   dbOneResultCallbackPack pack = {OneResultPack_STR_TYPE, 1, 0, 0, "db_ver", ""};
   getOneResultFromDb(dbLuckChainWrite, sql, pack);
   float f = atof( pack.sRzt.c_str() ), f2 = 1.101200, f3 = 1.102200;
   //if( fDebug ){ printf("db_ver [%f :: %f], [%s] \n", f, f2, pack.sRzt.c_str()); }
   if( f < f2 )
   {
		if( fDebug ){ printf("db_ver[%f] < 1.1012, upgrade \n", f); }
		sql = "alter table AllBets add hide int default 0;";               sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update AllBets set hide=0;";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update Settings set db_ver='1.1012';";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
   }
   if( f < f3 )
   {
		if( fDebug ){ printf("db_ver[%f] < 1.1022, upgrade \n", f); }
		sql = "alter table AllBets add encrypt int default 0;";               sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update AllBets set encrypt=0;";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update Settings set db_ver='1.1022';";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
   }

   f3 = 1.102700;   // 2016.10.27 add
   if( f < f3 )
   {
		if( fDebug ){ printf("db_ver[%f] < 1.1027, upgrade \n", f); }
		sql = "alter table AllBets add encashTx varchar(64);";               sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update AllBets set encashTx='-';";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update Settings set db_ver='1.1027';";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		createAllBetsIndex();   //-- 2016.10.27 add index for speed
		//sqlite3_close(dbLuckChainWrite);   sqlite3_open(pLuckChainDb, &dbLuckChainWrite);
   }
   
   f3 = 1.111200;   // 2016.11.12 add
   if( f < f3 )
   {
		if( fDebug ){ printf("db_ver[%f] < 1.1112, upgrade \n", f); }
		sql = "alter table AllBets add max_bet_count integer DEFAULT 0; alter table AllBets add one_addr_once int DEFAULT 0; alter table AllBets add est_bet_count int DEFAULT 0;";               sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update AllBets set max_bet_count=0, one_addr_once=0;";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update Settings set db_ver='1.1112';";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
   }

   f3 = 1.112900;   // 2016.11.29 add
   if( f < f3 )
   {	   
		if( fDebug ){ printf("db_ver[%f] < 1.1129, upgrade \n", f); }

		sql = "alter table AllBets add firstOfThisAddr int DEFAULT 0; alter table AllBets add totalOfThisAddrBetCount int DEFAULT 0; alter table AllBets add totalOfThisAddrCoins bigint DEFAULT 0;";
		if( sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL) == SQLITE_OK )
		{
			sql = "update AllBets set firstOfThisAddr=0, totalOfThisAddrBetCount=0, totalOfThisAddrCoins=0;";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
			printf("db_ver[%f] < 1.1129, set firstOfThisAddr... \n", f);
		}
		sql = "alter table AllBets add winCoins bigint DEFAULT 0; alter table AllBets add winCount int DEFAULT 0;";
		if( sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL) == SQLITE_OK )
		{
			sql = "update AllBets set winCoins=0, winCount=0;";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
			printf("db_ver[%f] < 1.1129, set winCoins, winCount ... \n", f);
		}

		sql = buildCreatAllBetsTableStr("AllBetsTmp");
		rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
		if( fDebug ){ printf("openSqliteDb :: AllBetsTmp :: rc=[%d] [%s] \n", rc, sql.c_str()); }

		sql = "alter table AllBets add oneAddrMaxBetAmount bigint DEFAULT 0; alter table AllBets add enCashFlag int DEFAULT 0; alter table AllBets add uniqueNumber int DEFAULT 0;";               sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update AllBets set oneAddrMaxBetAmount=0, enCashFlag=0, uniqueNumber=0;";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update Settings set db_ver='1.1129';";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
   }
   
   f3 = 1.120900;   // 2016.11.12 add
   if( f < f3 )
   {
		if( fDebug ){ printf("db_ver[%f] < 1.120900, upgrade \n", f); }
		sql = "alter table Users add weight integer default 3; alter table Users add flag integer default 0; alter table Users add remarks varchar default '-';";               sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update Users set weight=3, flag=0, remarks='-';";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update Settings set db_ver='1.1209';";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
   }

   f3 = 1.121100;   // 2017.04.11 add
   if( f < f3 )
   {
		if( fDebug ){ printf("db_ver[%f] < 1.121100, upgrade \n", f); }
		sql = "alter table Settings add best_blknum bigint default 0;";               sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
		sql = "update Settings set best_blknum=0, db_ver='1.1211';";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
   }

   
   sql = "update AllBets set est_bet_count=bet_count where done=0 and max_bet_count>1 and opcode=1 and (bet_type != 4);";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);   // 2016.11.12 end
   sql = "update AllBets set est_bet_count=new_bet_count where done=0 and max_bet_count>1 and opcode=1 and (bet_type = 4);";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);   // 2016.11.12 end

   rc = sqlite3_open(sAllAddressDb.c_str(), &dbAllAddress);  //World.db3  alladdress.db
   rc = sqlite3_open(pLuckChainDb, &dbBitBet);
   rc = sqlite3_open(pLuckChainDb, &dbLuckChainRead);
   rc = sqlite3_open(pLuckChainDb, &dbLuckChainRead2);
   rc = sqlite3_open(pLuckChainDb, &dbLuckChainGu2);
   rc = sqlite3_open(pLuckChainDb, &dbLuckChainGui);   //sqlite3_exec(dbLuckChainGui, "PRAGMA synchronous = OFF; ", 0,0,0);
   sqlite3_busy_handler(dbLuckChainRead, dbBusyCallback, (void *)dbLuckChainRead);
   sqlite3_busy_handler(dbLuckChainRead2, dbBusyCallback, (void *)dbLuckChainRead2);
   sqlite3_busy_handler(dbLuckChainWrite, dbBusyCallback, (void *)dbLuckChainWrite);
   sqlite3_busy_handler(dbLuckChainGui, dbBusyCallback, (void *)dbLuckChainGui);   sqlite3_busy_handler(dbLuckChainGu2, dbBusyCallback, (void *)dbLuckChainGu2);

if( fDebug ){ printf("<--- openSqliteDb \n"); }
   return 0;
}

static int getOneResultCallback(void *data, int argc, char **argv, char **azColName)
{
   if( (data != NULL) && (argc > 0) )
   {
      dbOneResultCallbackPack* p = (dbOneResultCallbackPack *)data;
	  if( p->sFieldName.length() > 0 )
	  {
			const char* fp = p->sFieldName.c_str();
			for(int i=0; i<argc; i++)
			{
				if( strcmp(azColName[i], fp) == 0 )
				{
					p->fID = i;   break;
					//if( fDebug ){ printf("getOneResultCallback:: ColName[%s], id = [%d], argc = [%d] \n", azColName[i], i, argc); }  break; 
				}
			}
	  }
	  if( (p->fID >= 0) && (argc > p->fID) )
	  {
		//char* pc = argv[p->fID];
		//if( fDebug ){ printf("getOneResultCallback:: fID = [%d], type = [%d] [%s]\n", p->fID, p->vType, pc); }
		try{
		  if( p->vType == OneResultPack_U64_TYPE )
		  {
			 char *ps = argv[p->fID];   if( ps != NULL ){ p->u6Rzt = strToInt64(ps); }
		  }
		  else if( p->vType == OneResultPack_STR_TYPE )
		  {
			char *ps = argv[p->fID];   if( ps != NULL ){ p->sRzt = ps; }
		  }
		  p->fDone = 1;   //*(int64_t *)data = atoi64(argv[2]);
		}catch (std::exception &e) {
			printf("getOneResultCallback:: err [%s]\n", e.what());
		}
	  }
   }
   return 0;
}

bool getOneResultFromDb(sqlite3 *dbOne, const string sql, dbOneResultCallbackPack& pack)
{
   int rc = sqlite3_exec(dbOne, sql.c_str(), getOneResultCallback, (void*)&pack, NULL);      bool rzt = (rc == SQLITE_OK);
   if( fDebug && (rc != SQLITE_OK) ){ printf("getOneResultFromDb err: [%d] \n", rc); }
   return rzt;
}

int updateAllAdressDB(const string sAddr, uint64_t nValue, bool bAdd)
{
   int rzt = 0;
   string sql = "SELECT Count(*) from Addresss where addr = '" + sAddr + "'";  //"SELECT * from Keys";
   if( fDebug ) printf("updateAllAdressDB:: bAdd=[%d], sql [%s] \n", bAdd, sql.c_str());
   int64_t icnt = 0;
   int rc = sqlite3_exec(dbAllAddress, sql.c_str(), selectCountCallback, (void*)&icnt, NULL);
   if( (icnt == 0) || (rc != SQLITE_OK) ){ // not exist, 
      //printf("SQL error: %s\n", zErrMsg);
      //sqlite3_free(zErrMsg);
	  if( bAdd )  
	  {
         //sql = "SELECT Count(*) from Addresss where addr = '" + sAddr + "'";  //"SELECT * from Keys";
         sql = "INSERT INTO Addresss (addr, coin) VALUES ('" + sAddr + "', " + strprintf("%" PRId64 ");", nValue); 
         rc = sqlite3_exec(dbAllAddress, sql.c_str(), NULL, NULL, NULL);
	     if( fDebug ) printf("updateAllAdressDB:: sql [%s], rc = [%d]\n", sql.c_str(), rc);
		 if( rc == SQLITE_OK ){ rzt++; }
	  }else{
	     printf("updateAllAdressDB:: bAdd = [%d], nValue[%I64u], Address [%s] not exist :( eeeeeerror\n", bAdd, nValue, sAddr.c_str());
	  }
   }else if( icnt > 0 ){
      //printf("Query done, count = [%I64u]\n", icnt);
	  sql = "SELECT * from Addresss where addr = '" + sAddr + "'";
	  icnt = 0;
	  dbOneResultCallbackPack pack={OneResultPack_U64_TYPE, -1, 0, 0, "coin", ""};
      rc = sqlite3_exec(dbAllAddress, sql.c_str(), getOneResultCallback, (void*)&pack, NULL);
	  icnt = pack.u6Rzt;
	  if( fDebug ) printf("updateAllAdressDB:: getCoin [%I64u] rc=[%d] nValue=[%I64u]\n", icnt, rc, nValue);
      if( rc == SQLITE_OK )
	  {
         if( bAdd )
		 {
		    int64_t t = icnt;
			icnt = icnt + nValue;
			if( icnt < t ){ printf("updateAllAdressDB:: add overflow [%I64u] < [%I64u] eeeeeerror\n", icnt, t); }
		 }else{
		    if( icnt >= nValue ){ icnt = icnt - nValue; }
		 }
		 sql = "UPDATE Addresss set coin = " + strprintf("%" PRId64, icnt) + " where addr='" + sAddr + "'";
		 rc = sqlite3_exec(dbAllAddress, sql.c_str(), NULL, NULL, NULL);
		 if( fDebug ) printf("updateAllAdressDB:: bAdd=[%d], sql [%s], rc=[%d], icnt=[%I64u] \n", bAdd, sql.c_str(), rc, icnt);
		 if( rc == SQLITE_OK ){ rzt++; }
	  }
   }
   return rzt;
}

void updateTotalCoin(uint64_t nValue)
{
   string sql = "SELECT * from total";
   int64_t icnt = 0;
   dbOneResultCallbackPack pack={OneResultPack_U64_TYPE, -1, 0, 0, "totalcoin", ""};
   int rc = sqlite3_exec(dbAllAddress, sql.c_str(), getOneResultCallback, (void*)&pack, NULL);
   icnt = pack.u6Rzt;
   if( fDebug ) printf("updateTotalCoin:: Cur Total Coin [%I64u] rc=[%d] nValue=[%I64u]\n", icnt, rc, nValue);
   if( rc == SQLITE_OK )
   {
	   icnt = icnt + nValue;
	   sql = "UPDATE total set totalcoin = " + strprintf("%" PRId64, icnt);
	   rc = sqlite3_exec(dbAllAddress, sql.c_str(), 0, 0, NULL);
	   if( fDebug ) printf("updateTotalCoin:: sql [%s], rc=[%d], [%I64u] \n", sql.c_str(), rc, icnt);
	   //if( rc == SQLITE_OK ){ rzt++; }
   }
}

int isBlockProcessed(int nHi)
{
   int rzt = 0;
   string sql = "SELECT Count(*) from processedblock where num = " + strprintf("%d", nHi);
   if( fDebug ) printf("isBlockProcessed:: nHi=[%d], sql [%s] \n", nHi, sql.c_str());
   int64_t icnt = 0;
   int rc = sqlite3_exec(dbAllAddress, sql.c_str(), selectCountCallback, (void*)&icnt, NULL);
   if( icnt  > 0 ){ rzt++; }
   return rzt;
}

int getMultiGenBetCountForGui(const string s, uint64_t &rzt)
{
   //uint64_t rzt = 0;
   string sql = "SELECT Count(*) from AllBets where opcode=1 and bet_type" + s + " and done=0; ";
   //if( fDebug ){ printf("getMultiGenBetCountForGui:: [%s] \n", sql.c_str()); }
   return sqlite3_exec(dbLuckChainGui, sql.c_str(), selectCountCallback, (void*)&rzt, NULL);
}
int getRunSqlResultCountForGui(const string sql, uint64_t &rzt)
{
   if( bLuckChainRollbacking ){  rzt=0;   return 0;  }
   return sqlite3_exec(dbLuckChainGui, sql.c_str(), selectCountCallback, (void*)&rzt, NULL);
}
int getRunSqlResultCountForGu2(const string sql, uint64_t &rzt)
{
   return sqlite3_exec(dbLuckChainGu2, sql.c_str(), selectCountCallback, (void*)&rzt, NULL);
}
int getRunSqlResultCount(sqlite3 *dbOne, const string sql, uint64_t &rzt)
{
   return sqlite3_exec(dbOne, sql.c_str(), selectCountCallback, (void*)&rzt, NULL);
}
uint64_t getAliveLaunchBetCountCore( sqlite3 *dbOne, const std::string sBettor, bool bJustRcvTx, int iOpCode, int iBetType, bool bOnlyJustRcvTx )
{
   uint64_t rzt = 0;
   string sql = "SELECT Count(*) from AllBets where opcode";
   if( iOpCode >= 0 ){ sql = sql + "=" + inttostr(iOpCode); }
   else{ sql = sql + ">0"; }
   sql = sql + " and done=0";
   if( iBetType >= 0 ){ sql = sql + " and bet_type=" + inttostr(iBetType); }
   if( sBettor.length() < 33 ){ sql = sql + ";"; }
   else{ sql = sql + " and bettor='" + sBettor + "';"; }
   int rc=0;
   if( !bOnlyJustRcvTx )
   {
	   rc = sqlite3_exec(dbOne, sql.c_str(), selectCountCallback, (void*)&rzt, NULL);
   }
	if( bJustRcvTx )  // 2016.12.18 add
	{
		boost::replace_first(sql, "AllBets", "AllBetsTmp");      uint64_t u6zt = 0;
		rc = sqlite3_exec(dbOne, sql.c_str(), selectCountCallback, (void*)&u6zt, NULL);
		if( rc == SQLITE_OK ){  rzt = rzt + u6zt;  }
	}
	if( fDebug ) printf("getAliveLaunchBetCountCore:: rzt=[%s], sql [%s] \n", u64tostr(rzt).c_str(), sql.c_str());
   return rzt;
}
uint64_t getAliveLaunchBetCount( sqlite3 *dbOne, const string sBettor, bool bJustRcvTx )
{
   uint64_t rzt = 0;
   string sql = "SELECT Count(*) from AllBets where opcode=1 and done=0";
   if( sBettor.length() < 33 ){ sql = sql + ";"; }
   else{ sql = sql + " and bettor='" + sBettor + "';"; }
   int rc = sqlite3_exec(dbOne, sql.c_str(), selectCountCallback, (void*)&rzt, NULL);
	if( bJustRcvTx )  // 2016.12.18 add
	{
		boost::replace_first(sql, "AllBets", "AllBetsTmp");      uint64_t u6zt = 0;
		rc = sqlite3_exec(dbOne, sql.c_str(), selectCountCallback, (void*)&u6zt, NULL);
		if( rc == SQLITE_OK ){  rzt = rzt + u6zt;  }
	}
   return rzt;
}

void insertProcessedBlock(int nHi, string sHash)
{
   string sql = "INSERT INTO processedblock (hash, num) VALUES ('" + sHash + "', " + strprintf("%d);", nHi); 
   if( fDebug ) printf("insertProcessedBlock:: sql [%s] \n", sql.c_str());
   sqlite3_exec(dbAllAddress, sql.c_str(), NULL, NULL, NULL);
}

bool isBetTxExists(const string sTx, const string sTableName = "AllBets")
{
   bool rzt = false;
   string sql = "select count(*)  from " + sTableName + " where tx='" + sTx + "';";
   char* zErrMsg = 0;
   int64_t icnt = 0;
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), selectCountCallback, (void*)&icnt, &zErrMsg);
   rzt = icnt  > 0;
   if( fDebug ){ printf("isBetTxExists:: [%s] \n [%d] [%s]\n", sql.c_str(), rc, zErrMsg); }
   if( zErrMsg ){ sqlite3_free(zErrMsg); }
   return rzt;
}

bool isBitBetSignExists(const string sCmd)
{
   bool rzt = false;
   string sql = "select count(*)  from SpentSign where sign='" + sCmd + "';";
   //char* zErrMsg = 0;
   int64_t icnt = 0;
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), selectCountCallback, (void*)&icnt, NULL);
   rzt = icnt  > 0;
   if( fDebug ){  printf("isBitBetSignExists:: rzt=[%d], [%s]  \n", rzt, sql.c_str());  }
   //if( zErrMsg ){ sqlite3_free(zErrMsg); }
   return rzt;
}
bool insertBitBetSign(const string sCmd)
{
   bool rzt = false;
   string sql = "INSERT INTO SpentSign (sign) VALUES ('" + sCmd + "');";
   //char* zErrMsg = 0;
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), 0, 0, NULL);      rzt = (rc == SQLITE_OK);
   if( fDebug ){  printf("insertBitBetSign:: rzt=[%d], [%s] \n", rzt, sql.c_str());  }
   //if( zErrMsg ){ sqlite3_free(zErrMsg); }
   return rzt;
}
bool insertRefereeToRefereesDB(string sNick, string sAddr, string sLocal, string sFee, string sMaxCoins, string sRemark, string sPower, string sMsg, string sSign, string sSysAddr, uint64_t nTime)
{
   char* zErrMsg = 0;
   if( (sPower.length() < 1) || (sPower == "-") ){ sPower="1"; }
   if( (sRemark.length() < 1) || (sRemark == "-") ){ sRemark=" "; }
   string sql = "INSERT INTO Referees (nickName, coinAddress, local, fee, maxcoins, remark, power, msgForSign, systemSign, systemAddr, valid, nTime) ";
   string t = "'" + sNick + "', '" + sAddr + "', '" + sLocal + "', '" + sFee + "', " + sMaxCoins + ", '" + sRemark + "', " + sPower + ", '" + sMsg + "', '" + sSign + "', '" + sSysAddr + "', 1, " + u64tostr(nTime);
   sql = sql + "VALUES (" + t + ");";   printf("insertRefereeToRefereesDB:: [%s] \n", sql.c_str());
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &zErrMsg);
   if( rc != SQLITE_OK ){ printf("insertRefereeToRefereesDB:: err [%d] [%s] \n", rc, zErrMsg); }
   if( zErrMsg ){ sqlite3_free(zErrMsg); }
   return (rc == SQLITE_OK);
}
bool updateRefereeOfRefereesDB(string sNick, string sAddr, string sValid, string sFee, string sMaxCoins, string sRemark, string sPower, string sMsg, string sSign, string sSysAddr)
{
   char* zErrMsg = 0;
   //sql = "UPDATE total set totalcoin = " + strprintf("%" PRId64, icnt);
   string sql = "UPDATE Referees set ";
   if( (sNick.length() > 0) && (sNick != "-") ){ sql = sql + "nickName='" + sNick + "', "; }
   if( (sValid.length() > 0) && (sValid != "-") ){ sql = sql + "valid=" + sValid + ", "; }
   if( (sFee.length() > 0) && (sFee != "-") ){ sql = sql + "fee='" + sFee + "', "; }
   if( (sMaxCoins.length() > 0) && (sMaxCoins != "-") ){ sql = sql + "maxcoins=" + sMaxCoins + ", "; }
   if( (sRemark.length() > 0) && (sRemark != "-") ){ sql = sql + "remark='" + sRemark + "', "; }
   if( (sPower.length() > 0) && (sPower != "-") ){ sql = sql + "power=" + sPower + ", "; }
   if( (sMsg.length() > 0) && (sMsg != "-") ){ sql = sql + "msgForSign='" + sMsg + "', "; }
   if( (sSign.length() > 0) && (sSign != "-") ){ sql = sql + "systemSign='" + sSign + "', "; }
   if( (sSysAddr.length() > 0) && (sSysAddr != "-") ){ sql = sql + "systemAddr='" + sSysAddr + "', "; }
   char* p = (char*)sql.c_str();  p = p + sql.length() - 2;
   if( p[0] == ',' ){ p[0] = ' '; }
   
   sql = sql + "where coinAddress='" + sAddr + "';";   if( fDebug ){ printf("updateRefereeOfRefereesDB:: [%s] \n", sql.c_str()); }
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &zErrMsg);
   if( rc != SQLITE_OK ){ printf("updateRefereeOfRefereesDB:: err [%d] [%s] \n", rc, zErrMsg); }
   if( zErrMsg ){ sqlite3_free(zErrMsg); }
   return (rc == SQLITE_OK);
}

bool addOrEditUser(string sNick, string sAddr, string sWeight, string sFlag, string sRemarks, string sSign, uint64_t nTime)
{
   string sql = "INSERT INTO Users (coinAddr) VALUES('" + sAddr + "');";
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
   if( fDebug ){  printf("addOrEditUser : [%d] [%s] \n", rc, sql.c_str()); }
   sql = "UPDATE Users set nTime=" + u64tostr(nTime) + ", ";
   if( (sNick.length() > 0) && (sNick != "-") ){ sql = sql + "nickName='" + sNick + "', "; }
   if( (sWeight.length() > 0) && (sWeight != "-") ){ sql = sql + "weight=" + sWeight + ", "; }
   if( (sFlag.length() > 0) && (sFlag != "-") ){ sql = sql + "flag=" + sFlag + ", "; }
   if( (sRemarks.length() > 0) && (sRemarks != "-") ){ sql = sql + "remarks='" + sRemarks + "', "; }
   if( (sSign.length() > 0) && (sSign != "-") ){ sql = sql + "sign='" + sSign + "', "; }
   char* p = (char*)sql.c_str();  p = p + sql.length() - 2;
   if( p[0] == ',' ){ p[0] = ' '; }
   sql = sql + "where coinAddr='" + sAddr + "';";   if( fDebug ){ printf("addOrEditUser:: [%s] \n", sql.c_str()); }
   rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
   if( fDebug ){  printf("addOrEditUser:: rc=[%d] [%s] \n", rc, sql.c_str());  }
   return (rc == SQLITE_OK);
}

bool updateBetRefereeDecide(int opc, const string genTx, const string sDecide, const string referee, const string decideTx)
{
   char* pe = 0;   string sql = "UPDATE AllBets set win_num=";
   if( opc == 1 )
   {
		sql = sql + "'" + sDecide + "', refereeDecideTx='" + decideTx + "' where tx='" + genTx + "' and referee='" + referee + "' and opcode=1 and bet_type=5 and length(refereeDecideTx)<10;";
   }
   else if( opc == 2 ){
		sql = sql + "'-', refereeDecideTx='" + decideTx + "', referee='" + referee + "' where tx='" + genTx + "' and done=0 and opcode=1 and bet_type=5;";   
   }
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("updateBetRefereeDecide : opc=[%d], rc=[%d] [%s], sql = [%s] \n", opc, rc, pe, sql.c_str()); }
   //if( rc != SQLITE_OK ){ printf("updateBetRefereeDecide:: [%s] err [%d] [%s] \n", sql.c_str(), rc, pe); }
   if( pe ){ sqlite3_free(pe); }
   if( (rc == SQLITE_OK) && (opc == 1) )  // set winner
   {
		sql = "UPDATE AllBets set isWinner=0, win_num='" + sDecide + "' where gen_bet='" + genTx + "';";
		rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
		sql = "UPDATE AllBets set isWinner=1 where gen_bet='" + genTx + "' and bet_num='" + sDecide + "';";
		rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
		if( rc != SQLITE_OK ){ printf("updateBetRefereeDecide : set winner [%s] err [%d] [%s] \n", sql.c_str(), rc, pe); }
		if( pe ){ sqlite3_free(pe); }
   }
   return (rc == SQLITE_OK);
}

bool updateRefereeDecideCount(const string sReferee, int iAdd=1)
{
   bool rzt=false;      string sql = "SELECT * from Referees where coinAddress='" + sReferee + "';";
   dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, 11, 0, 0, "decideCount", ""};
	getOneResultFromDb(dbLuckChainWrite, sql, pack);
   if( fDebug ){ printf("updateRefereeDecideCount :: fDone=[%d] u6Rzt=[%s] sql=[%s] \n", pack.fDone, u64tostr(pack.u6Rzt).c_str(), sql.c_str()); }
   if( pack.fDone > 0 )
   {
       char* pe=0;      uint64_t u6 = pack.u6Rzt + iAdd;
	   sql = "UPDATE Referees set decideCount=" + u64tostr(u6) + " where coinAddress='" + sReferee + "';";
	   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), 0, 0, &pe);      rzt = (rc == SQLITE_OK);
	   if( fDebug ) printf("updateRefereeDecideCount:: rc=[%d] [%s] [%s] \n", rc, pe, sql.c_str());
	   if( pe ){ sqlite3_free(pe); }
	}
   return rzt;
}

bool updateRefereeTakeFees(const string sReferee, uint64_t u6Coins)
{
   bool rzt=false;      string sql = "SELECT * from Referees where coinAddress='" + sReferee + "';";
   dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, 12, 0, 0, "takeFees", ""};
	getOneResultFromDb(dbLuckChainWrite, sql, pack);
   if( fDebug ){ printf("updateRefereeTakeFees :: fDone=[%d] u6Rzt=[%s] sql=[%s] \n", pack.fDone, u64tostr(pack.u6Rzt).c_str(), sql.c_str()); }
   if( pack.fDone > 0 )
   {
       char* pe=0;      uint64_t u6 = pack.u6Rzt + u6Coins;
	   sql = "UPDATE Referees set takeFees=" + u64tostr(u6) + " where coinAddress='" + sReferee + "';";
	   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), 0, 0, &pe);      rzt = (rc == SQLITE_OK);
	   if( fDebug ) printf("updateRefereeTakeFees:: rc=[%d] [%s] [%s] \n", rc, pe, sql.c_str());
	   if( pe ){ sqlite3_free(pe); }
	}
   return rzt;
}

bool updateBitBetSettings(const string fieldName, const string sValue)
{
   char* pe = 0;   string sql = "UPDATE Settings set " + fieldName + "=" + sValue + ";";
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ){ printf("updateBitBetSettings : rc=[%d] [%s], sql = [%s] \n", rc, pe, sql.c_str()); }
   if( pe ){ sqlite3_free(pe); }
   return (rc == SQLITE_OK);
}

bool insertBitBetToAllBetsDB(const BitBetPack& bbp, const string betTable)
{
	int iFirstOfThisAddr = 0;      uint64_t u6TotalOfThisAddrCoins = 0, u6totalOfThisAddrBetCount = 0;      bool bTmpTab = (betTable == "AllBetsTmp");
	if( (!bTmpTab) && (iRecordPlayerInfo > 0) )
	{
		if( bbp.opCode == 1 ){  iFirstOfThisAddr++;  }
		else{
			uint64_t u6Rzt=0;      string sql =  "select Count(*) from " + betTable + " where gen_bet='" + bbp.genBet + "' and bettor='" + bbp.bettor + "';";
			int rc = getRunSqlResultCount(dbLuckChainWrite, sql, u6Rzt);
			if( rc == SQLITE_OK ){  if( u6Rzt < 1 ){  iFirstOfThisAddr++;  }  }
			else{  iFirstOfThisAddr++;  }
		}
		if( iFirstOfThisAddr > 0 ){ u6TotalOfThisAddrCoins = bbp.u6TotalBetAmount;   u6totalOfThisAddrBetCount++;  }
	}
// id _0, opcode _1, bet_type _2, bet_amount _3, min_amount _4, sblock _5, blockspace _6, tblock _7, bet_len _8, bet_num _9, bet_num2 _10, gen_bet _11,
// tx _12, bet_title _13, referee _14, bettor _15, bet_start_num _16, confirmed _17, maxbetcoins _18, nTime _19, win_num _20, isWinner _21, bet_count _22,
// total_bet_amount _23, refereeDecideTx _24, tblockHash _25, done _26, payIndex _27
   bool rzt=false;
   string sql = "INSERT INTO " + betTable + " (opcode, bet_type, bet_amount, min_amount, sblock, blockspace, tblock, bet_len, bet_num, bet_num2, gen_bet, tx, bet_title, "
                      "referee, bettor, bet_start_num, confirmed, maxbetcoins, nTime, win_num, isWinner, bet_count, total_bet_amount, refereeDecideTx, tblockHash, done, payIndex, "
					  "refereeNick, hide, encrypt, max_bet_count, one_addr_once, est_bet_count, rounds, new_bet_count, firstOfThisAddr, totalOfThisAddrBetCount, totalOfThisAddrCoins,"
					  "oneAddrMaxBetAmount, enCashFlag, uniqueNumber) ";
   //if( fDebug ){ printf("insertBitBetToAllBetsDB:: [%s]\n [%s][%s]\n", sql.c_str(), bbp.tx.c_str(), bbp.betTitle.c_str()); }
   //printf("miniBetAmount[%" PRIu64 "]\n", bbp.u6MiniBetAmount);
   //printf("startBlock[%" PRIu64 "]\n", bbp.u6StartBlock);
   //printf("targetBlock[%" PRIu64 "]\n", bbp.u6TargetBlock);
   //printf("betLen[%d]\n", bbp.betLen);
   try{
   //string v = strprintf("%d, %d, %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %d, '%s', '%s', '%s', '%s', '%s', %d, %" PRIu64 ", %" PRIu64 ", %" PRIu64, 
/*   string v = strprintf("%d, %d, %"PRIu64", %"PRIu64", %"PRIu64", %"PRIu64", %"PRIu64", %d, "
                                "'%s', '%s', '%s', '%s', '%s', '%s', %d, %"PRIu64", %"PRIu64", %"PRIu64, 
                                bbp.opCode, bbp.betType, bbp.betAmount, bbp.miniBetAmount, bbp.startBlock, bbp.blockSpace, bbp.targetBlock, bbp.betLen, 
								bbp.betNum.c_str(), bbp.genBet.c_str(), bbp.tx.c_str(), bbp.betTitle.c_str(), bbp.referee.c_str(), bbp.bettor.c_str(), bbp.confirmed, 
								bbp.maxBetCoins, bbp.nTime, bbp.total_bet_amount); */
// [1, 0, 999999, 222, 0, 0, 111, 1, 'a', '', 'tx wei zhi', 'I'm lucky lee', '', 'B8KzD68zaMyACTXtqdeKGByUeT3xsri6qq', 1, 0, 1473206791, 999999] 
   string bet_num=bbp.betNum;   if( bet_num.length() < 1 ){ bet_num="-"; }
   string gen_bet=bbp.genBet;      if( gen_bet.length() < 1 ){ gen_bet="-"; }
   string bet_title=bbp.betTitle;     if( bet_title.length() < 1 ){ bet_title="-"; }
   string referee=bbp.referee;       if( referee.length() < 1 ){ referee="-"; }
   string refereeNick = bbp.refereeNick;      if( refereeNick.length() < 1 ){ refereeNick="-"; }
   string sEncrypt = "0";      if( (bbp.betType == 5) && (bbp.encryptFlag5 == "1") ){ sEncrypt="1"; }   //int iEncrypt = 0;    if( bbp.betType == 5 ){  iEncrypt = atoi(bbp.encryptFlag5);  }

   string t = inttostr(bbp.opCode) + ", " + inttostr(bbp.betType) + ", " + u64tostr(bbp.u6BetAmount) + ", " + u64tostr(bbp.u6MiniBetAmount) + ", " + u64tostr(bbp.u6StartBlock) + ", " + u64tostr(bbp.u6BlockSpace) + ", " + u64tostr(bbp.u6TargetBlock) +
                  ", " + inttostr(bbp.betLen) + ", '" + bet_num + "', 0, '" + gen_bet + "', '" + bbp.tx + "', '" + bet_title + "', '" + referee + "', '" + 
				  bbp.bettor + "', " + inttostr(bbp.betStartNum) + ", " + inttostr(bbp.confirmed) + ", " + u64tostr(bbp.u6MaxBetCoins) + ", " + u64tostr(bbp.u6Time) + ", '-', 0, " + 
				  u64tostr(bbp.u6BetCount) + ", " + u64tostr(bbp.u6TotalBetAmount) + ", '-', '-', 0, " + inttostr(bbp.payIndex) + ", '" + refereeNick + "', 0, " + sEncrypt + ", " + 
				  inttostr(bbp.maxBetCount) + ", " + inttostr(bbp.oneAddrOnce) + ", 1, 0, 1, " + inttostr(iFirstOfThisAddr) + ", " + u64tostr(u6totalOfThisAddrBetCount) + ", " + u64tostr(u6TotalOfThisAddrCoins) +
				  ", " + u64tostr(bbp.u6OneAddrMaxBetAmount) + ", " + inttostr(bbp.enCashFlag) + ", " + inttostr(bbp.uniqueNumber);
   //if( fDebug ){ printf("insertBitBetToAllBetsDB:: t[%s] \n", t.c_str()); }
   //printf("betAmount [%" PRIu64 ", %" PRIu64 " ]\n", bbp.u6BetAmount, bbp.u6MiniBetAmount);

   // insertBitBetToAllBetsDB:: [1, 0, 1000000000, 0, 700, 0, 0, 0, ] 
   //string v = strprintf("%d, %d, %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %d, ",
   //                             bbp.opCode, bbp.betType, bbp.betAmount, bbp.miniBetAmount, bbp.startBlock, bbp.blockSpace, bbp.targetBlock, bbp.betLen);
					  //"VALUES ('" + sHash + "', " + strprintf("%d);", nHi); 
   //printf("insertBitBetToAllBetsDB:: [%s] \n", v.c_str());
   sql = sql + "VALUES (" + t + ");";      char* pe = 0;
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
   if( fDebug ) printf("insertBitBetToAllBetsDB 1 : rc =[%d], [%s], sql [%s] \n", rc, pe, sql.c_str());
   rzt = (rc == SQLITE_OK);      if( pe ){ sqlite3_free(pe); }
   
   if( rzt && (!bTmpTab) && (iRecordPlayerInfo > 0) && (iFirstOfThisAddr == 0) )   // 2016.11.24 add
   {
		sql = "UPDATE " + betTable + " set totalOfThisAddrBetCount=(totalOfThisAddrBetCount+1), totalOfThisAddrCoins=(totalOfThisAddrCoins + " + u64tostr(bbp.u6BetAmount) + ") where gen_bet='" + bbp.genBet + "' and bettor='" + bbp.bettor + "' and firstOfThisAddr>0;";
		rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, 0);
		//rzt = (rc == SQLITE_OK);
		if( fDebug ){ printf("insertBitBetToAllBetsDB : up player info :: rc = [%d], sql = [%s] \n", rc, sql.c_str()); }
   }

   if( (!bTmpTab) && (rc==SQLITE_OK) && (bbp.opCode == 2) && (bbp.betType>0) && (bbp.genBet.length() > 60) )  // bet, not lucky 16, genbet valid, up bet_count total_bet_amount
   {
		sql = "UPDATE " + betTable + " set bet_count=(bet_count+1), new_bet_count=(new_bet_count+1), est_bet_count=(bet_count+1), total_bet_amount=(total_bet_amount + " + u64tostr(bbp.u6BetAmount) + ") where tx='" + bbp.genBet + "' and opcode=1 and done=0;";
		rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, 0);
		rzt = (rc == SQLITE_OK);
		if( fDebug ){ printf("insertBitBetToAllBetsDB : up gen bet bet_count and total_amount :: rzt = [%d], sql = [%s] \n", rzt, sql.c_str()); }

		/*
		sql = "select * from AllBets where tx='" + bbp.genBet + "' and opcode=1 and bet_type>0 and done=0;";
		if( fDebug ){ printf("insertBitBetToAllBetsDB : up gen bet : sql = [%s] \n", sql.c_str()); }
		dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, AllBets_bet_count_idx, 0, 0, "bet_count", ""};
		getOneResultFromDb(dbLuckChainRead, sql, pack);
		if( fDebug ){ printf("insertBitBetToAllBetsDB : up gen bet : fDone = [%d], bet_count = [%s] \n", pack.fDone, u64tostr(pack.u6Rzt).c_str()); }
		if( pack.fDone > 0 )  //   uint64_t u6Rzt;
		{
			uint64_t u6BetCount = pack.u6Rzt + 1;
			pack = {OneResultPack_U64_TYPE, AllBets_total_bet_amount_idx, 0, 0, "total_bet_amount", ""};
			getOneResultFromDb(dbLuckChainRead, sql, pack);
			if( pack.fDone > 0 )  //   uint64_t u6Rzt;
			{
		uint64_t u6AllBets = pack.u6Rzt + bbp.u6BetAmount;
		sql = "UPDATE AllBets set bet_count=" + u64tostr(u6BetCount) + ",  total_bet_amount=" + u64tostr(u6AllBets) + " where tx='" + bbp.genBet + "' and opcode=1 and bet_type>0;";
		rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
		if( fDebug ) printf("<-- insertBitBetToAllBetsDB : up gen bet : sql [%s] rc =[%d], [%s]\n", sql.c_str(), rc, pe);
		if( pe ){ sqlite3_free(pe); }
			}
		}*/
   }

   }catch (std::exception &e) {
      printf("insertBitBetToAllBetsDB:: err [%s]\n", e.what());
   }
   catch (...)
   {
      printf("insertBitBetToAllBetsDB:: err 222\n");
   }
   return rzt;
}
/*void insertABitBetToGenBetDB(const string genBet, const BitBetPack& bbp)
{
   insertBitBetToAllBetsDB(bbp, genBet);
}*/

struct synAllBitBetPack
{
   int iStep;      bool bForce;
   uint64_t u6CurBlockNum;
   uint64_t u6RecordCount;
   std::vector<std::string> vSqls;    std::vector<std::string> vReBetLuckyLottos;   //std::vector<std::string> vNeedExpBets;
   std::vector<std::pair<std::string, std::string> > vNeedExpBets;
};


string getStrLastNChar(const string sHash, int nByte)
{
	string rzt="";   int iHashLen = sHash.length();
	if( iHashLen < nByte ){ return rzt; }
	rzt = sHash.substr(iHashLen - nByte, nByte);
	return rzt;
}
int hexCharToInt(char c)
{
	int rzt=0;
	if( (c > 0x2f) && (c < 0x3a) ){ rzt = (int)(c - 0x30); }    // 0 ~ 9
	else if( (c > 0x60) && (c < 0x67) ){ rzt = (int)(c - 0x61) + 10; }  // a ~ f
	return rzt;
}
std::string getLucky16MultiBetNums(int iStart, int iCount)
{
	string rzt = "";
	for( int i=0; i < iCount; i++ )
	{
		if( iStart > 15 )
		{
			int j = (iStart % 15) - 1;
			rzt = rzt + strprintf("%x,", j);
		}else{ rzt = rzt + strprintf("%x,", iStart); }
		iStart++;
	}
	return rzt;
}
int isLucky16MultiBetWin(int iStart, int iCount, const string sWinNum)
{
	int rzt = 0;
	if( sWinNum.length() > 0 )
	{
		const char* p = sWinNum.c_str();   int c = hexCharToInt(p[0]);
		if( fDebug ){ printf("isLucky16MultiBetWin:: c=[%d] [%s] \n", c, p); }
		int iTg = iStart + iCount - 1;
		if( iTg > 15 )
		{
			int j = iTg % 15;
			if( (c >= iStart) && (c <= 15) ){ rzt++; }
			else if( (c >= 0) && (c < j) ){ rzt++; }
		}else{
			if( (c >= iStart) && (c <= iTg) ){ rzt++; }
		}
	}
	return rzt;
}
string getTgBlkHashWinNumber(const string sHash,  int betType, int betLen, bool isBankerMode, const string bankerNum)
{
	string rzt="";     int iHashLen = sHash.length();
	if( iHashLen > 63 )
	{
		const char* p = sHash.c_str() + (iHashLen - 1);     int c = hexCharToInt(p[0]);
		if( (betType == 0) || (betType == 4) )	{ rzt = sHash.substr(iHashLen - betLen, betLen); }  // Lucky 16   or   Lucky Lottery
		else if( betType == 1 )  // Odd-Even
		{
			if( (c % 2) > 0 ){ rzt = "odd"; } else{ rzt = "even"; }
		}
		else if( betType == 2 )  // Big-Small
		{
			if( (c >= 0) && (c<8) ){ rzt = "small"; } else{ rzt = "big"; }     // 0 ~ 7 = small,   8 ~ f = Big
			if( isBankerMode )   // 2016.10.15 add
			{
				if( bankerNum == "small" )
				{
					if( (c >= 0) && (c<9) ){ rzt = "small"; } else{ rzt = "big"; }  // 0 ~ 8 = small,   9 ~ f = Big
				}else{
					if( (c >= 0) && (c<7) ){ rzt = "small"; } else{ rzt = "big"; }    // 0 ~ 6 = small,   7 ~ f = Big				
				}
			}
		}
	}
	return rzt;
}
/*string getTgBlkHashWinNumber(uint64_t nHeight,  int betType, int betLen)
{
	string sHash = GetBlockHashStr(nHeight);  // 64 Chars, 00000bc87d 0385e25417 c877e00e95 9087e8eebb f76722dfcf f9e76da7cb f3ea
	return getTgBlkHashWinNumber(sHash,  betType, betLen);
}*/
/*string getBetBlockHashInDB2(uint64_t nHi)
{
	string rzt = "";
	string sql = "select * from BetBlockHash where height=" + u64tostr(nHi) + ";";
	dbOneResultCallbackPack pack = {OneResultPack_STR_TYPE, -1, 0, 0, "hash", ""};  // 2 = string,  hash index = 2
	getOneResultFromDb(dbLuckChainRead2, sql, pack);
	if( pack.fDone > 0 ){ return pack.sRzt; }
	return rzt;
}

void upinsertBetBlockHashToDb(uint64_t nHi, const string oldHash, const string newHash)
{
   string sql = "";     bool b=true;
   if( oldHash.length() > 63 )  // exists, update
   {
		if( oldHash != newHash ){ sql = "UPDATE BetBlockHash set hash='" + newHash + "' where height=" + u64tostr(nHi) + ";"; }
		else b = false;
   }else{
		sql = "INSERT INTO BetBlockHash (height, hash) VALUES (" + u64tostr(nHi) + ", '" + newHash + "');";
   }
   char *zErrMsg = 0;     int rc=0;
   if( b ){ rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &zErrMsg); }
   if( fDebug ) printf("<-- upinsertBetBlockHashToDb:: sql [%s] rc =[%d], [%s]\n", sql.c_str(), rc, zErrMsg);
   if( zErrMsg ){ sqlite3_free(zErrMsg); } 
}
void upinsertBetBlockHashToDb(uint64_t nHi, const string newHash)
{
   string s = getBetBlockHashInDB2(nHi),   sql = "";
   upinsertBetBlockHashToDb(nHi, s, newHash);
}*/

string getTargetBlockHashByAllBetsDB(string tx)
{
	string rzt = "";
	string sql = "select * from AllBets where tx='" + tx + "';";
	dbOneResultCallbackPack pack = {OneResultPack_STR_TYPE, -1, 0, 0, "tblockHash", ""};  // 2 = string,  hash index = 2
	getOneResultFromDb(dbLuckChainRead2, sql, pack);
	//if( fDebug ){ printf("getTargetBlockHashByAllBetsDB:: [%s] [%d] [%s] \n", sql.c_str(), pack.fDone, pack.sRzt.c_str()); }
	if( pack.fDone > 0 ){ rzt = pack.sRzt; }
	return rzt;
}
void updateTargetBlockHashToAllBetsDb(const string tx, const string oldHash, const string newHash)
{
   string sql = "";     bool b=true;
   if( oldHash.length() > 63 )  // exists, update
   {
		if( oldHash != newHash ){ sql = "UPDATE AllBets set tblockHash='" + newHash + "' where tx='" + tx + "';"; }
		else b = false;
   }
   char *zErrMsg = 0;     int rc=0;
   if( b ){ rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &zErrMsg); }
   if( fDebug ) printf("<-- updateTargetBlockHashToAllBetsDb:: sql [%s] rc =[%d], [%s]\n", sql.c_str(), rc, zErrMsg);
   if( zErrMsg ){ sqlite3_free(zErrMsg); } 
}

string getLuckyBossWinTx(sqlite3 *dbOne, const string sGenTx)
{
	string rzt = "";
	string sql = "select * from AllBets where gen_bet='" + sGenTx + "' order by bet_num2 desc limit 1;";
	dbOneResultCallbackPack pack = {OneResultPack_STR_TYPE, AllBets_tx_idx, 0, 0, "tx", ""};
	getOneResultFromDb(dbOne, sql, pack);
	if( pack.fDone > 0 ){ 	rzt = pack.sRzt;  }
	if( fDebug ){ printf("getLuckyBossWinTx:: rzt = [%s], fDone = [%d],  sql = [%s] \n", rzt.c_str(), pack.fDone, sql.c_str()); }
	return rzt;
}

int isBiggestBetNumber(const string sGenTx, const string curTx)  //int isBiggestBetNumber(uint64_t x, const string sGenTx, const string curTx)
{
	int rzt = 0;      string sRzt = getLuckyBossWinTx(dbLuckChainRead2, sGenTx);
	if( sRzt.length() > 60 ){
		if( curTx == sRzt ){ rzt = 1; }
	}
	if( fDebug ){ printf("isBiggestBetNumber:: rzt = [%d], curTx=[%s :: %s], sql = [%s] \n", rzt, curTx.c_str(), sRzt.c_str()); }
	return rzt;
}

bool isDuplicatesBossNums( const string sBetNum )  // 2016.10.22 add
{
	bool rzt = false;
	BitBetBossPack bp = {0, 0, 0, 0, 0, 0, 0};
	int i = getBetBossNums(sBetNum, bp);
	if( i > 5 )
	{
		if( (bp.b1 == bp.b2) || (bp.b1 == bp.b3) || (bp.b1 == bp.b4) || (bp.b1 == bp.b5) || (bp.b1 == bp.b6) ){ return true; }
		if( (bp.b2 == bp.b1) || (bp.b2 == bp.b3) || (bp.b2 == bp.b4) || (bp.b2 == bp.b5) || (bp.b2 == bp.b6) ){ return true; }
		if( (bp.b3 == bp.b1) || (bp.b3 == bp.b2) || (bp.b3 == bp.b4) || (bp.b3 == bp.b5) || (bp.b3 == bp.b6) ){ return true; }
		if( (bp.b4 == bp.b1) || (bp.b4 == bp.b2) || (bp.b4 == bp.b3) || (bp.b4 == bp.b5) || (bp.b4 == bp.b6) ){ return true; }
		if( (bp.b5 == bp.b1) || (bp.b5 == bp.b2) || (bp.b5 == bp.b3) || (bp.b5 == bp.b4) || (bp.b5 == bp.b6) ){ return true; }
		if( (bp.b6 == bp.b1) || (bp.b6 == bp.b2) || (bp.b6 == bp.b3) || (bp.b6 == bp.b4) || (bp.b6 == bp.b5) ){ return true; }
	}
	return rzt;
}

string explainBetNums(int iBetLen, const string sBetNum, const string sBlkHash)
{
	string rzt="";
	if( (iBetLen  > 0) && (sBetNum.length() > 0) && (sBlkHash.length() > 63) )
	{
		BitBetBossPack bbbp = {0, 0, 0, 0, 0, 0, 0};
		int i = getBetBossNums(sBetNum, bbbp);
		if( i > 5 )
		{
			const char* p = sBlkHash.c_str();
			if( (bbbp.b1 >=0 ) && (bbbp.b1 < 64) ){ rzt = rzt + p[bbbp.b1]; }
			if( (bbbp.b2 >=0 ) && (bbbp.b2 < 64) ){ rzt = rzt + p[bbbp.b2]; }
			if( (bbbp.b3 >=0 ) && (bbbp.b3 < 64) ){ rzt = rzt + p[bbbp.b3]; }
			if( (bbbp.b4 >=0 ) && (bbbp.b4 < 64) ){ rzt = rzt + p[bbbp.b4]; }
			if( (bbbp.b5 >=0 ) && (bbbp.b5 < 64) ){ rzt = rzt + p[bbbp.b5]; }
			if( (bbbp.b6 >=0 ) && (bbbp.b6 < 64) ){ rzt = rzt + p[bbbp.b6]; }
			if( rzt.length() > 0 ) rzt = "0x" + rzt;
		}
		if( fDebug ){ printf("explainBetNums : sBetNum=[%s], getBetBossNums() return [%d], rzt = [%s] \n", sBetNum.c_str(), i, rzt.c_str()); }
	}
	return rzt;
}

bool isWinnerBitBet(const string sGenTx)
{
	bool rzt = false;
	string sql = "select * from AllBets where gen_bet='" + sGenTx + "' and isWinner>0 limit 1;";
	dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, AllBets_opcode_idx, 0, 0, "opcode", ""};
	getOneResultFromDb(dbLuckChainRead2, sql, pack);
	if( pack.fDone > 0 ){ rzt = true; }
	if( fDebug ){ printf("isWinnerBitBet:: rzt = [%d], fDone = [%d],  sql = [%s] \n", rzt, pack.fDone, sql.c_str()); }
	return rzt;
}

static int synAllBitBetCallback(void *data, int argc, char **argv, char **azColName)
{
   if( (data != NULL) && (argc > 23) )
   {
      synAllBitBetPack* p = (synAllBitBetPack *)data;   p->u6RecordCount++;
	  string sTx = argv[AllBets_tx_idx],  sBanker = argv[AllBet_refereeNick_idx];  // tx index = 12
	  int64_t u6TxBlockNum = GetTransactionBlockHeight(sTx);      bool isBankerMode = (sBanker == "1");     //int isWiner = atoi( argv[AllBets_isWinner_idx] );
	  int bid = atoi(argv[0]), iconfirmed = atoi( argv[AllBets_confirmed_idx] ),   iBetType=atoi(argv[AllBets_bet_type_idx]);;  // confirmed index = 17
	  if( fDebug ){ printf("\nsynAllBitBetCallback:: id = [%d], confirmed [%d], sBanker=[%s : %d], tx [%s], u6TxBlockNum=[%s]  \n", bid, iconfirmed, sBanker.c_str(), isBankerMode, sTx.c_str(), u64tostr(u6TxBlockNum).c_str()); }
	  if( u6TxBlockNum > 0 )  // tx exists, is a valid tx
	  {
		  uint64_t u6Confirmed = 0, u6StartBlock = strToInt64(argv[AllBets_sblock_idx]), u6BlockSpace = strToInt64(argv[AllBets_blockspace_idx]);
		  uint64_t u6TargetBlockInDb = strToInt64(argv[AllBets_tblock_idx]),  u6BetNum2 = strToInt64( argv[AllBets_bet_num2_idx] ), u6Rounds = strToInt64( argv[AllBet_rounds_idx] );
		  int iOpcode=atoi(argv[AllBets_opcode_idx]),     iBetLen=atoi(argv[AllBets_bet_len_idx]);
		  //if( u6TxBlockNum != u6StartBlock )
          {
	      uint64_t u6RealTargetBlockNum = u6TargetBlockInDb;
		  if( iOpcode == 1 )  // Launch Bet
		  {
				if( (iBetType == 4) && (u6Rounds > 0) ){   }  // 2016.10.07 add, no winner Lucky Lotto,  u6RealTargetBlockNum = u6TargetBlockInDb
				else{	 u6RealTargetBlockNum = u6TxBlockNum + u6BlockSpace;  }
		  }
		  string sql = "UPDATE AllBets set sblock=" + u64tostr(u6TxBlockNum);
		  if( p->u6CurBlockNum > u6TxBlockNum )
		  {
			 u6Confirmed = p->u6CurBlockNum - u6TxBlockNum + 1;
			 sql = sql + ", confirmed=" + u64tostr(u6Confirmed);
		  }
if( fDebug ){ printf("synAllBitBetCallback:: u6CurBlockNum = [%s], real target block Num[%s], Tx in block [%s], Confirmed = [%s], u6BetNum2=[%s], u6Rounds[%s] \n", u64tostr(p->u6CurBlockNum).c_str(),  u64tostr(u6RealTargetBlockNum).c_str(), u64tostr(u6TxBlockNum).c_str(), u64tostr(u6Confirmed).c_str(), u64tostr(u6BetNum2).c_str(), u64tostr(u6Rounds).c_str()); }

		  if( (!p->bForce)  && (iOpcode == 1) && (iBetType != 5) && (p->u6CurBlockNum > u6RealTargetBlockNum) )
		  {
				uint64_t u6TgBlkConfirmed = p->u6CurBlockNum - u6RealTargetBlockNum;
if( fDebug ){ printf("synAllBitBetCallback :: u6TgBlkConfirmed(%s) :: BitBet_MAX_Confirms(%d), opcode=1 and iBetType=[%d], tx=[%s] \n", u64tostr(u6TgBlkConfirmed).c_str(), BitBet_MAX_Confirms, iBetType, sTx.c_str()); }

				if( (iBetType < 4) && (u6TgBlkConfirmed > BitBet_MAX_Confirms) )  // 2016,10.18 add ++, Lucky 16,  Lucky Odd Even, Lucky Big Small, Lucky Boss
				{
					//if( u6TgBlkConfirmed > BitBet_MAX_Confirms )  // 60, set no winner's bet to done
					{
						if( isWinnerBitBet(sTx) == false ){  // no winner, set done=1
if( fDebug ){ printf("synAllBitBetCallback :: no winner, set tx=[%s] done=1 \n", sTx.c_str()); }
							sql = "UPDATE AllBets set done=1 where gen_bet='" + sTx + "';";     p->vSqls.push_back(sql);     return 0;
						}
					}
					if( fDebug ){ printf("synAllBitBetCallback :: iBetType [%d] < 4, pass !!! \n", iBetType); }
					return 0;
				}  // 2016.10.18 add end

				if( (iBetType == 2) || (iBetType == 4) )   // Lucky Big Small, Lucky Lotto,  2016.10.06 add
				{
if( fDebug ){ printf("synAllBitBetCallback :: Lucky [%d] opcode=1, sBanker=[%s : %d], u6TgBlkConfirmed(%s) :: [%d] \n", iBetType, sBanker.c_str(), isBankerMode, u64tostr(u6TgBlkConfirmed).c_str(), BitBet_Lucky_Lotto_ReBet_Confirms); }
					if( isBankerMode && (u6TgBlkConfirmed >= BitBet_Standard_Confirms) )  // BitBet_Standard_Confirms = 10
					{
						if( isWinnerBitBet(sTx) == false )
						{
							//if( iBetType == 2 )   // 2016.10.15 add, Is Banker mode, set banker is winner
							{
								string sTgBlkHash = GetBlockHashStr(u6RealTargetBlockNum),  sBetNum = argv[AllBets_bet_num_idx];
								string aWinNum = getTgBlkHashWinNumber(sTgBlkHash,  iBetType, iBetLen, isBankerMode, sBetNum);
if( fDebug ){ printf("synAllBitBetCallback :: Lucky [%d] [%s] no winner, set banker is winner, WinNum = [%s] \n", iBetType, sTx.c_str(), aWinNum.c_str()); }
								sql = "UPDATE AllBets set bet_num='" + aWinNum + "', isWinner='1' where tx='" + sTx + "';";
								p->vSqls.push_back(sql);     return 0;
							}
							/*else //if( iBetType == 4 )
							{
									string sTgBlkHash = GetBlockHashStr(u6RealTargetBlockNum);
									string aWinNum = getTgBlkHashWinNumber(sTgBlkHash,  iBetType, iBetLen, isBankerMode, "");
if( fDebug ){ printf("synAllBitBetCallback :: Lucky Lotto [%s] no winner, set banker is winner, WinNum = [%s] \n", sTx.c_str(), aWinNum.c_str()); }
									sql = "UPDATE AllBets set bet_num='" + aWinNum + "', isWinner='1' where tx='" + sTx + "';";
									p->vSqls.push_back(sql);     return 0;
							}*/
						}					
					}

					if( ( !isBankerMode ) && (iBetType == 4) && (u6TgBlkConfirmed > BitBet_Lucky_Lotto_ReBet_Confirms) )  // BitBet_Lucky_Lotto_ReBet_Confirms = 20
					{
						if( isWinnerBitBet(sTx) == false )   // no winner
						{
							//set new target block,  bet_num2 at here is not win flag
							uint64_t u6FstTgBlkNum = u6TxBlockNum + u6BlockSpace;      uint64_t u6FstTgBlkDeep = p->u6CurBlockNum - u6FstTgBlkNum;
							uint64_t u6NextRound = (u6FstTgBlkDeep / (u6BlockSpace + 22)) + 1;     uint64_t u6NewTgBlkNum = u6NextRound * (u6BlockSpace + 22) + u6FstTgBlkNum;
							uint64_t u6NewTgBlk = p->u6CurBlockNum + u6BlockSpace + 1;
if( fDebug ){ printf("synAllBitBetCallback :: Lucky Lotto no winner, set new target block = [%s : %s (u6NewTgBlkNum)] (u6CurBlockNum[%s] + %s + 1), tx=[%s] can rebet \n", u64tostr(u6NewTgBlk).c_str(), u64tostr(u6NewTgBlkNum).c_str(), u64tostr(p->u6CurBlockNum).c_str(), u64tostr(u6BlockSpace).c_str(), sTx.c_str()); }
//if( fDebug ){ printf("synAllBitBetCallback :: Lucky Lotto no winner, u6CurBlockNum=[%s], u6FstTgBlkNum=[%s], u6Rounds = [%s : %s (u6NextRound)]  \n", u64tostr(p->u6CurBlockNum).c_str(), u64tostr(u6FstTgBlkNum).c_str(), u64tostr(u6Rounds).c_str(), u64tostr(u6NextRound).c_str()); }
							sql = "UPDATE AllBets set win_num='-', tblockHash='-', tblock=" + u64tostr(u6NewTgBlkNum) + ", rounds=" + u64tostr(u6NextRound) + ", est_bet_count=0, new_bet_count=0 where gen_bet='" + sTx + "';";
#ifdef QT_GUI
							p->vReBetLuckyLottos.push_back(sTx);
#endif
							p->vSqls.push_back(sql);     return 0;
						}
					}
				}
				/*if( (iBetType < 4) && ( !isBankerMode ) )
				{   //    0, 1, 2, 3,   Lucky 16, Lucky Odd Even, Lucky Big Small, Lucky Boss
					if( u6TgBlkConfirmed > BitBet_MAX_Confirms )  // 60, set no winner's bet to done
					{
						if( isWinnerBitBet(sTx) == false ){  // no winner, set done=1
if( fDebug ){ printf("synAllBitBetCallback :: no winner, set tx=[%s] done=1 \n", sTx.c_str()); }
							sql = "UPDATE AllBets set done=1 where gen_bet='" + sTx + "';";     p->vSqls.push_back(sql);     return 0;
						}
					}
				}*/
		  }

		  
		  if( (iOpcode == 1) && (u6TargetBlockInDb != u6RealTargetBlockNum) ){ sql = sql + ", tblock=" + u64tostr(u6RealTargetBlockNum); }
		  //if( fDebug ){ printf("synAllBitBetCallback:: [%s] \n", sql.c_str()); }
		  /*if( (iBetType == 4) && (u6BetNum2 > 0) )  // 2016.10.08 add
		  {
			  if( fDebug ){ printf("synAllBitBetCallback :: iBetType=[%d] and u6BetNum2=[%s] > 0, not winner, pass \n", iBetType, u64tostr(u6BetNum2).c_str()); }
		  }
		  else */
		  if( p->u6CurBlockNum >= u6RealTargetBlockNum )	// meet target block, up  [ bet_num2 win_num and isWinner ]
          {
			  //if( fDebug ){ printf("synAllBitBetCallback:: meet target block \n"); }
			  string sCurrTgBlkHash = GetBlockHashStr(u6RealTargetBlockNum);
			  //if( fDebug ){ printf("synAllBitBetCallback:: meet target block [%s] \n", sCurrTgBlkHash.c_str()); }
			  string sBetNum = argv[AllBets_bet_num_idx];
			  //uint32_t iBetNum2 = atoi( argv[AllBets_bet_num2_idx] );
			  if( fDebug ){ printf("synAllBitBetCallback:: meet real target block [%s],  sBetNum = [%s] [%s] \n", sCurrTgBlkHash.c_str(), sBetNum.c_str(), u64tostr(u6BetNum2).c_str()); }
              string sTgBlkHashInDb = getTargetBlockHashByAllBetsDB(sTx),   sGenBet = argv[AllBets_gen_bet_idx];
			  if( fDebug ){ printf("synAllBitBetCallback:: sTgBlkHashInDb [%s] [%s] \n", sTgBlkHashInDb.c_str(), sCurrTgBlkHash.c_str()); }
			  if( iBetType < 5 )  // Bet Real Event's result not from block hash;
              {
				  string sWinNum = "-", sIsWiner="0";   //(iOpcode == 1) &&
				  if( (sCurrTgBlkHash.length() > 60) && ((p->bForce) || (sTgBlkHashInDb != sCurrTgBlkHash)) )
				  {
					  if( (iBetType == 0) || (iBetType == 1)  || (iBetType == 2)  || (iBetType == 4) )  // Lucky 16  and Lucky Lottery direct compare bet number
					  {
                          sWinNum = getTgBlkHashWinNumber(sCurrTgBlkHash,  iBetType, iBetLen, isBankerMode, sBetNum);   uint64_t u6BetCount = strToInt64( argv[AllBets_bet_count_idx] );
						  int iStartNum = atoi( argv[AllBets_bet_start_num_idx] );   if( u6BetCount > BitBet_Lucky16_Max_Bet_Count ){ u6BetCount = BitBet_Lucky16_Max_Bet_Count; }
						  //if( fDebug ){ printf("synAllBitBetCallback:: iBetType=[%d}, u6BetCount=[%s], sWinNum=[%s] [%s] \n", iBetType, u64tostr(u6BetCount).c_str(), sWinNum.c_str()); }
						  if( (iBetType == 0) && (u6BetCount > 1) )  // Lucky 16, bet multi times
						  {
								int iWin = isLucky16MultiBetWin(iStartNum, u6BetCount, sWinNum);   sIsWiner = inttostr(iWin);
								//if( fDebug ){ printf("synAllBitBetCallback:: iBetType=0, IsWiner = [%s] \n", sIsWiner.c_str()); }
						  }
						  else{
							if( iBetType == 4 )	{   // bet_num2 at here is not win flag
								if( (u6Rounds == 0) && (sWinNum == sBetNum) ){ sIsWiner = "1"; }
							}
							else if( sWinNum == sBetNum ){ sIsWiner = "1"; }
						  }
						  if( fDebug ){ printf("synAllBitBetCallback:: sBetNum [%s] sWinNum[%s], isWin [%s] \n", sBetNum.c_str(), sWinNum.c_str(), sIsWiner.c_str()); }
					  }else if( iBetType == 3 )   // Lucky Boss,  Biggest
					  {
							string sExpBetNum = explainBetNums(iBetLen, sBetNum, sCurrTgBlkHash);
							//uint64_t u6Num = strToInt64(sExpBetNum.c_str());   uint32_t x = u6Num;
							uint64_t u6Num = strToInt64(sExpBetNum, 16);   uint32_t x = u6Num;
							if( fDebug ){ printf("synAllBitBetCallback:: sExpBetNum [%s :: %s] [0x%X] \n", sExpBetNum.c_str(), u64tostr(u6Num).c_str(), x); }
							//int iWin = isBiggestBetNumber(x, sGenBet);     sIsWiner = inttostr(iWin);
							sql = sql + ", bet_num2=" + u64tostr(u6Num);
					  }
					  sql = sql + ", win_num='" + sWinNum + "', isWinner=" + sIsWiner + ", tblockHash='" + sCurrTgBlkHash + "'";
					  //updateTargetBlockHashToAllBetsDb(sTx, sTgBlkHashInDb, sCurrTgBlkHash);  //updateTargetBlockHashToAllBetsDb(u6RealTargetBlockNum, sTgBlkHashInDb, sCurrTgBlkHash);
				  //}
					//if( p->u6CurBlockNum == u6RealTargetBlockNum )
					{
						if( iBetType == 3 )	// Lucky Boss, Biggest, we need explain all bet nums at first
						{
							p->vNeedExpBets.push_back( make_pair(sTx, sCurrTgBlkHash) );
						}
					}
					//if( p->u6CurBlockNum == (u6RealTargetBlockNum+1) )
					{
						if( iBetType > 0 )
						{
							//string sq20 = "UPDATE AllBets set isWinner=0, confirmed=" + u64tostr(p->u6CurBlockNum) + " - sblock + 1, win_num='" + sWinNum + "', tblockHash='" + sCurrTgBlkHash + "' where gen_bet='" + sTx + "';";
							string sq20 = "UPDATE AllBets set isWinner=0, win_num='" + sWinNum + "', tblockHash='" + sCurrTgBlkHash + "' where gen_bet='" + sTx + "';";
							p->vSqls.push_back( sq20 ); 
						}
						/*if( iBetType == 3 )	// Lucky Boss, Biggest
						{
							string sWinTx = getLuckyBossWinTx( dbLuckChainRead2, sGenBet );
							string sq21 = "UPDATE AllBets set isWinner=1 where tx='" + sWinTx + "' and gen_bet='" + sTx + "';";      p->vSqls.push_back(sq21);    // return 0;
						}*/
						if( iBetType > 0 )  // 1, 2, 4
						{
							string sq2="";
							if( iBetType == 4 ){ sq2 = "UPDATE AllBets set isWinner=1 where rounds<1 and bet_num='" + sWinNum + "' and gen_bet='" + sTx + "';"; }
							else{ sq2 = "UPDATE AllBets set isWinner=1 where bet_num='" + sWinNum + "' and bet_num!='-' and gen_bet='" + sTx + "';"; }
							p->vSqls.push_back(sq2);     //return 0;
						}
					}
				 }
			  }else if( iBetType == 5 )  // Bet Real Event
			  {
				if( iOpcode == 1 )  // Genesis Bet
				{
					/*if( sTgBlkHashInDb != sCurrTgBlkHash )
					{
					    string sq20 = "UPDATE AllBets set confirmed=" + u64tostr(p->u6CurBlockNum) + " - sblock + 1, tblockHash='" + sCurrTgBlkHash + "' where gen_bet='" + sTx + "';";
						//string sq20 = "UPDATE AllBets set confirmed=" + u64tostr(p->u6CurBlockNum) + " - sblock + 1 where gen_bet='" + sTx + "';";
						p->vSqls.push_back( sq20 ); 
					}*/

					// bet_num2 iBetNum2 at this game is referee decide tx's Confirms
					string sRefereeDecideTx = argv[AllBets_refereeDecideTx_idx];
					if( fDebug ){ printf("synAllBitBetCallback:: Bet Real Event, Decide tx [%s] \n", sRefereeDecideTx.c_str()); }
					if( sRefereeDecideTx.length() > 60 )
					{
						int64_t u6DecideTxBlockNum = GetTransactionBlockHeight(sRefereeDecideTx);
						if( fDebug ){ printf("synAllBitBetCallback:: u6CurBlockNum=[%s], u6DecideTxBlockNum=[%s] \n", u64tostr(p->u6CurBlockNum).c_str(), u64tostr(u6DecideTxBlockNum).c_str()); }
						if( p->u6CurBlockNum > u6DecideTxBlockNum )
						{
							u6Confirmed = p->u6CurBlockNum - u6DecideTxBlockNum + 1;
							//sql = sql + ", bet_num2=" + u64tostr(u6Confirmed);
							string sq3 = "UPDATE AllBets set bet_num2=" + u64tostr(u6Confirmed) + " where gen_bet='" + sTx + "';";
							p->vSqls.push_back(sq3);
						}
					}
			  }
			 }
		  }
		  if( (iOpcode == 1) && ( p->bForce || (p->u6CurBlockNum == (u6RealTargetBlockNum + BitBet_Standard_Confirms - 1))) )
		  {
				string sq110 = "UPDATE AllBets set confirmed=(" + u64tostr(p->u6CurBlockNum) + " - sblock + 1) where gen_bet='" + sTx + "';";
				p->vSqls.push_back( sq110 ); 
		  }

		  if( sql.length() > 10 ){ sql = sql + " where tx='" + sTx + "';";     p->vSqls.push_back(sql); }
		  /* char* pe=0;   // there can't do sql write, else will endless loop
	      int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), 0, 0, &pe);
		  if( fDebug ){ printf("synAllBitBetCallback:: rc = [%d] [%s], sql = [%s]\n", rc, pe, sql.c_str()); }
		  if( pe != NULL ){ sqlite3_free(pe); } */
		  }
	  }
	  /*else if( iconfirmed > 0 ){  // 2016.10.19 add,   forked ?  delete all of this bet
			if( sTx.length() > 50 )
			{
				string sq1019 = "DELETE FROM AllBets where gen_bet='" + sTx + "';";
				p->vSqls.push_back( sq1019 ); 
			}
	  }*/
   }
   return 0;
}

int deleteBitBetTx(const string tx)
{
	string sql = "DELETE FROM AllBets where gen_bet='" + tx + "' and confirmed<1 and opcode=1;";
	int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
	if( fDebug ){ printf("deleteBitBetTx [%s] return [%d] \n", sql.c_str(), rc); }
	return rc;
}

int setPlayerInfo(const CTransaction& tx, const BitBetPack bbp, int opc)
{
	int rzt = 0;      string sPlayer = bbp.bettor;      uint64_t u6Coins = bbp.u6BetAmount;
	if( iRecordPlayerInfo > 0)
	{
		char* pe = 0;     string sql="";      bool bSetWin = (bbp.opCode == 3);
		if( fDebug ){  printf("setPlayerInfo: opCode=[%d], opc=[%d], bSetWin=[%d] [%s] \n", bbp.opCode, opc, bSetWin, sPlayer.c_str());  }
		if( bSetWin )  // set win coins and win times
		{
			std::vector<txOutPairPack > txOutPair;       txOutPair.resize(0);
			int iOpCount = GetTxOutDetails(tx.vout, txOutPair);
			if( fDebug ){  printf("setPlayerInfo: bSetWin=true, GetTxOutDetails()=[%d] \n", iOpCount);  }
			if( iOpCount > 0 )   // if( (iOpCount > 0) && (opc > 0) )
			{
				for(int i=0; i<iOpCount; i++)
				{
					txOutPairPack &op = txOutPair[i];   string s = "UPDATE Users set totalWinCoins=(totalWinCoins";
					if( opc > 0 ){ s = s + "+"; }else{ s = s + "-"; }
					s = s + u64tostr(op.v_nValue / COIN) + "), totalWinCount=(totalWinCount";
					if( opc > 0 ){ s = s + "+"; }else{ s = s + "-"; }
					s = s + "1) where coinAddr='" + op.sAddr + "';";
					/*if( opc > 0 )   // add
					{
						s = "UPDATE Users set totalWinCoins=(totalWinCoins+" + u64tostr(op.v_nValue / COIN) + "), totalWinCount=(totalWinCount+1) where coinAddr='" + op.sAddr + "';";
					}else{   // dec
						s = "UPDATE Users set totalWinCoins=(totalWinCoins-" + u64tostr(op.v_nValue / COIN) + "), totalWinCount=(totalWinCount-1) where coinAddr='" + op.sAddr + "';";
					}*/
					rzt = sqlite3_exec(dbLuckChainWrite, s.c_str(), NULL, NULL, NULL);
					if( fDebug ){  printf("setPlayerInfo: i=[%d] rzt=[%d], [%s] \n", i, rzt, s.c_str());  }
					
					//string s2 = "select * from AllBets where bettor='" + op.sAddr +  "' and gen_bet='" + bbp.genBet + "' limit 1;";
					string s3 = "where bettor='" + op.sAddr + "' and gen_bet='" + bbp.genBet + "'";
					s = "UPDATE AllBets set winCoins=(winCoins";
					if( opc > 0 ){ s = s + "+"; }else{ s = s + "-"; }
					s = s + u64tostr(op.v_nValue / COIN) + "), winCount=(winCount";
					if( opc > 0 ){ s = s + "+"; }else{ s = s + "-"; }
					s = s + "1) " + s3 + " and firstOfThisAddr>0;";   //s = s + "1) " + s3 + " and tx=(select tx from AllBets " + s3 + " limit 1);";*/
					rzt = sqlite3_exec(dbLuckChainWrite, s.c_str(), NULL, NULL, &pe);
					if( fDebug ){  printf("setPlayerInfo: (UPDATE AllBets set winCoins...) i=[%d] rzt=[%d], [%s] [%s] \n", i, rzt, s.c_str(), pe);  }
					if( pe ){ sqlite3_free(pe);    pe=0; }
				}
			}
		}else{
			if( opc > 0 )   // add
			{
				sql = "INSERT INTO Users (coinAddr, nickName, totalBetCoins, totalWinCoins, totalBetCount, totalWinCount, nTime, sign) VALUES ('" + sPlayer + "', '-', " + u64tostr(u6Coins) + ", 0, 1, 0, " + inttostr(tx.nTime) + ", '-');"; 
				rzt = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
				if( fDebug ){  printf("setPlayerInfo: bSetWin=false, opc > 0, rzt=[%d] [%s] \n", rzt, sql.c_str());  }
				if( rzt != SQLITE_OK )
				{
					sql= "UPDATE Users set totalBetCoins=(totalBetCoins+" + u64tostr(u6Coins) + "), totalBetCount=(totalBetCount+1), nTime=" + inttostr(tx.nTime) + " where coinAddr='" + sPlayer + "';";
				}else{ sql = ""; }
			}else{   // dec
				sql= "UPDATE Users set totalBetCoins=(totalBetCoins-" + u64tostr(u6Coins) + "), totalBetCount=(totalBetCount-1) where coinAddr='" + sPlayer + "';";
				if( bbp.opCode == 2 )
				{
					/*string sq2 = "SELECT * from AllBets where gen_bet='" + bbp.genBet + "' and bettor='" + sPlayer + "' ;";
					dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, AllBets_opcode_idx, 0, 0, "opcode", ""};
					if( bbp.opCode < 3 ){  getOneResultFromDb(dbLuckChainWrite, sql, pack);      iOpCode = pack.u6Rzt;  } */
					pe=0;	  string sq2 = "UPDATE AllBets set totalOfThisAddrBetCount=(totalOfThisAddrBetCount-1), totalOfThisAddrCoins=(totalOfThisAddrCoins-" + u64tostr(u6Coins) + ")  where gen_bet='" + bbp.genBet + "' and bettor='" + sPlayer + "' and firstOfThisAddr>0;";
					int rz2 = sqlite3_exec(dbLuckChainWrite, sq2.c_str(), NULL, NULL, &pe);
					if( fDebug ){  printf("setPlayerInfo: (UPDATE AllBets set totalOfThisAddrBetCount...)rz2=[%d], [%s] [%s] \n", rz2, sq2.c_str(), pe);  }
					if( pe ){ sqlite3_free(pe);    pe=0; }
				}
			}
		}
		if( sql.length() > 10 )
		{
			rzt = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, &pe);
		}
		if( fDebug ){ printf("setPlayerInfo :: opc=[%d], bSetWin=[%d], rzt=[%d] [%s], sql=[%s] \n", opc, bSetWin, rzt, pe, sql.c_str()); }
		if( pe ){ sqlite3_free(pe);    pe=0; }
	}
	return rzt;
}

bool disconnectBitBet(const CTransaction& tx)
{
   bool rzt = false;
   string sTx = tx.GetHash().ToString();
	BitBetPack bbp = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "", "", "", "", "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 0, 0};
	int bbpRzt = GetTxBitBetParam(tx, bbp);
   if( bbpRzt >= 14 )
   {
        LOCK(cs_bitbet);  // 2017.04.30
		int iOpCode = 0;      string sql = "SELECT * from AllBets where tx='" + sTx + "';";
		dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, AllBets_opcode_idx, 0, 0, "opcode", ""};
	   if( bbp.opCode < 3 ){  getOneResultFromDb(dbLuckChainWrite, sql, pack);      iOpCode = pack.u6Rzt;  }
	   else if( (bbp.opCode == 3) && (bbpRzt > 15) ){ iOpCode = 3;  }

   //if( fDebug ){ printf("disconnectBitBet(%s), fDone = [%d], opcode = [%d]  \n", sTx.c_str(), pack.fDone, iOpCode); }
   if( (iOpCode == 3) || (pack.fDone > 0) )
   {
		if( fDebug ){ printf("disconnectBitBet(),  opcode = [%d], bbpRzt=[%d], betType=[%d], sql=[%s] \n", iOpCode, bbpRzt, bbp.betType, sql.c_str()); }  // bet_type
		rzt = true;      string sGenBet = "";      setPlayerInfo(tx, bbp, 0);
		if( iOpCode == 1 )  // Launch Bet
		{
			sGenBet = sTx;      sql = "DELETE FROM AllBets where gen_bet='" + sTx + "';";
			if( fDebug ){ printf("disconnectBitBet(), opcode=1, [%s]  \n", sql.c_str()); }
		}
		else if( iOpCode == 2 ){  // Join Bet
			sGenBet = bbp.genBet;      dbOneResultCallbackPack pac2 = {OneResultPack_U64_TYPE, AllBets_bet_amount_idx, 0, 0, "bet_amount", ""};      pack = pac2;
			getOneResultFromDb(dbLuckChainWrite, sql, pack);
			if( fDebug ){ printf("disconnectBitBet(), opcode=2, [%s] fDone [%d] \n", sql.c_str(), pack.fDone); }
			if( pack.fDone > 0 )
			{
				uint64_t u6BetAmount = pack.u6Rzt;      
				/*pack = {OneResultPack_STR_TYPE, AllBets_gen_bet_idx, 0, 0, "gen_bet", ""};
				getOneResultFromDb(dbLuckChainWrite, sql, pack);      sGenBet = pack.sRzt;  */
				if( fDebug ){ printf("disconnectBitBet(), opcode=2, u6BetAmount = [%s],  gen_bet = [%s], sql = [%s] \n", u64tostr(u6BetAmount).c_str(), sGenBet.c_str(), sql.c_str()); }
				if( sGenBet.length() > 50 )
				{
					sql = "UPDATE AllBets set bet_count=(bet_count - 1), new_bet_count=(new_bet_count-1), est_bet_count=(bet_count - 1), total_bet_amount=(total_bet_amount - " + u64tostr(u6BetAmount) + ") where tx='" + sGenBet + "' and opcode=1;";
					int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
					if( fDebug ){ printf("disconnectBitBet(), opcode=2, update gen_bet(%s) = [%d], sql = [%s] \n", sGenBet.c_str(), rc, sql.c_str()); }
					sql = "DELETE FROM AllBets where tx='" + sTx + "';";
				}
			}
		}
		else if( iOpCode == 3 )   // Encash, 2016.11.12 add
		{
			if( bbpRzt > 15 )  // isBitBetEncashTx()
			{
				sGenBet = bbp.genBet;
				sql = "UPDATE AllBets set done=0, encashTx='-' where gen_bet='" + sGenBet + "';";  // and opcode=1;";
				//if( fDebug ){ printf("disconnectBitBet(), opcode=3, [%s]  \n", sql.c_str()); }
			}
		}
		else{  sql = "";  }

		if( sql.length() > 10 ){
			int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
			if( fDebug ){ printf("disconnectBitBet(), sql.length() > 10 = [%s], rc = [%d] \n", sql.c_str(), rc); }
		}
#ifdef QT_GUI
		if( sGenBet.length() > 50 ){
			if( bbp.betType > 0 )  // not lucky 16
			{
				BitBetPack bbz = {0, iOpCode, bbp.betType, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "", sGenBet, "", "", "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 1, 0};      NotifyReceiveNewBitBetMsg(bbz);
			}
		}
#endif
   } }
   return rzt;
}

struct syncLuckyBossPack
{
   //uint64_t u6CurBlockNum;
   uint64_t u6RecordCount;
   string sCurrTgBlkHash;
   std::vector<std::string> vSqls;
};

void dbLuckChainWriteSqlBegin(int bStart)
{
	if( bStart  > 0 )
	{
		if( bStart == 2 ){ sqlite3_exec( dbLuckChainWrite, "ROLLBACK", 0, 0, 0); }
		else{  sqlite3_exec( dbLuckChainWrite, "BEGIN", 0, 0, 0);  }
	}
	else{ sqlite3_exec( dbLuckChainWrite, "COMMIT", 0, 0, 0); }
}
bool syncAllBitBets(uint64_t nHeight, bool vForce)
{
    uint64_t u6Time = 0;   if( fDebug ){ u6Time = GetTimeMillis(); }
   bool rzt = false;
    LOCK(cs_bitbet);  // 2017.04.30
   //string sql = "SELECT * from AllBets where done=0 and confirmed<" + inttostr(BitBetBeginEndBlockSpace_10) + ";";
   string sql = "SELECT * from AllBets where opcode=1 and done=0; ";
   synAllBitBetPack abbp;  // = {1, nBestHeight, 0};
   abbp.iStep = 1;    abbp.bForce = vForce;   abbp.u6CurBlockNum = nHeight;   abbp.u6RecordCount = 0;   abbp.vSqls.clear();    abbp.vNeedExpBets.clear();   abbp.vReBetLuckyLottos.clear();
   int rc = sqlite3_exec(dbLuckChainRead, sql.c_str(), synAllBitBetCallback, (void*)&abbp, NULL);
   char* pe=0;      //rzt = abbp.u6RecordCount;

dbLuckChainWriteSqlBegin( 1 );
   int isz = abbp.vNeedExpBets.size();
   if( isz > 0 )
   {
		if( fDebug ){ printf("synAllBitBetCallback NeedExpBets count = [%d] > 0 \n", isz); }
		BOOST_FOREACH(const PAIRTYPE(std::string, std::string)& item, abbp.vNeedExpBets){   //for (unsigned int nStr = 0; nStr < isz; nStr++) {
			string sLuckyBossGenBet = item.first;    //abbp.vNeedExpBets[ nStr ];
			sql = "SELECT * from AllBets where bet_type=3 and done=0 and gen_bet='" + sLuckyBossGenBet + "';";
			syncLuckyBossPack slbp;      slbp.u6RecordCount = 0;   slbp.vSqls.clear();     //slbp.u6CurBlockNum = nBestHeight;
			slbp.sCurrTgBlkHash = item.second;    //GetBlockHashStr(slbp.u6CurBlockNum);
			int rc3 = sqlite3_exec(dbLuckChainWrite, sql.c_str(), synLuckyBossCallback, (void*)&slbp, NULL);
			
			for (unsigned int ie = 0; ie < slbp.vSqls.size(); ie++) { 
				string sq2 = slbp.vSqls[ ie ];   pe = NULL;
				int re = sqlite3_exec(dbLuckChainWrite, sq2.c_str(), 0, 0, &pe);
				if( fDebug ){ printf("synAllBitBetCallback for NeedExpBets:: rc = [%d] [%s], sq2 = [%s]\n", re, pe, sq2.c_str()); }
				if( pe != NULL ){ sqlite3_free(pe); }
			}
			slbp.vSqls.resize(0);
		}
   }
dbLuckChainWriteSqlBegin( 0 );

dbLuckChainWriteSqlBegin( 1 );
   for (unsigned int nStr = 0; nStr < abbp.vSqls.size(); nStr++) { 
		  string sq3 = abbp.vSqls[nStr];   pe = NULL;   // if (vstr[nStr] == "change=1")
	      int r = sqlite3_exec(dbLuckChainWrite, sq3.c_str(), 0, 0, &pe);
		  if( fDebug ){ printf("synAllBitBetCallback:: rc = [%d] [%s], sq3 = [%s]\n", r, pe, sq3.c_str()); }
		  if( pe != NULL ){ sqlite3_free(pe); }
   }
dbLuckChainWriteSqlBegin( 0 );
   abbp.vSqls.resize(0);

   isz = abbp.vNeedExpBets.size();  // Total Lucky Boss Winners
   if( isz > 0 )
   {
dbLuckChainWriteSqlBegin( 1 );
		BOOST_FOREACH(const PAIRTYPE(std::string, std::string)& item, abbp.vNeedExpBets){   //for (unsigned int nStr = 0; nStr < isz; nStr++) {
			string sLuckyBossGenBet = item.first;    //abbp.vNeedExpBets[ nStr ];
			//if( iBetType == 3 )	// Lucky Boss, Biggest
			{
				//uint64_t u6Num = strToInt64( argv[AllBets_bet_num2_idx] );
				/* int iWin = isBiggestBetNumber(sGenBet, sTx);     sIsWiner = inttostr(iWin);
					sql = sql + ", isWinner=" + sIsWiner; */
				string sWinTx = getLuckyBossWinTx( dbLuckChainWrite, sLuckyBossGenBet );
				string sq21 = "UPDATE AllBets set isWinner=1 where tx='" + sWinTx + "' and gen_bet='" + sLuckyBossGenBet + "';";
				int rw = sqlite3_exec(dbLuckChainWrite, sq21.c_str(), 0, 0, 0);
				if( fDebug ){ printf("synAllBitBetCallback for set NeedExpBets Winner :: rw = [%d], sq21 = [%s]\n", rw, sq21.c_str()); }
			}
		}
dbLuckChainWriteSqlBegin( 0 );
		abbp.vNeedExpBets.resize(0);
   }

#ifdef QT_GUI
   isz = abbp.vReBetLuckyLottos.size();   // 2016.10.06 add, sync rebet lucky lotto to gui
   if( isz > 0 )
   {
		BitBetPack bbp = {0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "", "", "", "", "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 0, 0};      NotifyReceiveNewBitBetMsg(bbp);   //for(i=0; i++; i<isz)
   }
   abbp.vReBetLuckyLottos.resize(0);
//    NotifyReceiveNewBlockMsg(nHeight);
#endif
    uint64_t u6Time2 = 0;     if( fDebug ){ u6Time2 = GetTimeMillis() - u6Time; }
    rzt = (rc == SQLITE_OK);   //if( rc == SQLITE_OK )

    if( fDebug ){ printf("syncAllBitBets:: rzt [%d], record count = [%s] rc=[%d], used time [%s]  \n", rzt, u64tostr(abbp.u6RecordCount).c_str(), rc, u64tostr(u6Time2).c_str()); }
    return rzt;
}

void notifyReceiveNewBlockMsg(uint64_t nHeight, uint64_t nTime)
{
	NotifyReceiveNewBlockMsg(nHeight, nTime);
}

static int synLuckyBossCallback(void *data, int argc, char **argv, char **azColName)
{
   if( (data != NULL) && (argc > 23) )
   {
		syncLuckyBossPack* p = (syncLuckyBossPack *)data;   p->u6RecordCount++;
		string sTx = argv[AllBets_tx_idx];  // tx index = 12
		int64_t u6TxBlockNum = GetTransactionBlockHeight(sTx);
		int bid = atoi(argv[0]), iconfirmed = atoi( argv[AllBets_confirmed_idx] );  // confirmed index = 17
		if( fDebug ){ printf("synLuckyBossCallback:: id = [%d], confirmed [%d] tx [%s] [%s] \n", bid, iconfirmed, sTx.c_str(), u64tostr(u6TxBlockNum).c_str()); }
		if( u6TxBlockNum > 0 )  // tx exists, is a valid tx
		{
			//uint64_t u6Confirmed = 0, u6StartBlock = strToInt64(argv[AllBets_sblock_idx]), u6BlockSpace = strToInt64(argv[AllBets_blockspace_idx]);
			//uint64_t u6TargetBlockInDb = strToInt64(argv[AllBets_tblock_idx]),  u6BetNum2 = strToInt64( argv[AllBets_bet_num2_idx] );
			string sBetNum = argv[AllBets_bet_num_idx];
			int iBetType=atoi(argv[AllBets_bet_type_idx]),     iBetLen=atoi(argv[AllBets_bet_len_idx]);

			string sExpBetNum = explainBetNums(iBetLen, sBetNum, p->sCurrTgBlkHash);
			uint64_t u6Num = strToInt64(sExpBetNum, 16);   uint32_t x = u6Num;
			if( fDebug ){ printf("synLuckyBossCallback:: sExpBetNum [%s :: %s] [0x%X] \n", sExpBetNum.c_str(), u64tostr(u6Num).c_str(), x); }
			//int iWin = isBiggestBetNumber(x, sGenBet);     sIsWiner = inttostr(iWin);
			string sql = "UPDATE AllBets set sblock=" + u64tostr(u6TxBlockNum);
			sql = sql + ", bet_num2=" + u64tostr(u6Num);
			sql = sql + " where tx='" + sTx + "';";     p->vSqls.push_back(sql);
		}
   }
   return 0;
}

/*struct dbBitBetTotalAmountAndWinnerPack{
   uint64_t u6RecordCount, u6AllBetCoins,  u6WinnerCount, u6AllWinerBetCoins;
   std::vector<txOutPairPack > allWiners;
};*/
static int getBitBetTotalBetsAndMoreCallback(void *data, int argc, char **argv, char **azColName)
{
	int rzt=0;
	if( (data != NULL) && (argc > 25) )
	{
		dbBitBetTotalAmountAndWinnerPack* p = (dbBitBetTotalAmountAndWinnerPack *)data;
		int betType = atoi( argv[AllBets_bet_type_idx] );      int isWiner = atoi( argv[AllBets_isWinner_idx] );
		uint64_t u6BetAmount = strToInt64( argv[AllBets_bet_amount_idx] );
		if( betType == 0 )   // Lucky 16
		{
			uint64_t u6BetCount = strToInt64( argv[AllBets_bet_count_idx] );
			if( u6BetCount < 1 ){ u6BetCount = 1; }
			else if( u6BetCount > BitBet_Lucky16_Max_Bet_Count ){ u6BetCount = BitBet_Lucky16_Max_Bet_Count; }
			//if( u6BetCount > 1 ){ u6BetAmount = u6BetAmount * u6BetCount; }
			p->u6AllBetCoins =  p->u6AllBetCoins + (u6BetAmount * u6BetCount);
		}
		else{ p->u6AllBetCoins =  p->u6AllBetCoins + u6BetAmount; }
		if( isWiner > 0 )
		{
			p->u6WinnerCount++;      
			if( betType == 0 )   // Lucky 16
			{
				p->u6AllWinerBetCoins =  p->u6AllBetCoins;      //rzt++;
			}
			else{ p->u6AllWinerBetCoins =  p->u6AllWinerBetCoins + u6BetAmount; }
			string sAddr = argv[AllBets_bettor_idx];
			txOutPairPack opp = {p->u6RecordCount, u6BetAmount, sAddr};
			p->allWiners.push_back(opp);
		}
		p->u6RecordCount++;
	}
	return rzt;  // 0
}
unsigned int getBitBetTotalBetsAndMore(sqlite3 *dbOne, const string genBet, dbBitBetTotalAmountAndWinnerPack& pack)
{
	unsigned int rzt=0;      char* pe=0;
	string sql = "SELECT * from AllBets where gen_bet='" + genBet + "' and confirmed>=" + inttostr(BitBet_Standard_Confirms - 1) + ";";
	int rc = sqlite3_exec(dbOne, sql.c_str(), getBitBetTotalBetsAndMoreCallback, (void*)&pack, &pe);
	rzt = pack.u6RecordCount;
	if( fDebug ){ printf("getBitBetTotalBetsAndMore:: rzt = [%d], rc = [%d] [%s], sql = [%s]\n", rzt, rc, pe, sql.c_str()); }
	if( pe != NULL ){ sqlite3_free(pe); }
	return rzt;
}
unsigned int getBitBetTotalBetsAndMore(const string genBet, dbBitBetTotalAmountAndWinnerPack& pack)
{
    return getBitBetTotalBetsAndMore(dbLuckChainRead, genBet, pack);
}

int  GetCoinAddrInTxOutAmount(const CTransaction& tx, const string sAddr, uint64_t& v_nValue)
{
	int rzt = 0;   v_nValue = 0;
	//if( IsFinalTx(tx, nBestHeight + 1) )
	{
		//BOOST_FOREACH(const CTxOut& txout, tx.vout) 	
		for (unsigned int i = 0; i < tx.vout.size(); i++)
		{
			const CTxOut& txout = tx.vout[i];
			//bool bOk = false;
			//if( v_nValue == 0 ){ bOk = true; }	 // = 0 mean Ignore nValue param
			//if( bOk ) //if( txout.nValue == v_nValue )
			{	
				txnouttype type;
				vector<CTxDestination> addresses;
				int nRequired;
				if( ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired) )
				{
					BOOST_FOREACH(const CTxDestination& addr, addresses)
					{
						string sAa = CBitcoinAddress(addr).ToString();
						if( sAa == sAddr )
						{ 
							v_nValue = v_nValue + txout.nValue;   rzt++;
						}
					}
				}
			}
		}
	}
	return rzt;
}

int GetTxOutBurnedCoins(const std::vector<CTxOut> &vout, int64_t& u6Rzt, bool bZeroFirt=true)
{
	int rzt = 0;      if( bZeroFirt ){ u6Rzt = 0; }
	int j = vout.size();
	for( int i = 0; i < j; i++)  //BOOST_FOREACH(const CTxOut& txout, tx.vout)
	{
		const CTxOut& txout = vout[i];
		txnouttype type;      vector<CTxDestination> addresses;      int nRequired;
		if( ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired) )
		{
			BOOST_FOREACH(const CTxDestination& addr, addresses)
			{
				string sAa = CBitcoinAddress(addr).ToString();
				if( sAa == BitBetBurnAddress ){
					rzt++;   u6Rzt += txout.nValue;
				}
			}
		}
	}
	return rzt;
}

//std::vector<std::pair<unsigned int, WORD> > vAllNodeIpPorts;	// 2015.06.06 add
int  GetTxOutDetails(const std::vector<CTxOut> &vout, std::vector<txOutPairPack > &outPair)
{
	int rzt = 0;
	
	int j = vout.size();
	for( int i = 0; i < j; i++)  //BOOST_FOREACH(const CTxOut& txout, tx.vout)
	{
		const CTxOut& txout = vout[i];
		txnouttype type;
		vector<CTxDestination> addresses;
		int nRequired;      txOutPairPack p = {i, txout.nValue, ""};
		if( ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired) )
		{
			BOOST_FOREACH(const CTxDestination& addr, addresses)
			{
				string sAa = CBitcoinAddress(addr).ToString();   p.sAddr = sAa;
				break;
			}
		}
		outPair.push_back(p);   rzt++;
	}
	return rzt;
}

int  GetTxOutDetails(const CTransaction& tx, std::vector<txOutPairPack > &outPair)
{
	int rzt = 0;
	//if( IsFinalTx(tx, nBestHeight + 1) )
	{
		rzt = GetTxOutDetails(tx.vout, outPair);
	}
	return rzt;
}

bool isTxOutToThisAddress(const CTxOut txout, const std::string sAddress)
{
	bool rzt=false;
	{
		txnouttype type;
		vector<CTxDestination> addresses;
		int nRequired;
		if( ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired) )
		{
			BOOST_FOREACH(const CTxDestination& addr, addresses)
			{
				string sAa = CBitcoinAddress(addr).ToString();
				if( sAddress.find(sAa) != string::npos ){ rzt = true;   break; }   //if( sAa == sAddress )
			}
		}
	}
	return rzt;
}

bool isCanntSpendAddress(const CTxOut txout)
{
	bool rzt=false;      std::string sAddr = GetArg("-canntspendaddress", "");
	if( sAddr.length() >= 34 ){ rzt = isTxOutToThisAddress(txout, sAddr); }
	return rzt;
}

bool GetTxOutCoinAddrAndAmoutByOutId(const string sCallFrom, const CTransaction& tx, int outId, string& sRztAddr, uint64_t& nRztValue)
{
	bool rzt = false;     int iOutSz = tx.vout.size();
	if( iOutSz > outId )  //if( IsFinalTx(tx, nBestHeight + 1) )
	{
		const CTxOut& txout = tx.vout[outId];     nRztValue = txout.nValue;
		txnouttype type;     vector<CTxDestination> addresses;     int nRequired;
		if( ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired) )
		{
			BOOST_FOREACH(const CTxDestination& addr, addresses)
			{
				sRztAddr = CBitcoinAddress(addr).ToString();     rzt = true;
			}
		}
	}
	if( fDebug ){ printf("%s Call GetTxOutCoinAddrAndAmoutByOutId() :: rzt=[%d], outId=[%d], tx.vout.size()=[%d], nRztValue=[%f], sRztAddr=[%s], tx=[%s]\n", sCallFrom.c_str(), rzt, outId, iOutSz, (double)(nRztValue / COIN), sRztAddr.c_str(), tx.GetHash().ToString().c_str()); }
	return rzt;
}

int  GetCoinAddrInTxOutIndex(const CTransaction& tx, string sAddr, uint64_t v_nValue, int iCmpType)
{
	int rzt = -1;
	//if( IsFinalTx(tx, nBestHeight + 1) )
	{
		//BOOST_FOREACH(const CTxOut& txout, tx.vout) 	
		for (unsigned int i = 0; i < tx.vout.size(); i++)
		{
			const CTxOut& txout = tx.vout[i];
			bool bOk = false;
			if( v_nValue == 0 ){ bOk = true; }	 // = 0 mean Ignore nValue param
			else
			{
				if( iCmpType == 0 ){ if( txout.nValue == v_nValue ){ bOk = true; } }		// equ
				else if( iCmpType == 1 ){ if( txout.nValue < v_nValue ){ bOk = true; } }	// less
				else if( iCmpType == 2 ){ if( txout.nValue > v_nValue ){ bOk = true; } }	// big
				else if( iCmpType == 3 ){ if( txout.nValue >= v_nValue ){ bOk = true; } }	// equ or big
			}
		
			if( bOk ) //if( txout.nValue == v_nValue )
			{	
				txnouttype type;
				vector<CTxDestination> addresses;
				int nRequired;
				if( ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired) )
				{
					BOOST_FOREACH(const CTxDestination& addr, addresses)
					{
						string sAa = CBitcoinAddress(addr).ToString();
						if( sAa == sAddr ){ return i; }
					}
				}
			}
		}
	}
	return rzt;
}
int  GetCoinAddrInTxOutIndex(const string txID, string sAddr, uint64_t v_nValue, int iCmpType)
{
	int rzt = -1;
	if( txID.length() > 34 )
	{
		//string srzt = "";
		uint256 hash;
		hash.SetHex(txID);

		CTransaction tx;
		uint256 hashBlock = 0;
		if (!GetTransaction(hash, tx, hashBlock))
			return rzt;
		//if( hashBlock > 0 )
		{
			rzt = GetCoinAddrInTxOutIndex(tx, sAddr, v_nValue, iCmpType);
		}
	}
	return rzt;
}

int SplitCmdParamFromStr(const std::string betStr, BitBetCommand &bbc, const string cmdHeader, char *delim)
{
	int rzt = 0;
	string stxData = "";
	int iLen = betStr.length();   if( iLen > MAX_BitBet_Str_Param_Len ){ return rzt; }
	if( betStr.length() > 0 ){ stxData = betStr.c_str(); }
	if( (stxData.length() > 13) && (stxData.find(cmdHeader) == 0) )   //  BitBet_CMD_Magic = "BitBetCMD:"
	{
		int i = 0;
		try{
		//char *delim = "|";    //double dv = 0;
					
		char * pp = (char *)stxData.c_str();
        char *reserve;
		char *pch = strtok_r(pp, delim, &reserve);
		while (pch != NULL)
		{
			i++;
			//if( fDebug ){ printf("SplitCmdParamFromStr:: [%s] \n", pch); }
			if( i == 2 ){ bbc.cmdName = pch; }
			else if( i == 3 ){ bbc.p1 = pch; }
			else if( i == 4 ){ bbc.p2 = pch; }
			else if( i == 5 ){ bbc.p3 = pch; }
			else if( i == 6 ){ bbc.p4 = pch; }
			else if( i == 7 ){ bbc.p5 = pch; }
			else if( i == 8 ){ bbc.p6 = pch; }
			else if( i == 9 ){ bbc.p7 = pch; }
			else if( i == 10 ){ bbc.p8 = pch; }
			else if( i == 11 ){ bbc.p9 = pch;  }
			else if( i == 12 ){ bbc.pa = pch;  }
			else if( i > 29 ){ break; }
			//if( fDebug ){ printf ("%s, %d\n", pch, i); }
			pch = strtok_r(NULL, delim, &reserve);
		}
		}catch (std::exception &e) {
			printf("SplitCmdParamFromStr:: err [%s]\n", e.what());
		}
		rzt = i;
	}
	bbc.paramCount = rzt;    return rzt;
}

int GetTxBitBetCmdParamFromStr(const std::string betStr, BitBetCommand &bbc)
{
    return SplitCmdParamFromStr(betStr, bbc, BitBet_CMD_Magic, "|");
}
int unsigatoi(const char* s)
{
	int rzt = atoi(s);
	if( rzt < 0 ){  rzt = 0;  }
	return rzt;
}
int GetTxBitBetParamFromStr(const std::string betStr, BitBetPack &bbp)
{
	int rzt = 0;
	string stxData = "";
	int iLen = betStr.length();   if( iLen > MAX_BitBet_Str_Param_Len ){ return rzt; }
	if( betStr.length() > 0 ){ stxData = betStr.c_str(); }
	//if( fDebug ){ printf("GetTxMsgParam: tx Msg = [%s] \n", stxData.c_str()); }
// BitBet Magic | Lottery ID | Opcode( Create = 1, Bet = 2, Cash = 3 ) | Bet Type | Amount | Mini Bet | Start block | Target block | Guess HASH Len | Guess Txt | Lottery wallet address | Lottery wallet PrivKey | Def_WalletAddress | Lottery Tx ID Str ( If it's Bet tx ) | SignMsg ( if it's Cash tx )
    //memset(&bbp, 0, sizeof(BitBetPack));
	if( (iLen > 38) && (stxData.find(BitBet_Magic) == 0) )   //  "BitBet: "
	{
		int i = 0;
		try{
		char *delim = "|";    //int i = 0;    double dv = 0;
					
		char * pp = (char *)stxData.c_str();
        char *reserve;
		char *pch = strtok_r(pp, delim, &reserve);
		while (pch != NULL)
		{
			i++;
			//if( fDebug ){ printf("GetTxBitBetParamFromStr:: [%s] \n", pch); }
			//if( i == 1 ){ bbp.betMagic = pch; }
			if( i == 2 ){ bbp.id = unsigatoi(pch); }
			else if( i == 3 ){ bbp.opCode = unsigatoi(pch); }	//  create = 1, bet = 2, cash = 3
			else if( i == 4 ){ bbp.betType = unsigatoi(pch); }
			else if( i == 5 ){ bbp.u6BetAmount = strToInt64(pch); }  //dv = atof(pch);  bbp.betAmount = roundint64(dv * COIN); }
			else if( i == 6 ){ bbp.u6MiniBetAmount = strToInt64(pch); }  //dv = atof(pch);  bbp.miniBetAmount = roundint64(dv * COIN); }
			else if( i == 7 ){ bbp.u6StartBlock = strToInt64(pch); }  //dv =  atof(pch);  bbp.startBlock = roundint64(dv); }  //strToInt64(pch); }
			else if( i == 8 ){ bbp.u6TargetBlock = strToInt64(pch); }
			else if( i == 9 ){ bbp.betLen = unsigatoi(pch); }  //printf("betLen [%s]\n", pch); }
			else if( i == 10 ){ bbp.u6BetCount = unsigatoi(pch); }
			else if( i == 11 ){ bbp.betStartNum = unsigatoi(pch); }
			else if( i == 12 ){ bbp.betNum = pch; }
			else if( i == 13 ){ bbp.bettor = pch; }   // a9df6a65f6833d27ec3113079009e06b2fc8e5fd6e22a3d219ef15f39bac7b68
			else if( i == 14 ){ bbp.genBet = pch;   if( bbp.genBet.length() < 60 ){ bbp.genBet.resize(0); }   }
			else if( i == 15 ){ bbp.referee = pch;    if( bbp.referee.length() < 2 ){ bbp.referee.resize(0); }   }
			else if( i == 16 ){
				bbp.betTitle = pch;
				if( bbp.betTitle.length() < 2 ){ bbp.betTitle.resize(0); }
				else if( bbp.betTitle.length() > 256 ){
					if( bbp.betType == 5 ){ if(bbp.betTitle.length() > 512){ bbp.betTitle.resize(512); } }
					else{ bbp.betTitle.resize(256); }
				}
			}
			else if( i == 17 ){
				bbp.refereeNick = pch;
				if( bbp.refereeNick.length() > 34 ){ bbp.refereeNick.resize(34); }      // 2016.09.27 add, if bet type = 5, there is refereeNick
			}
			else if( i == 18 ){ bbp.encryptFlag5 = pch; }               // 2016.11.12 change from bbp.ex2
			else if( i == 19 ){  bbp.maxBetCount = unsigatoi(pch);  }   // 2016.11.12 add
			else if( i == 20 ){  bbp.oneAddrOnce = unsigatoi(pch);  }   // 2016.11.12 add
			else if( i == 21 ){ bbp.u6OneAddrMaxBetAmount = strToInt64(pch); }   // 2016.11.29 add
			else if( i == 22 ){ bbp.enCashFlag = unsigatoi(pch); }                 // 2016.11.29 add
			else if( i == 23 ){ bbp.uniqueNumber = strToInt64(pch); }         // 2016.11.29 add
			else if( i == 24 ){ bbp.ex3 = pch; }
			else if( i > 26 ){ break; }
			//else if( i == 13 ){ sMakerAddr = pch; }
			//else if( i == 14 ){ sLotteryLinkedTxid = pch; }
			//else if( i == 15 ){ sSignMsg = pch; }
			//if( fDebug ){ printf ("%s, %d\n", pch, i); }
			pch = strtok_r(NULL, delim, &reserve);
		}
		}catch (std::exception &e) {
			printf("GetTxBitBetParamFromStr:: err [%s]\n", e.what());
		}
		rzt = i;
	}
	bbp.paramCount = rzt;    return rzt;
}

int GetTxBitBetParam(const CTransaction& tx, BitBetPack &bbp)
{
    return GetTxBitBetParamFromStr(tx.chaindata, bbp);
}

int GetTxBitBetParam(const string& txID, BitBetPack &bbp)
{
	int rzt = 0;
	CTransaction tx;
	if( GetTransactionByTxStr(txID, tx) > 0 )
	{
		rzt = GetTxBitBetParam(tx, bbp);
	}
	return rzt;
}

int GetTxBitBetCmdParam(const CTransaction& tx, BitBetCommand &bbc)
{
	return GetTxBitBetCmdParamFromStr(tx.chaindata, bbc);
}

bool isValidBitBetGenesisTx(const CTransaction& tx, uint64_t iTxHei, BitBetPack &bbp, int& payIdx, bool mustExist, bool bCheckGameCount)
{
	bool rzt = false, bJustRcvTx = (iTxHei == 0);
	
	if( bbp.tx.length() < 63){ bbp.tx = tx.GetHash().ToString(); }  // tx hash = 64 chars, d0369849874bb4d6467b0600318b3c2b31329c39ac58e75df08bf521a819008f
	if( iTxHei <= 0 ){ iTxHei = GetTransactionBlockHeight(bbp.tx); }
	bool bTxInBlock = iTxHei > 0,  isBankerMode = (bbp.refereeNick == "1");
	if( iTxHei <= 0 )
    {
		if( fDebug ) printf("isValidBitBetGenesisTx: Tx [%s] Hei = 0, set to nBestHeight [%u], bTxInBlock = [%d] \n", bbp.tx.c_str(), nBestHeight, bTxInBlock);
		if( mustExist ){ return rzt; }
		iTxHei = nBestHeight + 1;
	}
	if( fDebug ){ printf("isValidBitBetGenesisTx: bCheckGameCount=[%d], isBankerMode=[%d], Tx [%s] Hei = [%s], nBestHeight [%s], maxBetCount=[%d], oneAddrOnce=[%d] \n", bCheckGameCount, isBankerMode, bbp.tx.c_str(), u64tostr(iTxHei).c_str(), u64tostr(nBestHeight).c_str(), bbp.maxBetCount, bbp.oneAddrOnce); }

	//if( iTxHei < BitNetLotteryStartTestBlock_286000 ){ return rzt; }
	if( bTxInBlock && (iTxHei >= New_Rules_161013_Active_Block) && (bbp.betType==0) ){ bbp.u6TargetBlock = 1; }   // 2016.10.13 add, Lucky 16 target block is next block

	if( (bbp.u6TargetBlock > Total_Blocks_of_20Days) && (bbp.u6BetAmount < Big_Target_Block_Min_Bet_Amount) )  // 2016_10_15 add
	{
		if( fDebug ) printf("isValidBitBetGenesisTx :: TaregBlockNum [%s] > 28800, Amount [%s] < Big_Target_Block_Min_Bet_Amount [%u] :(\n", u64tostr(bbp.u6TargetBlock).c_str(), u64tostr(bbp.u6BetAmount).c_str(), Big_Target_Block_Min_Bet_Amount);
		if( iTxHei >= New_Rules_161013_Active_Block ){  return rzt;  }
	}

	if( (bbp.betType > 5) || (bbp.paramCount < 14) || (bbp.opCode != 1) || (bbp.betLen < 1) || (bbp.betLen > 60) || (bbp.betNum.length() < bbp.betLen) )
	{ 
		if( fDebug ){ printf("isValidBitBetGenesisTx: paramCount (%u) < 14 or opCode(%u) != 1 or betLen(%u) < 1 or > 64, betNum = [%s] \n", bbp.paramCount, bbp.opCode, bbp.betLen, bbp.betNum.c_str()); }
		return rzt; 
	}	// block hash len = 64
	//if( fDebug ){ printf("isValidLotteryGenesisTx: iGuessType = [%u], sLotteryAddr = [%s],  sMakerAddr = [%s]\n", iGuessType, sLotteryAddr.c_str(), sMakerAddr.c_str()); }

	if( bbp.betType == 3 )   // Lucky Boss new rules,  2016.10.22 add
	{
		if( isDuplicatesBossNums( bbp.betNum ) )
		{
			if( fDebug ){ printf("isValidBitBetGenesisTx :: Lucky Boss [%s] duplicates :(\n", bbp.betNum.c_str()); }
			if( iTxHei >= New_LuckyBossRule_Active_Block ){  return rzt;  }
		}
	}
	else if( bbp.betType == 5 )  // Bet Real Event,  referee must exists and valid
	{
		CBitcoinAddress address(bbp.referee);
		if( !address.IsValid() ){ if( fDebug ){ printf("isValidBitBetGenesisTx: referee [%s] unvalid \n", bbp.referee.c_str());  return rzt;  } }
	}
	else if( (bbp.betType == 2) || (bbp.betType == 4) ){  // 2016_10_15 add, check banker
	    uint64_t u6a = getMini_Banker_Bet_Amount(iTxHei);  // 2017.05.26 add
		if( isBankerMode && (bbp.u6BetAmount < u6a) )
		{
			if( fDebug ) printf("isValidBitBetGenesisTx :: Banker mode [%s], Amount [%s] < Mini_Banker_Bet_Amount [%s] :(\n", bbp.refereeNick.c_str(), u64tostr(bbp.u6BetAmount).c_str(), u64tostr(u6a).c_str());
			return rzt;
		}
	}
	
	CBitcoinAddress aBettor(bbp.bettor);
    if( !aBettor.IsValid() ){ if( fDebug ){ printf("isValidBitBetGenesisTx: bettor [%s] unvalid \n", bbp.bettor.c_str());  return rzt;  } }
	
	if( (bbp.u6StartBlock > 0) || (bbp.genBet.length() > 0) ){ return rzt; }
	if( (bbp.u6TargetBlock < 1) || ((bbp.betType > 0) && (bbp.u6TargetBlock <  BitBetBeginEndBlockSpace_10)) ){ return rzt; }
	if( bbp.u6MiniBetAmount < BitBet_Mini_Amount ){ return rzt; }
	if( bbp.betType > 0 )  // 2016.09.30 add,  game type not lucky 16
	{
		if( bbp.u6BetAmount  < BitBet_Launch_MultiBet_MinAmount )
		{
			if( fDebug ) printf("isValidBitBetGenesisTx: Amount [%s] < BitBet_Launch_MultiBet_MinAmount [%u] :(\n", u64tostr(bbp.u6BetAmount).c_str(), BitBet_Launch_MultiBet_MinAmount);
			return rzt;
		}
	}
	if( bbp.u6BetAmount < bbp.u6MiniBetAmount )
	{
#ifdef WIN32
		if( fDebug ) printf("isValidBitBetGenesisTx: Amount [%I64u] < minimum bet value [%I64u] :(\n", bbp.u6BetAmount, bbp.u6MiniBetAmount);
#endif
		return rzt;			
	}

	if( (bbp.u6OneAddrMaxBetAmount > 0)  && (bbp.u6BetAmount > bbp.u6OneAddrMaxBetAmount ) )   // 2016.11.29 add
	{
#ifdef WIN32
		if( fDebug ) printf("isValidBitBetGenesisTx: u6BetAmount [%I64u] > u6OneAddrMaxBetAmount [%I64u] :(\n", bbp.u6BetAmount, bbp.u6OneAddrMaxBetAmount);
#endif
		if( iTxHei >= New_Rules_161129_BLK_10W ){  return rzt;  }
	}

	/*if( (iTxHei < iStartBlock) || (iTxHei > (iEndBlock - (BitNetBeginAndEndBlockMargin_Mini_30 - 10))) )	//iBitNetBlockMargin3
	{ 
		if( fDebug ) printf("isValidBitBetGenesisTx: Blocks not under rules, Hei = [%u] : [%I64u ~ %I64u] :(\n", iTxHei, iStartBlock, (iEndBlock - 20));
		return rzt;
	}*/
	uint64_t iAmount = (bbp.u6BetAmount * COIN);
	if( bbp.betType ==  0 )  // Lucky 16, check bet multi times
	{
		if( bbp.u6BetCount < 1 ){ bbp.u6BetCount = 1; }
		else if( bbp.u6BetCount > BitBet_Lucky16_Max_Bet_Count ){ bbp.u6BetCount = BitBet_Lucky16_Max_Bet_Count; }
		if( bbp.u6BetCount > 1 ){ iAmount = (bbp.u6BetAmount * bbp.u6BetCount) * COIN; }
	}
	payIdx = GetCoinAddrInTxOutIndex(tx, BitBetBurnAddress, iAmount);
	if( payIdx >= 0 )	// Check Bet Amount, =-1 is invalid
	{ 
		uint64_t u6OBC = 0, u6ABC=0;
		if( bCheckGameCount )
		{
			u6OBC = getAliveLaunchBetCount(dbLuckChainWrite, bbp.bettor, bJustRcvTx) + 1;
			if( u6OBC > Max_One_Bettor_Alive_Launch_BitBets )
			{
				if( iTxHei >= New_Rules_161013_Active_Block )   //if( bTxInBlock && (iTxHei >= New_Rules_161013_Active_Block) )   // 2016.10.13 add
				{
					if( fDebug ){ printf("isValidBitBetGenesisTx :: bettor [%s] launched [%s] bets :( \n", bbp.bettor.c_str(), u64tostr(u6OBC).c_str()); }
					return rzt;
				}
			}
			u6ABC = getAliveLaunchBetCount(dbLuckChainWrite, "", bJustRcvTx) + 1;
			if( u6ABC < Max_Alive_Launch_BitBets ){ rzt = true; }
		}else{  rzt = true;  }
		if( fDebug ){ printf("isValidBitBetGenesisTx: rzt = [%d], All Launch Bet Count = [%s], Bettor [%s] launched bet count = [%s] \n", rzt, u64tostr(u6ABC).c_str(), bbp.bettor.c_str(), u64tostr(u6OBC).c_str()); }
	}else{ printf("isValidBitBetGenesisTx: [%"PRIu64"] coins not send to [%s] :(\n", iAmount / COIN, BitBetBurnAddress.c_str()); }
	return rzt;
}

uint64_t getLuckyLottoTgBlkNum(const string sGenTx)
{
	uint64_t rzt = 0;
	string sql = "select * from AllBets where gen_bet='" + sGenTx + "' and opcode=1 and bet_type=4;";
	dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, AllBets_tblock_idx, 0, 0, "tblock", ""};
	getOneResultFromDb(dbLuckChainWrite, sql, pack);
	if( pack.fDone > 0 ){ rzt = pack.u6Rzt; }
	if( fDebug ){ printf("getLuckyLottoTgBlkNum:: rzt = [%s], fDone = [%d],  sql = [%s] \n", u64tostr(rzt).c_str(), pack.fDone, sql.c_str()); }
	return rzt;
}

uint64_t getRefereeMaxCoins(sqlite3 *db, const string sReferee)
{
	uint64_t rzt = 0;
	// get referee's maxcoins and all users bet coins
	string sql = "select * from Referees where coinAddress='" + sReferee + "';";
	dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, 5, 0, 0, "maxcoins", ""};  // 1 = uint64_t,  maxcoins id = 5
	getOneResultFromDb(db, sql, pack);
	if( pack.fDone == 0 ){  rzt = 1;  }
	else rzt = pack.u6Rzt;   // = 0 mean no limit, else have limit bet amount
	return rzt;
}
string getRefereeNickName(sqlite3 *db, const string sReferee)
{
	string rzt = "";
	string sql = "select * from Referees where coinAddress='" + sReferee + "';";
	dbOneResultCallbackPack pack = {OneResultPack_STR_TYPE, 1, 0, 0, "nickName", ""};  // 1 = uint64_t,  maxcoins id = 5
	getOneResultFromDb(db, sql, pack);
	if( pack.fDone > 0 ){  rzt = pack.sRzt;  }
	return rzt;
}

int DeleteAllBetsTmpTx(const string tx)
{
	string sql = "DELETE FROM AllBetsTmp where tx='" + tx + "';";
	int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
	if( fDebug ){ printf("DeleteAllBetsTmpTx([%s]) return [%d] \n", sql.c_str(), rc); }
	return rc;
}
uint64_t getPlayerTotalBetsInAGame(sqlite3 *db, const string genBet, const string sBettor, bool bJustRcvTx)
{
	uint64_t rzt = 0;
	string sql = "select sum(bet_amount) as abc from AllBets where gen_bet='" + genBet + "'";   // and bettor='" + sBettor + "';";
	if( sBettor.length() > 30 ){  sql = sql + " and bettor='" + sBettor + "'";  }
	sql = sql + ";";
	dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, 0, 0, 0, "", ""};  // 1 = uint64_t,  maxcoins id = 5
	getOneResultFromDb(db, sql, pack);
	if( pack.fDone > 0 ){  rzt = pack.u6Rzt;  }
	if( bJustRcvTx )
	{
		boost::replace_first(sql, "AllBets", "AllBetsTmp");   //sql = std::replace(sql.begin(), sql.end(), "AllBets", "AllBetsTmp");   //sql = "select sum(bet_amount) as abc from AllBetsTmp where gen_bet='" + genBet + "' and bettor='" + sBettor + "';";
		dbOneResultCallbackPack p2 = {OneResultPack_U64_TYPE, 0, 0, 0, "", ""};   pack = p2;   // 1 = uint64_t,  maxcoins id = 5
		getOneResultFromDb(db, sql, pack);
		if( pack.fDone > 0 ){  rzt = rzt + pack.u6Rzt;  }
	}
	if( fDebug ){ printf("getPlayerTotalBetsInAGame:: rzt = [%s], fDone = [%d], bJustRcvTx=[%d], sql = [%s] \n", u64tostr(rzt).c_str(), pack.fDone, bJustRcvTx, sql.c_str()); }
	return rzt;
}
bool isBetNumberExist(sqlite3 *db, const string sGenTx, const string sNum, bool bJustRcvTx)
{
	bool rzt = false;
	string sql = "select * from AllBets where gen_bet='" + sGenTx + "' and bet_num='" + sNum + "'";
	//if( stx.length() > 30 ){  sql = sql + " and tx != '" + stx + "'";  }
	sql = sql + ";";
	dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, AllBets_tblock_idx, 0, 0, "tblock", ""};
	getOneResultFromDb(db, sql, pack);
	if( pack.fDone > 0 ){ rzt = true; }
	if( bJustRcvTx && (!rzt) )
	{
		boost::replace_first(sql, "AllBets", "AllBetsTmp");   //sql = std::replace(sql.begin(), sql.end(), "AllBets", "AllBetsTmp");   //sql = "select * from AllBetsTmp where gen_bet='" + sGenTx + "' and bet_num='" + sNum + "';";
		dbOneResultCallbackPack p2 = {OneResultPack_U64_TYPE, AllBets_tblock_idx, 0, 0, "tblock", ""};      pack = p2;
		getOneResultFromDb(db, sql, pack);
		if( pack.fDone > 0 ){ rzt = true; }
	}
	if( fDebug ){ printf("isBetNumberExist:: rzt = [%d], fDone = [%d],  bJustRcvTx=[%d], sql = [%s] \n", rzt, pack.fDone, bJustRcvTx, sql.c_str()); }
	return rzt;
}
uint64_t getAGameBetCount(sqlite3 *db, const string sGenTx, const string sBettor, int betType, bool isBankerMode, bool bJustRcvTx)
{
	uint64_t rzt = 0;      string sql =  "select Count(*) from AllBets where gen_bet='" + sGenTx + "'";
	if( sBettor.length() > 30 ){  sql = sql + " and bettor='" + sBettor + "'";   }
	if( betType == 4 ){  sql = sql + " and rounds=0";  }   //if( isBankerMode && (betType == 4) ){  sql = sql + " and rounds=0";  }
	sql = sql + ";";
	int rc = getRunSqlResultCount(db, sql, rzt);      //if( rc == SQLITE_OK ){  if( u6Rzt < 1 ){  iFirstOfThisAddr++;  }  }
	if( bJustRcvTx )
	{
		boost::replace_first(sql, "AllBets", "AllBetsTmp");   //sql = std::replace(sql.begin(), sql.end(), "AllBets", "AllBetsTmp");
		uint64_t u6=0;      rc = getRunSqlResultCount(db, sql, u6);
		if( rc == SQLITE_OK ){  rzt = rzt + u6;  }
	}
	if( fDebug ){ printf("getAGameBetCount:: rzt = [%s], rc = [%d], bJustRcvTx=[%d], sql = [%s] \n", u64tostr(rzt).c_str(), rc, bJustRcvTx, sql.c_str()); }
	return rzt;
}
/*uint64_t sumBitBetColumn(const string genBet, const string sColumn)
{
	uint64_t rzt = 0;
	string sql = "select sum(" + sColumn + ")from AllBets where gen_bet='" + genBet + "';";
    sqlite3_exec(dbLuckChainWrite, sql.c_str(), selectCountCallback, (void*)&rzt, NULL);
	return rzt;
}
uint64_t sumBitBet_BetAmount(const string genBet)
{
	return sumBitBetColumn(genBet, "bet_amount");
}*/

bool isValidBitBetBetTx(const CTransaction& tx, uint64_t iTxHei, BitBetPack &bbp, int& payIdx)
{
   bool rzt = false, bJustRcvTx = (iTxHei == 0);
   
	if( bbp.tx.length() < 63){ bbp.tx = tx.GetHash().ToString(); }  // tx hash = 64 chars
	if( iTxHei <= 0 ){ iTxHei = GetTransactionBlockHeight(bbp.tx); }
	if( iTxHei <= 0 )
    {
		if( fDebug ) printf("isValidBitBetBetTx: Tx [%s] Hei = 0, set to nBestHeight [%u] \n", bbp.tx.c_str(), nBestHeight);
		iTxHei = nBestHeight + 1;
	}
	if( fDebug ){ printf("isValidBitBetBetTx: Tx [%s :: %s] Hei = [%s], nBestHeight [%s], bJustRcvTx=[%d] \n", bbp.tx.c_str(), bbp.genBet.c_str(), u64tostr(iTxHei).c_str(), u64tostr(nBestHeight).c_str(), bJustRcvTx); }
	if( bbp.tx == bbp.genBet ){ return rzt; }
	CBitcoinAddress aBettor(bbp.bettor);
    if( !aBettor.IsValid() ){ if( fDebug ){ printf("isValidBitBetBetTx: bettor [%s] unvalid \n", bbp.bettor.c_str());  return rzt;  } }

	if( bbp.betType == 3 )   // Lucky Boss new rules,  2016.10.22 add
	{
		if( isDuplicatesBossNums( bbp.betNum ) )
		{
			if( fDebug ){ printf("isValidBitBetBetTx :: Lucky Boss [%s] duplicates :(\n", bbp.betNum.c_str()); }
			if( iTxHei >= New_LuckyBossRule_Active_Block ){  return rzt;  }
		}
	}
	
   CTransaction genTx;   uint256 genHashBlock = 0;
   //if( !isBetTxExists(bbp.genBet) )
   if( GetTransactionByTxStr(bbp.genBet, genTx, genHashBlock) > 0 )
   {
      BitBetPack genBbp = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "", "", "", "", "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 0, 0};
      int bbpRzt = GetTxBitBetParam(genTx, genBbp);
	  if( (bbpRzt < 15) || (genBbp.betType == 0) ){ return rzt; }   // betType == 0 == Lucky 16 / Slot Machine can't multi bet
	  uint64_t genTxHei = getTxBlockHeightBy_hashBlock( genHashBlock );
	  uint64_t genTBlockNum = genBbp.u6TargetBlock + genTxHei, u6NewTgBlkNum = 0;
	  if( fDebug ){ printf("isValidBitBetBetTx: betNum=[%s], genBbp.betLen[%d] genTxHei[%s] genTBlockNum=[%s] \n", bbp.betNum.c_str(), genBbp.betLen, u64tostr(genTxHei).c_str(), u64tostr(genTBlockNum).c_str());  }
	  if( (iTxHei > genTBlockNum) && (genBbp.betType == 4) )   // 2016.10.09 add, support Lucky Lotto ReBet
	  {
		uint64_t u6BlockSpace = genBbp.u6TargetBlock,  u6FstTgBlkNum = genTBlockNum;      uint64_t u6FstTgBlkDeep = nBestHeight - u6FstTgBlkNum;
		uint64_t u6NextRound = (u6FstTgBlkDeep / (u6BlockSpace + 22)) + 1;     u6NewTgBlkNum = u6NextRound * (u6BlockSpace + 22) + u6FstTgBlkNum;
		genTBlockNum = getLuckyLottoTgBlkNum(bbp.genBet);
	  }
	  
	  bool bOk = (iTxHei < genTBlockNum);   // && (bbp.u6TargetBlock == genTBlockNum);
	  if( !bOk && fDebug ){ printf("isValidBitBetBetTx: error :: iTxHei=[%s] vs genTBlockNum=[%s : %s(u6NewTgBlkNum)] vs bbp.u6TargetBlock[%s] \n", u64tostr(iTxHei).c_str(), u64tostr(genTBlockNum).c_str(), u64tostr(u6NewTgBlkNum).c_str(), u64tostr(bbp.u6TargetBlock).c_str()); }
	  if( !bOk && (bbp.betType == 4) )
	  {
		   if( iTxHei < New_Rules_161013_Active_Block ){ bOk = true; }
		   //else if( iTxHei == 64319 ){  bOk = true;  }   // 2016.11.19, just for test
	  }
	  //if( (bbp.betType == genBbp.betType) && (bbp.betNum.length() >= genBbp.betLen) && (genTxHei > 0) && (iTxHei > genTxHei) && (iTxHei < genTBlockNum) && (bbp.opCode == 2) )
	  if( (bbp.betType == genBbp.betType) && (bbp.betNum.length() >= genBbp.betLen) && (genTxHei > 0) && (iTxHei > genTxHei) && bOk && (bbp.opCode == 2) )
	  {
		 genBbp.tx = bbp.genBet;      int gPayIdx=0;
		 if( isValidBitBetGenesisTx(genTx, genTxHei, genBbp, gPayIdx, true, (iTxHei >= BLOCK_NO_12W ? false : true)) )
	     {
	        if( bbp.u6BetAmount >= genBbp.u6MiniBetAmount )
			{
				bool isBankerMode = (bbp.refereeNick == "1") && (  (bbp.betType == 2) || (bbp.betType == 4)  );
				bool bNewRuleActive = ( genTxHei >= f20161111_NewTxFee_Active_Height );

				if( bJustRcvTx ){  DeleteAllBetsTmpTx(bbp.tx);  }   // 2016.11.30 add
				if( genBbp.u6OneAddrMaxBetAmount > 0 )   // 2016.11.29 add
				{
					uint64_t u6TtBets=0, u6ThisPlayerAllBets = bbp.u6BetAmount;
					bool bOverMax = (u6ThisPlayerAllBets > genBbp.u6OneAddrMaxBetAmount );
					if( !bOverMax )
					{
						u6TtBets = getPlayerTotalBetsInAGame(dbLuckChainWrite, bbp.genBet, bbp.bettor, bJustRcvTx);       u6ThisPlayerAllBets = u6ThisPlayerAllBets + u6TtBets;
						bOverMax = (genBbp.u6OneAddrMaxBetAmount > 0) && (u6ThisPlayerAllBets > genBbp.u6OneAddrMaxBetAmount);
					}
					if( fDebug ){  printf("isValidBitBetBetTx() : u6OneAddrMaxBetAmount=[%s], bOverMax=[%d], u6TtBets=[%s], u6ThisPlayerAllBets=[%s] \n",  u64tostr(genBbp.u6OneAddrMaxBetAmount).c_str(), bOverMax, u64tostr(u6TtBets).c_str(), u64tostr(u6ThisPlayerAllBets).c_str());  }
					if( bOverMax )
					{
						if( fDebug ) printf("isValidBitBetBetTx: u6ThisPlayerAllBets [%s] > u6OneAddrMaxBetAmount [%s] :(\n", u64tostr(u6ThisPlayerAllBets).c_str(), u64tostr(genBbp.u6OneAddrMaxBetAmount).c_str());
						if( iTxHei >= New_Rules_161129_BLK_10W ){  return rzt;  }
					}
				}
				
				if( (genBbp.betType > 0) && (genBbp.uniqueNumber > 0) )
				{
					bool bNumExist = isBetNumberExist(dbLuckChainWrite, bbp.genBet, bbp.betNum, bJustRcvTx);
					if( fDebug ){  printf("isValidBitBetBetTx() :: bNumExist=[%d] betType=[%d], uniqueNumber=[%d] \n", bNumExist, genBbp.betType, genBbp.uniqueNumber);  }
					if(  bNumExist && (iTxHei >= New_Rules_161129_BLK_10W)  ){  return rzt;  }
				}

				if( genBbp.maxBetCount > 0 )   // 2016.11.12 begin, limit bet count
				{
						uint64_t u6BetCount=0, u6MaxBetCount = genBbp.maxBetCount;      if(  u6MaxBetCount == 1 ){  u6MaxBetCount++;  }   // = 2
						u6BetCount = getAGameBetCount(dbLuckChainWrite, bbp.genBet, "", bbp.betType, isBankerMode, bJustRcvTx);
						
						/*string sql =  "select Count(*) from AllBets where gen_bet='" + bbp.genBet + "'";
						if( isBankerMode && (bbp.betType == 4) ){  sql = sql + " and rounds=0";  }
						sql = sql + ";";
						int rc = getRunSqlResultCount(dbLuckChainWrite, sql, u6BetCount);
						if( fDebug ){  printf("isValidBitBetBetTx : genBbp.maxBetCount=[%d] : [%s](u6BetCount), rc=[%d] [%s] \n", genBbp.maxBetCount,  u64tostr(u6BetCount).c_str(), rc, sql.c_str());  }
						if( (rc != SQLITE_OK) || ( (u6BetCount + 1) > u6MaxBetCount ) )  */
						if( fDebug ){  printf("isValidBitBetBetTx : genBbp.maxBetCount=[%d] : [%s](u6BetCount) \n", genBbp.maxBetCount,  u64tostr(u6BetCount).c_str());  }
						if( (u6BetCount + 1) > u6MaxBetCount  )
						{
							//bool bBan = ( genTxHei >= f20161111_NewTxFee_Active_Height );
							//if( fDebug ){  printf("isValidBitBetBetTx : rc(%d) != 0  Or u6BetCount(%s) > genBbp.maxBetCount(%d),  bNewRuleActive=[%d]  :( \n", rc, u64tostr(u6BetCount + 1).c_str(), genBbp.maxBetCount, bNewRuleActive);  }
							if( fDebug ){  printf("isValidBitBetBetTx :  u6BetCount(%s) > genBbp.maxBetCount(%d),  bNewRuleActive=[%d]  :( \n", u64tostr(u6BetCount + 1).c_str(), genBbp.maxBetCount, bNewRuleActive);  }
							if( bNewRuleActive ){  return rzt;  }
						}
				}
				if( genBbp.oneAddrOnce > 0 )
				{
						uint64_t u6BetCount=0, u6OneAddrOnce = genBbp.oneAddrOnce;
						u6BetCount = getAGameBetCount(dbLuckChainWrite, bbp.genBet, bbp.bettor, bbp.betType, isBankerMode, bJustRcvTx);
						/*string sql =  "select Count(*) from AllBets where gen_bet='" + bbp.genBet + "' and bettor='" + bbp.bettor + "'";
						if( isBankerMode && (bbp.betType == 4) ){  sql = sql + " and rounds=0";  }
						sql = sql + ";";
						int rc = getRunSqlResultCount(dbLuckChainWrite, sql, u6BetCount);
						if( fDebug ){  printf("isValidBitBetBetTx : genBbp.oneAddrOnce=[%d] : [%s](u6BetCount), rc=[%d] [%s] \n", genBbp.oneAddrOnce,  u64tostr(u6BetCount).c_str(), rc, sql.c_str());  }
						if( (rc != SQLITE_OK) || ( (u6BetCount + 1) > u6OneAddrOnce ) )   //if( (u6BetCount + 1) > ((uint64_t)(genBbp.oneAddrOnce)) ){ return rzt; } */
						if( fDebug ){  printf("isValidBitBetBetTx : genBbp.oneAddrOnce=[%d] : [%s](u6BetCount)  \n", genBbp.oneAddrOnce,  u64tostr(u6BetCount).c_str());  }
						if( ( u6BetCount + 1) > u6OneAddrOnce  )
						{
							//bool bBan = ( genTxHei >= f20161111_NewTxFee_Active_Height );
							//if( fDebug ){  printf("isValidBitBetBetTx : rc(%d) != 0  Or u6BetCount(%s) > genBbp.oneAddrOnce(%d),  bNewRuleActive=[%d]  :( \n", rc, u64tostr(u6BetCount + 1).c_str(), genBbp.oneAddrOnce, bNewRuleActive);  }
							if( fDebug ){  printf("isValidBitBetBetTx :  u6BetCount(%s) > genBbp.oneAddrOnce(%d),  bNewRuleActive=[%d]  :( \n", u64tostr(u6BetCount + 1).c_str(), genBbp.oneAddrOnce, bNewRuleActive);  }
							if( bNewRuleActive ){  return rzt;  }
						}
				}   // 2016.11.12 end

				if( bbp.betType == 5 )  // Bet RealEvents, we need limit total bet amount
				{
					/* get referee's maxcoins and all users bet coins
					string sql = "select * from Referees where coinAddress='" + genBbp.referee + "';";
					dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, 5, 0, 0, "maxcoins", ""};  // 1 = uint64_t,  maxcoins id = 5
					getOneResultFromDb(dbLuckChainWrite, sql, pack);
					if( fDebug ){ printf("isValidBitBetBetTx : betType=5, [%s], fDone=[%d], u6Rzt=[%s] \n", sql.c_str(), pack.fDone, u64tostr(pack.u6Rzt).c_str()); } */
					uint64_t u6RefereeMaxCoins = getRefereeMaxCoins(dbLuckChainWrite, genBbp.referee);
					if( fDebug ){ printf("isValidBitBetBetTx : betType=5, getRefereeMaxCoins()=[%s] \n", u64tostr(u6RefereeMaxCoins).c_str()); }
					if( u6RefereeMaxCoins == 1 ){  return rzt;  }   //if( pack.fDone == 0 ){ return rzt; }
					if( u6RefereeMaxCoins > 0 )  // = 0 mean no limit, else have limit bet amount
					{
						string sql = "select * from AllBets where tx='" + bbp.genBet + "' and opcode=1;";  // and done=0;";
						dbOneResultCallbackPack packGen = {OneResultPack_U64_TYPE, AllBets_total_bet_amount_idx, 0, 0, "total_bet_amount", ""};  // 1 = uint64_t,  total_bet_amount id = 23
						getOneResultFromDb(dbLuckChainWrite, sql, packGen);
						if( fDebug ){ printf("isValidBitBetBetTx : betType=5, 2,  [%s], fDone=[%d], u6Rzt=[%s] \n", sql.c_str(), packGen.fDone, u64tostr(packGen.u6Rzt).c_str()); }
						if( packGen.fDone == 0 ){ return rzt; }
						uint64_t u6AllBets = bbp.u6BetAmount + packGen.u6Rzt;
						if( fDebug ){ printf("isValidBitBetBetTx : betType=5, 2,  u6AllBets=[%s] :: maxcoins(pack.u6Rzt)=[%s] \n", u64tostr(u6AllBets).c_str(),  u64tostr(u6RefereeMaxCoins).c_str()); }
						if( u6AllBets > u6RefereeMaxCoins ){ return rzt; }
					}
				}
				uint64_t iAmount = (bbp.u6BetAmount * COIN);
				payIdx = GetCoinAddrInTxOutIndex(tx, BitBetBurnAddress, iAmount);
				if( payIdx >= 0 )	// Check Bet Amount, =-1 is invalid
				{ 
					rzt = true;
					/* if( bJustRcvTx )   // 2016.11.12 begin, limit bet count
					{
						if( genBbp.maxBetCount > 0 )
						{
							string sql = "select * from AllBets where tx='" + bbp.genBet + "' and opcode=1;";
							dbOneResultCallbackPack packEst = {OneResultPack_U64_TYPE, AllBet_est_bet_count_idx, 0, 0, "est_bet_count", ""};
							getOneResultFromDb(dbLuckChainWrite, sql, packEst);
						if( fDebug ){ printf("isValidBitBetBetTx : bJustRcvTx,  bNewRuleActive=[%d], maxBetCount=[%d], fDone=[%d], est_bet_count=[%s], [%s] \n", bNewRuleActive, genBbp.maxBetCount, packEst.fDone, u64tostr(packEst.u6Rzt).c_str(), sql.c_str()); }
						if( packEst.fDone == 0 ){  if( bNewRuleActive ){  rzt = false;  }  }
						else{
							uint64_t u6EstBetCount = packEst.u6Rzt + 1,  u6MaxBetCount = genBbp.maxBetCount;
							if(  u6MaxBetCount == 1 ){  u6MaxBetCount++;  };   // = 2
							if( u6EstBetCount > u6MaxBetCount )
							{
								if( bNewRuleActive ){  rzt = false;  }
								if( fDebug ){  printf("isValidBitBetBetTx: u6EstBetCount(%s) > u6MaxBetCount(%s), rzt=[%d] :( \n",  u64tostr(u6EstBetCount).c_str(), u64tostr(u6MaxBetCount).c_str(), rzt);  }
							}
							else{
								sql = "update AllBets set est_bet_count=(est_bet_count+1) where tx='" + bbp.genBet + "';";     
								sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
							}
							}
						}
					}  */  // 2016.11.12 end

					if( fDebug ){ printf("isValidBitBetBetTx: Pass, rzt=[%d]  :) \n", rzt); }
				}else{ printf("isValidBitBetBetTx: [%s] coins not send to [%s] :(\n", u64tostr(iAmount).c_str(), BitBetBurnAddress.c_str()); }
			}else{ printf("isValidBitBetBetTx : bbp.u6BetAmount(%s) < genBbp.u6MiniBetAmount(%s) :(\n", u64tostr(bbp.u6BetAmount).c_str(), u64tostr(genBbp.u6MiniBetAmount).c_str()); }
	     }else{ printf("isValidBitBetBetTx : isValidBitBetGenesisTx() return false :(\n"); }
	  }
   }else if( fDebug ){ printf("isValidBitBetBetTx: GetTransactionByTxStr(%s) false :( \n", bbp.genBet.c_str()); }
   return rzt;
}

unsigned int getWinnersReward(sqlite3 *dbOne, int betType, int enCashFlag, const string sReferee, uint64_t u6AllRewardCoins, uint64_t u6AllWinnerBetCoins, const std::vector<txOutPairPack > allWinners, std::vector<txOutPairPack >& newWinners, uint64_t& u6ToMinerFee)
{
	unsigned int rzt=0;     newWinners.resize(0);
	int awSz = allWinners.size();
	if( fDebug ){ printf("getWinnersReward : allWinners.size() = [%d], betType = [%d] enCashFlag=[%d] u6AllRewardCoins=[%s] u6AllWinnerBetCoins=[%s] \n", awSz, betType, enCashFlag, u64tostr(u6AllRewardCoins).c_str(), u64tostr(u6AllWinnerBetCoins).c_str()); }
	if( awSz > 0 )
	{
		if( betType == 0 ){  u6AllRewardCoins = (allWinners[0].v_nValue * BitBet_Lucky16_Max_Reward_Times);  }
		double d2 = (double)u6AllRewardCoins * BitBet_RewardMiner_Rate;
		uint64_t nTxFees = (uint64_t)(d2 + 0.5);   //  <= 0.4 drop, >=0.5 add 1
		if( nTxFees < 1 ){ nTxFees = 1; }
		u6ToMinerFee = nTxFees;      u6AllRewardCoins = u6AllRewardCoins - nTxFees;  // sub nTxFees to Miner
	if( fDebug ){ printf("getWinnersReward  :  betType = [%d] u6AllRewardCoins=[%s] u6ToMinerFee=[%s] \n", betType, u64tostr(u6AllRewardCoins).c_str(), u64tostr(u6ToMinerFee).c_str()); }

		if( (betType==0) || (betType==3) )   // Lucky 16   or   Lucky Boss (Biggest)
		{
			//uint64_t u6AC = u6AllRewardCoins;
			//if( betType == 0 ){ u6AC = (allWinners[0].v_nValue * BitBet_Lucky16_Max_Reward_Times) - nTxFees; }
			txOutPairPack opp = {0, u6AllRewardCoins, allWinners[0].sAddr};
			newWinners.push_back(opp);   rzt++;
		}else{
			uint64_t u6RefereeFee=0;
			if( betType == 5 )
			{
				//  get referee's fee
				string sFee="",  sql = "select * from Referees where coinAddress='" + sReferee + "';";
				dbOneResultCallbackPack pack = {OneResultPack_STR_TYPE, 4, 0, 0, "fee", ""};
				getOneResultFromDb(dbOne, sql, pack);
				if( fDebug ){ printf("getWinnersReward : betType=5, [%s] pack.fDone=[%d], pack.sRzt=[%s] \n", sql.c_str(), pack.fDone, pack.sRzt.c_str()); }
				if( pack.fDone > 0 )
				{
					if( pack.sRzt.length() > 0 ){
						double vfr = atof(pack.sRzt.c_str()),  dd = u6AllRewardCoins;   u6RefereeFee = (uint64_t)( dd * vfr );   
						u6AllRewardCoins = u6AllRewardCoins - u6RefereeFee;
						txOutPairPack opr = {0, u6RefereeFee, sReferee};     newWinners.push_back(opr); 
					}
				}else{
					if( fDebug ){ printf("getWinnersReward : pack.fDone=0, pack.sRzt=[%s] :( \n", pack.sRzt.c_str()); }
					return 0;
				}
				if( fDebug ){ printf("getWinnersReward : betType=5, u6RefereeFee=[%s] \n", u64tostr(u6RefereeFee).c_str()); }
			}
			double d = 0;      if( fDebug ){ printf("getWinnersReward : u6AllRewardCoins=[%s], awSz=[%d], x6=[%d] \n", u64tostr(u6AllRewardCoins).c_str(), awSz, (int)(u6AllRewardCoins / awSz)); }
			uint64_t x6 = 0;  if( enCashFlag > 0 ){  x6 = u6AllRewardCoins / awSz;  }  // Divide equally
			for( int i = 0; i < awSz; i++ )  // BOOST_FOREACH(const CTxIn& txin, tx.vin)
			{
				uint64_t u6 = x6;      const txOutPairPack& opp = allWinners[i];
				if( fDebug ){  printf("getWinnersReward : opp.v_nValue=[%s] [%s] x6=[%s] \n", u64tostr(opp.v_nValue).c_str(), opp.sAddr.c_str(), u64tostr(x6).c_str());  }
				if( enCashFlag <= 0 )
				{
					double d2 = (double)opp.v_nValue;      d = u6AllRewardCoins * ( d2 / u6AllWinnerBetCoins );      u6 = d;
					if( fDebug ){ printf("getWinnersReward : u6AllRewardCoins(%lf) * ( (d2(%lf) / u6AllWinnerBetCoins(%lf) ) =  u6 = [%s] \n", (double)u6AllRewardCoins, d2, (double)u6AllWinnerBetCoins, u64tostr(u6).c_str()); }
				}else if( fDebug ){ printf("getWinnersReward : u6AllRewardCoins(%lf)  / awSz(%d) =  u6(%s) \n", (double)u6AllRewardCoins, awSz, u64tostr(u6).c_str()); }
				txOutPairPack p2 = {i, u6, opp.sAddr};
				newWinners.push_back(p2);   rzt++;
			}
		}
	}
	return rzt;
}
int updateBetConfirmed(const string sGenBet, uint64_t nHeight)
{
	string sql = "UPDATE AllBets set confirmed=(" + u64tostr(nHeight) + " - sblock + 1) where gen_bet='" + sGenBet + "';";
	return sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
}
bool isValidBitBetTxOut(int betType, int enCashFlag, const std::vector<CTxOut> &vout, const string genBet, uint64_t txHei, uint64_t u6TxFees)
{
	bool rzt=false;
	int osz = vout.size();
	if( fDebug ){ printf("isValidBitBetTxOut : betTye=[%d] enCashFlag=[%d], vout.size() = [%d] genBet = [%s], u6TxFees=[%s] \n", betType, enCashFlag, osz, genBet.c_str(), u64tostr(u6TxFees).c_str()); }
	if( osz > 0 )
	{
		int rc = updateBetConfirmed(genBet, txHei);
		std::vector<txOutPairPack > txOutPair;       txOutPair.resize(0);
		int iOpCount = GetTxOutDetails(vout, txOutPair);     dbBitBetTotalAmountAndWinnerPack pack;
		pack.u6RecordCount = 0;   pack.u6AllBetCoins = 0;   pack.u6WinnerCount = 0;   pack.u6AllWinerBetCoins=0;   pack.allWiners.resize(0);
		if( fDebug ){ printf("isValidBitBetTxOut : updateBetConfirmed()=[%d], GetTxOutDetails() return [%d] \n", rc, iOpCount); }
		unsigned int k = getBitBetTotalBetsAndMore(dbLuckChainWrite, genBet, pack);
		if( fDebug ){ printf("isValidBitBetTxOut : u6WinnerCount = [%s] \n", u64tostr(pack.u6WinnerCount).c_str()); }
		if( (betType == 0) || (betType == 3) )  // Lucky 16 = 0, Lucky Boss(Biggest) = 3, only one winner
		{
			if( (iOpCount == 1) && (pack.u6WinnerCount == 1) )  // must only one winner
			{
				uint64_t u6WinCoins = pack.u6AllBetCoins;      txOutPairPack &op2 = pack.allWiners[0];
				if( betType == 0 ){ u6WinCoins = op2.v_nValue * BitBet_Lucky16_Max_Reward_Times; }  // Lucky 16
				double d2 = (double)u6WinCoins * BitBet_RewardMiner_Rate;  // ********** sub nTxFees to Miner
				uint64_t u6ToMinerFee = (uint64_t)(d2 + 0.5);   // < 0.5 drop,  >= 0.5 add 1
				if( u6ToMinerFee < 1 ){ u6ToMinerFee = 1; }
				if( fDebug ){ printf("isValidBitBetTxOut : u6ToMinerFee=[%s], u6TxFees=[%s]  \n", u64tostr(u6ToMinerFee).c_str(), u64tostr(u6TxFees).c_str()); }
				if( u6ToMinerFee != u6TxFees ){ return rzt; }
				u6WinCoins = u6WinCoins - u6ToMinerFee;  // ********* sub nTxFees to Miner
				if( fDebug ){ printf("isValidBitBetTxOut : u6WinCoins = [%s] \n", u64tostr(u6WinCoins).c_str()); }
				u6WinCoins = u6WinCoins * COIN;      txOutPairPack &op = txOutPair[0];
				rzt = (op.v_nValue == u6WinCoins) && (op.sAddr.length() > 33)  && (op.sAddr == op2.sAddr);
#ifdef WIN32
				//if( fDebug ){ printf("isValidBitBetTxOut : op.v_nValue = [%s  :: %s], rzt = [%d] \n", u64tostr(op.v_nValue).c_str(), u64tostr(u6WinCoins).c_str(), rzt); }
				if( fDebug ){ printf("isValidBitBetTxOut : op.v_nValue = [%"PRIu64" :: %"PRIu64"], rzt = [%d] \n", op.v_nValue, u6WinCoins, rzt); }
#endif
			}
		}else{  // 1, 2, 4, 5, multi winners
			string sReferee = "";     std::vector<txOutPairPack > newWinners;      newWinners.resize(0);
			if( betType == 5 )  // get referee
			{
				string sql = "select * from AllBets where tx='" + genBet + "' and opcode=1 and bet_type=5;";
				dbOneResultCallbackPack pack = {OneResultPack_STR_TYPE, AllBets_referee_idx, 0, 0, "referee", ""};
				getOneResultFromDb(dbLuckChainWrite, sql, pack);
				if( pack.fDone > 0 ){ sReferee = pack.sRzt; }
				if( fDebug ){ printf("isValidBitBetTxOut : pack.fDone=[%d], sReferee=[%s] [%s] \n", pack.fDone, sReferee.c_str(), sql.c_str()); }
			}
			uint64_t u6ToMinerFee = 0;      unsigned int wr = getWinnersReward(dbLuckChainWrite, betType, enCashFlag, sReferee, pack.u6AllBetCoins, pack.u6AllWinerBetCoins, pack.allWiners, newWinners, u6ToMinerFee);
			if( fDebug ){ printf("isValidBitBetTxOut : u6ToMinerFee=[%s], u6TxFees=[%s]  \n", u64tostr(u6ToMinerFee).c_str(), u64tostr(u6TxFees).c_str()); }
			if( u6ToMinerFee != u6TxFees ){ return rzt; }

			if( wr > 0 )
			{
				int nwSz=newWinners.size();     if( fDebug ){ printf("isValidBitBetTxOut : txOutPair.size() = [%d :: %d :: %d]  \n", txOutPair.size(), iOpCount, nwSz); }
				if( iOpCount == nwSz )
				{
					for(int i=0; i<iOpCount; i++)
					{
						txOutPairPack &op = txOutPair[i];      txOutPairPack &nw = newWinners[i];
						uint64_t u6WinCoins = nw.v_nValue * COIN;
#ifdef WIN32
						if( fDebug ){ printf("isValidBitBetTxOut : i = [%d], u6WinCoins = [%I64u] :: [%I64u] [%s] \n", i, u6WinCoins, op.v_nValue, op.sAddr.c_str()); }
#endif
						rzt = (op.v_nValue == u6WinCoins) && (op.sAddr.length() > 33) && (op.sAddr == nw.sAddr);
						if( rzt == false ){ break; }
					}
					if( rzt && (betType == 5) && (txHei > 1) )  // update referee take fees;
					{
						txOutPairPack &nw = newWinners[0];
						updateRefereeTakeFees(nw.sAddr, nw.v_nValue);
					}
				}else if( fDebug ){ printf("isValidBitBetTxOut :: iOpCount(%d) != newWinners.size(%d) :( \n", iOpCount, nwSz); }
			}else if( fDebug ){ printf("isValidBitBetTxOut :: getWinnersReward() return 0 :( \n"); }
		}
	}
	return rzt;
}

void updateRefereeDecideConfirmed(const string genBet, uint64_t u6CurBlockNum)
{
	string sql = "select * from AllBets where tx='" + genBet + "';";
	dbOneResultCallbackPack pkG = {OneResultPack_STR_TYPE, AllBet_refereeNick_idx, 0, 0, "refereeDecideTx", ""};  // 1 = uint64_t,  tblock id = 7
	getOneResultFromDb(dbLuckChainWrite, sql, pkG);      string sRefereeDecideTx = pkG.sRzt;
	if( fDebug ){ printf("updateRefereeDecideConfirmed:: Bet Real Event, Decide tx [%s] \n", sRefereeDecideTx.c_str()); }
	if( (pkG.fDone > 0) && (sRefereeDecideTx.length() > 60) )
	{
		//if( sRefereeDecideTx.length() > 60 )
		{
			int64_t u6DecideTxBlockNum = GetTransactionBlockHeight(sRefereeDecideTx);
			if( fDebug ){ printf("updateRefereeDecideConfirmed:: u6CurBlockNum=[%s], u6DecideTxBlockNum=[%s] \n", u64tostr(u6CurBlockNum).c_str(), u64tostr(u6DecideTxBlockNum).c_str()); }
			if( u6CurBlockNum > u6DecideTxBlockNum )
			{
				uint64_t u6Confirmed = u6CurBlockNum - u6DecideTxBlockNum;
				sql = "UPDATE AllBets set bet_num2=" + u64tostr(u6Confirmed) + " where gen_bet='" + genBet + "';";
				int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
			}
		}
	}
}

bool isValidBitBetEncashTx(const CTransaction& tx, uint64_t iTxHei, BitBetPack &bbp)
{
	bool rzt=false, bJustRcvTx = (iTxHei == 0);
	if( bbp.opCode != 3 ){ return rzt; }
	if( bJustRcvTx ){ iTxHei = nBestHeight + 1; }
	int vinSz = tx.vin.size();
	if( fDebug ){ printf( "isValidBitBetEncashTx: bJustRcvTx=[%d], tx.vin.size = [%d], \n", bJustRcvTx, vinSz); }
	if( vinSz == 1 )
	{
		int vinId=0;
		BOOST_FOREACH(const CTxIn& txin, tx.vin)  // only once   //std::vector<CTxIn> vin;
		{
			uint256 hashBlock = 0;      CTransaction txGen;
			if( fDebug ){ printf( "isValidBitBetEncashTx: (%u) txin.prevout.n = [%u], \n", vinId, txin.prevout.n ); }
			if( GetTransaction(txin.prevout.hash, txGen, hashBlock) )	// get the vin's previous transaction
			{
				string genHash = txGen.GetHash().ToString();  //string sPrevTxMsg = txGen.chaindata;
				uint64_t genTxHei = getTxBlockHeightBy_hashBlock( hashBlock );
				if( fDebug ){ printf( "isValidBitBetEncashTx: genTx Hei=[%s], genHash = [%s],  genBet = [%s], \n", u64tostr(genTxHei).c_str(), genHash.c_str(), bbp.genBet.c_str()); }
				if( genHash == bbp.genBet )
				{
					string sql = "select * from AllBets where tx='" + bbp.genBet + "';";
					dbOneResultCallbackPack packGen = {OneResultPack_U64_TYPE, 26, 0, 0, "done", ""};  // 1 = uint64_t,  done id = 26
					getOneResultFromDb(dbLuckChainWrite, sql, packGen);
					if( fDebug ){ printf( "isValidBitBetEncashTx: [%s] packGen.fDone=[%d], packGen.u6Rzt=[%s] \n", sql.c_str(), packGen.fDone, u64tostr(packGen.u6Rzt).c_str()); }
					if( (packGen.fDone > 0 ) && (packGen.u6Rzt == 0) )  // Genesis Tx exist and done = 0
					{
						BitBetPack genBbp = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "", "", "", "", "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 0, 0};
						int bbpRzt = GetTxBitBetParam(txGen, genBbp);
						genBbp.tx = bbp.genBet;      int gPayIdx=0;
						if( isValidBitBetGenesisTx(txGen, genTxHei, genBbp, gPayIdx, true, (iTxHei >= BLOCK_NO_12W ? false : true)) )
						{
							sql = "select * from AllBets where tx='" + bbp.genBet + "';";
							dbOneResultCallbackPack pkG = {OneResultPack_U64_TYPE, 7, 0, 0, "tblock", ""};  // 1 = uint64_t,  tblock id = 7
							getOneResultFromDb(dbLuckChainWrite, sql, pkG);
							if( fDebug ){ printf( "isValidBitBetEncashTx: [%s] pkG.fDone=[%d], pkG.u6Rzt=[%s] \n", sql.c_str(), pkG.fDone, u64tostr(pkG.u6Rzt).c_str()); }
							if( (pkG.fDone > 0 ) && (pkG.u6Rzt > 0) && (iTxHei > pkG.u6Rzt) )   //  iTxHei > target block num
							{
				uint64_t u6Deeps = iTxHei - pkG.u6Rzt;
				if( fDebug ){ printf( "isValidBitBetEncashTx: genBbp.betType=[%d], iTxHei[%s]  - pkG.u6Rzt[%s]  = u6Deeps[%s] \n", genBbp.betType, u64tostr(iTxHei).c_str(), u64tostr(pkG.u6Rzt).c_str(), u64tostr(u6Deeps).c_str()); }
				if( u6Deeps >= BitBet_Standard_Confirms )
				{
					if( genBbp.betType == 5 )  // Bet Real Event, bet_num2 is referee decide tx's confirms
					{
						updateRefereeDecideConfirmed(bbp.genBet, iTxHei);
						sql = "select * from AllBets where tx='" + bbp.genBet + "' and opcode=1 and done=0;";
						dbOneResultCallbackPack pkG = {OneResultPack_U64_TYPE, 10, 0, 0, "bet_num2", ""};  // 1 = uint64_t,  bet_num2 id = 10
						getOneResultFromDb(dbLuckChainWrite, sql, pkG);
						if( fDebug ){ printf( "isValidBitBetEncashTx: genBbp.betType=5, pkG.fDone=%d, pkG.u6Rzt =[%s  vs %u] \n", pkG.fDone, u64tostr(pkG.u6Rzt ).c_str(), BitBet_Real_Event_Confirms); }
						if( (pkG.fDone == 0 ) || (pkG.u6Rzt < BitBet_Real_Event_Confirms) ){ return rzt; } // gen tx not exists or bet_num2 = 0
						//if( (iTxHei < pkG.u6Rzt) || ((iTxHei - pkG.u6Rzt) < BitBet_Real_Event_Confirms) ){ return rzt; }
					}
					// Check if someone is winning, at least one.   // std::vector<CTxOut> vout;
					sql = "select * from AllBets where gen_bet='" + bbp.genBet + "' and isWinner>0 Limit 1;";
					dbOneResultCallbackPack pk2 = {OneResultPack_U64_TYPE, AllBets_tblock_idx, 0, 0, "tblock", ""};      pkG = pk2;   // 1 = uint64_t,  tblock id = 7
					getOneResultFromDb(dbLuckChainWrite, sql, pkG);
					if( fDebug ){ printf( "isValidBitBetEncashTx: [%s] pkG.fDone=[%d], pkG.u6Rzt=[%s] \n", sql.c_str(), pkG.fDone, u64tostr(pkG.u6Rzt).c_str()); }
					if( (pkG.fDone > 0 ) && (pkG.u6Rzt > 0) )  // if someone is winner?
					{
						rzt = isValidBitBetTxOut(genBbp.betType, genBbp.enCashFlag, tx.vout, bbp.genBet, iTxHei, bbp.u6StartBlock);  // bool isValidTxOut(const std::vector<CTxOut> &vout, const string genBet);
						if( rzt  && (!bJustRcvTx) )   // is valid encash tx and deeps > 1, set done=1
						{
							sql = "UPDATE AllBets set done=1 where gen_bet='" + bbp.genBet + "';";
//sqlite3_exec( dbLuckChainWrite, "BEGIN", 0, 0, 0);
							int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
//sqlite3_exec( dbLuckChainWrite, "COMMIT", 0, 0, 0);
							if( fDebug ) printf("isValidBitBetEncashTx : set done=1, sql=[%s] rc =[%d] \n", sql.c_str(), rc);
							sql = "UPDATE AllBets set encashTx='"  + tx.GetHash().ToString() + "' where tx='" + bbp.genBet + "';";      sqlite3_exec(dbLuckChainWrite, sql.c_str(), 0, 0, 0);
						}
					}
				}else if( fDebug ){ printf( "isValidBitBetEncashTx: u6Deeps [%s] < [%d] \n", u64tostr(u6Deeps).c_str(), BitBet_Standard_Confirms); }
							}else if( fDebug ){ printf( "isValidBitBetEncashTx() false, iTxHei[%s] <= [%s] ??? , pkG.fDone=[%d]  \n", u64tostr(iTxHei).c_str(), u64tostr(pkG.u6Rzt).c_str(), pkG.fDone); }
						}else if( fDebug ){ printf( "isValidBitBetEncashTx: isValidBitBetGenesisTx(...) return false \n"); }
					}else{
						if( fDebug ){ printf( "isValidBitBetEncashTx: packGen.fDone=[%d], packGen.u6Rzt=[%s], sql = [%s] :( \n", packGen.fDone, u64tostr(packGen.u6Rzt).c_str(), sql.c_str()); }
						rzt = (packGen.u6Rzt > 0);   // done > 0
					}
				}
			}
			break;
		}
	}
	return rzt;
}

bool insertBitBetTx(const CTransaction tx, BitBetPack &bbp, uint64_t iTxHei, const string sTableName = "AllBets")
{
   bool rzt = false;
   if( !isBetTxExists(bbp.tx) )
   {
      bbp.u6Time = tx.nTime;
      if( bbp.opCode == 1 )  // Launch bet
	  {
		  bbp.u6BlockSpace = bbp.u6TargetBlock;   bbp.genBet = bbp.tx;   bbp.u6TotalBetAmount = bbp.u6BetAmount;
		  if( bbp.betType == 0 ) // Lucky 16
		  { 
			if( bbp.u6BetCount > 1 )
			{
				if( bbp.u6BetCount > BitBet_Lucky16_Max_Bet_Count ){ bbp.u6BetCount = BitBet_Lucky16_Max_Bet_Count; }
				bbp.u6TotalBetAmount = (bbp.u6BetAmount * bbp.u6BetCount);
			}
		  }
		  else{ bbp.u6BetCount = 1; }
		  if( iTxHei > 0 ){ bbp.u6TargetBlock = bbp.u6BlockSpace + iTxHei; }  // 2016.09.21 add
		  if( bbp.maxBetCount == 1 ){  bbp.maxBetCount++;  }   // 2016.11.12 add
		  if( bbp.betType == 5 )   // Real Event, 2016.11.12 add
		  {
			  uint64_t u6RefereeMaxCoins = getRefereeMaxCoins(dbLuckChainWrite, bbp.referee);
			  if( u6RefereeMaxCoins > 1 ){  bbp.u6MaxBetCoins = u6RefereeMaxCoins;  }   // = 0 mean no limit, else have limit bet amount
			  bbp.refereeNick = getRefereeNickName(dbLuckChainWrite, bbp.referee);
			  if( fDebug ){ printf("insertBitBetTx : betType=5, u6RefereeMaxCoins=[%s], nickName=[%s] \n", u64tostr(u6RefereeMaxCoins).c_str(), bbp.refereeNick.c_str()); }
		  }
		  bbp.confirmed = 1;   // 2016.11.17 add
      }else if( bbp.opCode == 2 )  // Join Bet
	  {
			bbp.u6StartBlock = iTxHei;      bbp.u6TotalBetAmount = bbp.u6BetAmount;
	  }
	  rzt = insertBitBetToAllBetsDB(bbp, sTableName); // save to database,   bool insertBitBetToAllBetsDB(const BitBetPack& bbp, const string genTx="AllBets");
   }
   return rzt;
}

bool acceptBitBetTx(const CTransaction& tx, uint64_t iTxHei)  // iTxHei = 0 mean that recv a tx
{
    bool rzt = true;
    BitBetPack bbp = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "", "", "", "", "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 0, 0};
    int bbpRzt = GetTxBitBetParam(tx, bbp);
	if( fDebug ){ printf("acceptBitBetTx:: bbpRzt=[%d], opCode=[%d] TxHei=[%s] \n", bbpRzt, bbp.opCode, u64tostr(iTxHei).c_str()); }
    LOCK(cs_bitbet);  // 2017.04.30
    if( bbpRzt >= 14 )
    {
        int payIdx=0;      bbp.tx = tx.GetHash().ToString();
		if( fDebug ){ printf("acceptBitBetTx:: tx[%s], opCode = [%d] \n", bbp.tx.c_str(), bbp.opCode); }
		if( bbp.opCode == 1 )         // Launch
        {
			if( isValidBitBetGenesisTx(tx, iTxHei, bbp, payIdx, false, true) )
			{
               bbp.payIndex = payIdx;
			   if( iTxHei > 1 ){
			      //bbp.payIndex = payIdx;
				  if( insertBitBetTx(tx, bbp, iTxHei) ){  DeleteAllBetsTmpTx(bbp.tx);      setPlayerInfo(tx, bbp, 1);   NotifyReceiveNewBitBetMsg(bbp);  }
			   }else{  insertBitBetTx(tx, bbp, iTxHei, "AllBetsTmp");  }   // 2016.11.29 add
			}else{
				rzt = false;      deleteBitBetTx( bbp.tx );  // 2016.10.18 16.25  add
			}
        }
        else if( bbp.opCode == 2 )  // Join
        {
			if( isValidBitBetBetTx(tx, iTxHei, bbp, payIdx) )
			{
               bbp.payIndex = payIdx;
			   if( iTxHei > 1 ){
			      //bbp.payIndex = payIdx;      
				  if( insertBitBetTx(tx, bbp, iTxHei) ){  DeleteAllBetsTmpTx(bbp.tx);      setPlayerInfo(tx, bbp, 1);      NotifyReceiveNewBitBetMsg(bbp);  }
			   }else{  insertBitBetTx(tx, bbp, iTxHei, "AllBetsTmp");  }   // 2016.11.29 add
			}else{ rzt = false; }
        }
        else if( bbp.opCode == 3 )  // encash
        {
			rzt = isValidBitBetEncashTx(tx, iTxHei, bbp);      if( rzt && (iTxHei > 1) ){  setPlayerInfo(tx, bbp, 1);  }
#ifdef QT_GUI
			if( rzt && (iTxHei > 1) ){ NotifyReceiveNewBitBetMsg(bbp); }
#endif
        }
    }
	else{  processBitBetCmdTx(tx, iTxHei);  }  //else{ if( iTxHei > 1 ){ processBitBetCmdTx(tx, iTxHei); } }

	if( (bbp.opCode == 3) && rzt ){ }
	else{
		if( isSendCoinFromBurnAddr( tx ) ){ rzt = false; }
	}
    return rzt;
}

const string sOtherBurnAddress = "BTmxxxxxxxxBURNADDRESSxxYBTy2Bp3wT, ";
bool isSendCoinFromBurnAddr(const CTransaction& tx)
{
	bool rzt = false;
	int j = 0;
	BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        uint256 hashBlock = 0;
        CTransaction txPrev;
		j++;

        if( fDebug ){ printf( "isSendCoinFromBurnAddr: (%u) txin.prevout.n = [%u], \n", j, txin.prevout.n ); }
		if( GetTransaction(txin.prevout.hash, txPrev, hashBlock) )	// get the vin's previous transaction
		{  
			//string sPrevTxMsg = txPrev.chaindata;
			CTxDestination source;
			string preTxAddress = "";
			string sPreTxHash = txPrev.GetHash().ToString();
			uint64_t iPreTxHei = GetTransactionBlockHeight(sPreTxHash);
			if( fDebug ){ printf( "isSendCoinFromBurnAddr: (%u) iPreTxHei = [%s],  sPreTxHash [%s] \n", j, u64tostr(iPreTxHei).c_str(), sPreTxHash.c_str() ); }

			if (ExtractDestination(txPrev.vout[txin.prevout.n].scriptPubKey, source))  
			{
                CBitcoinAddress addressSource(source);
				preTxAddress = addressSource.ToString();			
				//printf("isSendCoinFromBurnAddr: preTxAddress is [%s] : [%s]\n", preTxAddress.c_str(), sPrevTxMsg.c_str());
				int iPos = preTxAddress.find(BitBetBurnAddress);
				if( iPos == string::npos ){  iPos = sOtherBurnAddress.find(preTxAddress);  }  // 2016.12.18 add
				if( iPos != string::npos)   //if( isSoCoinAddress(tx, preTxAddress, iPos) > 0 )	//if (lostWallet.Get() == addressSource.Get())
				{
					if( fDebug ){ printf("isSendCoinFromBurnAddr: (%u) Send coin from [%s]  [%u], ban. \n********************\n\n\n", j, preTxAddress.c_str(), iPos); }
                    return true;
                }				
			}
		}
	}
	return rzt;
}

extern int RollbackBlocks(int64_t nBlocks);
//extern boost::signals2::signal<void (int opc, const std::string nickName, const std::string coinAddr, const std::string sFee, const std::string maxBetCoin)> NotifyaddLuckChainRefereeMsg5Param;
bool processBitBetCmdTx(const CTransaction& tx, uint64_t iTxHei)
{
    bool rzt = false;      uint64_t u6TxHei = iTxHei;
	BitBetCommand bbc = {"", "", "", "", "", "", "", "", "", "", "", 0};
	BitBetPack bbp = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "", "", "", "", "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 0, 0};
	string sTx = tx.GetHash().ToString();
	int bbcRzt = GetTxBitBetCmdParam(tx, bbc);
	if( fDebug ){ printf("processBitBetCmdTx:: [%s] [%d] [%s] \n p1[%s] [%s] [%s] [%s] [%s] \n", sTx.c_str(), bbc.paramCount, bbc.cmdName.c_str(), bbc.p1.c_str(), bbc.p2.c_str(), bbc.p3.c_str(), bbc.p4.c_str(), bbc.p5.c_str()); }
	if( bbc.paramCount > 3 )
	{
		if( bbc.cmdName == "Register Referee" )
		{
//  BitBetCMD: | Register Referee | Time_1 | NickName_2 | CoinAddress_3 | Local_4 | Fee_5 | MaxCoins_6 | Remarks_7 | Power_8 | System Sign_9 | System Coin Address_A
			//CBitcoinAddress addr(bbc.pa);
			//string sp = bbc.p1 + ", " + bbc.p2 + ", " + bbc.p3 + ", " + bbc.p4 + ", " + bbc.p5 + ", " + bbc.p6 + ", " + bbc.p7 + ", " + bbc.p8 + ", " + bbc.p9 + ", " + bbc.pa;
			//printf("processBitBetCmdTx:: [%d] [%s]\n", bbc.paramCount, sp.c_str());
			if( (bbc.paramCount > 11) && isSystemAddress(bbc.pa) )
			{
				string sMsg = bbc.p1 + "," + bbc.p2 + "," + bbc.p3;  //bbc.cmdName + "|" + bbc.p1 + "|" + bbc.p2 + "|" + bbc.p3;
				if( verifyMessage(bbc.pa, bbc.p9, sMsg) )  // bool verifyMessage(const string strAddress, const string strSign, const string strMessage)
				{
					if( !isBitBetSignExists(bbc.p9) )
					{
						insertBitBetSign(bbc.p9);
						if( insertRefereeToRefereesDB(bbc.p2, bbc.p3, bbc.p4, bbc.p5, bbc.p6, bbc.p7, bbc.p8, bbc.p1, bbc.p9, bbc.pa, tx.nTime) )
						{
//#ifdef QT_GUI_LIB
							NotifyaddLuckChainRefereeMsg5Param(0, bbc.p2, bbc.p3, bbc.p5, bbc.p6);   // sync to gui
//#endif
							rzt = true;
						}
					}else{ printf("processBitBetCmdTx : [%s] isBitBetSignExists() = true \n", bbc.cmdName.c_str()); }
				}else{ printf("processBitBetCmdTx : [%s] verifyMessage(%s, %s, %s) faile \n", bbc.cmdName.c_str(), bbc.pa.c_str(), bbc.p9.c_str(), sMsg.c_str()); }
			}else{ printf("processBitBetCmdTx : [%s] err1 [%s]\n", bbc.cmdName.c_str(), bbc.pa.c_str()); }
		}
		else if( bbc.cmdName == "Edit Referee" )
		{
//  BitBetCMD: | Edit Referee | Time_1 | NickName_2 | CoinAddress_3 | valid_4 | Fee_5 | MaxCoins_6 | Remarks_7 | Power_8 | System Sign_9 | System Coin Address_A
			if( (bbc.paramCount > 11) && isSystemAddress(bbc.pa) )
			{
				string sMsg = bbc.p1 + "," + bbc.p2 + "," + bbc.p3;  // Time,NickName,CoinAddress
				if( verifyMessage(bbc.pa, bbc.p9, sMsg) )  // bool verifyMessage(const string strAddress, const string strSign, const string strMessage)
				{
					if( !isBitBetSignExists(bbc.p9) )
					{
						insertBitBetSign(bbc.p9);
						if( updateRefereeOfRefereesDB(bbc.p2, bbc.p3, bbc.p4, bbc.p5, bbc.p6, bbc.p7, bbc.p8, bbc.p1, bbc.p9, bbc.pa) )
						{
							int k = atoi(bbc.p4), opc=1;  // p4 = valid,  opc = 1 = edit
							if( k == 0 ){ opc=2; }  // del
							NotifyaddLuckChainRefereeMsg5Param(opc, bbc.p2, bbc.p3, bbc.p5, bbc.p6);   // sync to gui
							rzt = true;
						}
					}else{ printf("processBitBetCmdTx : [%s] isBitBetSignExists() = true \n", bbc.cmdName.c_str()); }
				}else{ printf("processBitBetCmdTx : [%s] verifyMessage(%s, %s, %s) faile \n", bbc.cmdName.c_str(), bbc.pa.c_str(), bbc.p9.c_str(), sMsg.c_str()); }
			}else{ printf("processBitBetCmdTx : [%s] err1 [%s]\n", bbc.cmdName.c_str(), bbc.pa.c_str()); }
		}
		else if( bbc.cmdName == "Edit User" )
		{
//  BitBetCMD: | Edit User | Time_1 | NickName_2 | CoinAddress_3 | weight_4 | flag_5 | Remarks_6 | System Sign_7 | System Coin Address_8
			if( (bbc.paramCount > 9) && isSystemAddress(bbc.p8) )
			{
				string sMsg = bbc.p1 + "," + bbc.p2 + "," + bbc.p3;  // Time,NickName,CoinAddress
				if( verifyMessage(bbc.p8, bbc.p7, sMsg) )  // bool verifyMessage(const string strAddress, const string strSign, const string strMessage)
				{
					if( !isBitBetSignExists(bbc.p7) )
					{
						insertBitBetSign(bbc.p7);   // bool addOrEditUser(string sNick, string sAddr, string sWeight, string sFlag, string sRemarks, string sSign, uint64_t nTime)
						if( bbc.p5 == "000" )
						{
							string sql = "DELETE FROM Users where coinAddr='" + bbc.p3 + "';";
							sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, NULL, NULL);
						}
						else{  addOrEditUser(bbc.p2, bbc.p3, bbc.p4, bbc.p5, bbc.p6, bbc.p7, tx.nTime);  }
						rzt = true;
					}else{ printf("processBitBetCmdTx : [%s] isBitBetSignExists() = true \n", bbc.cmdName.c_str()); }
				}else{ printf("processBitBetCmdTx : [%s] verifyMessage(%s, %s, %s) faile \n", bbc.cmdName.c_str(), bbc.p8.c_str(), bbc.p7.c_str(), sMsg.c_str()); }
			}else{ printf("processBitBetCmdTx : [%s] err1 [%s]\n", bbc.cmdName.c_str(), bbc.p8.c_str()); }
		}
		else if( bbc.cmdName == "Rollback Blocks" )
		{
//  BitBetCMD: | Rollback Blocks | Time_1 | CheckBlockNumber_2 | CheckBlkNumberHash_3 | RollbackBlocks_4 | System Sign_5 | System Coin Address_6
			if( (bbc.paramCount > 7) && isSystemAddress(bbc.p6) )
			{
				string sMsg = bbc.p1 + "," + bbc.p2 + "," + bbc.p3;  // Time,NickName,CoinAddress
				if( verifyMessage(bbc.p6, bbc.p5, sMsg) )  // bool verifyMessage(const string strAddress, const string strSign, const string strMessage)
				{
					if( !isBitBetSignExists(bbc.p5) )
					{
						uint64_t u6RollBlks = 0,  u6ChkBlk = strToInt64( bbc.p2.c_str() );      string sChkBlkHash = "";
						if( (u6ChkBlk > 0) && ((uint64_t)nBestHeight >= u6ChkBlk) )
						{
							sChkBlkHash = GetBlockHashStr(u6ChkBlk);
							if( (bbc.p3.length() > 60) && (sChkBlkHash.length() > 60) && (sChkBlkHash != bbc.p3) )
							{
								insertBitBetSign(bbc.p5);
								if( bbc.p4 == "-" ){ u6RollBlks = 300; }
								else{  u6RollBlks = strToInt64( bbc.p4.c_str() );  }
								if( u6RollBlks > 0 ){  RollbackBlocks( u6RollBlks );  }
								rzt = true;
							}else if( sChkBlkHash == bbc.p3 ){  insertBitBetSign(bbc.p5);  }
						}
						if( fDebug ){  printf("processBitBetCmdTx : rzt=[%d], u6RollBlks=[%s], sChkBlkHash=[%s] [%s] \n", rzt, u64tostr(u6RollBlks).c_str(), sChkBlkHash.c_str(), bbc.p3.c_str());  }
					}else{ printf("processBitBetCmdTx : [%s] isBitBetSignExists() = true \n", bbc.cmdName.c_str()); }
				}else{ printf("processBitBetCmdTx : [%s] verifyMessage(%s, %s, %s) faile \n", bbc.cmdName.c_str(), bbc.p6.c_str(), bbc.p5.c_str(), sMsg.c_str()); }
			}else{ printf("processBitBetCmdTx : [%s] err1 [%s]\n", bbc.cmdName.c_str(), bbc.p6.c_str()); }
		}
		else if( bbc.cmdName == "Referee Decide" )
		{
//  BitBetCMD: | Referee Decide | Time_1 | Gen Bet_2 | Decide_3 | Sign_4 | Referee Coin Address_5
			if( (u6TxHei > 1) && (bbc.paramCount > 4) && (bbc.p3.length() > 0) )
			{
				string sMsg = bbc.p1 + "," + bbc.p2 + "," + bbc.p3;  // Time,Gen Bet Tx, Decide
				if( verifyMessage(bbc.p5, bbc.p4, sMsg) )  // bool verifyMessage(const string strAddress, const string strSign, const string strMessage)
				{
string sql = "select * from AllBets where tx='" + bbc.p2 + "' and opcode=1 and bet_type=5 and done=0 and confirmed>=" + inttostr(BitBet_Standard_Confirms) + " and referee='" + bbc.p5 + "' and length(refereeDecideTx)<10;";
					if( fDebug ){ printf("processBitBetCmdTx : [%s] [%s] \n", bbc.cmdName.c_str(), sql.c_str()); }
					dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, AllBets_tblock_idx, 0, 0, "tblock", ""};  // 1 = uint64_t,  bet_amount id = 3
					getOneResultFromDb(dbLuckChainWrite, sql, pack);
					if( pack.fDone > 0 )  //   uint64_t u6Rzt;
					{
						uint64_t u6TgBlkNum = pack.u6Rzt;
						if( u6TxHei >= (u6TgBlkNum + BitBet_Standard_Confirms) )
						{
				//pack = {OneResultPack_U64_TYPE, AllBets_confirmed_idx, 0, 0, "confirmed", ""};  // 1 = uint64_t,  bet_amount id = 3
				//getOneResultFromDb(dbLuckChainRead, sql, pack);
				//if( (pack.fDone > 0) && (pack.u6Rzt >= BitBet_Standard_Confirms) )
				//{
							if( !isBitBetSignExists(bbc.p4) )
							{
								insertBitBetSign(bbc.p4);   // bool updateBetRefereeDecide(int opc, const string genTx, const string sDecide, const string referee, const string decideTx)
								if( updateBetRefereeDecide(1, bbc.p2, bbc.p3, bbc.p5, sTx) ){
									updateRefereeDecideCount(bbc.p5, 1);   bbp.opCode=5;   bbp.referee=bbc.p5;    NotifyReceiveNewBitBetMsg(bbp);
								}
							}else{ printf("processBitBetCmdTx : [%s] isBitBetSignExists() = true \n", bbc.cmdName.c_str()); }
				//}else if(fDebug){ printf("processBitBetCmdTx : [%s] confirmed [%s] < [%d] \n", bbc.cmdName.c_str(), u64tostr(pack.u6Rzt).c_str(), BitBet_Standard_Confirms); }
						}else if(fDebug){ printf("processBitBetCmdTx : [%s] u6TxHei [%s] < [%s] \n", bbc.cmdName.c_str(), u64tostr(u6TxHei).c_str(), u64tostr(u6TgBlkNum + BitBet_Standard_Confirms).c_str()); }
					}else if( fDebug ){ printf("processBitBetCmdTx : [%s] pack.fDone=0, [%s] :( \n", bbc.cmdName.c_str(), sql.c_str()); }
				}else{ printf("processBitBetCmdTx : [%s] verifyMessage(%s, %s, %s) faile \n", bbc.cmdName.c_str(), bbc.pa.c_str(), bbc.p9.c_str(), sMsg.c_str()); }
			}else{ printf("processBitBetCmdTx : [%s] err paramCount=[%d] \n", bbc.cmdName.c_str(), bbc.paramCount, bbc.p3.c_str()); }
		}
		else if( bbc.cmdName == "Set Bet Referee" )
		{
//  BitBetCMD: | Set Bet Referee | Time_1 | Gen Bet_2 | New Referee_3 | System Sign_4 | System Coin Address_5
			if( (bbc.paramCount > 4) && isSystemAddress(bbc.p5) )
			{
				string sMsg = bbc.p1 + "," + bbc.p2 + "," + bbc.p3;  // Time, Gen Bet Tx, New Referee
				if( verifyMessage(bbc.p5, bbc.p4, sMsg) )  // bool verifyMessage(const string strAddress, const string strSign, const string strMessage)
				{
					if( !isBitBetSignExists(bbc.p4) )
					{
						insertBitBetSign(bbc.p4);  // bool updateBetRefereeDecide(int opc, const string genTx, const string sDecide, const string referee, const string decideTx)
						if( updateBetRefereeDecide(2, bbc.p2, "-", bbc.p3, "-") ){ bbp.opCode=6;   bbp.referee=bbc.p3;    NotifyReceiveNewBitBetMsg(bbp); }
					}else{ printf("processBitBetCmdTx : [%s] isBitBetSignExists() = true \n", bbc.cmdName.c_str()); }
				}else{ printf("processBitBetCmdTx : [%s] verifyMessage(%s, %s, %s) faile \n", bbc.cmdName.c_str(), bbc.p5.c_str(), bbc.p4.c_str(), sMsg.c_str()); }
			}else{ printf("processBitBetCmdTx : [%s] err1 [%s]\n", bbc.cmdName.c_str(), bbc.p5.c_str()); }
		}
		else if( bbc.cmdName == "Set Settings" )
		{
//  BitBetCMD: | Set Bet Referee | Time_1 | Field Name_2 | New Value_3 | System Sign_4 | System Coin Address_5 |
			if( (bbc.paramCount > 4) && isSystemAddress(bbc.p5) )
			{
				string sMsg = bbc.p1 + "," + bbc.p2 + "," + bbc.p3;  // Time, Field Name, New Value
				if( verifyMessage(bbc.p5, bbc.p4, sMsg) )  // bool verifyMessage(const string strAddress, const string strSign, const string strMessage)
				{
					if( !isBitBetSignExists(bbc.p4) )
					{
						insertBitBetSign(bbc.p4);  // bool updateBitBetSettings(const string fieldName, const string sValue)
						updateBitBetSettings(bbc.p2, bbc.p3);
					}else{ printf("processBitBetCmdTx : [%s] isBitBetSignExists() = true \n", bbc.cmdName.c_str()); }
				}else{ printf("processBitBetCmdTx : [%s] verifyMessage(%s, %s, %s) faile \n", bbc.cmdName.c_str(), bbc.p5.c_str(), bbc.p4.c_str(), sMsg.c_str()); }
			}else{ printf("processBitBetCmdTx : [%s] err1 [%s]\n", bbc.cmdName.c_str(), bbc.p5.c_str()); }
		}
		else if( bbc.cmdName == "Evaluate Referee" )
		{
//  BitBetCMD: | Evaluate Referee | Time_1 | Referee_2 | Evaluate_3 | Sign_4 | Your Coin Address_5
// string sTime = u64tostr( GetAdjustedTime() ),   sMsg = sTime + "," + sReferee + "," + sEvaluate;
			if( (u6TxHei > 1) && (bbc.paramCount > 4) && (bbc.p3.length() > 0) && (bbc.p2.length() > 33) )
			{
				string sMsg = bbc.p1 + "," + bbc.p2 + "," + bbc.p3;  // Time, sReferee, Evaluate
				if( verifyMessage(bbc.p5, bbc.p4, sMsg) )  // bool verifyMessage(const string strAddress, const string strSign, const string strMessage)
				{
					if( !isBitBetSignExists(bbc.p4) )
					{
						uint64_t u6CanEva = bettorCanEvaluateReferee(dbLuckChainWrite, bbc.p2, bbc.p5);
						if( u6CanEva > 0 )				
						{
							insertBitBetSign(bbc.p4);   // bool updateBetRefereeDecide(int opc, const string genTx, const string sDecide, const string referee, const string decideTx)
							if( insertBitBetEvaluate(bbc) ){
if( updateBitBetRefereeEvaluate(bbc.p2, bbc.p3) ){   bbp.opCode=7;   bbp.betNum=bbc.p3;   bbp.referee=bbc.p2;    NotifyReceiveNewBitBetMsg(bbp); }
							}
						}else{ printf("processBitBetCmdTx : [%s] u6CanEva = 0 \n", bbc.cmdName.c_str()); }
					}else{ printf("processBitBetCmdTx : [%s] isBitBetSignExists() = true \n", bbc.cmdName.c_str()); }
				}else{ printf("processBitBetCmdTx : [%s] verifyMessage(%s, %s, %s) faile \n", bbc.cmdName.c_str(), bbc.pa.c_str(), bbc.p9.c_str(), sMsg.c_str()); }
			}else{ printf("processBitBetCmdTx : [%s] err paramCount=[%d] [%s] \n", bbc.cmdName.c_str(), bbc.paramCount, bbc.p3.c_str()); }
		}
	}
}

int getBitBetTxType(const CTransaction& tx)
{
    int rzt = 0;
    BitBetPack bbp = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "", "", "", "", "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 0, 0};
    int bbpRzt = GetTxBitBetParam(tx, bbp);
    if( bbpRzt > 13 ){ rzt = bbp.opCode; }
	return rzt;
}

bool isBitBetEncashTx(const CTransaction& tx)
{
    bool rzt = false;  //( getBitBetTxType(tx) == 3 );
    BitBetPack bbp = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "", "", "", "", "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 0, 0};
    int bbpRzt = GetTxBitBetParam(tx, bbp);
    if( (bbpRzt > 15) && (bbp.opCode == 3) ){ rzt=true; }
	//if( fDebug ){ printf("isBitBetEncashTx:: rzt=[%d], bbpRzt=[%d] opCode = [%d] [%s] \n", rzt, bbpRzt, bbp.opCode, tx.chaindata.c_str()); }
	return rzt;
}

bool isBitBetEncashTx(const CTransaction& tx, uint64_t& u6ToMinerFee)
{
    bool rzt = false;      u6ToMinerFee = 0;
    BitBetPack bbp = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", "", "", "", "", "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 0, 0};
    int bbpRzt = GetTxBitBetParam(tx, bbp);
    if( (bbpRzt > 15) && (bbp.opCode == 3) ){  u6ToMinerFee = bbp.u6StartBlock;      rzt=true;  }
	return rzt;
}

uint64_t bettorCanEvaluateReferee(sqlite3 *dbOne, const string sReferee, const string sBettor)
{  // int getRunSqlResultCount(sqlite3 *dbOne, const string sql, uint64_t &rzt)
	uint64_t rzt = 0;
	if( (sReferee.length() < 34) || (sBettor.length() < 34) ){ return rzt; }
	uint64_t u6PlayWithReferee=0;
	string sql =  "select Count(*) from AllBets where done>0 and bet_type=5 and referee='" + sReferee + "' and bettor='" + sBettor + "';";
	int rc = getRunSqlResultCount(dbOne, sql, u6PlayWithReferee);
	if( fDebug ){ printf("bettorCanEvaluateReferee :: rc=[%d],  u6PlayWithReferee=[%s] [%s] \n", rc, u64tostr(u6PlayWithReferee).c_str(), sql.c_str()); }
	if( rc != SQLITE_OK ){ return rzt; }
	uint64_t u6Used=0;
	sql =  "select Count(*) from Comment where target='" + sReferee + "' and user='" + sBettor + "';";
	rc = getRunSqlResultCount(dbOne, sql, u6Used);
	if( fDebug ){ printf("bettorCanEvaluateReferee :: rc=[%d],  u6Used=[%s] [%s] \n", rc, u64tostr(u6Used).c_str(), sql.c_str()); }
	if( rc != SQLITE_OK ){ return rzt; }
	if( u6PlayWithReferee > u6Used ){ rzt = u6PlayWithReferee - u6Used; }
	/*if( u6PlayWithReferee <= u6Used )
	{
		printf("bettorCanEvaluateReferee : u6PlayWithReferee[%s]  <= u6Used[%s] :( \n", rc, u6tostr(u6PlayWithReferee).c_str(), u6tostr(u6Used).c_str());
		return rzt;
	}*/
	return rzt;
}

bool insertBitBetEvaluate(const BitBetCommand& bbc)
{
   bool rzt = false;
//  BitBetCMD: | Evaluate Referee | Time_1 | Referee_2 | Evaluate_3 | Sign_4 | Your Coin Address_5
   string s = "'" + bbc.p5 + "', '" + bbc.p2 + "', '" + bbc.p3 + "', " + bbc.p1 + ", '" + bbc.p4 + "'";
   string sql = "INSERT INTO Comment (user, target, content, ntime, sign) VALUES (" + s + ");";
   char* pe = 0;
   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), 0, 0, &pe);      rzt = (rc == SQLITE_OK);
   if( fDebug ){ printf("insertBitBetEvaluate :: [%d] [%s] [%s]\n", rc, pe, sql.c_str()); }
   if( pe ){ sqlite3_free(pe); }
   return rzt;
}

bool updateBitBetRefereeEvaluate(const string sReferee, const string sEva)
{
   bool rzt=false;      string sFor="",   sql = "SELECT * from Referees where coinAddress='" + sReferee + "';";
   if( sEva == "+1" ){ sFor = "good"; }
   else{ sFor = "bad"; }
   dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, -1, 0, 0, sFor, ""};
	getOneResultFromDb(dbLuckChainWrite, sql, pack);
   if( fDebug ){ printf("updateBitBetRefereeEvaluate :: fDone=[%d] u6Rzt=[%s] sql=[%s] \n", pack.fDone, u64tostr(pack.u6Rzt).c_str(), sql.c_str()); }
   if( pack.fDone > 0 )
   {
       char* pe=0;      uint64_t u6 = pack.u6Rzt + 1;
	   sql = "UPDATE Referees set " + sFor + "=" + u64tostr(u6) + " where coinAddress='" + sReferee + "';";
	   int rc = sqlite3_exec(dbLuckChainWrite, sql.c_str(), 0, 0, &pe);      rzt = (rc == SQLITE_OK);
	   if( fDebug ) printf("updateBitBetRefereeEvaluate:: rc=[%d] [%s] [%s] \n", rc, pe, sql.c_str());
	   if( pe ){ sqlite3_free(pe); }
	}
   return rzt;
}

extern bool bNormalMinerWeight;
void balancedMining()
{
	if( pwalletMain != NULL )
	{
		int64_t i6b = pwalletMain->GetBalance() + pwalletMain->GetStake() + pwalletMain->GetUnconfirmedBalance() + pwalletMain->GetImmatureBalance();
		if( i6b >= Balanced_Mining_Amount  ){  bNormalMinerWeight = false;  }
		else{
			for(int i=0; i<60; i++)
			{
				MilliSleep(1000);
				if (fShutdown){  return;  }
			}
		}
	}
	MilliSleep(1000);
}
bool checkUserWeight(const string sAddr)
{
	bool rzt=true;
	//if( nBestHeight >= New_Rules_161129_BLK_10W )
	{
		string sql = "select * from Users where coinAddr='" + sAddr + "';";
		//if( fDebug ){ printf("checkUserWeight : [%s] \n", sql.c_str()); }
		dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, 9, 0, 0, "weight", ""};  // 1 = uint64_t, 
		getOneResultFromDb(dbLuckChainWrite, sql, pack);
		if( pack.fDone > 0 )  //   uint64_t u6Rzt;
		{
			if( pack.u6Rzt < 1 )
			{
				rzt = false;
				if( isMineCoinAddress(sAddr) ){  bNormalMinerWeight = false;  }
			}
		}
		if( !rzt ){ printf("checkUserWeight() : rzt=[%d], fDone=[%d : %s], [%s] \n", rzt, pack.fDone, u64tostr(pack.u6Rzt).c_str(), sql.c_str()); }
	}
	return rzt;
}


#define	LOGIC_MASK_COLOR			0xF0
#define	LOGIC_MASK_VALUE			0x0F
int myRandom(int max, int min)
{
    return rand()%(max - min + 1) + min;
}
BYTE GetCardValue(BYTE cbCardData) { return cbCardData&LOGIC_MASK_VALUE; }
BYTE GetOXLogicValue(BYTE cbCardData)
{
	//BYTE bCardColor=GetCardColor(cbCardData);
	BYTE bCardValue = GetCardValue(cbCardData);
	return (bCardValue>10) ? (10) : bCardValue;
}

//Get OX
bool GetOxCard(BYTE cbCardData[], BYTE cbCardCount)
{
	if( cbCardCount != MAX_OX_COUNT ){  return false;  }   //ASSERT(cbCardCount==MAX_OX_COUNT);

	BYTE bTemp[MAX_OX_COUNT], bTempData[MAX_OX_COUNT];
	memcpy(bTempData, cbCardData,sizeof(bTempData));
	BYTE bSum=0;
	for (BYTE i=0;i<cbCardCount;i++)
	{
		bTemp[i]=GetOXLogicValue(cbCardData[i]);
		bSum+=bTemp[i];
	}
	memcpy(bTempData, bTemp,sizeof(bTempData));

	//Find OX
	for (BYTE i=0;i<cbCardCount-1;i++)
	{
		for (BYTE j=i+1;j<cbCardCount;j++)
		{
			if((bSum-bTemp[i]-bTemp[j])%10==0)
			{
				BYTE bCount=0;
				for (BYTE k=0;k<cbCardCount;k++)
				{
					if(k!=i && k!=j)
					{
						cbCardData[bCount++] = bTempData[k];
					}
				}//ASSERT(bCount==3);

				cbCardData[bCount++] = bTempData[i];
				cbCardData[bCount++] = bTempData[j];

				return true;
			}
		}
	}
	return false;
}

int GetOXParamFromStr(const std::string betStr, BYTE cbCardData[])
{
	int rzt = 0;
	string stxData = "";
	int iLen = betStr.length();   if( iLen > MAX_BitBet_Str_Param_Len ){ return rzt; }
	if( betStr.length() > 0 ){ stxData = betStr.c_str(); }
	{
		int i = 0;
		try{
		char *delim = ",";
					
		char * pp = (char *)stxData.c_str();
        char *reserve;
		char *pch = strtok_r(pp, delim, &reserve);
		while (pch != NULL)
		{
			i++;
			if( i > 5 ){ break; }
			cbCardData[i-1] = strToInt(pch, 10);
			pch = strtok_r(NULL, delim, &reserve);
		}
		}catch (std::exception &e) {
			printf("GetOXParamFromStr:: err [%s]\n", e.what());
		}
		rzt = i;
	}
	return rzt;
}

int GetMaxOXNumb(BYTE cbCardData[])
{
	int rzt=0;
	for (BYTE i=0; i<MAX_OX_COUNT; i++)
	{
		BYTE v = cbCardData[i];
		if( (v < 10) && (v > rzt) ){ rzt = v; }
	}
	return rzt;
}
int GetOxCard(const string sCardData)
{
	BYTE cbCardData[MAX_OX_COUNT];   memset(&cbCardData[0], 0, sizeof(cbCardData));
	int rzt=0, i = GetOXParamFromStr(sCardData, cbCardData);
	printf("GetOxCard [%s], GetOXParamFromStr()=[%d] \n", sCardData.c_str(), i);
	//if( i >= MAX_OX_COUNT )
	{
		//i = MAX_OX_COUNT;
		bool b = GetOxCard(cbCardData, MAX_OX_COUNT);
		string s = strprintf("%d,%d,%d,%d,%d", cbCardData[0], cbCardData[1], cbCardData[2], cbCardData[3], cbCardData[4]);
		if( b )
		{
			rzt = cbCardData[0] + cbCardData[1] + cbCardData[2];      int z = cbCardData[3] + cbCardData[4];
			if( (z == 10) || (z == 20) ){  rzt = rzt + z;  }
			else{  rzt = rzt + ( z % 10);  }
		}else{ rzt = GetMaxOXNumb(cbCardData); }
		printf("GetOxCard b = [%d], card=[%s :: %s], OX = [%d] \n", b, s.c_str(), sCardData.c_str(), rzt);
	}
	return rzt;
}

bool isDuplicatesOXNums( const string sBetNum )
{
	bool rzt = false;
	BitBetBossPack bp = {0, 0, 0, 0, 0, 0, 0};
	int i = getBetBossNums(sBetNum, bp);
	if( i > 4 )
	{
		if( (bp.b1 == bp.b2) || (bp.b1 == bp.b3) || (bp.b1 == bp.b4) || (bp.b1 == bp.b5) || (bp.b1 == bp.b6) ){ return true; }
		if( (bp.b2 == bp.b1) || (bp.b2 == bp.b3) || (bp.b2 == bp.b4) || (bp.b2 == bp.b5) || (bp.b2 == bp.b6) ){ return true; }
		if( (bp.b3 == bp.b1) || (bp.b3 == bp.b2) || (bp.b3 == bp.b4) || (bp.b3 == bp.b5) || (bp.b3 == bp.b6) ){ return true; }
		if( (bp.b4 == bp.b1) || (bp.b4 == bp.b2) || (bp.b4 == bp.b3) || (bp.b4 == bp.b5) || (bp.b4 == bp.b6) ){ return true; }
		if( (bp.b5 == bp.b1) || (bp.b5 == bp.b2) || (bp.b5 == bp.b3) || (bp.b5 == bp.b4) || (bp.b5 == bp.b6) ){ return true; }
		//if( (bp.b6 == bp.b1) || (bp.b6 == bp.b2) || (bp.b6 == bp.b3) || (bp.b6 == bp.b4) || (bp.b6 == bp.b5) ){ return true; }
	}
	return rzt;
}

int hexStrToDec(char c)
{
	string s  = strprintf("0x%c", c);
	return strToInt(s.c_str(), 16);
}
string hexStrToDecStr(char c)
{
	int i = hexStrToDec(c);
	//printf("hexStrToDecStr(%c) :: [%d] \n", c, i);
	return inttostr(i);
}
string explainOXNums(int iBetLen, const string sBetNum, const string sBlkHash)
{
	string rzt="";
	if( (iBetLen  > 0) && (sBetNum.length() > 0) && (sBlkHash.length() > 63) )
	{
		BitBetBossPack bbbp = {0, 0, 0, 0, 0, 0, 0};
		int i = getBetBossNums(sBetNum, bbbp);
		if( i > 4 )
		{
			const char* p = sBlkHash.c_str();      int k=0;
			/* if( (bbbp.b1 >=0 ) && (bbbp.b1 < 64) ){ rzt = rzt + "0x" + p[bbbp.b1] + ",";  k++; }
			if( (bbbp.b2 >=0 ) && (bbbp.b2 < 64) ){ rzt = rzt + "0x" + p[bbbp.b2] + ",";  k++; }
			if( (bbbp.b3 >=0 ) && (bbbp.b3 < 64) ){ rzt = rzt + "0x" + p[bbbp.b3] + ",";  k++; }
			if( (bbbp.b4 >=0 ) && (bbbp.b4 < 64) ){ rzt = rzt + "0x" + p[bbbp.b4] + ",";  k++; }
			if( (bbbp.b5 >=0 ) && (bbbp.b5 < 64) ){ rzt = rzt + "0x" + p[bbbp.b5] + ",";  k++; } */
			//if( (bbbp.b6 >=0 ) && (bbbp.b6 < 64) ){ rzt = rzt + p[bbbp.b6]; }
			//if( rzt.length() > 0 ) rzt = "0x" + rzt;
			if( (bbbp.b1 >=0 ) && (bbbp.b1 < 64) ){ rzt = rzt + hexStrToDecStr(p[bbbp.b1]) + ",";  k++; }
			if( (bbbp.b2 >=0 ) && (bbbp.b2 < 64) ){ rzt = rzt + hexStrToDecStr(p[bbbp.b2]) + ",";  k++; }
			if( (bbbp.b3 >=0 ) && (bbbp.b3 < 64) ){ rzt = rzt + hexStrToDecStr(p[bbbp.b3]) + ",";  k++; }
			if( (bbbp.b4 >=0 ) && (bbbp.b4 < 64) ){ rzt = rzt + hexStrToDecStr(p[bbbp.b4]) + ",";  k++; }
			if( (bbbp.b5 >=0 ) && (bbbp.b5 < 64) ){ rzt = rzt + hexStrToDecStr(p[bbbp.b5]) + ",";  k++; }
			if( k != 5 ){ rzt = ""; }
		}
		if( fDebug ){ printf("explainOXNums : sBetNum=[%s], getBetBossNums() return [%d], rzt = [%s] \n", sBetNum.c_str(), i, rzt.c_str()); }
	}
	return rzt;
}

int GetOxCardFromBlock(int64_t i6BlockNumb, const string sBetNums, string& sRztCardData)
{
	int rzt = 0;
	string sBlkHash = GetBlockHashStr(i6BlockNumb);
	printf("GetOxCardFromBlock :: blockhash is [%s] \n", sBlkHash.c_str());
	if( sBlkHash.length() > 60 )
	{
		//string sBetNums = strprintf("%d,%d,%d,%d,%d,", myRandom(63, 0), myRandom(63, 0), myRandom(63, 0), myRandom(63, 0), myRandom(63, 0));
		printf("GetOxCardFromBlock :: sBetNums is [%s] \n", sBetNums.c_str());
		//BitBetBossPack bp = {0, 0, 0, 0, 0, 0, 0};
		//int i = getBetBossNums(sBetNums, bp);
		//printf("GetOxCardFromBlock :: i=[%d], sBetNums is [%s] \n", i, sBetNums.c_str());
		//if( i > 4 )
		{
			sRztCardData = explainOXNums(5, sBetNums, sBlkHash);
			printf("GetOxCardFromBlock :: sRztCardData is [%s] \n", sRztCardData.c_str());
			if( sRztCardData.length() > 9 )
			{
				rzt = GetOxCard(sRztCardData);
			}
		}
	}
	return rzt;
}

bool copyFile(const char *SourceFile, const char *NewFile)
{
    bool rzt=false;   ifstream in;   ofstream out;
    in.open(SourceFile, ios::binary);
    if( in.fail() )
    {
       in.close();   out.close();   return rzt;
    }
    out.open(NewFile, ios::binary);
    if(out.fail())
    {
       in.close();   out.close();
    }else{
        out << in.rdbuf();   out.close();   in.close();   rzt=true;
    }
	return rzt;
}
void myDeleteFile(const string sFn)
{
#ifndef WIN32
					remove(sFn.c_str());
#else
					DeleteFileA(sFn.c_str());
#endif
}

extern int rollBackToBlock(int64_t nBlockNum, CTxDB &txdb, bool bLock);
void updateNewBestBlkNum(int64_t blkNum)
{
    string sql = strprintf("update Settings set best_blknum=%s;", i64tostr(blkNum).c_str());      sqlite3_exec(dbLuckChainWrite, sql.c_str(), NULL, 0, NULL);
	//if( fDebug ){ printf("updateNewBestBlkNum(%s):: i6BackupLuckChainDb_BlkNum=[%s], sBackupLuckChainDbFn=[%s] \n", i64tostr(blkNum).c_str(), i64tostr(i6BackupLuckChainDb_BlkNum).c_str(), sBackupLuckChainDbFn.c_str()); }
	bool bAutoRest = GetArg("-autobackupdb", 0) > 0;
	if( bAutoRest && (blkNum > 100000) && (blkNum > i6BackupLuckChainDb_BlkNum) )
	{
		int64_t i6 = i6BackupLuckChainDb_BlkNum + (3 * 60);  //(60 * 24);
		if( (i6BackupLuckChainDb_BlkNum < 10000) || (blkNum > i6) )
		{
			string sNewDb = strprintf("%s/%s.db", sDataDbDir.c_str(), i64tostr(blkNum).c_str());
			if( copyFile(sLuckChainDbFn.c_str(), sNewDb.c_str()) )
			{
				if( sBackupLuckChainDbFn.length() > 6 ){ myDeleteFile(sBackupLuckChainDbFn); }   // delete old backup db file
				i6BackupLuckChainDb_BlkNum = blkNum;   sBackupLuckChainDbFn = sNewDb;
				ReadOrWriteLuckChainBackupInfoToDB(false);
			}
			if( fDebug ){ printf("updateNewBestBlkNum(%s):: sNewDb=[%s] \n", i64tostr(blkNum).c_str(),  sNewDb.c_str()); }
		}
	}
}

bool rollBackBlocksAndLuckChainDb(CTxDB &txdb, bool bForce)
{
	bool rzt=false;
	if( !bForce ){ if( GetArg("-autorestluckchain", 0) < 1 ){ return rzt; } }

	if( fDebug ){ printf("rollBackBlocksAndLuckChainDb():: nBestHeight=[%s], i6BackupLuckChainDb_BlkNum=[%s], sBackupLuckChainDbFn=[%s] \n", i64tostr(nBestHeight).c_str(), i64tostr(i6BackupLuckChainDb_BlkNum).c_str(), sBackupLuckChainDbFn.c_str()); }
	if( (i6BackupLuckChainDb_BlkNum > 100000) && (sBackupLuckChainDbFn.length() > 6) )
	{
		bLuckChainRollbacking=true;      closeLuckChainDB();
		if( copyFile(sBackupLuckChainDbFn.c_str(), sLuckChainDbFn.c_str()) )
		{
			int64_t i6 = i6BackupLuckChainDb_BlkNum + 1;      openSqliteDb();      rollBackToBlock(i6, txdb, false);      rzt = true;
		}else{ openSqliteDb(); }
		bLuckChainRollbacking = false;
	}
	if( fDebug ){ printf("rollBackBlocksAndLuckChainDb():: return [%d] \n", rzt); }

	return rzt;
}

bool rollBackBlocksAndLuckChainDb(bool bForce)
{
    CTxDB txdb;   return rollBackBlocksAndLuckChainDb(txdb, bForce);
}





// Queue PoS
int64_t GetQueuePoSRulesActiveHeight()
{
    return (fTestNet ? Queue_PoS_Rules_Acitve_Height_Test : Queue_PoS_Rules_Acitve_Height);
}

string GetSystemNodeById(int id)
{
    if( id == 1 ){ return (fTestNet ? "mosd3MhiW4CwcP58c2wdTtC6thXGck26mZ" : "BNbuur26wqJDH5JeKyN8q9b3u5jFG8fxzt"); }
    else if( id == 2 ){ return (fTestNet ? "mh2wDwFySmP41Kv6qERgx69PtZCFUPXvPa" : "BLPimaos8mHooaaVsMNziU85Pm92k9t6CW"); }
    else if( id == 3 ){ return (fTestNet ? "mzmVatwtAJM6RRCs3BYask8DMYD7jgFED8" : "B9SqeMBvgYQQ5v9pZv3WhSSfBZsGU1BQGW"); }
    else if( id == 4 ){ return (fTestNet ? "mxp1BXxdcASG7HDbc7v4Wq8NCefhdjAEG1" : "BMh5DdJKP6Q6i9qA7RPAjbsxF3p4ZHcCAK"); }
    else if( id == 5 ){ return (fTestNet ? "mfizwT1WuhmavT1Uhwu4Fu1XA7F7bjoBXC" : "BS84eHn684cdM7n5pGhEJS5YxbFcMtCTSB"); }
    else if( id == 6 ){ return (fTestNet ? "mspPHkP8ayN943nbEqPj939fDEDejwAuiC" : "BJtiVN4xkWHGmB9Pizvu7K4Vgvb5Jc85h5"); }
}

string strAllSystemNode="";
string GetSystemNodes()
{
    if( strAllSystemNode.length() < 34 ){ strAllSystemNode = GetSystemNodeById(1) + "," + GetSystemNodeById(2) + "," + GetSystemNodeById(3) + "," + GetSystemNodeById(4) + "," + GetSystemNodeById(5) + "," + GetSystemNodeById(6); }
	return strAllSystemNode;
}

bool isSystemNodeWallet()
{
    string s1 = GetSystemNodeById(1), s2 = GetSystemNodeById(2), s3 = GetSystemNodeById(3), s4 = GetSystemNodeById(4),  s5 = GetSystemNodeById(5),  s6 = GetSystemNodeById(6);
	bool rzt = isMineCoinAddress(s1) || isMineCoinAddress(s2) || isMineCoinAddress(s3) || isMineCoinAddress(s4) || isMineCoinAddress(s5) || isMineCoinAddress(s6);
	return rzt;
}

bool IsSystemNode(const string sCoinAddr)
{
    string sNodes = GetSystemNodes();
	return (sNodes.find(sCoinAddr) != string::npos);
}

int64_t GetPerDayLockBlocks()
{
    return ( fTestNet ? 60 : 1440 );
}
bool insertQueueNode(int64_t inBlock, int iLockDays, string sNick, string coinAddr, string tx, int payIdx, int64_t regTime)
{
   bool rzt = false;
   LOCK(cs_queue_mining);  // 2017.05.30
   if( iLockDays <= 0 ){ iLockDays = 1; }
   int64_t i6BlkSpace=inBlock, i6RuleHei = GetQueuePoSRulesActiveHeight();
   if( inBlock < i6RuleHei ){ i6BlkSpace = i6RuleHei; }
   int64_t unlockBlk = (GetPerDayLockBlocks() * iLockDays) + i6BlkSpace;
   string s = "'" + sNick + "', " + i64tostr(inBlock) + ", " + inttostr(iLockDays) + ", " + i64tostr(unlockBlk) + ", '" + coinAddr + "', '" + tx + "', " + inttostr(payIdx) + ", 0, " + i64tostr(regTime);
   string sql = "INSERT INTO Nodes (nick, inblock, lockdays, unlockblk, coinaddr, tx, payid, confirm, regtm) VALUES (" + s + ");";
   char* pe = 0;
   int rc = sqlite3_exec(dbQueueMining, sql.c_str(), 0, 0, &pe);      rzt = (rc == SQLITE_OK);
   if( fDebug ){ printf("insertQueueNode :: [%d] [%s] [%s]\n", rc, pe, sql.c_str()); }
   if( pe ){ sqlite3_free(pe); }
   return rzt;
}

bool deleteQueueNode(const string tx, int payIdx)
{
	bool rzt=false;   int iLen = tx.length();
	if( iLen > 33 )
	{
	    LOCK(cs_queue_mining);  // 2017.05.30
	    string sql = "DELETE FROM Nodes where ";  //gen_bet='" + tx + "' and confirmed<1 and opcode=1;";
		if( iLen == 34 ){ sql = sql + "coinaddr='" + tx + "'"; }
		else{ sql = sql + "tx='" + tx + "'"; }
		if( payIdx >= 0 ){ sql = sql + " and payid=" + inttostr(payIdx); }
		sql = sql + ";";
	    int rc = sqlite3_exec(dbQueueMining, sql.c_str(), NULL, NULL, NULL);   rzt = (rc == SQLITE_OK);
	    if( fDebug ){ printf("deleteQueueNode() :: return [%d], [%s] \n", rzt, sql.c_str()); }
	}
	return rzt;
}

uint64_t GetQueueNodeRegInBlocks(const string tx, int payIdx)
{
	uint64_t rzt=0;   int iLen = tx.length();
	if( iLen < 34 ){ return rzt; }
	LOCK(cs_queue_mining);  // 2017.05.30
	string sql = "select * from Nodes where ";
	if( iLen == 34 ){ sql = sql + "coinaddr='" + tx + "'"; }
	else{ sql = sql + "tx='" + tx + "'"; }
	if( payIdx >= 0 ){ sql = sql + " and payid=" + inttostr(payIdx); }
	sql = sql + ";";
	//if( fDebug ){ printf("checkUserWeight : [%s] \n", sql.c_str()); }
	dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, 2, 0, 0, "inblock", ""};  // 2 = inblock
	getOneResultFromDb(dbQueueMining, sql, pack);
	if( pack.fDone > 0 ) { rzt = pack.u6Rzt; }
	if( fDebug ){ printf("GetQueueNodeRegInBlocks() : rzt=[%s], [%s] \n", u64tostr(pack.u6Rzt).c_str(), sql.c_str()); }
	return rzt;
}
/*
   string sql = "Create TABLE Nodes ([id] integer PRIMARY KEY AUTOINCREMENT"
					",[nick] varchar(32) UNIQUE NOT NULL"
					",[inblock] bigint DEFAULT 0"
					",[coinaddr] varchar(34) UNIQUE NOT NULL"
					",[tx] varchar(64) UNIQUE NOT NULL"
					",[payid] int DEFAULT 0"
					",[gotblks] bigint DEFAULT 0"
					",[lost] bigint DEFAULT 0"
					",[clrlost] bigint DEFAULT 0"
					",[lockdays] int DEFAULT 0"
					",[unlockblk] bigint DEFAULT 0"
					",[lastblktm] bigint DEFAULT 0";
					",[confirm] int DEFAULT 0"
					",[regtm] bigint DEFAULT 0);";
*/
uint64_t GetQueueNodeLockDays(const string sCallFrom, const string tx, int payIdx, bool bGetLockDays)
{
	uint64_t rzt = 0;   int iLen = tx.length();
	if( iLen < 34 ){ return rzt; }
	LOCK(cs_queue_mining);  // 2017.05.30
	string sql = "select * from Nodes where ";
	if( iLen == 34 ){ sql = sql + "coinaddr='" + tx + "'"; }
	else{ sql = sql + "tx='" + tx + "'"; }
	if( payIdx >= 0 ){ sql = sql + " and payid=" + inttostr(payIdx); }
	sql = sql + ";";
	dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, (bGetLockDays ? 9 : 10), 0, 0, (bGetLockDays ? "lockdays" : "unlockblk"), ""};
	getOneResultFromDb(dbQueueMining, sql, pack);
	if( pack.fDone > 0 ) { rzt = pack.u6Rzt; }
	if( fDebug ){ printf("%s Call GetQueueNodeLockDays() : rzt=[%s], [%s] \n", sCallFrom.c_str(), u64tostr(pack.u6Rzt).c_str(), sql.c_str()); }
	return rzt;
}

bool isQueueNodeExists(const string tx)
{
    uint64_t u6 = GetQueueNodeRegInBlocks(tx);
	return u6 > 0;
}

bool isQueueNodeNickExists(const string sNick)
{
	bool rzt=false;
	LOCK(cs_queue_mining);  // 2017.05.30
	string sql = "select * from Nodes where nick='" + sNick + "';";
	dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, 2, 0, 0, "inblock", ""};  // 2 = inblock
	getOneResultFromDb(dbQueueMining, sql, pack);
	if( pack.fDone > 0 ) { rzt = pack.u6Rzt > 0; }
	if( fDebug ){ printf("isQueueNodeNickExists() : rzt=[%s], [%s] \n", u64tostr(pack.u6Rzt).c_str(), sql.c_str()); }
	return rzt;
}

bool isQueueNodeNickOrAddrExists(const string sNick, const string sCoinAddr)
{
	bool rzt=false;
	LOCK(cs_queue_mining);  // 2017.05.30
	string sql = "select * from Nodes where nick='" + sNick + "' or coinaddr='" + sCoinAddr + "';";
	dbOneResultCallbackPack pack = {OneResultPack_U64_TYPE, 2, 0, 0, "inblock", ""};  // 2 = inblock
	getOneResultFromDb(dbQueueMining, sql, pack);
	if( pack.fDone > 0 ) { rzt = pack.u6Rzt > 0; }
	if( fDebug ){ printf("isQueueNodeNickOrAddrExists() : rzt=[%d] [%s], [%s] \n", rzt, u64tostr(pack.u6Rzt).c_str(), sql.c_str()); }
	return rzt;
}

bool updateQueueNodeInfo(const string tx, const string newTx, int payIdx, int64_t lastblktm)
{
    LOCK(cs_queue_mining);  // 2017.05.30
    string sql = "update Nodes set lastblktm=" + i64tostr(lastblktm) + ", payid=" + inttostr(payIdx) + ", gotblks=(gotblks+1), tx='" + newTx + "' where tx='" + tx + "';";
	int rc = sqlite3_exec(dbQueueMining, sql.c_str(), NULL, 0, NULL);
	if( fDebug ){ printf("updateQueueNodeInfo:: [%d] [%s] \n", rc, sql.c_str()); }
	return (rc == SQLITE_OK);
}

int updateQueueNodeLostBlockCount(const string sMiner, int opc)
{
    LOCK(cs_queue_mining);  // 2017.05.30
	string sOpc =  "lost+1", sClr="";
	if( opc == 0 ){ sOpc = "0";      sClr = "clrlost=(clrlost+1), "; }
	else if( opc == 2 ){ sOpc = "lost-1"; }
    string sql = "update Nodes set " + sClr + "lost=(" + sOpc + ") where coinaddr='" + sMiner + "';";
	int rc = sqlite3_exec(dbQueueMining, sql.c_str(), NULL, 0, NULL);
	if( fDebug ){ printf("updateQueueNodeLostBlockCount:: rzt=[%d] [%s] \n", rc, sql.c_str()); }
	return rc;
}

string getCurrentQueueMiner(bool bGetAddr)
{
    string rzt="";
    LOCK(cs_queue_mining);  // 2017.05.30
	//int64_t i6 = nBestHeight - 8;
    // string sql = "select * from Nodes where lost<1 and inblock<" + i64tostr(i6) + " order by lastblktm asc, id asc limit 1;";
    string sql = "select * from Nodes where lost<1 and confirm>9 order by lastblktm asc, id asc limit 1;";
	dbOneResultCallbackPack pack = {OneResultPack_STR_TYPE, (bGetAddr ? 3 : 4), 0, 0, (bGetAddr ? "coinaddr" : "tx"), ""};  // 3 = coinaddr
	getOneResultFromDb(dbQueueMining, sql, pack);
	if( pack.fDone > 0 ) { rzt = pack.sRzt; }
	if( fDebug ){ printf("getCurrentQueueMiner:: rzt=[%s] [%s] \n", rzt.c_str(), sql.c_str()); }
    return rzt;
}

/* int processTxInForQueueMining(const std::vector<CTxIn> vin)
{
    int rzt=0;
	BOOST_FOREACH(const CTxIn& txin, vin)
	{
		if( txin.prevout.IsNull() ){ continue; }
		uint256 hashBlock = 0;   CTransaction txPrev;
		//if( fDebug ){ printf( "getTxinAddressAndAmount: txin.prevout.n = [%u], \n", txin.prevout.n ); }
		if( GetTransaction(txin.prevout.hash, txPrev, hashBlock) )	// get the vin's previous transaction
		{
			sPreTargetAddr = "";
			iAmnt = 0;
			rzt = get_Txin_prevout_n_s_TargetAddressAndAmount(txPrev, txin.prevout.n, sPreTargetAddr, iAmnt);
		}
	}
	return rzt;
} */

bool isValidRegQueueNodeTx(const CTransaction& tx, string& sCoinAddr, string& sNick, int& payIdx, int& iLockDays)
{
    bool rzt=false;   string sData = tx.chaindata.c_str();   payIdx=0;   iLockDays=1;
    if( sData.length() < 55 ){ return rzt; }
	//  "I Want To Register As A Node:BC3ZJJnM8s93nxY7zeL74RgxTd2t1t9RzX:Big Bash"
	// sendtoaddresswithmsg mkx1UXiLezSJ3FLQ6TydobRw5oeFr8B3r9 1000000 "Register Queue Node:mkx1UXiLezSJ3FLQ6TydobRw5oeFr8B3r9:Gray"
	// sendtoaddresswithmsg n3dAVpLSAbf4supyf2HFFD4T65eKSy4jUL 1000000 "Register Queue Node:n3dAVpLSAbf4supyf2HFFD4T65eKSy4jUL:Lucy:10"
	// sendtoaddresswithmsg mj1fZZydq9LxpDLsBbsWXLKLVxajeResFE 1000000 "Register Queue Node:mj1fZZydq9LxpDLsBbsWXLKLVxajeResFE:James:30"
    BitBetCommand bbc = {"", "", "", "", "", "", "", "", "", "", "", 0};   //sCoinAddr = sData.substr(29, 34);
	if( SplitCmdParamFromStr(sData, bbc, strRegisterAsNodeMagic, ":") > 2 )
	{
		sCoinAddr = bbc.cmdName;   sNick = bbc.p1;      if( bbc.p2.length() > 0 ){ iLockDays = strToInt(bbc.p2, 10); }
		payIdx = GetCoinAddrInTxOutIndex(tx, sCoinAddr, MIN_Queue_Node_AMOUNT);
		rzt = payIdx >= 0;  // Check Bet Amount, =-1 is invalid
		if( fDebug ){ printf( "isValidRegQueueNodeTx: tx data = [%s], rzt=[%d], [%s] [%s] \n", sData.c_str(), rzt, sCoinAddr.c_str(), sNick.c_str()); }
	}
	return rzt;
}
const string strResetQueueNodeLostBlockMagic = "Reset Queue Node:";
bool resetQueueNodeLostBlkCount(const CTransaction& tx)
{
    bool rzt=false;   string sData = tx.chaindata.c_str();
	// sendtoaddresswithmsg mj1fZZydq9LxpDLsBbsWXLKLVxajeResFE 1000000 "Reset Queue Node:mj1fZZydq9LxpDLsBbsWXLKLVxajeResFE:14xxxxx:sign str"
	//  Reset Queue Node Lost Block:Miner_Address_1: Time_2 | Sign_3
    BitBetCommand bbc = {"", "", "", "", "", "", "", "", "", "", "", 0};
	if( SplitCmdParamFromStr(sData, bbc, strResetQueueNodeLostBlockMagic, ":") > 2 )
	{
		string sCoinAddr = bbc.cmdName, sTime = bbc.p1, sSign = bbc.p2, sMsg = sCoinAddr + "," + sTime;
		if( verifyMessage(sCoinAddr, sSign, sMsg) )  // bool verifyMessage(const string strAddress, const string strSign, const string strMessage)
		{
		    rzt = (updateQueueNodeLostBlockCount(sCoinAddr, 0) == SQLITE_OK);
		}
		if( fDebug ){ printf( "resetQueueNodeLostBlkCount: tx data = [%s], rzt=[%d], [%s] [%s] \n", sData.c_str(), rzt, sCoinAddr.c_str(), sSign.c_str()); }
	}
	return rzt;
}

bool updateQueueNodesStatus()
{
    LOCK(cs_queue_mining);  // 2017.05.30
    string sql = "update Nodes set confirm=(confirm+1) where confirm<10;";
	int rc = sqlite3_exec(dbQueueMining, sql.c_str(), NULL, 0, NULL);
	if( fDebug ){ printf("updateQueueNodesStatus() : [%d] [%s] \n", rc, sql.c_str()); }

    //-- Auto quit  expired nodes,  unlockblk
    int64_t i6 = nBestHeight + 1;
    sql = "DELETE FROM Nodes where unlockblk<" + i64tostr(i6) + ";";
	rc = sqlite3_exec(dbQueueMining, sql.c_str(), NULL, 0, NULL);
	if( fDebug ){ printf("updateQueueNodesStatus() : [%d] [%s] \n", rc, sql.c_str()); }

	NotifyQueueNodeMsg(1, "");
	return (rc == SQLITE_OK);
}

// int SplitCmdParamFromStr(const std::string betStr, BitBetCommand &bbc, const string cmdHeader)
bool processQueueMiningTx(const CTransaction& tx, int64_t iTxHei, bool bConnect, int64_t blkTime)
{
	bool rzt=false,  bQPoS_Rules_Actived = Is_Queue_PoS_Rules_Acitved(iTxHei), bCoinStake = tx.IsCoinStake(), bAcceptRegNode=AcceptRegisterQueuePoSNode(iTxHei);
	{
		string sNick="", sCoinAddr="", sTx = tx.GetHash().ToString(), sData = tx.chaindata;     int payIdx=0, iLockDays=1;
		bool bRegNode = (!bCoinStake) && isValidRegQueueNodeTx(tx, sCoinAddr, sNick, payIdx, iLockDays);  //  "Register Queue Node:BC3ZJJnM8s93nxY7zeL74RgxTd2t1t9RzX"
		if( bConnect )
		{
			if( bRegNode )
			{
			    if( bAcceptRegNode && !isQueueNodeNickOrAddrExists(sNick, sCoinAddr) )
				{
				    if( insertQueueNode(iTxHei, iLockDays, sNick, sCoinAddr, sTx, payIdx, tx.nTime) ){ } //NotifyQueueNodeMsg(2, sTx); }
				}
			}
			else if( bQPoS_Rules_Actived ){ resetQueueNodeLostBlkCount(tx); }
			if( bQPoS_Rules_Actived )
			{
				if( bCoinStake )
				{
					const CTxIn& txin = tx.vin[0];
					uint256 hashBlock = 0;   CTransaction txPrev;
					if( fDebug ){ printf( "processQueueMiningTx: tx.IsCoinStake=true, txin.prevout.n=[%d] \n", txin.prevout.n); }
					if( GetTransaction(txin.prevout.hash, txPrev, hashBlock) )	// get the vin's previous transaction
					{
						string sTxPrev = txPrev.GetHash().ToString();
						if( fDebug ){ printf( "processQueueMiningTx: tx.IsCoinStake=true, txPrev = [%s], \n", sTxPrev.c_str() ); }
						if( updateQueueNodeInfo(sTxPrev, sTx, 1, blkTime) ){ NotifyQueueNodeMsg(3, sTxPrev); }  //if( updateQueueNodeInfo(sTxPrev, sTx, txin.prevout.n, blkTime) ){ NotifyQueueNodeMsg(3, sTxPrev); }
					}else{ printf( "processQueueMiningTx: IsCoinStake, GetTransaction() failed :( \n"); }
				}else{
				BOOST_FOREACH(const CTxIn& txin, tx.vin)
				{
					if( txin.prevout.IsNull() ){ continue; }
					uint256 hashBlock = 0;   CTransaction txPrev;
					if( fDebug ){ printf( "processQueueMiningTx: tx.IsCoinStake=false, txin.prevout.n = [%u], \n", txin.prevout.n ); }
					if( GetTransaction(txin.prevout.hash, txPrev, hashBlock) )	// get the vin's previous transaction
					{
						string sTxPrev = txPrev.GetHash().ToString();
						if( fDebug ){ printf( "processQueueMiningTx: txPrev = [%s], \n", sTxPrev.c_str() ); }
						if( deleteQueueNode(sTxPrev, txin.prevout.n) ){ NotifyQueueNodeMsg(0, sTxPrev); }
					}else{ printf( "processQueueMiningTx: GetTransaction() failed :( \n"); }
				}}
			}
		}else{
			if( bAcceptRegNode && bRegNode )
			{
				deleteQueueNode(sCoinAddr);
			}
		}
	}
	return rzt;
}

bool checkQueueNodeCoinLockTime(const string sCallFrom, const string sTxHash, int payIdx, int64_t iTxHei)
{
	bool rzt=true;
	uint64_t u6 = GetQueueNodeLockDays("checkQueueNodeCoinLockTime()", sTxHash, payIdx, false);  // get unlockblk //GetQueueNodeRegInBlocks(sTxHash, payIdx);
	if( u6 > 0 )
	{
		rzt = (iTxHei >= u6);
		//uint64_t u6Max = u6 + nLockQueueNodeCoinTime;
		//if( iTxHei < u6Max ){ rzt=false; }
	}
    if( fDebug ){ printf( "%s Call checkQueueNodeCoinLockTime: rzt=[%d], txHash = [%s], payIdx=[%d], unlockblk=[%s] : [%s] \n", sCallFrom.c_str(), rzt, sTxHash.c_str(), payIdx, u64tostr(u6).c_str(), i64tostr(iTxHei).c_str()); }
	return rzt;
}
bool canSpentQueueNodeCoin(const CTransaction& tx, int64_t iTxHei)
{
	bool rzt=true, bStake = tx.IsCoinStake(),  bQPoS_Rules_Actived = Is_Queue_PoS_Rules_Acitved(iTxHei);
	if( fDebug ){ printf( "canSpentQueueNodeCoin: iTxHei=[%s], tx.IsCoinStake = [%d], bQPoS_Rules_Actived=[%d], tx hash = [%s] \n", i64tostr(iTxHei).c_str(), bStake, bQPoS_Rules_Actived, tx.GetHash().ToString().c_str() ); }
	if( !bQPoS_Rules_Actived && bStake ){ bStake = false; }
	BOOST_FOREACH(const CTxIn& txin, tx.vin)
	{
		if( txin.prevout.IsNull() ){ continue; }
		uint256 hashBlock = 0;   CTransaction txPrev;
		if( fDebug ){ printf( "canSpentQueueNodeCoin: txin.prevout.n = [%u], \n", txin.prevout.n ); }
		if( GetTransaction(txin.prevout.hash, txPrev, hashBlock) )	// get the vin's previous transaction
		{
			string sTxPrev = txPrev.GetHash().ToString();
			if( !bStake && !checkQueueNodeCoinLockTime("canSpentQueueNodeCoin()", sTxPrev, txin.prevout.n, iTxHei) ){ rzt=false;  break; }
		}else{ printf( "canSpentQueueNodeCoin: GetTransaction() failed :( \n"); }
	}
	if( fDebug ){ printf( "canSpentQueueNodeCoin: rzt=[%d] \n", rzt); }
	return rzt;
}

bool isValidBlockHeight(const CBlock& block, int64_t nHei)
{
    int64_t blkHeight = strToInt64(block.blockData.c_str());
	return (nHei == blkHeight);
}

/*bool isRegedQueueStakeMiner(const CBlock& block)
{
	bool rzt=false;   std::string sBlockFinder = "";
	if( block.IsProofOfStake() )
	{
		int64_t iAmnt = 0;      const CTxIn& txin = block.vtx[1].vin[0];
		getTxinAddressAndAmount(txin, sBlockFinder, iAmnt);
		rzt = IsSystemNode(sBlockFinder) || isQueueNodeExists(sBlockFinder);
	}else{ rzt=true; }
	if(fDebug){ printf("isRegedQueueStakeMiner() : rzt=[%d], sBlockFinder(%s) \n", rzt, sBlockFinder.c_str()); }
	return rzt;
}*/

bool IsTheRightQueueStakeMiner(const CBlock& block)
{
	bool rzt=false;   std::string sBlockFinder = "", sCurQueueMiner;
	if( block.IsProofOfStake() )
	{
		int64_t iAmnt = 0;      const CTxIn& txin = block.vtx[1].vin[0];
		getTxinAddressAndAmount(txin, sBlockFinder, iAmnt);
		sCurQueueMiner = getCurrentQueueMiner();
		rzt = (sCurQueueMiner == sBlockFinder) || IsSystemNode(sBlockFinder);
	}else{ rzt=false; }
	if(fDebug){ printf("IsTheRightQueueStakeMiner() : rzt=[%d], sBlockFinder=(%s), sCurQueueMiner=[%s] \n", rzt, sBlockFinder.c_str(), sCurQueueMiner.c_str()); }
	return rzt;
}

/*bool isValidBlockTime(CBlockIndex* pindex, int64_t& blkTmSpace)
{
	bool rzt=true;   blkTmSpace=0;
	int64_t curBlkTm = pindex->GetBlockTime(), prevBlkTm=0, tmNow = GetAdjustedTime();
    if( fDebug ){ printf("isValidBlockTime() : curBlkTm=[%s], tmNow=[%s], pprev=[%x] \n", i64tostr(curBlkTm).c_str(), i64tostr(tmNow).c_str(), pindex->pprev); }
	if( curBlkTm > tmNow ){ return error("isValidBlockTime() : curBlkTm >= tmNow :("); }
	if( pindex->pprev )
	{
		int64_t prevBlkTm = pindex->pprev->GetBlockTime();   blkTmSpace = curBlkTm - prevBlkTm;
		if( fDebug ){ printf("isValidBlockTime() : curBlkTm=[%s] - prevBlkTm=[%s] = [%s] \n", i64tostr(curBlkTm).c_str(), i64tostr(prevBlkTm).c_str(), i64tostr(blkTmSpace).c_str()); }
		if( (curBlkTm <= prevBlkTm) || (curBlkTm < (prevBlkTm + Queue_Node_Block_Min_Interval)) || (curBlkTm > tmNow) ){ return error("isValidBlockTime() : block timestamp wrong"); }
		if( blkTmSpace < Queue_Node_Block_Min_Interval ){ return error("isValidBlockTime() : block time interval < Queue_Node_Block_Min_Interval :("); }
	}else{ return error("isValidBlockTime() : pindex->pprev is null :("); }
	return rzt;
}

bool isValidMiner(CBlockIndex* pindex, const string sBlockFinder)
{
	bool rzt = true;   int64_t blkTmSpace=0;
	if( isQueueNodeExists(sBlockFinder) )
	{
		if( isValidBlockTime(pindex, blkTmSpace) )
		{
			int iLost = blkTmSpace / Queue_Node_Block_Max_Interval;  // 120
			if( fDebug ){ printf("isValidMiner() : Lost miner =[%d] \n", iLost); }
		}else{ return error("isValidMiner() : isValidBlockTime() return failed :("); }
	}else{ return error("isValidMiner() : sBlockFinder(%s) not reg :(", sBlockFinder.c_str()); }
	return rzt;
} */


/*
   string sql = "Create TABLE Nodes ([id] integer PRIMARY KEY AUTOINCREMENT"
					",[nick] varchar(32) UNIQUE NOT NULL"
					",[inblock] bigint DEFAULT 0"
					",[coinaddr] varchar(34) UNIQUE NOT NULL"
					",[tx] varchar(64) UNIQUE NOT NULL"
					",[payid] int DEFAULT 0"
					",[gotblks] bigint DEFAULT 0"
					",[lost] bigint DEFAULT 0"
					",[clrlost] bigint DEFAULT 0"
					",[lockdays] int DEFAULT 0"
					",[unlockblk] bigint DEFAULT 0"
					",[lastblktm] bigint DEFAULT 0);";

struct OneQueueNodePack
{
   int id, payid, lockdays, confirm;
   string nick, coinaddr, tx;
   int64_t inblock, gotblks, lost, clrlost, unlockblk, lastblktm, regtm;
};
struct QueueNodeListPack
{
   int64_t i6RecordCount;
   std::vector<OneQueueNodePack> vQueueNodes;
}; */

static int getOneQueueNodeCallBack(void *data, int argc, char **argv, char **azColName)
{
   //if( fDebug ){ printf("getOneQueueNodeCallBack() :: data=[%x], argc=[%d] \n", data, argc); }
   if( (data != NULL) && (argc > 12) )
   {
      QueueNodeListPack* p = (QueueNodeListPack *)data;
	  OneQueueNodePack oneNode = { strToInt(argv[0]), strToInt(argv[5]), strToInt(argv[9]), strToInt(argv[12]), argv[1], argv[3], argv[4], strToInt64(argv[2]), strToInt64(argv[6]), strToInt64(argv[7]), strToInt64(argv[8]), strToInt64(argv[10]), strToInt64(argv[11]), strToInt64(argv[13]) };
	  p->vQueueNodes.push_back(oneNode);
	  p->i6RecordCount++;
   }
   return 0;
}

bool getAllQueueNodes(sqlite3 *dbOne, const string sql, QueueNodeListPack& pack)
{
   //if( fDebug ){ printf("getAllQueueNodes() :: [%s] \n", sql.c_str()); }
   pack.i6RecordCount = 0;      LOCK(cs_queue_mining);
   int rc = sqlite3_exec(dbOne, sql.c_str(), getOneQueueNodeCallBack, (void*)&pack, NULL);      bool rzt = (rc == SQLITE_OK) && (pack.i6RecordCount > 0);
   if( fDebug ){ printf("getAllQueueNodes() :: rzt = [%d], total nodes=[%d], [%s] \n", rzt, (int)pack.i6RecordCount, sql.c_str()); }
   return rzt;
}
bool getAllQueueNodes(const string sql, QueueNodeListPack& pack)
{
   return getAllQueueNodes(dbQueueMining, sql, pack);
}
bool getAllQueueNodes(QueueNodeListPack& pack)
{
    string sql = "select * from Nodes order by id asc limit 500;";  // string sql = "select * from Nodes order by lost asc, lastblktm asc limit 500;";
    return getAllQueueNodes(dbQueueMining, sql, pack);
}

bool getAllActiveQueueNodes(sqlite3 *dbOne, QueueNodeListPack& pack)
{
    string sql = "select * from Nodes where lost<1 order by lastblktm asc, id asc limit 500;";
    return getAllQueueNodes(dbOne, sql, pack);
}
bool getAllActiveQueueNodes(QueueNodeListPack& pack)
{
    string sql = "select * from Nodes where lost<1 order by lastblktm asc, id asc limit 500;";
    return getAllQueueNodes(dbQueueMining, sql, pack);
}

bool GetCurrentQueueMinerInfo(QueueNodeListPack& pack)
{
    string sql = "select * from Nodes where lost<1 and confirm>9 order by lastblktm asc, id asc limit 1;";
	bool rzt = getAllQueueNodes(sql, pack);
	return rzt;
}

bool ImTheCurrentQueueMiner()
{
	bool rzt = false, bSysMiningTime = isSystemNodeMiningTime();     string sCurQueueMiner = "";
	if( bSysMiningTime ){ rzt = bSystemNodeWallet; }
	else{
		sCurQueueMiner = getCurrentQueueMiner();   
		rzt = ( (sCurQueueMiner.length() < 33) ? false : isMineCoinAddress(sCurQueueMiner) );
	}
	if( fDebug ){ printf("ImTheCurrentQueueMiner() :: rzt=%d, bSysMiningTime=[%d],  bSystemNodeWallet=[%d], [%s] \n", rzt, bSysMiningTime, bSystemNodeWallet, sCurQueueMiner.c_str()); }
	return rzt;
}

int64_t getLast2BlockTimeSpace()
{
	CBlockIndex* pindexPrev = pindexBest;
	int64_t pBlkTm = pindexPrev->GetBlockTime(), prevBlkTm = pindexPrev->pprev->GetBlockTime(), tmSpace = pBlkTm - prevBlkTm;
    return tmSpace;
}

int64_t getLastBlockTimeSpaceWithNow()
{
	CBlockIndex* pindexPrev = pindexBest;
	int64_t prevBlkTm = pindexPrev->GetBlockTime(), tmNow = GetAdjustedTime(), tmSpace = tmNow - prevBlkTm;
    return tmSpace;
}

bool isSystemNodeMiningTime()
{
    int64_t tm = getLastBlockTimeSpaceWithNow();
    return (tm > Queue_Node_Block_Max_Interval);
}

bool isTheRightMiningTime()
{
    bool rzt = false;     int64_t tm = getLastBlockTimeSpaceWithNow();
	if( fDebug ){ printf("isTheRightMiningTime() :: time space = [%d] \n", (int)tm); }
	if( tm >= Queue_Node_Block_Min_Interval )
	{
		if( tm < Queue_Node_Block_Max_Interval ){ rzt = true; }
		else if( bSystemNodeWallet )
		{
			int64_t i6SysNodeWorkTime = GetArg("-sysnodeworktime", (Queue_Node_Block_Max_Interval * 2));  // 240
			if( tm > i6SysNodeWorkTime ){ rzt = true; }
		}
	}
    //return ( (tm >= Queue_Node_Block_Min_Interval) && (tm < Queue_Node_Block_Max_Interval) );
	return rzt;
}
