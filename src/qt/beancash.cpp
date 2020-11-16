/*
 * W.J. van der Laan 2011-2012
 */
#include "bitbeangui.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "guiutil.h"
#include "guiconstants.h"

#include "init.h"
#include "util.h"
#include "ui_interface.h"
#include "utilitydialog.h"
// #ifdef ENABLE_WALLET
#include "paymentserver.h"
#include "intro.h"

#include <QApplication>
#include <QMessageBox>
#if QT_VERSION < 0x050000
#include <QTextCodec>
#endif
#include <QLocale>
#include <QTranslator>
#include <QVBoxLayout>
#include <QLabel>
#include <QSplashScreen>
#include <QLibraryInfo>
#include <QTimer>
#include <QSettings>

#if defined(BEANCASH_NEED_QT_PLUGINS) && !defined(_BEANCASH_QT_PLUGINS_INCLUDED)
#define _BEANCASH_QT_PLUGINS_INCLUDED
#define __INSURE__
#include <QtPlugin>
Q_IMPORT_PLUGIN(qcncodecs)
Q_IMPORT_PLUGIN(qjpcodecs)
Q_IMPORT_PLUGIN(qtwcodecs)
Q_IMPORT_PLUGIN(qkrcodecs)
Q_IMPORT_PLUGIN(qtaccessiblewidgets)
#endif

// Declare meta types used for QMetaObject::invokeMethod
Q_DECLARE_METATYPE(bool*)

// Need a global reference for the notifications to find the GUI
static BitbeanGUI *guiref;
static QSplashScreen *splashref;

static void ThreadSafeMessageBox(const std::string& message, const std::string& caption, unsigned int style)
{
    // Message from network thread
    if(guiref)
    {
        bool modal = (style & CClientUIInterface::MODAL);
        // In case of modal message, use blocking connection to wait for user to click a button
        QMetaObject::invokeMethod(guiref, "message",
                                   modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                                   Q_ARG(QString, QString::fromStdString(caption)),
                                   Q_ARG(QString, QString::fromStdString(message)),
                                   Q_ARG(bool, modal),
                                   Q_ARG(unsigned int, style));
    }
    else
    {
        LogPrintf("%s: %s\n", caption.c_str(), message.c_str());
        fprintf(stderr, "%s: %s\n", caption.c_str(), message.c_str());
    }
}

static bool ThreadSafeAskFee(int64_t nFeeRequired)
{
    if(!guiref)
        return false;
    if(nFeeRequired < MIN_TX_FEE || nFeeRequired <= nTransactionFee || fDaemon)
        return true;

    bool payFee = false;

    QMetaObject::invokeMethod(guiref, "askFee", GUIUtil::blockingGUIThreadConnection(),
                               Q_ARG(qint64, nFeeRequired),
                               Q_ARG(bool*, &payFee));

    return payFee;
}

static void InitMessage(const std::string &message)
{
    if(splashref)
    {
        splashref->showMessage(QString::fromStdString(message), Qt::AlignBottom|Qt::AlignHCenter, QColor(232,186,63));
        QApplication::instance()->processEvents();
    }
    LogPrintf("init message: %s\n", message.c_str());
}

/*
   Translate string to current locale using Qt.
 */
static std::string Translate(const char* psz)
{
    return QCoreApplication::translate("beancash-core", psz).toStdString();
}

/* Handle runaway exceptions. Shows a message box with the problem and quits the program.
 */
static void handleRunawayException(std::exception *e)
{
    PrintExceptionContinue(e, "Runaway exception");
    QMessageBox::critical(0, "Runaway exception", BitbeanGUI::tr("A fatal error occurred. Bean Cash Core can no longer continue safely and will quit.") + QString("\n\n") + QString::fromStdString(strMiscWarning));
    exit(1);
}

