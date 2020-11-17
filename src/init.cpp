// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2019 Bean Core www.beancash.org
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcserver.h"
#include "rpcclient.h"
#include "init.h"
#include "main.h"
#include "txdb.h"
#include "walletdb.h"
#include "net.h"
#include "util.h"
#include "ui_interface.h"
#include "checkpoints.h"
#include "chainparams.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <openssl/crypto.h>

#ifndef WIN32
#include <signal.h>
#endif


using namespace std;
using namespace boost;

static const char* DEFAULT_WALLET_FILENAME="wallet.dat";

CWallet* pwalletMain;
CClientUIInterface uiInterface;

#ifdef WIN32
// Win32 LevelDB dosn't use file descriptors
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

int64_t nTimeNodeStart;
bool fConfChange;
bool fEnforceCanonical;
bool fMinimizeCoinAge;
unsigned int nNodeLifespan;
unsigned int nDerivationMethodIndex;
unsigned int nMinerSleep;
bool fUseFastIndex;
enum Checkpoints::CPMode CheckpointsMode;

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

volatile bool fRequestShutdown = false;

void StartShutdown()
{
fRequestShutdown = true;
}
bool ShutdownRequested()
{
    return fRequestShutdown;
}

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

void Shutdown()
{
    LogPrintf("%s: In progress...\n", __func__);
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown) return;

    /// Note: Shutdown() must be able to handle cases in which AppInit2() failed part of the way,
        /// for example if the data directory was found to be locked.
        /// Be sure that anything that writes files or flushes caches only does this if the respective
        /// module was initialized.
    // Make this thread recognisable as the shutdown thread
    RenameThread("BitBean-shutoff");
    nTransactionsUpdated++;
    StopRPCThreads();

    if (pwalletMain)
        bitdb.Flush(false);
    StopNode();
    {
        LOCK(cs_main);
        if (pwalletMain)
            pwalletMain->SetBestChain(CBlockLocator(pindexBest));
    }

    if (pwalletMain)
        bitdb.Flush(true);
    boost::filesystem::remove(GetPidFile());
    UnregisterAllWallets();
    delete pwalletMain;
    LogPrintf("Bean Cash exited\n\n");

#ifndef QT_GUI
    // ensure daemon is able to exit properly
    exit(0);
#endif
}

//
// Signal handlers are very limited in what they are allowed to do
//
void DetectShutdownThread(boost::thread_group* threadGroup)
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

void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}

