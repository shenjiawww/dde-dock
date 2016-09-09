/**
 * Copyright (C) 2015 Deepin Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 **/

#include <QSvgRenderer>
#include "mainitem.h"

#undef signals
extern "C" {
  #include <gtk/gtk.h>
}
#define signals public


static void requrestUpdateIcons()
{
    //can not passing QObject to the callback function,so use signal
    emit SignalManager::instance()->requestAppIconUpdate();
}

void initGtkThemeWatcher()
{
    GtkSettings* gs = gtk_settings_get_default();
    g_signal_connect(gs, "notify::gtk-icon-theme-name",
                     G_CALLBACK(requrestUpdateIcons), NULL);
}

SignalManager *SignalManager::m_signalManager = NULL;
SignalManager *SignalManager::instance()
{
    if (!m_signalManager)
        m_signalManager = new SignalManager;
    return m_signalManager;
}

MainItem::MainItem(QWidget *parent) : QLabel(parent)
{
    setAcceptDrops(true);
    setFixedSize(Dock::APPLET_FASHION_ITEM_WIDTH, Dock::APPLET_FASHION_ITEM_HEIGHT);

    m_dftm = new DBusFileTrashMonitor(this);
    connect(m_dftm, &DBusFileTrashMonitor::ItemCountChanged, [=]{
        updateIcon(false);
    });
    updateIcon(false);

    initGtkThemeWatcher();
    //can't use lambda here
    connect(SignalManager::instance(), SIGNAL(requestAppIconUpdate()), this, SLOT(onRequestUpdateIcon()));
}

MainItem::~MainItem()
{

}

void MainItem::emptyTrash()
{
    ClearTrashDialog *dialog = new ClearTrashDialog;
    dialog->setIcon(getThemeIconPath("user-trash-full"));
    connect(dialog, &ClearTrashDialog::buttonClicked, [=](int key){
        dialog->deleteLater();
        if (key == 1){
            qWarning() << "Clear trash...";
            QDBusPendingReply<QString, QDBusObjectPath, QString> tmpReply = m_dfo->NewEmptyTrashJob(false, "", "", "");
            QDBusObjectPath op = tmpReply.argumentAt(1).value<QDBusObjectPath>();
            DBusEmptyTrashJob * detj = new DBusEmptyTrashJob(op.path(), this);
            connect(detj, &DBusEmptyTrashJob::Done, detj, &DBusEmptyTrashJob::deleteLater);
            connect(detj, &DBusEmptyTrashJob::Done, [=](){
                updateIcon(false);
            });

            if (detj->isValid())
                detj->Execute();
        }
    });
    dialog->exec();
}

void MainItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        QProcess::startDetached("gvfs-open trash://");
    }

    //Makesure it parent can accept the mouse event too
    event->ignore();
}

