/*
    Copyright 2014 Paul Colby

    This file is part of Bipolar.

    Bipolar is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Biplar is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Bipolar.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mainwindow.h"

#ifdef Q_OS_WIN
#include "os/flowsynchook.h"
#endif

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTextEdit>
#include <QTimer>

#include "polar/v2/trainingsession.h"

#define SETTINGS_GEOMETRY QLatin1String("geometry")

MainWindow::MainWindow(QWidget *parent, Qt::WindowFlags flags) : QMainWindow(parent, flags) {
    // Set the main window title.
    setWindowTitle(tr("%1 %2")
        .arg(QApplication::applicationName())
        .arg(QStringList(QApplication::applicationVersion().split(QLatin1Char('.')).mid(0, 3)).join(QLatin1Char('.'))));

    log = new QTextEdit;
    log->setReadOnly(true);
    setCentralWidget(log);

    // Restore the window's previous size and position.
    QSettings settings;
    QVariant geometry=settings.value(SETTINGS_GEOMETRY);
    if (geometry.isValid()) restoreGeometry(geometry.toByteArray());
    else setGeometry(40,40,800,550); // Default to 800x550, at position (40,40).

    // Lazy, UI-less, once-off mode for v0.1.
    QTimer::singleShot(0, this, SLOT(checkHook()));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Save the window's current size and position.
    QSettings settings;
    settings.setValue(SETTINGS_GEOMETRY,saveGeometry());
    QMainWindow::closeEvent(event);
}

void MainWindow::logMessage(QtMsgType type, const QMessageLogContext &context,
                            const QString &message, const QDateTime &time)
{
    Q_UNUSED(context)
    if (log) {
        QString level(QLatin1String("invalid"));
        switch (type) {
        case QtDebugMsg:    level = QLatin1String("Debug");    break;
        case QtWarningMsg:  level = QLatin1String("Warning");  break;
        case QtCriticalMsg: level = QLatin1String("Critical"); break;
        case QtFatalMsg:    level = QLatin1String("Fatal");    break;
        }
        log->append(tr("%1 %2 %3").arg(time.toString()).arg(level).arg(message));
    }
}

void MainWindow::checkHook()
{
#ifdef Q_OS_WIN
    const QDir hookDir = FlowSyncHook::installableHookDir();
    const int availableVersion = FlowSyncHook::getVersion(hookDir);
    if (availableVersion <= 0) {
        qWarning() << "failed to find installable hook";
        QTimer::singleShot(0, this, SLOT(convertAll()));
        return;
    }

    const QDir flowSyncDir = FlowSyncHook::flowSyncDir();
    if (!flowSyncDir.exists()) {
        QMessageBox::information(this, tr(""),
            tr("Unable to check if the Bipolar hook has been installed,\n"
               "because the Polar FlowSync application could not be located."));
    } else {
        const int installedVersion = FlowSyncHook::getVersion(flowSyncDir);

        QString message;
        if (installedVersion <= 0) {
            message = tr("The Bipolar hook does not appear to be installed.\n"
                         "Would you like to install it now?");
        } else if (installedVersion < availableVersion) {
            message = tr("This version of Bipolar includes a newer FlowSync hook.\n"
                         "Would you like to install it now?");
        } else if (installedVersion > availableVersion) {
            qWarning() << "the installed flowsync hook version" << installedVersion
                       << "is more recent than available" << availableVersion;
        }

        if ((!message.isEmpty()) &&
            (QMessageBox::question(this, tr(""), message,
                QMessageBox::Yes|QMessageBox::No, QMessageBox::No) == QMessageBox::Yes
            ) && (!FlowSyncHook::install(hookDir, flowSyncDir))) {
            QMessageBox::warning(this, tr(""),
                tr("Failed to install Bipolar hook into Polar FlowSync. "
                   "See log for details.\n\n"
                   "You may need to re-run this application as an administrator,\n"
                   "and/or exit Polar FlowSync before trying again.\n"
                   ));
        }
    }
#endif
    QTimer::singleShot(0, this, SLOT(convertAll()));
}

void MainWindow::convertAll()
{
    const QDir dataDir(
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
        QLatin1String("/Polar/PolarFlowSync/export"));
    if (!dataDir.exists()) {
        qWarning() << "data dir not found" << QDir::toNativeSeparators(dataDir.absolutePath());
    }

    QStringList sessions;
    foreach (const QFileInfo &info, dataDir.entryInfoList()) {
        QApplication::instance()->processEvents();
        if (!info.fileName().startsWith(QLatin1String("v2-users-"))) {
            qDebug() << "ignoring" << QDir::toNativeSeparators(info.fileName());
            continue;
        }
        if ((info.fileName().endsWith(QLatin1String(".gpx"))) ||
            (info.fileName().endsWith(QLatin1String(".hrm"))) ||
            (info.fileName().endsWith(QLatin1String(".tcx")))) {
                qDebug() << "ignoring" << QDir::toNativeSeparators(info.fileName());
                continue;
        }

        const QStringList parts = info.fileName().split(QLatin1Char('-'));
        if ((parts.length() < 6) ||
            (parts.at(3) != QLatin1Literal("training")) ||
            (parts.at(4) != QLatin1Literal("sessions"))) {
            qDebug() << "ignoring" << QDir::toNativeSeparators(info.fileName());
            continue;
        }

        const QString session = info.path() + QLatin1Char('/')
            + QStringList(parts.mid(0, 6)).join(QLatin1Char('-'));
        if (!sessions.contains(session)) {
            sessions.append(session);
        }
    }

    if (sessions.empty()) {
        qDebug() << "found nothing to convert";
    }

    int failed=0, succeeded=0;
    foreach (const QString &session, sessions) {
        qDebug() << "converting" << QDir::toNativeSeparators(session);
        QApplication::instance()->processEvents();

        polar::v2::TrainingSession parser(session);
        if (!parser.parse()) {
            qWarning() << "failed to parse" << QDir::toNativeSeparators(session);
            failed++;
            continue;
        }

        const bool overwrite = false; ///< Just used for dev/debugging currently.

        const QString gpxFileName = session + QLatin1String(".gpx");
        if ((QFile::exists(gpxFileName)) && (!overwrite)) {
            qDebug() << QDir::toNativeSeparators(gpxFileName) << "already exists";
        } else if (!parser.writeGPX(gpxFileName)) {
            qWarning() << "failed to write GPX";
            failed++;
        } else {
            qDebug() << "wrote GPX" << QDir::toNativeSeparators(gpxFileName);
            succeeded++;
        }

        const QString hrmFileName = session + QLatin1String(".hrm");
        if ((QFile::exists(hrmFileName)) && (!overwrite)) {
            qDebug() << QDir::toNativeSeparators(hrmFileName) << "already exists";
        } else if (!parser.writeGPX(hrmFileName)) {
            qWarning() << "failed to write HRM";
            failed++;
        } else {
            qDebug() << "wrote HRM" << QDir::toNativeSeparators(hrmFileName);
            succeeded++;
        }

        const QString tcxFileName = session + QLatin1String(".tcx");
        if ((QFile::exists(tcxFileName)) && (!overwrite)) {
            qDebug() << QDir::toNativeSeparators(tcxFileName) << "already exists";
        } else if (!parser.writeTCX(tcxFileName)) {
            qWarning() << "failed to write TCX";
            failed++;
        } else {
            qDebug() << "wrote TCX" << QDir::toNativeSeparators(tcxFileName);
            succeeded++;
        }
    }

    qDebug() << succeeded << "succeeded," << failed << "failed.";
}
