// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/foreach.hpp>

#include "checkpoints.h"

#include "txdb.h"
#include "main.h"
#include "uint256.h"


static const int nCheckpointSpan = 10;

namespace Checkpoints
{
    typedef std::map<int, uint256> MapCheckpoints;

    //
    // What makes a good checkpoint block?
    // + Is surrounded by blocks with reasonable timestamps
    //   (no blocks before with a timestamp after, none after with
    //    timestamp before)
    // + Contains no strange transactions
    //
    static MapCheckpoints mapCheckpoints =
        boost::assign::map_list_of
        ( 0,      uint256("0x00000bc87d0385e25417c877e00e959087e8eebbf76722dfcff9e76da7cbf3ea"))
		( 10,     uint256("0x95b0f5c36cdce1b51cf5b6ff2c08e9e93497e08be66f2cecd3c765d1744fddb9"))
		( 126,     uint256("0xb6860467e5cb4e062e1c43650b96d69cd9245487941d24bf5d91fc19c99a0262"))
		( 15000,     uint256("0xa8361401f91dbcb4b6b2353a911a1ee48cdd03fe9fb879b78166fc7caa381fbd"))
		( 24547,     uint256("0x9a68364485590520199ff7f3b30e43408e95addbeb37d10e6a3f3802e72566b7"))
		( 27800,     uint256("0x61f3fded27bf3d6ccf9690c31e62f4f5e29a0b9718b666c496840e1b57e0a123"))
		( 60000,     uint256("0x37cc6891fa532fe3a94e074d322cd4aa469931b59a2bfdf1647b002c13f3e78d"))
		( 65000,     uint256("0x84c8042de36bc64f57951d3736055e0e707ce0f744840461e6fffdb77305f960"))
		( 70000,     uint256("0x04eca6493a913168c7ee52ab1e414055c3a3b998d7b0d9d9c3119daabcf33bea"))
		( 75000,     uint256("0x0d7caebf517da24c7c5aa531ef49e87fd5c6a84d23e20f9d75fbe9a6e750391e"))
		( 80000,     uint256("0x35bda67c443de2e7f5a0792afd40b6294eb96f783c52746ac902965e68c65530"))
		( 85000,     uint256("0xfe73daccc78fcebefce49e1b0cddd765647b295294a16b8ea4a957c7564668b1"))
		( 90000,     uint256("0x880a185b1ee4e390b5099bbc42f95550be1a9b1a28350533b26f08b0baec194e"))
		( 100000,     uint256("0xec428d374ffb246338f3aa83ffd17bdffb17da2abaa75e3d069852f7b79a6eab"))
		( 110000,     uint256("0xdaccc0aa5972f3c20b7c3a9afc88678c29c71e00cb9af9f0d4f8a12dcd0797f4"))
		( 120000,     uint256("0x82fe884f0705fb393bd78f67a084269f8b44f11384d59a30700744e233b3e7d6"))
		( 150000,     uint256("0x428becc1c5bc62d4b6e041e3395bae0f5dc0641a1c42449b6f8d05edf18af944"))
		( 200000,     uint256("0x77266e893a1104a2a6cfc6184a9f729723220f349167fabb70347044b0bcc460"))
		( 240000,     uint256("0xd876f59aee5bc9293dd99392767320786f1bb16ae86ae8871f6b5f5af53e0e18"))
		( 250000,     uint256("0x0c4affc4f9003e04d73f81800ab7e4c51f005939b6f62563eeed71c905bf4c62"))
		( 260000,     uint256("0xb02b55f419f4bffd3786d487b9c45a1a67634446e547f2a6d41a18ae93fa4f42"))
		( 270000,     uint256("0x23f6369cbdb5538ec4d49a481b4f3b6bfdc9e5ffd48e92d3f969149321d2f9da"))
		( 292293,     uint256("0x48fb32107293572f65077ce6d836af63577568ed63f1956da31b615be295a030"))
		( 300000,     uint256("0x876abd9fa83c97f0c81d92c29db81c68dcc4c16b12bd84b7f09e4b02800e0bdf"))
		( 330000,     uint256("0x0e73b398df5bc80d9f87e1433f274ee68cc4a1f8ca05306cd9001b96e217db73"))
		( 340000,     uint256("0x73a6ba16682f4e21ad85765ad586742bd336c2de82c6e824692434b2d5a1e551"))
    ;