/** Set up translations */
static void initTranslations(QTranslator &qtTranslatorBase, QTranslator &qtTranslator, QTranslator &translatorBase, QTranslator &translator)
{
    QSettings settings;

    // Get desired locale (e.g. "de_DE")
    // 1) System default language
    QString lang_territory = QLocale::system().name();
    // 2) Language from QSettings
    QString lang_territory_qsettings = settings.value("language", "").toString();
    if(!lang_territory_qsettings.isEmpty())
        lang_territory = lang_territory_qsettings;
    // 3) -lang command line argument
    lang_territory = QString::fromStdString(GetArg("-lang", lang_territory.toStdString()));

    // Convert to "de" only by truncating "_DE"
    QString lang = lang_territory;
    lang.truncate(lang_territory.lastIndexOf('_'));

    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator

    // Load e.g. qt_de.qm
    if (qtTranslatorBase.load("qt_" + lang, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslatorBase);

    // Load e.g. qt_de_DE.qm
    if (qtTranslator.load("qt_" + lang_territory, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);

    // Load e.g. beancash_de.qm (shortcut "de" needs to be defined in beancash.qrc)
    if (translatorBase.load(lang, ":/translations/"))
        QApplication::installTranslator(&translatorBase);

    // Load e.g. beancash_de_DE.qm (shortcut "de_DE" needs to be defined in beancash.qrc)
    if (translator.load(lang_territory, ":/translations/"))
        QApplication::installTranslator(&translator);
}

/* qDebug() message handler --> debug.log */
#if QT_VERSION < 0x050000
void DebugMessageHandler(QtMsgType type, const char * msg)
{
    LogPrint("Beancash-qt: %s\n", msg);
}
#else
void DebugMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString &msg)
{
    LogPrint("Beancash-qt: %s\n", qPrintable(msg));
}
#endif

#ifndef BEANCASH_QT_TEST
int main(int argc, char *argv[])
{
    bool fMissingDatadir = false;
    bool fSelParFromCLFailed = false;
    fHaveGUI = true;

    /// 1. Parse command-line options.
    // Command-line options take precedence:
    ParseParameters(argc, argv);

    // ... then Beancash.conf
    if (!boost::filesystem::is_directory(GetDataDir(false)))
    {
        fMissingDatadir = true;
    } else {
        ReadConfigFile(mapArgs, mapMultiArgs);
    }

    // Check for -testnet or -regtest parameter
    // TestNet() calls are only valid after this clause.
    if (!SelectParamsFromCommandLine())
    {
        fSelParFromCLFailed = true;
        fprintf(stderr, "Error: Invalid commandline combination.\n");
        return false;
    }

    // Parse URIs on command line -- this can affect TestNet() & RegTest() mode
    if (!PaymentServer::ipcParseCommandLine(argc, argv))
        exit(0);

    // Do not refer to data directory yet, this can be overridden by Intro::pickDataDirectory

    /// 2. Basic Qt initialization (not dependent on parameters or configuration)
#if QT_VERSION < 0x050000
    // Internal string conversion is all UTF-8
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForTr());
#endif

    // Doesn't need to be defined
    // Q_INIT_RESOURCE(beancash);

    QApplication app(argc, argv);
#if QT_VERSION > 0x050100
    // Generate high-dpi pixmaps
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#ifdef Q_OS_MAC
    app.setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    // Register meta types used for QMetaobject::invokeMethod
    qRegisterMetaType< bool* >();

    /// 3. Application identification
    // must be set before OptionsModel is initialized or translations loaded, as it is used to locate QSettings
    app.setWindowIcon(QIcon("./icons/icon.png"));
    app.setOrganizationName("Bean Core");
    app.setOrganizationDomain("beancash.org");
    if (TestNet()) // Separate UI settings for testnet
        app.setApplicationName("Beancash-qt-testnet");
    else
        app.setApplicationName("Beancash-qt");

    /// 4. Initialization of translations, so that intro dialog is in user's language
    // Now that QSettings are accessible, initialize translations
    QTranslator qtTranslatorBase, qtTranslator, translatorBase, translator;
    initTranslations(qtTranslatorBase, qtTranslator, translatorBase, translator);

    // Show help message immediately after parsing command-line options (for "-lang") and setting locale,
    // but before showing splash screen.
    if (mapArgs.count("-?") || mapArgs.count("--help") || mapArgs.count("-h") || mapArgs.count("-version") || mapArgs.count("-v"))
    {
        HelpMessageDialog help(NULL, mapArgs.count("-version"));
        help.showOrPrint();
        return 1;
    }

    // Now that translations are initialized, check for errors and allow a translatable error message
    if (fMissingDatadir)
    {
        QMessageBox::critical(0, QObject::tr("Bean Cash"),
                            QObject::tr("Error: Specified data directory \"%1\" does not exist.").arg(QString::fromStdString(mapArgs["-datadir"])));
        return 1;
    }
    else if (fSelParFromCLFailed)
    {
        QMessageBox::critical(0, QObject::tr("Bean Cash"),
                              QObject::tr("Error: Invalid combination of -regtest and -testnet used."));
        return 1;
    }

    /// 5. Now that settings and translations are available, ask the user for a data directory
    // User language is set up: pick a data directory
    Intro::pickDataDirectory(TestNet());

    /// 7. URI IPC sending
    // Do this early as we don't want to bother initializing if we are just calling IPC
    // ... but do it after creating app, so QCoreApplication::arguments is initialized:
        if (PaymentServer::ipcSendCommandLine())
            exit(0);

    // Start up the payment server early, so users that click on beancash: links have their requests routed
    PaymentServer* paymentServer = new PaymentServer(&app);

    /// 8. Main GUI initialization
    // Install global event filter that makes sure that long tooltips can be word-wrapped
    app.installEventFilter(new GUIUtil::ToolTipToRichTextFilter(TOOLTIP_WRAP_THRESHOLD, &app));

    // Load GUI settings from QSettings
    OptionsModel optionsModel;

    // Subscribe to global signals from core
    uiInterface.ThreadSafeMessageBox.connect(ThreadSafeMessageBox);
    uiInterface.ThreadSafeAskFee.connect(ThreadSafeAskFee);
    uiInterface.InitMessage.connect(InitMessage);
    uiInterface.Translate.connect(Translate);

    // Install qDebug() message handler to route to debug.log:
    #if QT_VERSION < 0x050000
        qInstallMsgHandler(DebugMessageHandler);
    #else
        qInstallMessageHandler(DebugMessageHandler);
    #endif

    // Show BeanGuy Splash Screen
    QSplashScreen splash(QPixmap(":/images/splash"), 0);
    if (GetBoolArg("-splash", true) && !GetBoolArg("-min"))
    {
        splash.show();
        splashref = &splash;
    }

    app.processEvents();

    app.setQuitOnLastWindowClosed(false);

    try
    {
        // Regenerate startup link, to fix links to old versions
        // OSX: makes no sense on MAC OS and might also scan/mount external (and sleeping) volumes (can take some time).
        if (GUIUtil::GetStartOnSystemStartup())
            GUIUtil::SetStartOnSystemStartup(true);

        boost::thread_group threadGroup;
        BitbeanGUI window(TestNet(), 0);
        guiref = &window;
        QTimer* pollShutdownTimer = new QTimer(guiref);
        QObject::connect(pollShutdownTimer, SIGNAL(timeout()), guiref, SLOT(detectShutdown()));
        pollShutdownTimer->start(200);

        if(AppInit2(threadGroup))
        {
            {
                // Put this in a block, so that the Model objects are cleaned up before
                // calling Shutdown().

                if (splashref)
                    splash.finish(&window);

                ClientModel clientModel(&optionsModel);
                WalletModel walletModel(pwalletMain, &optionsModel);

                window.setClientModel(&clientModel);
                window.setWalletModel(&walletModel);

                // If -min option passed, start window minimized.
                if(GetBoolArg("-min", false))
                {
                    window.showMinimized();
                }
                else
                {
                    window.show();
                }

                // Now that initialization/startup is done, process any command-line
                // Bean Cash:  URIs
                QObject::connect(paymentServer, SIGNAL(receivedURI(QString)), &window, SLOT(handleURI(QString)));
                QTimer::singleShot(100, paymentServer, SLOT(uiReady()));

                app.exec();

                window.hide();
                window.setClientModel(0);
                window.setWalletModel(0);
                guiref = 0;
            }
            // Shutdown the core and its threads, but don't exit Beancash-qt here

            // Show a simple window indicating shutdown status
            ShutdownWindow::showShutdownWindow(&window);

            threadGroup.interrupt_all();
            threadGroup.join_all();
            Shutdown();
        }
        else
        {
            // Exit cleanly if AppInit2 returns false
            threadGroup.interrupt_all();
            threadGroup.join_all();
            Shutdown();
            return 1;
        }
    } catch (std::exception& e) {
        handleRunawayException(&e);
    } catch (...) {
        handleRunawayException(NULL);
    }
    return 0;
}
#endif // BITBEAN_QT_TEST
