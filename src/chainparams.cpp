// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2020 Bean Core www.beancash.org
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assert.h"

#include "chainparams.h"
#include "main.h"
#include "util.h"

#include <boost/assign/list_of.hpp>

using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

//
// Main network
//

// Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress> &vSeedsOut, const SeedSpec6 *data, unsigned int count)
{
        // It'll only connect to one or two seed nodes because once it connects,
        // it'll get a pile of addresses with newer timestamps.
        // Seed nodes are given a random 'last seen time' of between one and two
        // weeks ago.
        const int64_t nOneWeek = 7*24*60*60;
        for (unsigned int i = 0; i < count; i++)
        {
            struct in6_addr ip;
            memcpy(&ip, data[i].addr, sizeof(ip));
            CAddress addr(CService(ip, data[i].port));
            addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
            vSeedsOut.push_back(addr);
        }
}

class CMainParams : public CChainParams {
public:
    CMainParams() {
        networkID = CChainParams::MAIN;
        // The message start string is designed to be unlikely to occur in normal data.
        // The characters are rarely used upper ASCII, not valid as UTF-8, and produce
        // a large 4-byte int at any alignment.
        pchMessageStart[0] = 0xa4;
        pchMessageStart[1] = 0xd2;
        pchMessageStart[2] = 0xf8;
        pchMessageStart[3] = 0xa6;
        vAlertPubKey = ParseHex("020dd8d110163315b5ed74293dcb6a69fc4a2ed0e549af37f69d4ecf99e1a21f40");
        nDefaultPort = 22460;
        nRPCPort = 22461;
        bnProofOfWorkLimit = CBigNum(~uint256(0) >> 20); // Starting Difficulty: results with 0,000244140625 proof-of-work difficulty

                // NewMainNet:

                //block.nTime = 1423862862
                //block.nNonce = 620091
                //block.nVersion = 1
                //block.hashMerkleRoot = da3215e78c191c4e5dd00e8ac2b57f71b20cdcad0c37562d39912df09a2f4d34
                //block.GetHash = 000009d2f828234d65299216e258242a4ea75d1b8d8a71d076377145068f08de


                // NewTestNet:



        /**
         * Build the genesis block. Note that the output of its generation transaction cannot be spent since it did not originally exist in the database.
         *
         * CBlock(hash=000009d2f828234d65299216e258242a4ea75d1b8d8a71d076377145068f08de, ver=1, hashPrevBlock=0000000000000000000000000000000000000000000000000000000000000000,
         * hashMerkleRoot=da3215e78c191c4e5dd00e8ac2b57f71b20cdcad0c37562d39912df09a2f4d34, nTime=1423862862, nBits=1f00ffff, nNonce=620091, vtx=1, vchBlockSig=)
         *   Beanbase(hash=da3215e78c, nTime=1423862862, ver=1, vin.size=1, vout.size=1, nLockTime=0)
         *     CTxIn(COutPoint(0000000000, 4294967295), beanbase 00012a4a3133204665622032303135202d204269744265616e206c61756e6368657320616e64206368616e6765732074686520776f726c642077697468206974277320617765736f6d656e657373)
         *     CTxOut(empty)
         *   vMerkleTree: da3215e78c
        */

        const char* pszTimestamp = "13 Feb 2015 - BitBean launches and changes the world with it's awesomeness";
        std::vector<CTxIn> vin;
        vin.resize(1);
        vin[0].scriptSig = CScript() << 0 << CBigNum(42) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        std::vector<CTxOut> vout;
        vout.resize(1);
        vout[0].SetEmpty();
        CTransaction txNew(1, 1423862862, vin, vout, 0);
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime    = 1423862862;
        genesis.nBits    = bnProofOfWorkLimit.GetCompact();
        genesis.nNonce   = 620091;

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x000009d2f828234d65299216e258242a4ea75d1b8d8a71d076377145068f08de"));
        assert(genesis.hashMerkleRoot == uint256("0xda3215e78c191c4e5dd00e8ac2b57f71b20cdcad0c37562d39912df09a2f4d34"));


        vSeeds.push_back(CDNSSeedData("bitbean.org", "stalk1.bitbean.org"));
        vSeeds.push_back(CDNSSeedData("bitbean.org", "stalk2.bitbean.org"));
        vSeeds.push_back(CDNSSeedData("bitbean.org", "stalk3.bitbean.org"));
        vSeeds.push_back(CDNSSeedData("beancash.org", "stalk1.beancash.org"));
        vSeeds.push_back(CDNSSeedData("beancash.org", "stalk2.beancash.org"));
        vSeeds.push_back(CDNSSeedData("beancash.org", "stalk3.beancash.org"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,3);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,85);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1,131);

        convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        fRequireRPCPassword = true;
        fRPCisTestNet = false;
    }
};

static CMainParams mainParams;

//
// Testnet (v2)
//
class CTestNetParams : public CMainParams {
public:
    CTestNetParams() {
        networkID = CChainParams::TESTNET;
        // The message start string is designed to be unlikely to occur in normal data.
        // The characters are rarely used upper ASCII, not valid as UTF-8, and produce
        // a large 4-byte int at any alignment.
        pchMessageStart[0] = 0xad;
        pchMessageStart[1] = 0xf1;
        pchMessageStart[2] = 0xc2;
        pchMessageStart[3] = 0xaf;
        CBigNum bnProofOfWorkLimit(~uint256(0) >> 16);
        vAlertPubKey = ParseHex("029ce30d356cdbee7553659bdd7a0bea8088791dd57c2ae7c04d28dcdd3b786861");
        nDefaultPort = 22462;
        nRPCPort = 22463;
        strDataDir = "testnet2";

        // Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nNonce = 98938;
        genesis.nBits = bnProofOfWorkLimit.GetCompact();
        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x0000021cddf3e66033819044559ebf09acdb95dd79b1743d367d03224e10674b"));

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("beancash.net", "stalk1.beancash.net"));
        vSeeds.push_back(CDNSSeedData("beancash.net", "stalk2.beancash.net"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1,239);

        fRequireRPCPassword = true;
        fRPCisTestNet = true;
    }
};

static CTestNetParams testNetParams;

//
// Regression test
//
class CRegTestParams : public CTestNetParams {
public:
    CRegTestParams() {
        networkID = CChainParams::REGTEST;
        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        bnProofOfWorkLimit = CBigNum(~uint256(0) >> 1);
        genesis.nTime = 1423862862;
        genesis.nBits = bnProofOfWorkLimit.GetCompact();
        genesis.nNonce = 2;
        hashGenesisBlock = genesis.GetHash();
        nDefaultPort = 18444;
        strDataDir = "regtest";
        assert(hashGenesisBlock == uint256("0x0000021cddf3e66033819044559ebf09acdb95dd79b1743d367d03224e10674b"));

        vSeeds.clear();  // Regtest mode doesn't have any DNS seeds.
        fRequireRPCPassword = false;
        fRPCisTestNet = true;
    }
};

static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = &mainParams;

const CChainParams &Params() {
    return *pCurrentParams;
}

void SelectParams(CChainParams::Network network) {
    switch (network) {
        case CChainParams::MAIN:
            pCurrentParams = &mainParams;
            break;
        case CChainParams::TESTNET:
            pCurrentParams = &testNetParams;
            break;
        case CChainParams::REGTEST:
            pCurrentParams = &regTestParams;
            break;
        default:
            assert(false && "Unimplemented network");
            return;
    }
}

bool SelectParamsFromCommandLine() {
    bool fRegTest = GetBoolArg("-regtest", false);
    bool fTestNet = GetBoolArg("-testnet", false);

    if (fTestNet && fRegTest) {
        return false;
    }

    if (fRegTest) {
        SelectParams(CChainParams::REGTEST);
    } else if (fTestNet) {
        SelectParams(CChainParams::TESTNET);
    } else {
        SelectParams(CChainParams::MAIN);
    }
    return true;
}
