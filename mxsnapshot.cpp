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
void mxsnapshot::setup() {
    proc = new QProcess(this);
    timer = new QTimer(this);
    proc->setReadChannel(QProcess::StandardOutput);
    proc->setReadChannelMode(QProcess::MergedChannels);
    ui->stackedWidget->setCurrentIndex(0);
    ui->buttonCancel->setEnabled(true);
    ui->buttonStart->setEnabled(true);        

    // If values in config file any are unset, these defaults will be used.
    error_log="/var/log/snapshot_errors.log";
    work_dir="/home/work";
    snapshot_dir="/home/snapshot";
    save_work="no";
    snapshot_excludes.setFileName("/usr/lib/mx-snapshot/snapshot_exclude.list");
    initrd_modules_file.setFileName("/usr/lib/mx-snapshot/initrd_modules.list");
    snapshot_persist="no";
    kernel_image="/vmlinuz";
    initrd_image="/initrd.gz";
    snapshot_basename="snapshot";
    make_md5sum="no";
    make_isohybrid="yes";
    edit_boot_menu="no";
    iso_dir="/usr/lib/mx-snapshot/new-iso";
    lib_mod_dir="/lib/modules/";
    ata_dir="kernel/drivers/ata";
}

// Util function
QString mxsnapshot::getCmdOut(QString cmd) {
    proc = new QProcess(this);
    proc->start("/bin/bash", QStringList() << "-c" << cmd);
    proc->setReadChannel(QProcess::StandardOutput);
    proc->setReadChannelMode(QProcess::MergedChannels);
    proc->waitForFinished(-1);
    return proc->readAllStandardOutput().trimmed();
}

// Get version of the program
QString mxsnapshot::getVersion(QString name) {
    QString cmd = QString("dpkg -l %1 | awk 'NR==6 {print $3}'").arg(name);
    return getCmdOut(cmd);
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

void mxsnapshot::procDone(int exitCode) {
    timer->stop();
    ui->progressBar->setValue(100);
    setCursor(QCursor(Qt::ArrowCursor));

    if (exitCode == 0) {
        ui->outputLabel->setText(tr("Finished searching for shares."));
    } else {
        QMessageBox::critical(this, tr("Error"),
                              tr("Process finished. Errors have occurred."));        
    }    
    ui->buttonStart->setEnabled(true);    
    ui->buttonStart->setText(tr("< Back"));
    ui->buttonStart->setIcon(QIcon());    
}


// set proc and timer connections
void mxsnapshot::setConnections(QTimer* timer, QProcess* proc) {
    disconnect(timer, SIGNAL(timeout()), 0, 0);
    connect(timer, SIGNAL(timeout()), SLOT(procTime()));
    disconnect(proc, SIGNAL(started()), 0, 0);
    connect(proc, SIGNAL(started()), SLOT(procStart()));
    disconnect(proc, SIGNAL(finished(int)), 0, 0);
    connect(proc, SIGNAL(finished(int)),SLOT(procDone(int)));
    disconnect(proc, SIGNAL(readyReadStandardOutput()), 0, 0);    
    connect(proc, SIGNAL(readyReadStandardOutput()), SLOT(onStdoutAvailable()));
}


//// slots ////

// update output box on Stdout
void mxsnapshot::onStdoutAvailable() {
    QByteArray output = proc->readAllStandardOutput();
    QString out = ui->outputBox->toPlainText() + QString::fromUtf8(output);
    ui->outputBox->setPlainText(out);
    QScrollBar *sb = ui->outputBox->verticalScrollBar();
    sb->setValue(sb->maximum());
}


// Start button clicked
void mxsnapshot::on_buttonStart_clicked() {
    // on first page
    if (ui->stackedWidget->currentIndex() == 0) {        
        ui->buttonStart->setEnabled(false);

    // on output page
    } else if (ui->stackedWidget->currentWidget() == ui->outputPage) {
        ui->stackedWidget->setCurrentIndex(0);
        // restore Start button
        ui->buttonStart->setText(tr("Start"));
        ui->buttonStart->setIcon(QIcon("/usr/share/mx-snapshot/icons/dialog-ok.png"));        
        ui->outputBox->clear();
    } else {
        qApp->exit(0);
    }
}


// About button clicked
void mxsnapshot::on_buttonAbout_clicked() {
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
void mxsnapshot::on_buttonHelp_clicked() {
    system("mx-viewer http://www.mepiscommunity.org/doc_mx/mxapps.html#snapshot 'MX Snapshot Help'");
}

