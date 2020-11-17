// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2019 Bean Core www.beancash.org
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITBEAN_INIT_H
#define BITBEAN_INIT_H

#include "wallet.h"

// namespace boost {
//    class thread_group;
// }

extern CWallet* pwalletMain;
void StartShutdown();
bool ShutdownRequested();
void Shutdown();
bool AppInit2(boost::thread_group& threadGroup);
/** Help for options shared between UI and daemon (for -help) */
std::string HelpMessage();
/** Returns licensing information (for -version & -v) */
std::string LicenseInfo();

#endif
