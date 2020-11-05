// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2020 Bean Core www.beancash.org
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITBEAN_CHAIN_PARAMS_H
#define BITBEAN_CHAIN_PARAMS_H

#include "bignum.h"
#include "uint256.h"
#include "core.h"
#include "protocol.h"
#include "util.h"

#include <vector>

using namespace std;

typedef unsigned char MessageStartChars[MESSAGE_START_SIZE];

struct CDNSSeedData {
    string name, host;
    CDNSSeedData(const string &strName, const string &strHost) : name(strName), host(strHost) {}
};

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Bean Cash system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams
{
public:
    enum Network {
        MAIN,
        TESTNET,
        REGTEST,
    };

    enum Base58Type {
        PUBKEY_ADDRESS,
        SCRIPT_ADDRESS,
        SECRET_KEY,

        MAX_BASE58_TYPES
    };

    const uint256& HashGenesisBlock() const { return hashGenesisBlock; }
    const MessageStartChars& MessageStart() const { return pchMessageStart; }
    const vector<unsigned char>& AlertKey() const { return vAlertPubKey; }
    int GetDefaultPort() const { return nDefaultPort; }
    const CBigNum& ProofOfWorkLimit() const { return bnProofOfWorkLimit; }
    const CBlock& GenesisBlock() const { return genesis; }
    bool RequireRPCPassword() const { return fRequireRPCPassword; }
    const string& DataDir() const { return strDataDir; }
    Network NetworkID() const { return networkID; }
    const vector<CDNSSeedData>& DNSSeeds() const { return vSeeds; }
    const std::vector<unsigned char> &Base58Prefix(Base58Type type) const { return base58Prefixes[type]; }
    const vector<CAddress>& FixedSeeds() const { return vFixedSeeds; }
    int RPCPort() const { return nRPCPort; }
protected:
    CChainParams() {}

    uint256 hashGenesisBlock;
    MessageStartChars pchMessageStart;
    // Raw pub key bytes for the broadcast alert signing key.
    vector<unsigned char> vAlertPubKey;
    int nDefaultPort;
    int nRPCPort;
    CBigNum bnProofOfWorkLimit;
    string strDataDir;
    vector<CDNSSeedData> vSeeds;
    std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
    Network networkID;
    CBlock genesis;
    vector<CAddress> vFixedSeeds;
    bool fRequireRPCPassword;
    bool fRPCisTestNet;
};

/**
 * Return the currently selected parameters. This won't change after app startup
 * outside of the unit tests.
 */
const CChainParams &Params();

/** Sets the params returned by Params() to those for the given network. */
void SelectParams(CChainParams::Network network);

/**
 * Looks for -regtest or -testnet and then calls SelectParams as appropriate.
 * Returns false if an invalid combination is given.
 */
bool SelectParamsFromCommandLine();

inline bool TestNet() {
    // Note: it's deliberate that this returns "false" for regression test mode.
    return Params().NetworkID() == CChainParams::TESTNET;
}

#endif // CHAINPARAMS_H