    // TestNet has no checkpoints
    static MapCheckpoints mapCheckpointsTestnet =
        boost::assign::map_list_of
        ( 0, uint256("0x00000335d0f101f4a000216013cdb9838b1965d09d2f0dade343ae73e682ab65"))
        ( 100, uint256("0x5a452a195eb657d3a85863b14ad9231565bc47d9c6ad40f3bf8d46d0be136c74"))
        ;

    bool CheckHardened(int nHeight, const uint256& hash)
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        MapCheckpoints::const_iterator i = checkpoints.find(nHeight);
        if (i == checkpoints.end()) return true;
        return hash == i->second;
    }

    int GetTotalBlocksEstimate()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        return checkpoints.rbegin()->first;
    }
	

    uint256 GetTotalBlocksEstimateHash()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        return checkpoints.rbegin()->second;
    }
	int insertToCheckpoints(int idx, uint256 hash)
	{
		//std::map<int, uint256> map_W;
		MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);
		//checkpoints.insert(idx, hash);
		return checkpoints.size();
	}
	
    bool IsCheckpoint(uint256 hash)
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, checkpoints)
        {
            if(i.second == hash)
            {
                return true;
            }
        }
        return false;
    }

    void LoadCheckpoints(CNode* pfrom)
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        BOOST_FOREACH(const MapCheckpoints::value_type& i, checkpoints)
        {
            if(i.second != hashGenesisBlock)
            {
                if( !mapOrphanBlocks.count(i.second) && !mapBlockIndex.count(i.second) )
                {
                    pfrom->AskFor(CInv(pfrom->zipblock > 0 ? MSG_BLKZP : MSG_BLOCK, i.second));
                }
            }
        }
    }

    int GetNumCheckpoints()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        return checkpoints.size();
    }

    int GetNumLoadedCheckpoints()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        int Ret=0;
        MapCheckpoints::const_iterator i = checkpoints.begin();
        for(;i != checkpoints.end();i++)
        {
            if( mapOrphanBlocks.count(i->second) || mapBlockIndex.count(i->second) )
            {
                Ret++;
            }
        }
        return Ret;
    }

    void InsertPlaceHoldersBetweenCheckpoints()
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        MapCheckpoints::const_iterator i = checkpoints.begin();
        int64_t prevIndex=0;
        uint256 prevHash = 0;
        while(i != checkpoints.end())
        {
            if (mapOrphanBlocks.count(i->second))
            {
                CBlock placeHolderBlock;
                //placeHolderBlock.hashOverride = mapOrphanBlocks[i->second]->hashPrevBlock;
                placeHolderBlock.hashPrevBlock = prevHash;
                //placeHolderBlock.numPlacesHeld = i->first - prevIndex;
                //placeHolderBlock.headerOnly = true;
                //placeHolderBlock.placeHolderBlock = true;
                ProcessBlock(NULL, &placeHolderBlock);
            }
            prevHash = i->second;
            prevIndex = i->first;
            // Check if already in tree - when loading an existing block index this may be the case.
            if (mapBlockIndex.count(prevHash))
            {
                prevHash = pindexBest->GetBlockHash();
                prevIndex = pindexBest->nHeight;
            }

            i++;
        }
    }
	
    CBlockIndex* GetLastCheckpoint(const std::map<uint256, CBlockIndex*>& mapBlockIndex)
    {
        MapCheckpoints& checkpoints = (fTestNet ? mapCheckpointsTestnet : mapCheckpoints);

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, checkpoints)
        {
            const uint256& hash = i.second;
            std::map<uint256, CBlockIndex*>::const_iterator t = mapBlockIndex.find(hash);
            if (t != mapBlockIndex.end())
                return t->second;
        }
        return NULL;
    }

    // ppcoin: synchronized checkpoint (centrally broadcasted)
    uint256 hashSyncCheckpoint = 0;
    uint256 hashPendingCheckpoint = 0;
    CSyncCheckpoint checkpointMessage;
    CSyncCheckpoint checkpointMessagePending;
    uint256 hashInvalidCheckpoint = 0;
    CCriticalSection cs_hashSyncCheckpoint;

    // ppcoin: get last synchronized checkpoint
    CBlockIndex* GetLastSyncCheckpoint()
    {
        LOCK(cs_hashSyncCheckpoint);
        if (!mapBlockIndex.count(hashSyncCheckpoint))
            error("GetSyncCheckpoint: block index missing for current sync-checkpoint %s", hashSyncCheckpoint.ToString().c_str());
        else
            return mapBlockIndex[hashSyncCheckpoint];
        return NULL;
    }

    // ppcoin: only descendant of current sync-checkpoint is allowed
    bool ValidateSyncCheckpoint(uint256 hashCheckpoint)
    {
        if (!mapBlockIndex.count(hashSyncCheckpoint))
            return error("ValidateSyncCheckpoint: block index missing for current sync-checkpoint %s", hashSyncCheckpoint.ToString().c_str());
        if (!mapBlockIndex.count(hashCheckpoint))
            return error("ValidateSyncCheckpoint: block index missing for received sync-checkpoint %s", hashCheckpoint.ToString().c_str());

        CBlockIndex* pindexSyncCheckpoint = mapBlockIndex[hashSyncCheckpoint];
        CBlockIndex* pindexCheckpointRecv = mapBlockIndex[hashCheckpoint];

        if (pindexCheckpointRecv->nHeight <= pindexSyncCheckpoint->nHeight)
        {
            // Received an older checkpoint, trace back from current checkpoint
            // to the same height of the received checkpoint to verify
            // that current checkpoint should be a descendant block
            CBlockIndex* pindex = pindexSyncCheckpoint;
            while (pindex->nHeight > pindexCheckpointRecv->nHeight)
                if (!(pindex = pindex->pprev))
                    return error("ValidateSyncCheckpoint: pprev null - block index structure failure");
            if (pindex->GetBlockHash() != hashCheckpoint)
            {
                hashInvalidCheckpoint = hashCheckpoint;
                return error("ValidateSyncCheckpoint: new sync-checkpoint %s is conflicting with current sync-checkpoint %s", hashCheckpoint.ToString().c_str(), hashSyncCheckpoint.ToString().c_str());
            }
            return false; // ignore older checkpoint
        }

        // Received checkpoint should be a descendant block of the current
        // checkpoint. Trace back to the same height of current checkpoint
        // to verify.
        CBlockIndex* pindex = pindexCheckpointRecv;
        while (pindex->nHeight > pindexSyncCheckpoint->nHeight)
            if (!(pindex = pindex->pprev))
                return error("ValidateSyncCheckpoint: pprev2 null - block index structure failure");
        if (pindex->GetBlockHash() != hashSyncCheckpoint)
        {
            hashInvalidCheckpoint = hashCheckpoint;
            return error("ValidateSyncCheckpoint: new sync-checkpoint %s is not a descendant of current sync-checkpoint %s", hashCheckpoint.ToString().c_str(), hashSyncCheckpoint.ToString().c_str());
        }
        return true;
    }

    bool WriteSyncCheckpoint(const uint256& hashCheckpoint)
    {
        CTxDB txdb;
        txdb.TxnBegin();
        if (!txdb.WriteSyncCheckpoint(hashCheckpoint))
        {
            txdb.TxnAbort();
            return error("WriteSyncCheckpoint(): failed to write to db sync checkpoint %s", hashCheckpoint.ToString().c_str());
        }
        if (!txdb.TxnCommit())
            return error("WriteSyncCheckpoint(): failed to commit to db sync checkpoint %s", hashCheckpoint.ToString().c_str());

        Checkpoints::hashSyncCheckpoint = hashCheckpoint;
        return true;
    }

    bool AcceptPendingSyncCheckpoint()
    {
        LOCK(cs_hashSyncCheckpoint);
        if (hashPendingCheckpoint != 0 && mapBlockIndex.count(hashPendingCheckpoint))
        {
            if (!ValidateSyncCheckpoint(hashPendingCheckpoint))
            {
                hashPendingCheckpoint = 0;
                checkpointMessagePending.SetNull();
                return false;
            }

            CTxDB txdb;
            CBlockIndex* pindexCheckpoint = mapBlockIndex[hashPendingCheckpoint];
            if (!pindexCheckpoint->IsInMainChain())
            {
                CBlock block;
                if (!block.ReadFromDisk(pindexCheckpoint))
                    return error("AcceptPendingSyncCheckpoint: ReadFromDisk failed for sync checkpoint %s", hashPendingCheckpoint.ToString().c_str());
                if (!block.SetBestChain(txdb, pindexCheckpoint))
                {
                    hashInvalidCheckpoint = hashPendingCheckpoint;
                    return error("AcceptPendingSyncCheckpoint: SetBestChain failed for sync checkpoint %s", hashPendingCheckpoint.ToString().c_str());
                }
            }

            if (!WriteSyncCheckpoint(hashPendingCheckpoint))
                return error("AcceptPendingSyncCheckpoint(): failed to write sync checkpoint %s", hashPendingCheckpoint.ToString().c_str());
            hashPendingCheckpoint = 0;
            checkpointMessage = checkpointMessagePending;
            checkpointMessagePending.SetNull();
            printf("AcceptPendingSyncCheckpoint : sync-checkpoint at %s\n", hashSyncCheckpoint.ToString().c_str());
            // relay the checkpoint
            if (!checkpointMessage.IsNull())
            {
                BOOST_FOREACH(CNode* pnode, vNodes)
                    checkpointMessage.RelayTo(pnode);
            }
            return true;
        }
        return false;
    }

    // Automatically select a suitable sync-checkpoint 
    uint256 AutoSelectSyncCheckpoint()
    {
        const CBlockIndex *pindex = pindexBest;
        // Search backward for a block within max span and maturity window
        while (pindex->pprev && (pindex->GetBlockTime() + nCheckpointSpan * GetTargetSpacing(nBestHeight) > pindexBest->GetBlockTime() || pindex->nHeight + nCheckpointSpan > pindexBest->nHeight))
            pindex = pindex->pprev;
        return pindex->GetBlockHash();
    }

    // Check against synchronized checkpoint
    bool CheckSync(const uint256& hashBlock, const CBlockIndex* pindexPrev)
    {
        if (fTestNet) return true; // Testnet has no checkpoints
        int nHeight = pindexPrev->nHeight + 1;

        LOCK(cs_hashSyncCheckpoint);
        // sync-checkpoint should always be accepted block
        assert(mapBlockIndex.count(hashSyncCheckpoint));
        const CBlockIndex* pindexSync = mapBlockIndex[hashSyncCheckpoint];

        if (nHeight > pindexSync->nHeight)
        {
            // trace back to same height as sync-checkpoint
            const CBlockIndex* pindex = pindexPrev;
            while (pindex->nHeight > pindexSync->nHeight)
                if (!(pindex = pindex->pprev))
                    return error("CheckSync: pprev null - block index structure failure");
            if (pindex->nHeight < pindexSync->nHeight || pindex->GetBlockHash() != hashSyncCheckpoint)
                return false; // only descendant of sync-checkpoint can pass check
        }
        if (nHeight == pindexSync->nHeight && hashBlock != hashSyncCheckpoint)
            return false; // same height with sync-checkpoint
        if (nHeight < pindexSync->nHeight && !mapBlockIndex.count(hashBlock))
            return false; // lower height than sync-checkpoint
        return true;
    }

    bool WantedByPendingSyncCheckpoint(uint256 hashBlock)
    {
        LOCK(cs_hashSyncCheckpoint);
        if (hashPendingCheckpoint == 0)
            return false;
        if (hashBlock == hashPendingCheckpoint)
            return true;
        if (mapOrphanBlocks.count(hashPendingCheckpoint) 
            && hashBlock == WantedByOrphan(mapOrphanBlocks[hashPendingCheckpoint]))
            return true;
        return false;
    }

    // ppcoin: reset synchronized checkpoint to last hardened checkpoint
    bool ResetSyncCheckpoint()
    {
        LOCK(cs_hashSyncCheckpoint);
        const uint256& hash = mapCheckpoints.rbegin()->second;
        if (mapBlockIndex.count(hash) && !mapBlockIndex[hash]->IsInMainChain())
        {
            // checkpoint block accepted but not yet in main chain
            printf("ResetSyncCheckpoint: SetBestChain to hardened checkpoint %s\n", hash.ToString().c_str());
            CTxDB txdb;
            CBlock block;
            if (!block.ReadFromDisk(mapBlockIndex[hash]))
                return error("ResetSyncCheckpoint: ReadFromDisk failed for hardened checkpoint %s", hash.ToString().c_str());
            if (!block.SetBestChain(txdb, mapBlockIndex[hash]))
            {
                return error("ResetSyncCheckpoint: SetBestChain failed for hardened checkpoint %s", hash.ToString().c_str());
            }
        }
        else if(!mapBlockIndex.count(hash))
        {
            // checkpoint block not yet accepted
            hashPendingCheckpoint = hash;
            checkpointMessagePending.SetNull();
            printf("ResetSyncCheckpoint: pending for sync-checkpoint %s\n", hashPendingCheckpoint.ToString().c_str());
        }

        BOOST_REVERSE_FOREACH(const MapCheckpoints::value_type& i, mapCheckpoints)
        {
            const uint256& hash = i.second;
            if (mapBlockIndex.count(hash) && mapBlockIndex[hash]->IsInMainChain())
            {
                if (!WriteSyncCheckpoint(hash))
                    return error("ResetSyncCheckpoint: failed to write sync checkpoint %s", hash.ToString().c_str());
                printf("ResetSyncCheckpoint: sync-checkpoint reset to %s\n", hashSyncCheckpoint.ToString().c_str());
                return true;
            }
        }

        return false;
    }

    void AskForPendingSyncCheckpoint(CNode* pfrom)
    {
        LOCK(cs_hashSyncCheckpoint);
        if (pfrom && hashPendingCheckpoint != 0 && (!mapBlockIndex.count(hashPendingCheckpoint)) && (!mapOrphanBlocks.count(hashPendingCheckpoint)))
            pfrom->AskFor(CInv(pfrom->zipblock > 0 ? MSG_BLKZP : MSG_BLOCK, hashPendingCheckpoint));
    }

    bool SetCheckpointPrivKey(std::string strPrivKey)
    {
        // Test signing a sync-checkpoint with genesis block
        CSyncCheckpoint checkpoint;
        checkpoint.hashCheckpoint = !fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet;
        CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
        sMsg << (CUnsignedSyncCheckpoint)checkpoint;
        checkpoint.vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());

        std::vector<unsigned char> vchPrivKey = ParseHex(strPrivKey);
        CKey key;
        key.SetPrivKey(CPrivKey(vchPrivKey.begin(), vchPrivKey.end())); // if key is not correct openssl may crash
        if (!key.Sign(Hash(checkpoint.vchMsg.begin(), checkpoint.vchMsg.end()), checkpoint.vchSig))
            return false;

        // Test signing successful, proceed
        CSyncCheckpoint::strMasterPrivKey = strPrivKey;
        return true;
    }

    bool SendSyncCheckpoint(uint256 hashCheckpoint)
    {
        CSyncCheckpoint checkpoint;
        checkpoint.hashCheckpoint = hashCheckpoint;
        CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
        sMsg << (CUnsignedSyncCheckpoint)checkpoint;
        checkpoint.vchMsg = std::vector<unsigned char>(sMsg.begin(), sMsg.end());

        if (CSyncCheckpoint::strMasterPrivKey.empty())
            return error("SendSyncCheckpoint: Checkpoint master key unavailable.");
        std::vector<unsigned char> vchPrivKey = ParseHex(CSyncCheckpoint::strMasterPrivKey);
        CKey key;
        key.SetPrivKey(CPrivKey(vchPrivKey.begin(), vchPrivKey.end())); // if key is not correct openssl may crash
        if (!key.Sign(Hash(checkpoint.vchMsg.begin(), checkpoint.vchMsg.end()), checkpoint.vchSig))
            return error("SendSyncCheckpoint: Unable to sign checkpoint, check private key?");

        if(!checkpoint.ProcessSyncCheckpoint(NULL))
        {
            printf("WARNING: SendSyncCheckpoint: Failed to process checkpoint.\n");
            return false;
        }

        // Relay checkpoint
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
                checkpoint.RelayTo(pnode);
        }
        return true;
    }

    // Is the sync-checkpoint outside maturity window?
    bool IsMatureSyncCheckpoint()
    {
        LOCK(cs_hashSyncCheckpoint);
        // sync-checkpoint should always be accepted block
        assert(mapBlockIndex.count(hashSyncCheckpoint));
        const CBlockIndex* pindexSync = mapBlockIndex[hashSyncCheckpoint];

		int vCoinbaseMaturity = Get_nCoinbaseMaturity(pindexSync->nHeight);
		//if( pindexSync->nHeight <= 10 )
		//	vCoinbaseMaturity = 0;

        return (nBestHeight >= pindexSync->nHeight + vCoinbaseMaturity ||
                pindexSync->GetBlockTime() + Get_nStakeMinAge(pindexSync->nHeight) < GetAdjustedTime());
    }
}

