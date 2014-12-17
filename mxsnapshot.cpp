/**********************************************************************
 *  mxsnapshot.cpp
 **********************************************************************
 * Copyright (C) 2014 MX Authors
 *
 * Authors: Adrian
 *          MEPIS Community <http://forum.mepiscommunity.org>
 *
 * This file is part of MX Snapshot.
 *
 * MX Tolls is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MX Tools is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MX Snapshot.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/


#include "mxsnapshot.h"
#include "ui_mxsnapshot.h"

#include <QFileDialog>
#include <QScrollBar>
#include <QTextStream>

#include <QDebug>

mxsnapshot::mxsnapshot(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::mxsnapshot)
{
    ui->setupUi(this);
    setup();
}

mxsnapshot::~mxsnapshot()
{
    delete ui;
}

// setup versious items first time program runs
void mxsnapshot::setup()
{
    config_file.setFileName("/etc/mx-snapshot.conf");
    QSettings settings(config_file.fileName(), QSettings::IniFormat);

    proc = new QProcess(this);
    timer = new QTimer(this);
    proc->setReadChannel(QProcess::StandardOutput);
    proc->setReadChannelMode(QProcess::MergedChannels);
    ui->stackedWidget->setCurrentIndex(0);
    ui->buttonCancel->setEnabled(true);
    ui->buttonStart->setEnabled(true);        

    // Load settings or use the default value
    error_log = settings.value("error_log", "/var/log/snapshot_errors.log").toString();
    work_dir = settings.value("work_dir", "/home/work").toString();
    snapshot_dir = settings.value("snapshot_dir", "/home/snapshot").toString();
    save_work = settings.value("save_work", "no").toString();
    snapshot_excludes.setFileName(settings.value("snapshot_excludes", "/usr/lib/mx-snapshot/snapshot_exclude.list").toString());
    initrd_modules_file.setFileName(settings.value("initrd_modules_file", "/usr/lib/mx-snapshot/initrd_modules.list").toString());
    snapshot_persist = settings.value("snapshot_persist", "no").toString();
    kernel_image = settings.value("kernel_image", "/vmlinuz").toString();
    initrd_image = settings.value("initrd_image", "/initrd.gz").toString();
    snapshot_basename = settings.value("snapshot_basename", "snapshot").toString();
    make_md5sum = settings.value("make_md5sum", "no").toString();
    make_isohybrid = settings.value("make_isohybrid", "yes").toString();
    edit_boot_menu = settings.value("edit_boot_menu", "no").toString();
    iso_dir = settings.value("iso_dir", "/usr/lib/mx-snapshot/new-iso").toString();
    lib_mod_dir = settings.value("lib_mod_dir", "/lib/modules/").toString();
    text_editor.setFileName(settings.value("text_editor", "/usr/bin/nano").toString());
    gui_editor.setFileName(settings.value("gui_editor", "/usr/bin/geany").toString());
    ata_dir = settings.value("ata_dir", "kernel/drivers/ata").toString();

    listDiskSpace();
}

// Util function
QString mxsnapshot::getCmdOut(QString cmd)
{
    proc = new QProcess(this);
    proc->start("/bin/bash", QStringList() << "-c" << cmd);
    proc->setReadChannel(QProcess::StandardOutput);
    proc->setReadChannelMode(QProcess::MergedChannels);
    proc->waitForFinished(-1);
    return proc->readAllStandardOutput().trimmed();
}

// Get version of the program
QString mxsnapshot::getVersion(QString name)
{
    QString cmd = QString("dpkg -l %1 | awk 'NR==6 {print $3}'").arg(name);
    return getCmdOut(cmd);
}


// List the info regarding the free space on drives
void mxsnapshot::listDiskSpace()
{
    QString out = QString("Current space on the partition* holding the work directory %1:").arg(work_dir.absolutePath());
    ui->labelCurrentSpace->setText(out);

    QString cmd = QString("df -h  %1 | awk '{printf \"%-8s\\t%-8s\\t%-8s\\t%-8s\\n\",$2,$3,$5,$4}'").arg(work_dir.absolutePath());
    out = getCmdOut(cmd);
    out.append("\n");
    ui->labelDiskSpace->setText(out);
    ui->labelDiskSpaceHelp->setText(tr("It is recommended that free space ('Avail') be at least twice as big as the total installed system size ('Used').\n\n"
                                       "      If necessary, you can create more available space\n"
                                       "      by removing previous snapshots and saved copies:\n"
                                       "      %1 snapshots are taking up %2 of disk space.").arg(getSnapshotCount()).arg(getSnapshotSize()));
}


// Checks if the editor listed in the config file is present
void mxsnapshot::checkEditor()
{
    if (gui_editor.exists()) {
        return;
    }
    QString msg = tr("The graphical text editor is set to %1, but it is not installed. Edit %2 "
                     "and set the gui_editor variable to the editor of your choice. "
                     "(examples: /usr/bin/gedit, /usr/bin/leafpad)").arg(gui_editor.fileName()).arg(config_file.fileName());
    QMessageBox::critical(0, QString::null, msg);
    qApp->quit();
}

// Checks if package is installed
bool mxsnapshot::checkInstalled(QString package)
{
    QString cmd = QString("dpkg -s live-init-mx | grep Status").arg(package);
    if (getCmdOut(cmd) == "Status: install ok installed") {
        return true;
    }
    return false;
}

// Installs live-init-mx package
void mxsnapshot::installLiveInitMx()
{
    QEventLoop loop;
    setCursor(QCursor(Qt::BusyCursor));
    if (proc->state() != QProcess::NotRunning){
        proc->kill();
    }
    proc->start("apt-get update");
    ui->outputBox->clear();
    setConnections(timer, proc);
    connect(proc, SIGNAL(finished(int)), &loop, SLOT(quit()));
    loop.exec();
    proc->start("apt-get install live-init-mx");
    loop.exec();
    timer->stop();
    ui->progressBar->setValue(100);
    setCursor(QCursor(Qt::ArrowCursor));
    if (proc->exitCode() != 0) {
        QMessageBox::critical(0, tr("Error"), tr("Count not install live-init-mx"));
    }
}

void mxsnapshot::checkDirectories()
{
    //  Create snapshot dir if it doesn't exist
    if (!snapshot_dir.exists()) {
        snapshot_dir.mkpath(snapshot_dir.absolutePath());
        QString path = snapshot_dir.absolutePath();
        QString cmd = QString("chmod 777 %1").arg(path);
        system(cmd.toAscii());
    }

    // Create work_dir if it doesn't exist
    if (!work_dir.exists()) {
        work_dir.mkpath(work_dir.absolutePath());
    }

    QString path1 = work_dir.absolutePath() + "/new-iso";
    QString path2 = work_dir.absolutePath() + "/new-squashfs";
    //  Remove folders if save_work = "no"
    if (save_work == "no") {
        QString cmd = "rm -rf " + path1 + path2;
        system(cmd.toAscii());
    }
    work_dir.mkpath(path1);
    work_dir.mkpath(path2);
}

void mxsnapshot::detectKernels()
{
    kernel_used = getCmdOut("uname -r");
    QString cmd = QString("ls /boot/vmlinuz-* | sed 's|/boot/vmlinuz-||g'| grep -v %1").arg(kernel_used);
    kernels_avail = getCmdOut(cmd);
}

void mxsnapshot::checkSaveWork()
{
    if (save_work == "yes") {
        save_message = QString(tr("* The temporary copy of the filesystem will be saved at %1/new-squashfs.")).arg(work_dir.absolutePath());
    } else {
        save_message = QString(tr("* The temporary copy of the filesystem will be created at %1/new-squashfs and removed when this program finishes.")).arg(work_dir.absolutePath());
    }
}

//// sync process events ////

void mxsnapshot::procStart() {
    timer->start(100);
}

void mxsnapshot::procTime() {
    int i = ui->progressBar->value() + 1;
    if (i > 100) {
        i = 0;
    }
    ui->progressBar->setValue(i);
}

// set proc and timer connections
void mxsnapshot::setConnections(QTimer* timer, QProcess* proc)
{
    disconnect(timer, SIGNAL(timeout()), 0, 0);
    connect(timer, SIGNAL(timeout()), SLOT(procTime()));
    disconnect(proc, SIGNAL(started()), 0, 0);
    connect(proc, SIGNAL(started()), SLOT(procStart()));
    disconnect(proc, SIGNAL(readyReadStandardOutput()), 0, 0);    
    connect(proc, SIGNAL(readyReadStandardOutput()), SLOT(onStdoutAvailable()));
    disconnect(proc, SIGNAL(finished(int)), 0, 0);
}

// return number of snapshots in snapshot_dir
QString mxsnapshot::getSnapshotCount()
{
    if (snapshot_dir.exists()) {
        QString cmd = QString("ls \"%1\"/*.iso | wc -l").arg(snapshot_dir.absolutePath());
        return getCmdOut(cmd);
    }
    return "0";
}

// return the size of the work folder
QString mxsnapshot::getSnapshotSize()
{
    QString size;
    if (snapshot_dir.exists()) {
        QString cmd = QString("du -sh \"%1\" | awk '{print $1}'").arg(snapshot_dir.absolutePath());
        size = getCmdOut(cmd);
        if (size == "" ) {
            return "0";
        }
        return size;
    }
    return "0";
}

//// slots ////

// update output box on Stdout
void mxsnapshot::onStdoutAvailable()
{
    QByteArray output = proc->readAllStandardOutput();
    QString out = ui->outputBox->toPlainText() + QString::fromUtf8(output);
    ui->outputBox->setPlainText(out);
    QScrollBar *sb = ui->outputBox->verticalScrollBar();
    sb->setValue(sb->maximum());
}


// Start button clicked
void mxsnapshot::on_buttonStart_clicked()
{
    // on first page
    if (ui->stackedWidget->currentIndex() == 0) {        
        ui->buttonStart->setEnabled(false);
        ui->stackedWidget->setCurrentIndex(1);
        if (edit_boot_menu == "yes") {
            checkEditor();
        }
        if (snapshot_persist == "yes") {
              if (!checkInstalled("live-init-mx")) {
                installLiveInitMx();
              }
        }
        checkDirectories();
        checkSaveWork();
        detectKernels();
        this->hide();
        QString msg = QString(tr("Snapshot will use the following settings.*\n\n"
                                 "* Working directory:") + "\n    %1\n" +
                              tr("* Snapshot directory:") + "\n   %2\n" +
                              tr("* Kernel to be used:") + "\n    %3\n%4\n-----\n" +
                              tr("These settings can be changed by exiting and editing") + "\n    %5")\
                              .arg(work_dir.absolutePath()).arg(snapshot_dir.absolutePath()).arg(kernel_used)\
                .arg(save_message).arg(config_file.fileName());
        QMessageBox msgBox(QMessageBox::NoIcon, QString("Settings"), msg, 0 ,this);
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        if (msgBox.exec() == QMessageBox::Cancel) {
            qApp->quit();
        }
        int ret = QMessageBox::question(this, tr("Final chance"),
                              tr("Snapshot now has all the information it needs to create an ISO from your running system.") + "\n\n" +
                              tr("It will take some time to finish, depending on the size of the installed system and the capacity of your computer.") + "\n\n" +
                              tr("OK to start?"), QMessageBox::Ok | QMessageBox::Cancel);
        if (ret == QMessageBox::Cancel) {
            qApp->quit();
        }
        this->show();

    // on output page
    } else if (ui->stackedWidget->currentWidget() == ui->outputPage) {
        ui->stackedWidget->setCurrentIndex(0);
        // restore Start button
        ui->buttonStart->setText(tr("Start"));
        ui->buttonStart->setIcon(QIcon("/usr/share/mx-snapshot/icons/dialog-ok.png"));        
        ui->outputBox->clear();
    } else {
        qApp->quit();
    }
}


// About button clicked
void mxsnapshot::on_buttonAbout_clicked()
{
    QMessageBox msgBox(QMessageBox::NoIcon,
                       tr("About MX Snapshot"), "<p align=\"center\"><b><h2>" +
                       tr("MX Snapshot") + "</h2></b></p><p align=\"center\">" + tr("Version: ") +
                       getVersion("mx-snapshot") + "</p><p align=\"center\"><h3>" +
                       tr("Program for creating a live-CD from the running system for MX Linux") + "</h3></p><p align=\"center\"><a href=\"http://www.mepiscommunity.org/mx\">http://www.mepiscommunity.org/mx</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) antiX") + "<br /><br /></p>", 0, this);
    msgBox.addButton(tr("Cancel"), QMessageBox::AcceptRole); // because we want to display the buttons in reverse order we use counter-intuitive roles.
    msgBox.addButton(tr("License"), QMessageBox::RejectRole);
    if (msgBox.exec() == QMessageBox::RejectRole)
        system("mx-viewer http://www.mepiscommunity.org/doc_mx/mx-snapshot-license.html 'MX Snapshot License'");
}

// Help button clicked
void mxsnapshot::on_buttonHelp_clicked()
{
    system("mx-viewer http://www.mepiscommunity.org/doc_mx/mxapps.html#snapshot 'MX Snapshot Help'");
}

// Select work directory
void mxsnapshot::on_buttonSelectWork_clicked()
{
    QFileDialog dialog;
    QDir selected = dialog.getExistingDirectory(0, tr("Select Work Directory"), QString(), QFileDialog::ShowDirsOnly);
    if (selected.exists()) {
        work_dir.setPath(selected.absolutePath());
        listDiskSpace();
    }
}
