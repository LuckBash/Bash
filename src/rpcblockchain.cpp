// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "bitcoinrpc.h"
#include "kernel.h"
#include "bitbet.h"
#include "db.h"
#include "txdb.h"

using namespace json_spirit;
using namespace std;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry);
extern enum Checkpoints::CPMode CheckpointsMode;

double GetDifficulty(const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (pindexBest == NULL)
            return 1.0;
        else
            blockindex = GetLastBlockIndex(pindexBest, false);
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetPoWMHashPS()
{
    if (pindexBest->nHeight >= LAST_POW_BLOCK)
        return 0;

    int nPoWInterval = 72;
    int64_t nTargetSpacingWorkMin = 30, nTargetSpacingWork = 30;

    CBlockIndex* pindex = pindexGenesisBlock;
    CBlockIndex* pindexPrevWork = pindexGenesisBlock;

    while (pindex)
    {
        if (pindex->IsProofOfWork())
        {
            int64_t nActualSpacingWork = pindex->GetBlockTime() - pindexPrevWork->GetBlockTime();
            nTargetSpacingWork = ((nPoWInterval - 1) * nTargetSpacingWork + nActualSpacingWork + nActualSpacingWork) / (nPoWInterval + 1);
            nTargetSpacingWork = max(nTargetSpacingWork, nTargetSpacingWorkMin);
            pindexPrevWork = pindex;
        }

        pindex = pindex->pnext;
    }

    return GetDifficulty() * 4294.967296 / nTargetSpacingWork;
}

double GetPoSKernelPS()
{
    int nPoSInterval = 72;
    double dStakeKernelsTriedAvg = 0;
    int nStakesHandled = 0, nStakesTime = 0;

    CBlockIndex* pindex = pindexBest;;
    CBlockIndex* pindexPrevStake = NULL;

    while (pindex && nStakesHandled < nPoSInterval)
    {
        if (pindex->IsProofOfStake())
        {
            dStakeKernelsTriedAvg += GetDifficulty(pindex) * 4294967296.0;
            nStakesTime += pindexPrevStake ? (pindexPrevStake->nTime - pindex->nTime) : 0;
            pindexPrevStake = pindex;
            nStakesHandled++;
        }

        pindex = pindex->pprev;
    }

    double result = 0;

    if (nStakesTime)
        result = dStakeKernelsTriedAvg / nStakesTime;

    if (IsProtocolV2(nBestHeight))
        result *= STAKE_TIMESTAMP_MASK + 1;

    return result;
}

Object blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool fPrintTransactionDetail)
{
    Object result;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    CMerkleTx txGen(block.vtx[0]);
    txGen.SetMerkleBranch(&block);
    result.push_back(Pair("confirmations", (int)txGen.GetDepthInMainChain()));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(Pair("mint", ValueFromAmount(blockindex->nMint)));
    result.push_back(Pair("time", (int64_t)block.GetBlockTime()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("blocktrust", leftTrim(blockindex->GetBlockTrust().GetHex(), '0')));
    result.push_back(Pair("chaintrust", leftTrim(blockindex->nChainTrust.GetHex(), '0')));
    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    if (blockindex->pnext)
        result.push_back(Pair("nextblockhash", blockindex->pnext->GetBlockHash().GetHex()));

    //if( fDebug ){ result.push_back(Pair("nFlags", (int)blockindex->nFlags)); }
    result.push_back(Pair("flags", strprintf("(%d) %s%s", blockindex->nFlags, blockindex->IsProofOfStake()? "proof-of-stake" : "proof-of-work", blockindex->GeneratedStakeModifier()? " stake-modifier": "")));
    result.push_back(Pair("proofhash", blockindex->hashProof.GetHex()));
    result.push_back(Pair("entropybit", (int)blockindex->GetStakeEntropyBit()));
    result.push_back(Pair("modifier", strprintf("%016"PRIx64, blockindex->nStakeModifier)));
	//if( fDebug ){ result.push_back(Pair("modifier64", strprintf("%"PRIx64"", blockindex->nStakeModifier))); }
    result.push_back(Pair("modifierchecksum", strprintf("%08x", blockindex->nStakeModifierChecksum)));
    Array txinfo;
    BOOST_FOREACH (const CTransaction& tx, block.vtx)
    {
        if (fPrintTransactionDetail)
        {
            Object entry;

            entry.push_back(Pair("txid", tx.GetHash().GetHex()));
            TxToJSON(tx, 0, entry);

            txinfo.push_back(entry);
        }
        else
            txinfo.push_back(tx.GetHash().GetHex());
    }

    result.push_back(Pair("tx", txinfo));

    if (block.IsProofOfStake())
        result.push_back(Pair("signature", HexStr(block.vchBlockSig.begin(), block.vchBlockSig.end())));

    return result;
}

Value getbestblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbestblockhash\n"
            "Returns the hash of the best block in the longest block chain.");

    return hashBestChain.GetHex();
}

Value getblockcount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "Returns the number of blocks in the longest block chain.");

    return nBestHeight;
}