void HandleSIGHUP(int)
{
    fReopenDebugLog = true;
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
#if !defined(QT_GUI)
bool AppInit(int argc, char* argv[])
{
    boost::thread_group threadGroup;
    boost::thread* detectShutdownThread = NULL;
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

        if (mapArgs.count("-?") || mapArgs.count("-help") || mapArgs.count("-version") || mapArgs.count("-h") || mapArgs.count("-v"))
        {
            std::string strUsage = _("Beancash version") + " " + FormatFullVersion() + "\n";
            if (mapArgs.count("-version")) || (mapArgs.count("-v"))
            {
                strUsage += LicenseInfo();
            }
            else
            {
                strUsage += "\n" + _("Usage:") + "\n" +
                  "  Beancashd [options] <command> [params]  " + _("Send command to -server or Beancashd") + "\n" +
                  "  Beancashd [options] help                " + _("List commands") + "\n" +
                  "  Beancashd [options] help <command>      " + _("Get help for a command") + "\n";

                strUsage += "\n" + HelpMessage();
            }

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

        detectShutdownThread = new boost::thread(std::bind(&DetectShutdownThread, &threadGroup));
        fRet = AppInit2(threadGroup);
    }
    catch (std::exception& e) {
        PrintException(&e, "AppInit()");
    } catch (...) {
        PrintException(NULL, "AppInit()");
    }

    if (!fRet)
    {
        if (detectShutdownThread)
            detectShutdownThread->interrupt();

        threadGroup.interrupt_all();


    }

    if (detectShutdownThread)
    {
        detectShutdownThread->join();
        delete detectShutdownThread;
        detectShudownThread = NULL;
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
#endif

bool static InitError(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR | CClientUIInterface::NOSHOWGUI);
    return false;
}

bool static InitWarning(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING | CClientUIInterface::NOSHOWGUI);
    return true;
}


bool static Bind(const CService &addr, bool fError = true) {
    if (IsLimited(addr))
        return false;
    std::string strError;
    if (!BindListenPort(addr, strError)) {
        if (fError)
            return InitError(strError);
        return false;
    }
    return true;
}

// Core-specific options shared between UI and daemon
std::string HelpMessage()
{
    string strUsage = _("Options:") + "\n";
strUsage += "  -?                     " + _("This help message") + "\n";
strUsage += "  -conf=<file>           " + _("Specify configuration file (default: Beancash.conf)") + "\n";
strUsage += "  -pid=<file>            " + _("Specify pid file (default: Beancashd.pid)") + "\n";
strUsage += "  -datadir=<dir>         " + _("Specify data directory") + "\n";
strUsage += "  -wallet=<file>         " + _("Specify wallet file (within data directory)") + " " + strprintf(_("(default: %s)"), DEFAULT_WALLET_FILENAME) + "\n";
strUsage += "  -dbcache=<n>           " + _("Set database cache size in megabytes (default: 25)") + "\n";
strUsage += "  -dblogsize=<n>         " + _("Set database disk log size in megabytes (default: 100)") + "\n";
strUsage += "  -timeout=<n>           " + _("Specify connection timeout in milliseconds (default: 5000)") + "\n";
strUsage += "  -proxy=<ip:port>       " + _("Connect through socks proxy") + "\n";
strUsage += "  -socks=<n>             " + _("Select the version of socks proxy to use (4-5, default: 5)") + "\n";
strUsage += "  -tor=<ip:port>         " + _("Use proxy to reach tor hidden services (default: same as -proxy)") + "\n";
strUsage += "  -dns                   " + _("Allow DNS lookups for -addnode, -seednode and -connect") + "\n";
strUsage += "  -port=<port>           " + _("Listen for connections on <port> (default: 22460 or testnet: 22462)") + "\n";
strUsage += "  -maxconnections=<n>    " + _("Maintain at most <n> connections to peers (default: 125)") + "\n";
strUsage += "  -addnode=<ip>          " + _("Add a node to connect to and attempt to keep the connection open") + "\n";
strUsage += "  -connect=<ip>          " + _("Connect only to the specified node(s)") + "\n";
strUsage += "  -seednode=<ip>         " + _("Connect to a node to retrieve peer addresses, and disconnect") + "\n";
strUsage += "  -externalip=<ip>       " + _("Specify your own public address") + "\n";
strUsage += "  -onlynet=<net>         " + _("Only connect to nodes in network <net> (IPv4, IPv6 or Tor)") + "\n";
strUsage += "  -discover              " + _("Discover own IP address (default: 1 when listening and no -externalip)") + "\n";
strUsage += "  -listen                " + _("Accept connections from outside (default: 1 if no -proxy or -connect)") + "\n";
strUsage += "  -bind=<addr>           " + _("Bind to given address. Use [host]:port notation for IPv6") + "\n";
strUsage += "  -dnsseed               " + _("Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect)") + "\n";
strUsage += "  -forcednsseed          " + _("Always query for peer addresses via DNS lookup (default: 0)") + "\n";
strUsage += "  -sprouting                " + _("Sprout your beans to support the network and grow new beans! (default: 1)") + "\n";
strUsage += "  -synctime              " + _("Sync time with other nodes. Disable if time on your system is precise e.g. syncing with NTP (default: 1)") + "\n";
strUsage += "  -cppolicy              " + _("Sync checkpoints policy (default: strict)") + "\n";
strUsage += "  -banscore=<n>          " + _("Threshold for disconnecting misbehaving peers (default: 100)") + "\n";
strUsage += "  -bantime=<n>           " + _("Number of seconds to keep misbehaving peers from reconnecting (default: 86400)") + "\n";
strUsage += "  -maxreceivebuffer=<n>  " + _("Maximum per-connection receive buffer, <n>*1000 bytes (default: 5000)") + "\n";
strUsage += "  -maxsendbuffer=<n>     " + _("Maximum per-connection send buffer, <n>*1000 bytes (default: 1000)") + "\n";
strUsage += "  -detachdb              " + _("Detach block and address databases. Increases shutdown time (default: 1)") + "\n";
strUsage += "  -paytxfee=<amt>        " + _("Fee per KB to add to transactions you send") + "\n";
strUsage += "  -mininput=<amt>        " + _("When creating transactions, ignore inputs with value less than this (default: 0.01)") + "\n";
if (fHaveGUI)
    strUsage += "  -server                " + _("Accept command line and JSON-RPC commands") + "\n";
#if !defined(WIN32)
   if (fHaveGUI)
    strUsage += "  -daemon                " + _("Run in the background as a daemon and accept commands") + "\n";
#endif
strUsage += "  -testnet               " + _("Use the test network") + "\n";
strUsage += "  -debug                 " + _("Output extra debugging information. Implies all other -debug* options") + "\n";
strUsage += "  -debugnet              " + _("Output extra network debugging information") + "\n";
strUsage += "  -logtimestamps         " + _("Prepend debug output with timestamp") + "\n";
strUsage += "  -shrinkdebugfile       " + _("Shrink debug.log file on client startup (default: 1 when no -debug)") + "\n";
strUsage += "  -printtoconsole        " + _("Send trace/debug info to console instead of debug.log file") + "\n";
strUsage += "  -regtest               " + _("Enter regression test mode, which uses a special chain in which blocks can be ") + "\n";
strUsage +=                               _("solved instantly. This is intended for regression testing tools and application development.") + "\n";
#ifdef WIN32
    strUsage += "  -printtodebugger       " + _("Send trace/debug info to debugger") + "\n";
#endif
strUsage += "  -rpcuser=<user>        " + _("Username for JSON-RPC connections") + "\n";
strUsage += "  -rpcpassword=<pw>      " + _("Password for JSON-RPC connections") + "\n";
strUsage += "  -rpcport=<port>        " + _("Listen for JSON-RPC connections on <port> (default: 22461 or testnet: 22463)") + "\n";
strUsage += "  -rpcallowip=<ip>       " + _("Allow JSON-RPC connections from specified IP address") + "\n";
if (!fHaveGUI)
    strUsage += "  -rpcconnect=<ip>       " + _("Send commands to node running on <ip> (default: 127.0.0.1)") + "\n";
strUsage += "  -rpcwait               " + _("Wait for RPC server to start") + "\n";
strUsage += "  -rpcthreads=<n>        " + _("Use this to set the number of threads to service RPC calls (defult: 4)") + "\n";
strUsage += "  -blocknotify=<cmd>     " + _("Execute command when the best block changes (%s in cmd is replaced by block hash)") + "\n";
strUsage += "  -walletnotify=<cmd>    " + _("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)") + "\n";
strUsage += "  -zapwallet=<mode>      " + _("Delete all wallet transactions and rebuild transactions from blockchain data") + "\n";
strUsage +=                               _("(1 = keep tx meta data (e.g. account owner & payment request info), 2 = drop tx meta data)") + "\n";
strUsage += "  -confchange            " + _("Require a confirmations for change (default: 0)") + "\n";
strUsage += "  -enforcecanonical      " + _("Enforce transaction scripts to use canonical PUSH operators (default: 1)") + "\n";
strUsage += "  -minimizebeanage       " + _("Minimize weight consumption (experimental) (default: 0)") + "\n";
strUsage += "  -alertnotify=<cmd>     " + _("Execute command when a relevant alert is received (%s in cmd is replaced by message)") + "\n";
strUsage += "  -upgradewallet         " + _("Upgrade wallet to latest format") + "\n";
strUsage += "  -keypool=<n>           " + _("Set key pool size to <n> (default: 100)") + "\n";
strUsage += "  -rescan                " + _("Rescan the block chain for missing wallet transactions") + "\n";
strUsage += "  -salvagewallet         " + _("Attempt to recover private keys from a corrupt wallet file") + "\n";
strUsage += "  -checkblocks=<n>       " + _("How many blocks to check at startup (default: 600, 0 = all)") + "\n";
strUsage += "  -checklevel=<n>        " + _("How thorough the block verification is (0-6, default: 1)") + "\n";
strUsage += "  -loadblock=<file>      " + _("Imports blocks from external blk000?.dat file") + "\n";
strUsage += "  -blockminsize=<n>      "   + _("Set minimum block size in bytes (default: 0)") + "\n";
strUsage += "  -blockmaxsize=<n>      "   + _("Set maximum block size in bytes (default: 250000)") + "\n";
strUsage += "  -blockprioritysize=<n> "   + _("Set maximum size of high-priority/low-fee transactions in bytes (default: 27000)") + "\n";
strUsage += "  -rpcssl                                  " + _("Use OpenSSL (https) for JSON-RPC connections") + "\n";
strUsage += "  -rpcsslcertificatechainfile=<file.cert>  " + _("Server certificate file (default: server.cert)") + "\n";
strUsage += "  -rpcsslprivatekeyfile=<file.pem>         " + _("Server private key (default: server.pem)") + "\n";
strUsage += "  -rpcsslciphers=<ciphers>                 " + _("Acceptable ciphers (default: TLSv1+HIGH:!SSLv2:!aNULL:!eNULL:!AH:!3DES:@STRENGTH)") + "\n";
        
    return strUsage;
}

std::string LicenseInfo()
{
    return FormatParagraph(strprintf(_("Copyright (C) 2009-%i The Bitcoin Core Developers"), COPYRIGHT_YEAR)) + "\n" +
            "\n" +
			  FormatParagraph(strprintf(_("Copyright (C) 2012-2014 The Novacoin Developers"))) + "\n" +
            "\n" +           
           FormatParagraph(strprintf(_("Copyright (C) 2015-%i Bean Core www.beancash.org"), COPYRIGHT_YEAR)) + "\n" +
            "\n" +
           FormatParagraph(_("Distributed under the MIT/X11 software license, see accompanying file COPYING or <http://www.opensource.org/licenses/mit-license.php>.")) + "\n" +
            "\n" +
           FormatParagraph(_("This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit <https://www.openssl.org/> and cryptographic software written by Eric Young.")) +
            "\n";
}

bool LoadExternalBlockFile(FILE* fileIn)
{
    int64_t nStart = GetTimeMillis();
    int nLoaded = 0;
    {
        LOCK(cs_main);
        try {
            CAutoFile blkdat(fileIn, SER_DISK, CLIENT_VERSION);
            unsigned int nPos = 0;
            while (nPos != std::numeric_limits<uint32_t>::max() && blkdat.good())
                {
                boost::this_thread::interruption_point();
                unsigned char pchData[65536];
                do {
                       fseek(blkdat, nPos, SEEK_SET);
                       int nRead = fread(pchData, 1, sizeof(pchData), blkdat);
                       if (nRead <= 8)
                           {
                               nPos = std::numeric_limits<uint32_t>::max();
                               break;
                           }
                       void* nFind = memchr(pchData, Params().MessageStart()[0], nRead+1-MESSAGE_START_SIZE);
                       if (nFind)
                       {
                        if (memcmp(nFind, Params().MessageStart(), MESSAGE_START_SIZE)==0)
                        {
                            nPos += ((unsigned char*)nFind - pchData) + MESSAGE_START_SIZE;
                            break;
                        }
                        nPos += ((unsigned char*)nFind - pchData) + 1;
                       }
                    else
                        nPos += sizeof(pchData) - MESSAGE_START_SIZE + 1;
                    boost::this_thread::interruption_point();
                    } while(true);
            if (nPos == std::numeric_limits<uint32_t>::max())
            break;
            fseek(blkdat, nPos, SEEK_SET);
            unsigned int nSize;
            blkdat >> nSize;
            if (nSize > 0 && nSize <= MAX_BLOCK_SIZE)
                    {
                    CBlock block;
                    blkdat >> block;
                    LOCK(cs_main);
                    if (ProcessBlock(NULL,&block))
                        {
                            nLoaded++;
                            nPos += 4 + nSize;
                        }
                    }
                }
        }
                    catch (std::exception &e) {
                        LogPrintf("%s() : Deserialize or I/O error caught during load\n",
                               __PRETTY_FUNCTION__);
                    }
    }
                LogPrintf("Loaded %i blocks from external file in %" PRId64 "ms\n", nLoaded, GetTimeMillis() - nStart);
                return nLoaded > 0;
}

struct CImportingNow
{
    CImportingNow()
    {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow()
    {
        assert(fImporting == true);
        fImporting = false;
    }

};


static void ThreadImport(std::vector<boost::filesystem::path> vImportFiles)
{
    RenameThread("beancash-loadblk");

    // hardcoded $DATADIR/bootstrap.dat
    filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (filesystem::exists(pathBootstrap))
    {
          FILE *file = fopen(pathBootstrap.string().c_str(), "rb");
          if (file)
          {
            CImportingNow imp;
            filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LogPrintf("Importing blockchain data now...\n");
            LoadExternalBlockFile(file);
            RenameOver(pathBootstrap, pathBootstrapOld);
          }
    }

    // -loadblock=
    for (boost::filesystem::path &path : vImportFiles)
    {
    FILE *file = fopen(path.string().c_str(), "rb");
    if (file)
        {
            CImportingNow imp;
            LogPrintf("Importing %s...\n", path.string().c_str());
            LoadExternalBlockFile(file);
        }
    }
}


/** Initialize Bean cash.
 *  @pre Parameters should be parsed and config file should be read.
 */
bool AppInit2(boost::thread_group& threadGroup)
{
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable Data Execution Prevention (DEP)
    // Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
    // A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
// We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
// which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL (WINAPI *PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol != NULL) setProcDEPPol(PROCESS_DEP_ENABLE);
#endif
#ifndef WIN32
    umask(077);

    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Reopen debug.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, NULL);
#endif


    // ********************************************************* Step 2: parameter interactions

    nNodeLifespan = GetArg("-addrlifespan", 7);
    fUseFastIndex = GetBoolArg("-fastindex", true);
    nMinerSleep = GetArg("-minersleep", 500);

    CheckpointsMode = Checkpoints::STRICT;
    std::string strCpMode = GetArg("-cppolicy", "strict");

    if(strCpMode == "strict")
        CheckpointsMode = Checkpoints::STRICT;

    if(strCpMode == "advisory")
        CheckpointsMode = Checkpoints::ADVISORY;

    if(strCpMode == "permissive")
        CheckpointsMode = Checkpoints::PERMISSIVE;

    nDerivationMethodIndex = 0;

    if (!SelectParamsFromCommandLine()) {
        return InitError("Invalid combination of -testnet and regtest.");
    }

    if (mapArgs.count("-bind")) {
        // when specifying an explicit binding address, you want to listen on it
        // even when -connect or -proxy is specified
        SoftSetBoolArg("-listen", true);
    }

    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        SoftSetBoolArg("-dnsseed", true);
        SoftSetBoolArg("-listen", false);
    }

    if (mapArgs.count("-proxy")) {
        // to protect privacy, do not listen by default if a proxy server is specified
        SoftSetBoolArg("-listen", false);
    }

    if (!GetBoolArg("-listen", true)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        SoftSetBoolArg("-discover", false);
    }

    if (mapArgs.count("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        SoftSetBoolArg("-discover", false);
    }

    if (GetBoolArg("-salvagewallet")) {
        // Rewrite just private keys: rescan to find transactions
        SoftSetBoolArg("-rescan", true);
    }

	 if (GetBoolArg("-zapwallet", false)) {
		if (SoftSetBoolArg("-rescan", true))
            LogPrintf("init: parameter interaction: -zapwallet=<mode> -> setting -rescan=1\n");
	 }

    // Make sure there are enough file descriptors
    int nBind = std::max((int)mapArgs.count("-bind"), 1);
    nMaxConnections = GetArg("-maxconnections", 200);
    nMaxConnections = std::max(std::min(nMaxConnections, FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS), 0);
    int nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available."));
    if (nFD - MIN_CORE_FILEDESCRIPTORS < nMaxConnections)
        nMaxConnections = nFD - MIN_CORE_FILEDESCRIPTORS;

    // ********************************************************* Step 3: parameter-to-internal-flags

    if (mapMultiArgs.count("-debug")) fDebug = true;

    // -debug implies fDebug*
    if (fDebug)
        fDebugNet = true;
    else
        fDebugNet = GetBoolArg("-debugnet");

    bitdb.SetDetach(GetBoolArg("-detachdb", true));

    if (fDaemon)
    {
        fServer = true;
    }
    else
        fServer = GetBoolArg("-server");

    /* force fServer when running without GUI */
if (!fHaveGUI)
    fServer = true;
    fPrintToConsole = GetBoolArg("-printtoconsole");
    fPrintToDebugger = GetBoolArg("-printtodebugger");
    fLogTimestamps = GetBoolArg("-logtimestamps");

    if (mapArgs.count("-timeout"))
    {
        int nNewTimeout = GetArg("-timeout", 5000);
        if (nNewTimeout > 0 && nNewTimeout < 600000)
            nConnectTimeout = nNewTimeout;
    }

    if (mapArgs.count("-paytxfee"))
    {
        if (!ParseMoney(mapArgs["-paytxfee"], nTransactionFee))
            return InitError(strprintf(_("Invalid amount for -paytxfee=<amount>: '%s'"), mapArgs["-paytxfee"].c_str()));
        if (nTransactionFee > 0.25 * bean)
            InitWarning(_("Warning: -paytxfee is set very high! This is the transaction fee you will pay if you send a transaction."));
    }

    fConfChange = GetBoolArg("-confchange", false);
    fEnforceCanonical = GetBoolArg("-enforcecanonical", true);
    fMinimizeCoinAge = GetBoolArg("-minimizebeanage", false);

    if (mapArgs.count("-mininput"))
    {
        if (!ParseMoney(mapArgs["-mininput"], nMinimumInputValue))
            return InitError(strprintf(_("Invalid amount for -mininput=<amount>: '%s'"), mapArgs["-mininput"].c_str()));
    }

    // ********************************************************* Step 4: application initialization: dir lock, daemonize, pidfile, debug log

    std::string strDataDir = GetDataDir().string();
    std::string strWalletFileName = GetArg("-wallet", DEFAULT_WALLET_FILENAME);

    // strWalletFileName must be a plain filename without a directory
    if (strWalletFileName != boost::filesystem::basename(strWalletFileName) + boost::filesystem::extension(strWalletFileName))
        return InitError(strprintf(_("Wallet %s resides outside data directory %s."), strWalletFileName.c_str(), strDataDir.c_str()));

    // Make sure only a single Bitbean process is using the data directory.
    boost::filesystem::path pathLockFile = GetDataDir() / ".lock";
    FILE* file = fopen(pathLockFile.string().c_str(), "a"); // empty lock file; created if it doesn't exist.
    if (file) fclose(file);
    static boost::interprocess::file_lock lock(pathLockFile.string().c_str());
    if (!lock.try_lock())
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s. Bean Cash is probably already running."), strDataDir.c_str()));
    if (GetBoolArg("-shrinkdebugfile", !fDebug))
        ShrinkDebugFile();
    LogPrintf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    LogPrintf("Beancash version %s (%s)\n", FormatFullVersion().c_str(), CLIENT_DATE.c_str());
    LogPrintf("Using OpenSSL version %s\n", SSLeay_version(SSLEAY_VERSION));

    nTimeNodeStart = GetTime();
    if (!fLogTimestamps)
        LogPrintf("Startup time: %s\n", DateTimeStrFormat("%x %H:%M:%S", nTimeNodeStart).c_str());
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string().c_str());
    LogPrintf("Used data directory %s\n", strDataDir.c_str());
    LogPrintf("Using at most %i connections (%i file descriptors available)\n", nMaxConnections, nFD);
    std::ostringstream strErrors;

    if (fDaemon)
        fprintf(stdout, "Beancash server starting\n");

    int64_t nStart;

    // ********************************************************* Step 5: verify database integrity

    uiInterface.InitMessage(_("Verifying database integrity..."));

    if (!bitdb.Open(GetDataDir()))
    {
        string msg = strprintf(_("Error initializing database environment %s!"
                                 " To recover, BACKUP THAT DIRECTORY, then remove"
                                 " everything from it except for the wallet file (%s)."), strDataDir.c_str(), strWalletFileName.c_str());
        return InitError(msg);
    }

    if (GetBoolArg("-salvagewallet"))
    {
        // Recover readable keypairs:
        if (!CWalletDB::Recover(bitdb, strWalletFileName, true))
            return false;
    }

    if (filesystem::exists(GetDataDir() / strWalletFileName))
    {
        CDBEnv::VerifyResult r = bitdb.Verify(strWalletFileName, CWalletDB::Recover);
        if (r == CDBEnv::RECOVER_OK)
        {
            string msg = strprintf(_("Warning: %s corrupt, data salvaged!"
                                     " Original %s saved as wallet.{timestamp}.bak in %s; if"
                                     " your balance or transactions are incorrect you should"
                                     " restore from a backup."), strWalletFileName.c_str(), strWalletFileName.c_str(), strDataDir.c_str());
            InitWarning(msg);
        }
        if (r == CDBEnv::RECOVER_FAIL)
            return InitError(strprintf(_("Wallet file (%s) corrupt, salvage failed\n"), strWalletFileName.c_str()));
    }

    // ********************************************************* Step 6: network initialization
    RegisterNodeSignals(GetNodeSignals());

    int nSocksVersion = GetArg("-socks", 5);

    if (nSocksVersion != 4 && nSocksVersion != 5)
        return InitError(strprintf(_("Unknown -socks proxy version requested: %i"), nSocksVersion));

    if (mapArgs.count("-onlynet")) {
        std::set<enum Network> nets;
        for (std::string snet : mapMultiArgs["-onlynet"]) {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet.c_str()));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network)n;
            if (!nets.count(net))
                SetLimited(net);
        }
    }

    CService addrProxy;
    bool fProxy = false;
    if (mapArgs.count("-proxy")) {
        addrProxy = CService(mapArgs["-proxy"], 9050);
        if (!addrProxy.IsValid())
            return InitError(strprintf(_("Invalid -proxy address: '%s'"), mapArgs["-proxy"].c_str()));

        if (!IsLimited(NET_IPV4))
            SetProxy(NET_IPV4, addrProxy, nSocksVersion);
        if (nSocksVersion > 4) {
            if (!IsLimited(NET_IPV6))
                SetProxy(NET_IPV6, addrProxy, nSocksVersion);
            SetNameProxy(addrProxy, nSocksVersion);
        }
        fProxy = true;
    }

    // -tor can override normal proxy, -notor disables tor entirely
    if (!(mapArgs.count("-tor") && mapArgs["-tor"] == "0") && (fProxy || mapArgs.count("-tor"))) {
        CService addrOnion;
        if (!mapArgs.count("-tor"))
            addrOnion = addrProxy;
        else
            addrOnion = CService(mapArgs["-tor"], 9050);
        if (!addrOnion.IsValid())
            return InitError(strprintf(_("Invalid -tor address: '%s'"), mapArgs["-tor"].c_str()));
        SetProxy(NET_TOR, addrOnion, 5);
        SetReachable(NET_TOR);
    }

    // see Step 2: parameter interactions for more information about these
    fNoListen = !GetBoolArg("-listen", true);
    fDiscover = GetBoolArg("-discover", true);
    fNameLookup = GetBoolArg("-dns", true);

    bool fBound = false;
    if (!fNoListen)
    {
        std::string strError;
        if (mapArgs.count("-bind")) {
            for (std::string strBind : mapMultiArgs["-bind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
                    return InitError(strprintf(_("Cannot resolve -bind address: '%s'"), strBind.c_str()));
                fBound |= Bind(addrBind);
            }
        } else {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
            if (!IsLimited(NET_IPV6))
                fBound |= Bind(CService(in6addr_any, GetListenPort()), false);
            if (!IsLimited(NET_IPV4))
                fBound |= Bind(CService(inaddr_any, GetListenPort()), !fBound);
        }
        if (!fBound)
            return InitError(_("Failed to listen on any port. Use -listen=0 if you want this."));
    }

    if (mapArgs.count("-externalip"))
    {
        for (string strAddr : mapMultiArgs["-externalip"]) {
            CService addrLocal(strAddr, GetListenPort(), fNameLookup);
            if (!addrLocal.IsValid())
                return InitError(strprintf(_("Cannot resolve -externalip address: '%s'"), strAddr.c_str()));
            AddLocal(CService(strAddr, GetListenPort(), fNameLookup), LOCAL_MANUAL);
        }
    }

    if (mapArgs.count("-reservebalance")) // ppbean: reserve balance amount
    {
        if (!ParseMoney(mapArgs["-reservebalance"], nReserveBalance))
        {
            InitError(_("Invalid amount for -reservebalance=<amount>"));
            return false;
        }
    }

    if (mapArgs.count("-checkpointkey")) // ppbean: checkpoint master priv key
    {
        if (!Checkpoints::SetCheckpointPrivKey(GetArg("-checkpointkey", "")))
            InitError(_("Unable to sign checkpoint, wrong checkpointkey?\n"));
    }

    for (string strDest : mapMultiArgs["-seednode"])
        AddOneShot(strDest);

    // ********************************************************* Step 7: load blockchain 

    if (!bitdb.Open(GetDataDir()))
    {
        string msg = strprintf(_("Error initializing database environment %s!"
                                 " To recover, BACKUP THAT DIRECTORY, then remove"
                                 " everything from it except for %s."), strDataDir.c_str(), strWalletFileName.c_str());
        return InitError(msg);
    }

    if (GetBoolArg("-loadblockindextest"))
    {
        CTxDB txdb("r");
        txdb.LoadBlockIndex();
        PrintBlockTree();
        return false;
    }

    uiInterface.InitMessage(_("Loading block index..."));
    nStart = GetTimeMillis();
    if (!LoadBlockIndex())
        return InitError(_("Error loading blkindex.dat"));
    
    // If the loaded chain has the wrong genesis, bail out immediately
    // It is likely using a Testnet data directory or visversa.
    if (!mapBlockIndex.empty() && pindexGenesisBlock == NULL)
        return InitError(_("Incorrect or no genesis block found. Perhaps the wrong datadir is specified for the network?"));


    // as LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill Beancash-qt during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (fRequestShutdown)
    {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }
    LogPrintf(" block index %15" PRId64 "ms\n", GetTimeMillis() - nStart);

    if (GetBoolArg("-printblockindex") || GetBoolArg("-printblocktree"))
    {
        PrintBlockTree();
        return false;
    }

    if (mapArgs.count("-printblock"))
    {
        string strMatch = mapArgs["-printblock"];
        int nFound = 0;
        for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
        {
            uint256 hash = (*mi).first;
            if (strncmp(hash.ToString().c_str(), strMatch.c_str(), strMatch.size()) == 0)
            {
                CBlockIndex* pindex = (*mi).second;
                CBlock block;
                block.ReadFromDisk(pindex);
                block.BuildMerkleTree();
                block.print();
                LogPrintf("\n");
                nFound++;
            }
        }
        if (nFound == 0)
            LogPrintf("No blocks matching %s were found\n", strMatch.c_str());
        return false;
    }

    // ********************************************************* Step 8: load wallet

	 // Needed to restore wallet transaction meta data after a -zapwallet 
	 std::vector<CWalletTx> vWtx;
	 
	 if (GetBoolArg("-zapwallet", false)) {
	 		uiInterface.InitMessage(_("Removing all transactions from wallet..."));
	 		pwalletMain = new CWallet(strWalletFileName);
	 		DBErrors nZapWalletRet = pwalletMain->ZapWalletTx(vWtx);
	 		if (nZapWalletRet != DB_LOAD_OK) {
	 			uiInterface.InitMessage(_("Error loading wallet.dat: Wallet corrupted"));
	 			return false;
	 		}
	 		delete pwalletMain;
	 		pwalletMain = NULL;	 
	 }	 
	 
    uiInterface.InitMessage(_("Loading wallet..."));
    nStart = GetTimeMillis();
    bool fFirstRun = true;
    pwalletMain = new CWallet(strWalletFileName);
    DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
    if (nLoadWalletRet != DB_LOAD_OK)
    {
        if (nLoadWalletRet == DB_CORRUPT)
        {
            string msg = (_("Error loading wallet file (%s): Wallet corrupted\n"), strWalletFileName.c_str());

            InitWarning(msg);
            fprintf(stderr, "%s", msg.c_str());
        }
        else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
        {
            string msg = (_("Warning: error reading wallet file! All keys read correctly, but transaction data"
                         " or address book entries might be missing or incorrect.\n"));
            InitWarning(msg);
            fprintf(stderr, "%s", msg.c_str());
        }
        else if (nLoadWalletRet == DB_TOO_NEW)
        {
            string msg = (_("Error loading wallet file (%s): Wallet requires newer version of Bean Cash\n"), strWalletFileName.c_str());
            InitWarning(msg);
            fprintf(stderr, "%s", msg.c_str());
        }
        else if (nLoadWalletRet == DB_NEED_REWRITE)
        {
            string msg = (_("Wallet (%s) needed to be rewritten: restart Bean Cash to complete\n"), strWalletFileName.c_str());
            InitWarning(msg);
            fprintf(stderr, "%s", msg.c_str());
            return InitError(msg);
        }
        else
        {
            string msg = (_("Error loading wallet file (%s)\n"), strWalletFileName.c_str());
            InitWarning(msg);
            fprintf(stderr, "%s", msg.c_str());
        }
    }

    if (GetBoolArg("-upgradewallet", fFirstRun))
    {
        int nMaxVersion = GetArg("-upgradewallet", 0);
        if (nMaxVersion == 0) // the -upgradewallet without argument case
        {
            LogPrintf("Performing wallet upgrade to %i\n", FEATURE_LATEST);
            nMaxVersion = CLIENT_VERSION;
            pwalletMain->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
        }
        else
            LogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
        if (nMaxVersion < pwalletMain->GetVersion())
            strErrors << _("Cannot downgrade wallet") << "\n";
        pwalletMain->SetMaxVersion(nMaxVersion);
    }

    if (fFirstRun)
    {
        // Create new keyUser and set as default key
        RandAddSeedPerfmon();

        CPubKey newDefaultKey;
        if (pwalletMain->GetKeyFromPool(newDefaultKey, false)) {
            pwalletMain->SetDefaultKey(newDefaultKey);
            if (!pwalletMain->SetAddressBookName(pwalletMain->vchDefaultKey.GetID(), ""))
                strErrors << _("Cannot write default address") << "\n";
        }
    }

    LogPrintf("%s", strErrors.str().c_str());
    LogPrintf(" wallet      %15" PRId64 "ms\n", GetTimeMillis() - nStart);

    RegisterWallet(pwalletMain);

    CBlockIndex *pindexRescan = pindexBest;
    if (GetBoolArg("-rescan"))
        pindexRescan = pindexGenesisBlock;
    else
    {
        CWalletDB walletdb(strWalletFileName);
        CBlockLocator locator;
        if (walletdb.ReadBestBlock(locator))
            pindexRescan = locator.GetBlockIndex();
    }
    if (pindexBest != pindexRescan && pindexBest && pindexRescan && pindexBest->nHeight > pindexRescan->nHeight)
    {
        uiInterface.InitMessage(_("Rescanning..."));
        LogPrintf("Rescanning last %i blocks (from block %i)...\n", pindexBest->nHeight - pindexRescan->nHeight, pindexRescan->nHeight);
        nStart = GetTimeMillis();
        pwalletMain->ScanForWalletTransactions(pindexRescan, true);
        LogPrintf(" rescan      %15" PRId64 "ms\n", GetTimeMillis() - nStart);
    
	 	// Restore wallet transaction metadata after -zapwallet=1
	 	if (GetBoolArg("-zapwallet", false) && GetArg("-zapwallet", "1") != "2")
	 	{
				CWalletDB walletdb(strWalletFileName);
				for (const CWalletTx& wtxOld : vWtx)
				{
					uint256 hash = wtxOld.GetHash();
					std::map<uint256, CWalletTx>::iterator mi = pwalletMain->mapWallet.find(hash);
					if (mi != pwalletMain->mapWallet.end())
					{
						const CWalletTx* copyFrom = &wtxOld;
						CWalletTx* copyTo = &mi->second;
						copyTo->mapValue = copyFrom->mapValue;
						copyTo->vOrderForm = copyFrom->vOrderForm;
						copyTo->nTimeReceived = copyFrom->nTimeReceived;
						copyTo->nTimeSmart = copyFrom->nTimeSmart;
						copyTo->fFromMe = copyFrom->fFromMe;
                        copyTo->strFromAccount = copyFrom->strFromAccount;
						copyTo->nOrderPos = copyFrom->nOrderPos;
						copyTo->WriteToDisk(&walletdb);				
					}			
				}	 
	 	}    
    }

    // ********************************************************* Step 9: import blocks

    std::vector<boost::filesystem::path> vPath;
    if (mapArgs.count("-loadblock"))
    {
        for (string strFile : mapMultiArgs["-loadblock"])
            vPath.push_back(strFile);
    }

    threadGroup.create_thread(std::bind(&ThreadImport, vPath));

    // ********************************************************* Step 10: load peers

    uiInterface.InitMessage(_("Loading addresses..."));
    nStart = GetTimeMillis();

    {
        CAddrDB adb;
        if (!adb.Read(addrman))
            LogPrintf("Invalid or missing peers.dat; recreating\n");
    }

    LogPrintf("Loaded %i addresses from peers.dat  %" PRId64 "ms\n",
           addrman.size(), GetTimeMillis() - nStart);

    // ********************************************************* Step 11: start node

    if (!CheckDiskSpace())
        return false;

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

    RandAddSeedPerfmon();

    //// debug print
    LogPrintf("mapBlockIndex.size() = %u\n",   mapBlockIndex.size());
    LogPrintf("nBestHeight = %d\n",            nBestHeight);
    LogPrintf("setKeyPool.size() = %u\n",      pwalletMain->setKeyPool.size());
    LogPrintf("mapWallet.size() = %u\n",       pwalletMain->mapWallet.size());
    LogPrintf("mapAddressBook.size() = %u\n",  pwalletMain->mapAddressBook.size());

    StartNode(threadGroup);

    if (fServer)
        StartRPCThreads();

    // Mine Proof-of-Bean blocks in the background
    if (!GetBoolArg("-sprouting", true))
        LogPrintf("Sprouting is disabled\n");
    else
        threadGroup.create_thread(std::bind(&ThreadStakeMiner, pwalletMain));

    // ********************************************************* Step 12: finished

    uiInterface.InitMessage(_("Done loading"));

     // Add wallet transactions that aren't already in a block to mapTransactions
    pwalletMain->ReacceptWalletTransactions();

    // Run a thread to flush wallet periodically
    threadGroup.create_thread(std::bind(&ThreadFlushWalletDB, std::ref(pwalletMain->strWalletFile)));

    return !fRequestShutdown;
}
