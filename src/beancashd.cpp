// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2020 Bean Core www.beancash.org
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "bitbeanrpc.h"
#include <boost/algorithm/string/predicate.hpp>

void WaitForShutdown(boost::thread_group* threadGroup)
{
    bool fShutdown = ShutdownRequested();
    // Tell the main threads to shutdown
    while (!fShutdown)
    {
        MilliSleep(200);
        fShutdown = ShutdownRequested();
    }
    if (threadGroup)
    {
        threadGroup->interrupt_all();
        threadGroup->join_all();
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
bool AppInit(int argc, char* argv[])
{
    boost::thread_group threadGroup;
    bool fRet = false;
    fHaveGUI = false;
    try
    {
        //
        // Parameters
        //
        // If Qt is used, parameters/beancash.conf are parsed in qt/bitbean.cpp's main()
        ParseParameters(argc, argv);
        if (!boost::filesystem::is_directory(GetDataDir(false)))
        {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", mapArgs["-datadir"].c_str());
            return false;
        }
        ReadConfigFile(mapArgs, mapMultiArgs);
        // Check for -testnet or -regtest parameter (TestNet() calls are only valid after this clause)
        if (!SelectParamsFromCommandLine()) {
            fprintf(stderr, "Error: Invalid combination of -regtest and -testnet used.\n");
            return false;
        }

        if (mapArgs.count("-?") || mapArgs.count("--help"))
        {
            // First part of help message is specific to bitbeand / RPC client
            std::string strUsage = _("Beancash version") + " " + FormatFullVersion() + "\n\n" +
                _("Usage:") + "\n" +
                  "  Beancashd [options]                     " + "\n" +
                  "  Beancashd [options] <command> [params]  " + _("Send command to -server or Beancashd") + "\n" +
                  "  Beancashd [options] help                " + _("List commands") + "\n" +
                  "  Beancashd [options] help <command>      " + _("Get help for a command") + "\n";

            strUsage += "\n" + HelpMessage();

            fprintf(stdout, "%s", strUsage.c_str());
            return false;
        }

        // Command-line RPC
        bool fCommandLine = false;
        for (int i = 1; i < argc; i++)
            if (!IsSwitchChar(argv[i][0]) && !boost::algorithm::istarts_with(argv[i], "Beancash:"))
                fCommandLine = true;

        if (fCommandLine)
        {
            int ret = CommandLineRPC(argc, argv);
            exit(ret);
        }
#ifndef WIN32
    fDaemon = GetBoolArg("-daemon");
    if (fDaemon)
    {
        // Daemonize
        pid_t pid = fork();
        if (pid < 0)
        {
            fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
            return false;
        }
        if (pid > 0) // Parent process, pid is child process ID
        {
            CreatePidFile(GetPidFile(), pid);
            return true;
        }
        // Child process fall through to the rest of initialization

        pid_t sid = setsid();
        if (sid < 0)
            fprintf(stderr, "Error: setsid() teturned %d errno %d\n", sid, errno);
    }
#endif

        fRet = AppInit2(threadGroup);
    }
    catch (std::exception& e) {
        PrintException(&e, "AppInit()");
    } catch (...) {
        PrintException(NULL, "AppInit()");
    }

    if (!fRet)
    {
        threadGroup.interrupt_all();


    } else {
        WaitForShutdown(&threadGroup);
    }
    Shutdown();

    return fRet;
}

extern void noui_connect();
int main(int argc, char *argv[])
{
    bool fRet = false;
    fHaveGUI = false;

    // Connect daemon signal handlers
    noui_connect();

    fRet = AppInit(argc, argv);

    if (fRet && fDaemon)
        return 0;

    return (fRet ? 0 : 1);
}