// ppcoin: sync-checkpoint master key
const std::string CSyncCheckpoint::strMasterPubKey = "049005d1a9fe0162fb7f152e2ec2fa6793c3cc920127c50bfba5b16b8337e338f7fd91881cd85d3b72987fb5a65cbccf99e354fbc5680a87aa0c521098aea151cd";

std::string CSyncCheckpoint::strMasterPrivKey = "";

// ppcoin: verify signature of sync-checkpoint message
bool CSyncCheckpoint::CheckSignature()
{
    CKey key;
    if (!key.SetPubKey(ParseHex(CSyncCheckpoint::strMasterPubKey)))
        return error("CSyncCheckpoint::CheckSignature() : SetPubKey failed");
    if (!key.Verify(Hash(vchMsg.begin(), vchMsg.end()), vchSig))
        return error("CSyncCheckpoint::CheckSignature() : verify signature failed");

    // Now unserialize the data
    CDataStream sMsg(vchMsg, SER_NETWORK, PROTOCOL_VERSION);
    sMsg >> *(CUnsignedSyncCheckpoint*)this;
    return true;
}

// ppcoin: process synchronized checkpoint
bool CSyncCheckpoint::ProcessSyncCheckpoint(CNode* pfrom)
{
    if (!CheckSignature())
        return false;

    LOCK(Checkpoints::cs_hashSyncCheckpoint);
    if (!mapBlockIndex.count(hashCheckpoint))
    {
        // We haven't received the checkpoint chain, keep the checkpoint as pending
        Checkpoints::hashPendingCheckpoint = hashCheckpoint;
        Checkpoints::checkpointMessagePending = *this;
        printf("ProcessSyncCheckpoint: pending for sync-checkpoint %s\n", hashCheckpoint.ToString().c_str());
        // Ask this guy to fill in what we're missing
        if (pfrom)
        {
            pfrom->PushGetBlocks(pindexBest, hashCheckpoint);
            // ask directly as well in case rejected earlier by duplicate
            // proof-of-stake because getblocks may not get it this time
            pfrom->AskFor(CInv(pfrom->zipblock > 0 ? MSG_BLKZP : MSG_BLOCK, mapOrphanBlocks.count(hashCheckpoint)? WantedByOrphan(mapOrphanBlocks[hashCheckpoint]) : hashCheckpoint));
        }
        return false;
    }

    if (!Checkpoints::ValidateSyncCheckpoint(hashCheckpoint))
        return false;

    CTxDB txdb;
    CBlockIndex* pindexCheckpoint = mapBlockIndex[hashCheckpoint];
    if (!pindexCheckpoint->IsInMainChain())
    {
        // checkpoint chain received but not yet main chain
        CBlock block;
        if (!block.ReadFromDisk(pindexCheckpoint))
            return error("ProcessSyncCheckpoint: ReadFromDisk failed for sync checkpoint %s", hashCheckpoint.ToString().c_str());
        if (!block.SetBestChain(txdb, pindexCheckpoint))
        {
            Checkpoints::hashInvalidCheckpoint = hashCheckpoint;
            return error("ProcessSyncCheckpoint: SetBestChain failed for sync checkpoint %s", hashCheckpoint.ToString().c_str());
        }
    }

    if (!Checkpoints::WriteSyncCheckpoint(hashCheckpoint))
        return error("ProcessSyncCheckpoint(): failed to write sync checkpoint %s", hashCheckpoint.ToString().c_str());
    Checkpoints::checkpointMessage = *this;
    Checkpoints::hashPendingCheckpoint = 0;
    Checkpoints::checkpointMessagePending.SetNull();
    printf("ProcessSyncCheckpoint: sync-checkpoint at %s\n", hashCheckpoint.ToString().c_str());
    return true;
}
