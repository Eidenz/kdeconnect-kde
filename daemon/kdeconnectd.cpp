/**
 * SPDX-FileCopyrightText: 2014 Yuri Samoilenko <kinnalru@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include <QApplication>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusMessage>
#include <QSessionManager>
#include <QStandardPaths>
#include <QIcon>
#include <QProcess>

#include <KAboutData>
#include <KDBusService>
#include <KNotification>
#include <KLocalizedString>
#include <KIO/AccessManager>

#include <dbushelper.h>

//UDP
#include <winsock2.h>
#include <stdio.h>
#include <tchar.h>

#include "core/daemon.h"
#include "core/device.h"
#include "core/backends/pairinghandler.h"
#include "kdeconnect-version.h"
#include "kdeconnectd_debug.h"

class DesktopDaemon : public Daemon
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.daemon")
public:
    DesktopDaemon(QObject* parent = nullptr)
        : Daemon(parent)
        , m_nam(nullptr)
    {
        qApp->setWindowIcon(QIcon(QStringLiteral(":/icons/kdeconnect/kdeconnect.svg")));
    }

    int sendUdp(std::string message) {

        int server_port = 42069;

        WSADATA wsaData;
        WSAStartup(0x0202, &wsaData);

        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        serverAddr.sin_port = htons((u_short) server_port);

        int clientSock = socket(PF_INET, SOCK_DGRAM, 0);
        sendto(clientSock, message.c_str(), message.length(), 0, (LPSOCKADDR) &serverAddr, sizeof(struct sockaddr));
        closesocket(clientSock);

        WSACleanup();

        return 0;
    }

    void askPairingConfirmation(Device* device) override
    {
        KNotification* notification = new KNotification(QStringLiteral("pairingRequest"), KNotification::NotificationFlag::Persistent);
        QTimer::singleShot(PairingHandler::pairingTimeoutMsec(), notification, &KNotification::close);
        notification->setIconName(QStringLiteral("dialog-information"));
        notification->setComponentName(QStringLiteral("kdeconnect"));
        notification->setTitle(QStringLiteral("KDE Connect"));
        notification->setText(i18n("Pairing request from %1\nKey: %2...", device->name().toHtmlEscaped(), QString::fromUtf8(device->verificationKey().left(8))));
        notification->setDefaultAction(i18n("Open"));
        notification->setActions(QStringList() << i18n("Accept") << i18n("Reject") << i18n("View key"));
        connect(notification, &KNotification::action1Activated, device, &Device::acceptPairing);
        connect(notification, &KNotification::action2Activated, device, &Device::rejectPairing);
        QString deviceId = device->id();
        auto openSettings = [this, deviceId] {
            openConfiguration(deviceId);
        };
        connect(notification, &KNotification::action3Activated, openSettings);
        connect(notification, QOverload<>::of(&KNotification::activated), openSettings);
        notification->sendEvent();
    }

    void reportError(const QString & title, const QString & description) override
    {
        qCWarning(KDECONNECT_DAEMON) << title << ":" << description;
        KNotification::event(KNotification::Error, title, description);
    }

    QNetworkAccessManager* networkAccessManager() override
    {
        if (!m_nam) {
            m_nam = new KIO::Integration::AccessManager(this);
        }
        return m_nam;
    }

    KJobTrackerInterface* jobTracker() override
    {
        return KIO::getJobTracker();
    }

    Q_SCRIPTABLE void sendSimpleNotification(const QString &eventId, const QString &title, const QString &text, const QString &iconName) override
    {
        KNotification* notification = new KNotification(eventId); //KNotification::Persistent
        notification->setIconName(iconName);
        notification->setComponentName(QStringLiteral("kdeconnect"));
        notification->setTitle(title);
        notification->setText(text);
        notification->sendEvent();

        sendUdp("{messageType: 1, index: 0, timeout: 5, height: 175, opacity: 0.9, volume: 0, audioPath: '', title: '"+title.toStdString()+"', content: '"+text.toStdString()+"', useBase64Icon: false, icon: '', sourceApp: 'KDE Connect'}");
    }

    void quit() override {
        QApplication::quit();
    }

private:
    QNetworkAccessManager* m_nam;
};

// Copied from plasma-workspace/libkworkspace/kworkspace.cpp
static void detectPlatform(int argc, char **argv)
{
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        return;
    }
    for (int i = 0; i < argc; i++) {
        if (qstrcmp(argv[i], "-platform") == 0 ||
                qstrcmp(argv[i], "--platform") == 0 ||
                QByteArray(argv[i]).startsWith("-platform=") ||
                QByteArray(argv[i]).startsWith("--platform=")) {
            return;
        }
    }
    const QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
    if (sessionType.isEmpty()) {
        return;
    }
    if (qstrcmp(sessionType, "wayland") == 0) {
        qputenv("QT_QPA_PLATFORM", "wayland");
    } else if (qstrcmp(sessionType, "x11") == 0) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
}

int main(int argc, char* argv[])
{
    detectPlatform(argc, argv);

    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_UseHighDpiPixmaps, true);
    KAboutData aboutData(
        QStringLiteral("kdeconnect.daemon"),
        i18n("KDE Connect Daemon"),
        QStringLiteral(KDECONNECT_VERSION_STRING),
        i18n("KDE Connect Daemon"),
        KAboutLicense::GPL
    );
    KAboutData::setApplicationData(aboutData);
    app.setQuitOnLastWindowClosed(false);

#ifdef USE_PRIVATE_DBUS
    DBusHelper::launchDBusDaemon();
#endif

    QCommandLineParser parser;
    QCommandLineOption replaceOption({QStringLiteral("replace")}, i18n("Replace an existing instance"));
    parser.addOption(replaceOption);
    aboutData.setupCommandLine(&parser);

    parser.process(app);
    aboutData.processCommandLine(&parser);
    if (parser.isSet(replaceOption)) {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kdeconnect"),
                                                    QStringLiteral("/MainApplication"),
                                                    QStringLiteral("org.qtproject.Qt.QCoreApplication"),
                                                    QStringLiteral("quit"));
        DBusHelper::sessionBus().call(message); //deliberately block until it's done, so we register the name after the app quits
    }

    KDBusService dbusService(KDBusService::Unique);

    DesktopDaemon daemon;

#ifdef Q_OS_WIN
    // make sure indicator shows up in the tray whenever daemon is spawned
    QProcess::startDetached(QStringLiteral("kdeconnect-indicator.exe"));
#endif

    // kdeconnectd is autostarted, so disable session management to speed up startup
    auto disableSessionManagement = [](QSessionManager &sm) {
        sm.setRestartHint(QSessionManager::RestartNever);
    };
    QObject::connect(&app, &QGuiApplication::commitDataRequest, disableSessionManagement);
    QObject::connect(&app, &QGuiApplication::saveStateRequest, disableSessionManagement);

    return app.exec();
}

#include "kdeconnectd.moc"