Value getdifficulty(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "Returns the difficulty as a multiple of the minimum difficulty.");

    Object obj;
    obj.push_back(Pair("proof-of-work",        GetDifficulty()));
    obj.push_back(Pair("proof-of-stake",       GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    obj.push_back(Pair("search-interval",      (int)nLastCoinStakeSearchInterval));
    return obj;
}


Value settxfee(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1 || AmountFromValue(params[0]) < getMIN_TX_FEE(nBestHeight))
        throw runtime_error(
            "settxfee <amount>\n"
            "<amount> is a real and is rounded to the nearest 0.01");

    nTransactionFee = AmountFromValue(params[0]);
    nTransactionFee = (nTransactionFee / CENT) * CENT;  // round to cent

    return true;
}

Value getrawmempool(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getrawmempool\n"
            "Returns all transaction ids in memory pool.");

    vector<uint256> vtxid;
    mempool.queryHashes(vtxid);

    Array a;
    BOOST_FOREACH(const uint256& hash, vtxid)
        a.push_back(hash.ToString());

    return a;
}

Value getblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash <index>\n"
            "Returns hash of block in best-block-chain at <index>.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
    return pblockindex->phashBlock->GetHex();
}

Value getblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock <hash> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-hash.");

    std::string strHash = params[0].get_str();
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

extern multimap<uint256, CBlock*> mapOrphanBlocksByPrev;
Value processblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "processblock <hash>\n"
            "Returns details of a block with given block-hash.");

    std::string strHash = params[0].get_str();
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);
	mapBlockIndex.clear();      //mapOrphanBlocksByPrev.clear();   //mapBlockIndex.erase(hash);
    return ProcessBlock(NULL, &block);
    //return blockToJSON(block, pblockindex, false);
}

Value getblockinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockinfo <index>\n"
            "Returns info of block in best-block-chain at <index>.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
	CBlock block;
    block.ReadFromDisk(pblockindex, true);
    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

Value getblockbynumber(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockbynumber <number> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-number.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;

    uint256 hash = *pblockindex->phashBlock;

    pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

// ppcoin: get information of sync-checkpoint
Value getcheckpoint(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getcheckpoint\n"
            "Show info of synchronized checkpoint.\n");

    Object result;
    CBlockIndex* pindexCheckpoint;

    result.push_back(Pair("synccheckpoint", Checkpoints::hashSyncCheckpoint.ToString().c_str()));
    pindexCheckpoint = mapBlockIndex[Checkpoints::hashSyncCheckpoint];
    result.push_back(Pair("height", pindexCheckpoint->nHeight));
    result.push_back(Pair("timestamp", DateTimeStrFormat(pindexCheckpoint->GetBlockTime()).c_str()));

    // Check that the block satisfies synchronized checkpoint
    if (CheckpointsMode == Checkpoints::STRICT)
        result.push_back(Pair("policy", "strict"));

    if (CheckpointsMode == Checkpoints::ADVISORY)
        result.push_back(Pair("policy", "advisory"));

    if (CheckpointsMode == Checkpoints::PERMISSIVE)
        result.push_back(Pair("policy", "permissive"));

    if (mapArgs.count("-checkpointkey"))
        result.push_back(Pair("checkpointmaster", true));

    return result;
}

int RollbackBlocks(int64_t nBlocks) 
{
    int rzt=0;      if( nBestHeight <= nBlocks ){  return rzt;  }   //"nBestHeight <= nBlocks";  }
	int64_t nHei = nBestHeight - nBlocks;      //std::string s;
	if( fDebug ){  printf("RollbackBlocks(%s) : from (%s) down to [%s] \n", u64tostr(nBlocks).c_str(), u64tostr(nBestHeight).c_str(), u64tostr(nHei).c_str());  }
	try{
		
    // List of what to disconnect
    CTxDB txdb;      /*vector<CBlockIndex*> vDisconnect;      int64_t i = 0;
    for( i=nBestHeight;  i>nHei;  i-- )
	{
		CBlockIndex* cBlkIdx = FindBlockByHeight(i);      vDisconnect.push_back(cBlkIdx);
	}
    if( fDebug ){  printf("RollbackBlocks(%s) : vDisconnect.size()=[%d] \n", u64tostr(nBlocks).c_str(), vDisconnect.size());  }
    // Disconnect shorter branch

	list<CTransaction> vResurrect;
    BOOST_FOREACH(CBlockIndex* pindex, vDisconnect)
    {
        CBlock block;
        if (!block.ReadFromDisk(pindex))
            return "RollbackBlocks() : ReadFromDisk for disconnect failed";
        if (!block.DisconnectBlock(txdb, pindex))
            return strprintf("RollbackBlocks() : DisconnectBlock %s failed", pindex->GetBlockHash().ToString().substr(0,20).c_str());

        // Queue memory transactions to resurrect.
        // We only do this for blocks after the last checkpoint (reorganisation before that
        // point should only happen with -reindex/-loadblock, or a misbehaving peer.
        BOOST_REVERSE_FOREACH(const CTransaction& tx, block.vtx)
            if (!(tx.IsCoinBase() || tx.IsCoinStake()) && pindex->nHeight > Checkpoints::GetTotalBlocksEstimate())
                vResurrect.push_front(tx);
    }
    // Resurrect memory transactions that were in the disconnected branch
    BOOST_FOREACH(CTransaction& tx, vResurrect)
        AcceptToMemoryPool(mempool, tx, NULL);  */

	    //CValidationState state;
        CBlock block;      CBlockIndex* pindexNew = FindBlockByHeight(nHei);      
        if( !block.ReadFromDisk(pindexNew) ){  return -1;  }   //"ReadFromDisk failed :(";  }
		int iMaxReogBlocks = GetArg("-maxreorganizeblks", BitBet_Standard_Confirms);
		string sMaxReogBlks = u64tostr((uint64_t)(nBlocks + 10));      mapArgs["-maxreorganizeblks"] = sMaxReogBlks;
		LOCK(cs_main);
		if( block.SetBestChain(txdb, pindexNew) )   //if( SetBestChain(state, pindexNew) )
        {
            if( vNodes.size() > 0 )
            {
                LOCK(cs_vNodes);
                BOOST_FOREACH(CNode* pnode, vNodes)
                    pnode->PushGetBlocks(pindexBest, uint256(0));
			}
            rzt = nHei;   //s = "Rollback to block " + u64tostr(nHei);
        }else{  rzt = -2;  }  // s = "RollbackBlocks false"; }
		sMaxReogBlks = u64tostr((uint64_t)(iMaxReogBlocks));      mapArgs["-maxreorganizeblks"] = sMaxReogBlks;
		if( fDebug ){  printf("RollbackBlocks(%s) : set MaxReogBlks=[%s] \n", u64tostr(nBlocks).c_str(), sMaxReogBlks.c_str());  }
	} catch(std::runtime_error &e) {
		rzt = -3;   //s = "Rollback to block error";
		//s = strprintf("Rollback to block error: %s", e.what().c_str());
	}
	return rzt;   //s;
}

