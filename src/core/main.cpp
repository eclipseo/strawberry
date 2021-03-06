/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"
#include "version.h"

#include <QtGlobal>

#include <glib.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory>
#include <time.h>

#ifdef Q_OS_UNIX
#  include <unistd.h>
#endif

#ifdef Q_OS_MACOS
#  include <sys/resource.h>
#  include <sys/sysctl.h>
#endif

#ifdef Q_OS_WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
  #endif
  #include <windows.h>
  #include <iostream>
#endif  // Q_OS_WIN32

#include <QObject>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QFileDevice>
#include <QIODevice>
#include <QByteArray>
#include <QNetworkProxy>
#include <QFile>
#include <QString>
#include <QImage>
#include <QSettings>
#include <QLoggingCategory>
#include <QtDebug>
#ifdef HAVE_DBUS
#  include <QDBusArgument>
#endif

#include "main.h"

#include "core/logging.h"

#include "qtsingleapplication.h"
#include "qtsinglecoreapplication.h"

#ifdef HAVE_DBUS
#  include "mpris.h"
#endif
#include "utilities.h"
#include "metatypes.h"
#include "iconloader.h"
#include "mainwindow.h"
#include "commandlineoptions.h"
#include "systemtrayicon.h"
#include "application.h"
#include "networkproxyfactory.h"
#include "scangiomodulepath.h"

#include "widgets/osd.h"

#ifdef HAVE_DBUS
  QDBusArgument &operator<<(QDBusArgument &arg, const QImage &image);
  const QDBusArgument &operator>>(const QDBusArgument &arg, QImage &image);
#endif

int main(int argc, char* argv[]) {

#ifdef Q_OS_MACOS
  // Do Mac specific startup to get media keys working.
  // This must go before QApplication initialisation.
  mac::MacMain();
#endif

#if defined(Q_OS_WIN32) || defined(Q_OS_MACOS)
  QCoreApplication::setApplicationName("Strawberry");
  QCoreApplication::setOrganizationName("Strawberry");
#else
  QCoreApplication::setApplicationName("strawberry");
  QCoreApplication::setOrganizationName("strawberry");
#endif
  QCoreApplication::setApplicationVersion(STRAWBERRY_VERSION_DISPLAY);
  QCoreApplication::setOrganizationDomain("strawbs.org");

// This makes us show up nicely in gnome-volume-control
#if !GLIB_CHECK_VERSION(2, 36, 0) // Deprecated in glib 2.36.0
  g_type_init();
#endif
  g_set_application_name(QCoreApplication::applicationName().toLocal8Bit());

  RegisterMetaTypes();

  // Initialise logging.  Log levels are set after the commandline options are parsed below.
  logging::Init();
  g_log_set_default_handler(reinterpret_cast<GLogFunc>(&logging::GLog), nullptr);

  CommandlineOptions options(argc, argv);

  {
    // Only start a core application now so we can check if there's another Strawberry running without needing an X server.
    // This MUST be done before parsing the commandline options so QTextCodec gets the right system locale for filenames.
    QtSingleCoreApplication a(argc, argv);
    Utilities::CheckPortable();

    // Parse commandline options - need to do this before starting the full QApplication so it works without an X server
    if (!options.Parse()) return 1;
    logging::SetLevels(options.log_levels());

    if (a.isRunning()) {
      if (options.is_empty()) {
        qLog(Info) << "Strawberry is already running - activating existing window";
      }
      if (a.sendMessage(options.Serialize(), 5000)) {
	main_exit_safe(0);
        return 0;
      }
      // Couldn't send the message so start anyway
    }
  }

#ifdef Q_OS_MACOS
  // Must happen after QCoreApplication::setOrganizationName().
  setenv("XDG_CONFIG_HOME", QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toLocal8Bit().constData(), 1);
#endif

  // Output the version, so when people attach log output to bug reports they don't have to tell us which version they're using.
  qLog(Info) << "Strawberry" << STRAWBERRY_VERSION_DISPLAY;

  // Seed the random number generators.
  time_t t = time(nullptr);
  srand(t);
  qsrand(t);

  Utilities::IncreaseFDLimit();

  QtSingleApplication a(argc, argv);

#ifdef Q_OS_MACOS
  //QCoreApplication::setLibraryPaths(QStringList() << QCoreApplication::applicationDirPath() + "/../PlugIns");
#endif

  a.setQuitOnLastWindowClosed(false);

  // Do this check again because another instance might have started by now
  if (a.isRunning() && a.sendMessage(options.Serialize(), 5000)) {
    return 0;
  }

#ifndef Q_OS_MACOS
  // Gnome on Ubuntu has menu icons disabled by default.  I think that's a bad idea, and makes some menus in Strawberry look confusing.
  QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);
#else
  QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus, true);
  // Fixes focus issue with NSSearchField, see QTBUG-11401
  QCoreApplication::setAttribute(Qt::AA_NativeWindows, true);
#endif

  // Set the permissions on the config file on Unix - it can contain passwords for internet services so it's important that other users can't read it.
  // On Windows these are stored in the registry instead.
#ifdef Q_OS_UNIX
  {
    QSettings s;

    // Create the file if it doesn't exist already
    if (!QFile::exists(s.fileName())) {
      QFile file(s.fileName());
      file.open(QIODevice::WriteOnly);
    }

    // Set -rw-------
    QFile::setPermissions(s.fileName(), QFile::ReadOwner | QFile::WriteOwner);
  }
#endif

  // Resources
  Q_INIT_RESOURCE(data);
  Q_INIT_RESOURCE(icons);

  QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, true);

  Application app;

  // Network proxy
  QNetworkProxyFactory::setApplicationProxyFactory(NetworkProxyFactory::Instance());

  // Create the tray icon and OSD
  std::unique_ptr<SystemTrayIcon> tray_icon(SystemTrayIcon::CreateSystemTrayIcon());
  OSD osd(tray_icon.get(), &app);

#ifdef HAVE_DBUS
  mpris::Mpris mpris(&app);
#endif

  // Window
  MainWindow w(&app, tray_icon.get(), &osd, options);
#ifdef Q_OS_MACOS
  mac::EnableFullScreen(w);
#endif  // Q_OS_MACOS
#ifdef HAVE_GIO
  ScanGIOModulePath();
#endif
#ifdef HAVE_DBUS
  QObject::connect(&mpris, SIGNAL(RaiseMainWindow()), &w, SLOT(Raise()));
#endif
  QObject::connect(&a, SIGNAL(messageReceived(QString)), &w, SLOT(CommandlineOptionsReceived(QString)));

  int ret = a.exec();

  main_exit_safe(ret);

  return ret;
}

void main_exit_safe(int ret) {

#ifdef Q_OS_LINUX
  bool have_nvidia = false;

  QFile proc_modules("/proc/modules");
  if (proc_modules.open(QIODevice::ReadOnly)) {
    forever {
      QByteArray line = proc_modules.readLine();
      if (line.startsWith("nvidia ") || line.startsWith("nvidia_")) {
        have_nvidia = true;
      }
      if (proc_modules.atEnd()) break;
    }
    proc_modules.close();
  }

  QFile self_maps("/proc/self/maps");
  if (self_maps.open(QIODevice::ReadOnly)) {
    forever {
      QByteArray line = self_maps.readLine();
      if (line.startsWith("libnvidia-")) {
        have_nvidia = true;
      }
      if (self_maps.atEnd()) break;
    }
    self_maps.close();
  }

  if (have_nvidia) {
    qLog(Warning) << "Exiting immediately to work around NVIDIA driver bug.";
    _exit(ret);
  }
#endif

}