void MainItem::dragEnterEvent(QDragEnterEvent * event)
{
    if (event->source())
        return;//just accept the object outside this app
    updateIcon(true);

    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void MainItem::dragLeaveEvent(QDragLeaveEvent *)
{
    updateIcon(false);
}

void MainItem::dropEvent(QDropEvent *event)
{
    updateIcon(false);

    if (event->source())
        return;

    if (event->mimeData()->formats().indexOf("RequestDock") != -1) {    //from desktop or launcher
        QJsonObject dataObj = QJsonDocument::fromJson(event->mimeData()->data("RequestDock")).object();
        if (!dataObj.isEmpty()){
            QString appKey = dataObj.value("appKey").toString();
            QString appName = dataObj.value("appName").toString();
            QString appIcon = dataObj.value("appIcon").toString();
            if (appKey.isEmpty())
                return;
            event->ignore();

            execUninstall(appKey, appName, appIcon);
        }
    }
    else {//File or Dirctory
        trashFiles(event->mimeData()->urls());
    }
}

void MainItem::onRequestUpdateIcon()
{
    updateIcon(false);
}

void MainItem::execUninstall(const QString &appKey, const QString &appName, const QString &appIcon)
{
    ConfirmUninstallDialog *dialog = new ConfirmUninstallDialog;
    //TODO: need real icon name
    dialog->setIcon(getThemeIconPath(appIcon));
    QString message = tr("Are you sure to uninstall %1?").arg(appName);
    dialog->setMessage(message);
    connect(dialog, &ConfirmUninstallDialog::buttonClicked, [=](int key){
        dialog->deleteLater();
        if (key == 1){
            qWarning() << "Uninstall application:" << appKey << appName;
            m_launcher->RequestUninstall(appKey, true);
        }
    });
    dialog->exec();
}

void MainItem::trashFiles(const QList<QUrl> &files)
{
    QStringList normalFiles;
    for (QUrl url : files) {
        //try to uninstall app by .desktop file
        if (url.fileName().endsWith(".desktop")) {
            QSettings ds(url.path(), QSettings::IniFormat);
            ds.setIniCodec(QTextCodec::codecForName("UTF-8"));
            ds.beginGroup("Desktop Entry");
            QString appKey = ds.value("X-Deepin-AppID").toString();
            if (!appKey.isEmpty()) {
                QString l(qgetenv(QString("LANGUAGE").toUtf8().data()));
                QString appName = ds.value(QString("Name[%1]").arg(l)).toString();
                appName = appName.isEmpty() ? ds.value("Name").toString() : appName;
                execUninstall(appKey, appName, ds.value("Icon").toString());
            }
            else {
                normalFiles << url.path();
            }
            ds.endGroup();
        }
        else {
            normalFiles << url.path();
        }
    }

    //remove normal files
    QDBusPendingReply<QString, QDBusObjectPath, QString> tmpReply = m_dfo->NewTrashJob(normalFiles, false, "", "", "");
    QDBusObjectPath op = tmpReply.argumentAt(1).value<QDBusObjectPath>();
    DBusTrashJob * dtj = new DBusTrashJob(op.path(), this);
    connect(dtj, &DBusTrashJob::Done, dtj, &DBusTrashJob::deleteLater);
    connect(dtj, &DBusTrashJob::Done, [=](){
        updateIcon(false);
    });

    if (dtj->isValid())
        dtj->Execute();

    qWarning()<< op.path() << "Move files to trash: "<< normalFiles;
}

void MainItem::updateIcon(bool isOpen)
{
    QString iconName = "";
    if (isOpen)
    {
        if (m_dftm->ItemCount() > 0)
            iconName = "user-trash-full-opened";
        else
            iconName = "user-trash-empty-opened";
    }
    else
    {
        if (m_dftm->ItemCount() > 0)
            iconName = "user-trash-full";
        else
            iconName = "user-trash-empty";
    }

    QPixmap pixmap(getThemeIconPath(iconName));
    setPixmap(pixmap.scaled(Dock::APPLET_FASHION_ICON_SIZE,Dock::APPLET_FASHION_ICON_SIZE));
}

// iconName should be a icon name constraints to the freeedesktop standard.
QString MainItem::getThemeIconPath(QString iconName)
{
    // iconPath is an absolute path of the system.
    if (QFile::exists(iconName) && iconName.contains(QDir::separator())) {
        return iconName;
    }
    else {
        QByteArray bytes = iconName.toUtf8();
        const char *name = bytes.constData();

        GtkIconTheme* theme = gtk_icon_theme_get_default();

        GtkIconInfo* info = gtk_icon_theme_lookup_icon(theme, name, 48, GTK_ICON_LOOKUP_GENERIC_FALLBACK);

        if (info) {
            char* path = g_strdup(gtk_icon_info_get_filename(info));
#if GTK_MAJOR_VERSION >= 3
            g_object_unref(info);
#elif GTK_MAJOR_VERSION == 2
            gtk_icon_info_free(info);
#endif
            return QString(path);
        } else {
            return "";
        }
    }
}