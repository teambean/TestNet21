// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2020 Bean Core www.beancash.org
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _BITBEANRPC_CLIENT_H_
#define _BITBEANRPC_CLIENT_H_ 1

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"

int CommandLineRPC(int argc, char *argv[]);

/** Convert parameter values for RPC call from strings to command-specific JSON objects. */
json_spirit::Array RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams);

#endif
