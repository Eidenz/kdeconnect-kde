/**
 * SPDX-FileCopyrightText: 2016 Aleix Pol Gonzalez <aleixpol@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef REMOTECOMMANDSPLUGIN_H
#define REMOTECOMMANDSPLUGIN_H

class QObject;

#include <core/kdeconnectplugin.h>
#include <QString>

class Q_DECL_EXPORT RemoteCommandsPlugin
    : public KdeConnectPlugin
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.remotecommands")
    Q_PROPERTY(QByteArray commands READ commands NOTIFY commandsChanged)
    Q_PROPERTY(QString deviceId READ deviceId CONSTANT)
    Q_PROPERTY(bool canAddCommand READ canAddCommand CONSTANT)

public:
    explicit RemoteCommandsPlugin(QObject* parent, const QVariantList& args);
    ~RemoteCommandsPlugin() override;

    Q_SCRIPTABLE void triggerCommand(const QString& key);
    Q_SCRIPTABLE void editCommands();

    QByteArray commands() const { return m_commands; }
    QString deviceId() const { return device()->id(); }
    bool canAddCommand() const { return m_canAddCommand; }

    bool receivePacket(const NetworkPacket& np) override;
    void connected() override;
    QString dbusPath() const override;

Q_SIGNALS:
    Q_SCRIPTABLE void commandsChanged(const QByteArray& commands);

private:
    void setCommands(const QByteArray& commands);

    QByteArray m_commands;
    bool m_canAddCommand;
};

#endif