Value rollbackblocks(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "rollbackblocks <nBlocks=150>\n"
            "Rollback N Blocks from current Block Chain\n"
        );
	return RollbackBlocks(params.size() > 0 ? params[0].get_int() : 150);
}

Value rollbackto(const Array& params, bool fHelp) 
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error( "rollbackto <nBlocks>\n" );
	if( params.size() > 0 )
	{
		int64_t x = params[0].get_int();
		if( nBestHeight > x )
		{
			int64_t n = nBestHeight - x;
			return RollbackBlocks(n);
		}
		
	}
	throw runtime_error("<nBlocks> must big than current blocks(nBestHeight)\n");
}

extern bool isRejectTransaction(const CTransaction& tx, uint64_t iTxHei);
Value testtxmsg(const Array& params, bool fHelp) 
{
    if (fHelp || params.size() < 1 )
        throw runtime_error( "testtxmsg <txmsg>\n" );
	if( params.size() > 0 )
	{
        std::string sMsg = params[0].get_str();
		CTransaction tx;      tx.chaindata = sMsg;
		return isRejectTransaction(tx, 0);	  // nBestHeight
	}
	throw runtime_error("need <txmsg>\n");
}

extern string signMessage(const string strAddress, const string strMessage);
Value bitbetcmd(const Array& params, bool fHelp) 
{
    if (fHelp || params.size() < 3 )
        throw runtime_error( "bitbetcmd <params>\n" );
	int ipc = params.size();
	if( ipc > 3 )
	{
//  BitBetCMD: | Edit User | Time_1 | NickName_2 | CoinAddress_3 | weight_4 | flag_5 | Remarks_6 | System Sign_7 | System Coin Address_8
//  BitBetCMD: | Rollback Blocks | Time_1 | CheckBlockNumber_2 | CheckBlkNumberHash_3 | RollbackBlocks_4 | System Sign_5 | System Coin Address_6
        string sSystemAddr = GetArg("-systemaddr", system_address_1), sRzt="BitBetCMD: |";
		std::string sCmd = params[0].get_str(),  sNick = params[1].get_str(), sCoinAddr = params[2].get_str(), p4 = params[3].get_str();
		uint64_t u6Tm = GetTime();
		if( sCmd == "Edit User" )
		{
			string p5 = params[4].get_str(), p6 = params[5].get_str();
			string sMsg = u64tostr(u6Tm) + "," + sNick + "," + sCoinAddr;
			string sSign = signMessage(sSystemAddr, sMsg);
			sRzt = sRzt + "Edit User|" + u64tostr(u6Tm) + "|" + sNick + "|" + sCoinAddr + "|" + p4 + "|" + p5 + "|" + p6 + "|" + sSign + "|" + sSystemAddr;
		}
		else if( sCmd == "Rollback Blocks" )
		{
			string sMsg = u64tostr(u6Tm) + "," + sNick + "," + sCoinAddr;
			string sSign = signMessage(sSystemAddr, sMsg);
			sRzt = sRzt + "Rollback Blocks|" + u64tostr(u6Tm) + "|" + sNick + "|" + sCoinAddr + "|" + p4 + "|" + sSign + "|" + sSystemAddr;
		}
		return sRzt;
	}
	throw runtime_error("need <params>\n");
}