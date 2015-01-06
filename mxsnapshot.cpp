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
    stamp = settings.value("stamp", "datetime").toString();

    listDiskSpace();
}

// Util function for getting bash command output
QString mxsnapshot::getCmdOut(QString cmd)
{    
    QEventLoop loop;
    proc = new QProcess(this);
    connect(proc, SIGNAL(finished(int)), &loop, SLOT(quit()));
    proc->start("/bin/bash", QStringList() << "-c" << cmd);
    loop.exec();
    return proc->readAllStandardOutput().trimmed();
}

// Util function for getting bash command output + updating the progress bar and output
QString mxsnapshot::getCmdOut2(QString cmd)
{
    QEventLoop loop;
    proc = new QProcess(this);
    setConnections(timer, proc);
    connect(proc, SIGNAL(finished(int)), &loop, SLOT(quit()));
    proc->start("/bin/bash", QStringList() << "-c" << cmd);
    loop.exec();
    return proc->readAllStandardOutput().trimmed();
}

// Get version of the program
QString mxsnapshot::getVersion(QString name)
{
    QString cmd = QString("dpkg -l %1 | awk 'NR==6 {print $3}'").arg(name);
    return getCmdOut(cmd);
}

// return number of snapshots in snapshot_dir
int mxsnapshot::getSnapshotCount()
{
    if (snapshot_dir.exists()) {
        QFileInfoList list = snapshot_dir.entryInfoList(QStringList("*.iso"), QDir::Files);
        return list.size();
    }
    return 0;
}

// return the size of the work folder
int mxsnapshot::getSnapshotSize()
{
    QString size;
    if (snapshot_dir.exists()) {
        QString cmd = QString("du -sh \"%1\" | awk '{print $1}'").arg(snapshot_dir.absolutePath());
        size = getCmdOut(cmd);
        if (size != "" ) {
            return size.toInt();
        }        
    }
    return 0;
}

// List the info regarding the free space on drives
void mxsnapshot::listDiskSpace()
{
    QString out = QString("Current space on the partition* holding the work directory %1:").arg(work_dir.absolutePath());
    ui->labelCurrentSpace->setText(out);

    QString path;
    if (work_dir.absolutePath() == "/home/work") {
        path = "/home";
    } else {
        path = work_dir.absolutePath();
    }
    QString cmd = QString("df -h  %1 | awk '{printf \"%-8s\\t%-8s\\t%-8s\\t%-8s\\n\",$2,$3,$5,$4}'").arg(path);
    out = getCmdOut(cmd);
    out.append("\n");
    ui->labelDiskSpace->setText(out);
    ui->labelDiskSpaceHelp->setText(tr("It is recommended that free space ('Avail') be at least twice as big as the total installed system size ('Used').\n\n"
                                       "      If necessary, you can create more available space\n"
                                       "      by removing previous snapshots and saved copies:\n"
                                       "      %1 snapshots are taking up %2 of disk space.").arg(QString::number(getSnapshotCount())).arg(QString::number(getSnapshotSize())));
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
    return qApp->quit();
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
    if (proc->state() != QProcess::NotRunning){
        proc->kill();
    }
    ui->outputBox->clear();
    setConnections(timer, proc);
    connect(proc, SIGNAL(finished(int)), &loop, SLOT(quit()));
    proc->start("apt-get update");
    loop.exec();
    proc->start("apt-get install live-init-mx");
    loop.exec();
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
        QString cmd = "rm -rf " + path1 + " " + path2 + " 2>/dev/null";
        setConnections(timer, proc);
        getCmdOut2(cmd);
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
        save_message = QString(tr("- The temporary copy of the filesystem will be saved at: %1/new-squashfs.")).arg(work_dir.absolutePath());
    } else {
        save_message = QString(tr("- The temporary copy of the filesystem will be created at: %1/new-squashfs and removed when this program finishes.")).arg(work_dir.absolutePath());
    }
}

void mxsnapshot::checkInitrdModules()
{
    if (!initrd_modules_file.exists()) {
        QString msg = tr("Could not find list of modules to put into the initrd") + "/n"\
            + tr("Missing file:") + " " + initrd_modules_file.fileName();
        QMessageBox::critical(0, tr("Error"), msg);
        return qApp->exit(2);
    }
}

void mxsnapshot::openInitrd(QString file, QString initrd_dir)
{
    QString cmd = "mkdir -p " + initrd_dir;
    system(cmd.toAscii());
    cmd = "chmod a+rx " + initrd_dir;
    system(cmd.toAscii());
    QDir::setCurrent(initrd_dir);
    cmd = QString("gunzip -c %1 | cpio -idum").arg(file);
    system(cmd.toAscii());
}

void mxsnapshot::closeInitrd(QString initrd_dir, QString file)
{
    QDir::setCurrent(initrd_dir);
    QString cmd = "(find . | cpio -o -H newc --owner root:root | gzip -9) >" + file;
    system(cmd.toAscii());
    cmd = "rm -r " + initrd_dir;
    getCmdOut2(cmd);
}

// Copying the new-iso filesystem
void mxsnapshot::copyNewIso()
{
    ui->outputBox->clear();

    ui->outputLabel->setText(tr("Copying the new-iso filesystem..."));
    QString cmd = "rsync -a " + iso_dir +  "/ " + work_dir.absolutePath() + "/new-iso/";
    getCmdOut2(cmd);

    cmd = "cp /boot/vmlinuz-" + kernel_used + " " + work_dir.absolutePath() + "/new-iso/antiX/vmlinuz";
    getCmdOut2(cmd);

    QString initrd_dir = getCmdOut("mktemp -d /tmp/mx-snapshot-XXXXXX");    
    openInitrd(iso_dir + "/antiX/initrd.gz", initrd_dir);

    QString mod_dir = initrd_dir + "/lib/modules";
    cmd = "rm -r " + mod_dir + "/*";
    getCmdOut2(cmd);

    copyModules(mod_dir + "/" + kernel_used, "/lib/modules/" + kernel_used);

    closeInitrd(initrd_dir, work_dir.absolutePath() + "/new-iso/antiX/initrd.gz");
}

// copyModules(mod_dir/kernel_used /lib/modules/kernel_used)
void mxsnapshot::copyModules(QString to, QString from)
{
    QStringList mod_list; // list of modules
    QString expr; // expression list for find

    // read list of modules from file
    if (initrd_modules_file.open(QIODevice::ReadOnly)) {
        QTextStream in(&initrd_modules_file);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line != "") {
                mod_list.append(line);
            }
        }
        initrd_modules_file.close();
    } else {
        QMessageBox::critical(0, tr("Error"), tr("Cound not open file: ") + initrd_modules_file.fileName());
        return qApp->exit(2);
    }

    // modify module names for find operation
    for (QStringList::Iterator it = mod_list.begin(); it != mod_list.end(); ++it) {
        QString mod_name;
        mod_name = *it;
        mod_name.replace(QRegExp("[_-]"), "[_-]"); // replace _ or - with [_-]
        mod_name.replace(QRegExp("$"), ".ko"); // add .ko at end of the name
        *it = mod_name;
    }
    expr = mod_list.join(" -o -name ");
    expr = "-name " + expr;

    QDir dir;
    dir.mkpath(to);

    // find modules from list in "from" directory
    QString cmd = QString("find %1 %2").arg(from).arg(expr);
    QString files = getCmdOut(cmd);
    QStringList file_list = files.split("\n");
    ui->outputLabel->setText(tr("Copying %1 modules into the initrd").arg(file_list.count()));

    // copy modules to destination
    for (QStringList::Iterator it = file_list.begin(); it != file_list.end(); ++it) {
        QString sub_dir, file_name;
        cmd = QString("basename $(dirname %1)").arg(*it);
        sub_dir = to + "/" + getCmdOut(cmd.toAscii());
        dir.mkpath(sub_dir);
        cmd = QString("basename %1").arg(*it);
        file_name = getCmdOut2(cmd.toAscii());
        QFile::copy(*it, sub_dir + "/" + file_name);
    }
}

void mxsnapshot::copyFileSystem()
{
    QEventLoop loop;
    ui->outputBox->clear();
    setConnections(timer, proc);
    connect(proc, SIGNAL(finished(int)), &loop, SLOT(quit()));

    ui->outputLabel->setText(tr("Copying filesystem..."));
    QDir::setCurrent("/");
    QString cmd = QString("rsync -av / %1/new-squashfs/ --delete --exclude=\"%1\" --exclude=\"%2\" --exclude-from=\"%3\"")\
            .arg(work_dir.absolutePath()).arg(snapshot_dir.absolutePath()).arg(snapshot_excludes.fileName());
    proc->start(cmd.toAscii());
    loop.exec();

    // create fstab if it doesn't exist
    cmd = QString("touch %1/new-squashfs/etc/fstab").arg(work_dir.absolutePath());
    system(cmd.toAscii());
}

// Create the output filename
QString mxsnapshot::getFilename() {
    if (stamp == "datetime") {
        return snapshot_basename + "-" + getCmdOut("date +%Y%m%d_%H%M") + ".iso";
    } else {
        int n = 1;
        QString name = snapshot_dir.absolutePath() + "/" + snapshot_basename + QString::number(n) + ".iso";
        QDir dir;
        dir.setPath(name);
        while (dir.exists(dir.absolutePath())) {
            n++;
            QString name = snapshot_dir.absolutePath() + "/" + snapshot_basename + QString::number(n) + ".iso";
            dir.setPath(name);
        }
        return dir.absolutePath();
    }
}

// removes the directory of the old package-list directories (that use the same basename)
void mxsnapshot::removeOldPackageDirectory() {
    QString dir =  work_dir.absolutePath() + "/new-iso/" + snapshot_basename;
    QString cmd = "rm -r " + dir + "*";
    getCmdOut2(cmd);
    ui->outputLabel->setText(tr("Removing old package-list directory: ") + dir);
}

// make working directory using the base filename
void mxsnapshot::mkDir(QString filename) {
    QDir dir;
    filename.chop(4); //remove ".iso" string
    dir.setPath(work_dir.absolutePath() + "/new-iso/" + filename);
    dir.mkpath(dir.absolutePath());
}

// save package list in working directory
void mxsnapshot::savePackageList(QString filename) {
    filename.chop(4); //remove .iso
    filename = work_dir.absolutePath() + "/new-iso/" + filename + "/package_list";
    QString cmd = "dpkg -l | grep \"ii\" | awk '{ print $2 }' >" + filename;
    system(cmd.toAscii());
}

void mxsnapshot::createIso(QString filename) {
    // squash the filesystem copy
    QDir::setCurrent(work_dir.absolutePath());
    QString cmd = "mksquashfs new-squashfs new-iso/antiX/linuxfs";
    ui->outputLabel->setText(tr("Squashing filesystem..."));
    setConnections(timer, proc);
    getCmdOut2(cmd);

    // create the iso file
    QDir::setCurrent(work_dir.absolutePath() + "/new-iso");
    cmd = "genisoimage -l -V MX-14live -R -J -pad -no-emul-boot -boot-load-size 4 -boot-info-table -b boot/isolinux/isolinux.bin -c boot/isolinux/isolinux.cat -o " + snapshot_dir.absolutePath() + "/" + filename + " .";
    ////ui->outputBox->clear();
    ui->outputLabel->setText(tr("Creating CD/DVD image file..."));
    setConnections(timer, proc);
    getCmdOut2(cmd);

    // make it isohybrid
    if (make_isohybrid == "yes") {
        ui->outputLabel->setText(tr("Making hybrid iso"));
        cmd = "isohybrid " + snapshot_dir.absolutePath() + "/" + filename;
        getCmdOut2(cmd);
    }

    // make md5sum
    if (make_md5sum == "yes") {
        ui->outputLabel->setText(tr("Making md5sum"));
        cmd = "md5sum " + snapshot_dir.absolutePath() + "/" + filename + " > " + snapshot_dir.absolutePath() + "/" + filename + ".md5";
        getCmdOut2(cmd);
    }
}

void mxsnapshot::cleanUp() {
    if (save_work == "no") {
        QDir::setCurrent("/");
        ui->outputLabel->setText(tr("Cleaning..."));
        setConnections(timer, proc);
        getCmdOut2("rm -rf " + work_dir.absolutePath() + "/new-iso 2>/dev/null");
        getCmdOut2("rm -rf " + work_dir.absolutePath() + "/new-squashfs 2>/dev/null");
    } else {
        QDir::setCurrent(work_dir.absolutePath());
        system("rm new-iso/antiX/linuxfs 2>/dev/null");
    }

    if (snapshot_persist == "yes") {
        ui->outputLabel->setText(tr("Removing live-init-mx"));
        setConnections(timer, proc);
        getCmdOut2("apt-get purge linux-init-mx");
    }
    ui->outputLabel->clear();
}

//// sync process events ////
void mxsnapshot::procStart()
{
    timer->start(100);
    setCursor(QCursor(Qt::BusyCursor));
}

void mxsnapshot::procTime()
{
    int i = ui->progressBar->value() + 1;
    if (i > 100) {
        i = 0;
    }
    ui->progressBar->setValue(i);
}

void mxsnapshot::procDone(int)
{
    timer->stop();
    ui->progressBar->setValue(100);
    setCursor(QCursor(Qt::ArrowCursor));
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
    connect(proc, SIGNAL(finished(int)), SLOT(procDone(int)));
}

//// slots ////

// update output box on Stdout
void mxsnapshot::onStdoutAvailable()
{
    QByteArray output = proc->readAllStandardOutput();
    ui->outputBox->insertPlainText(output);
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
        checkInitrdModules();
        this->hide();

        QString msg = QString(tr("Snapshot will use the following settings:*\n\n"
                         "- Working directory:") + " %1\n" +
                      tr("- Snapshot directory:") + " %2\n" +
                      tr("- Kernel to be used:") + " %3\n%4\n-----\n" +
                      tr("*These settings can be changed by exiting and editing:") + "\n    %5")\
                .arg(work_dir.absolutePath()).arg(snapshot_dir.absolutePath()).arg(kernel_used)\
                .arg(save_message).arg(config_file.fileName());
        QMessageBox msgBox(QMessageBox::NoIcon, tr("Settings"), msg, 0 ,this);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.addButton(tr("Back"), QMessageBox::RejectRole);
        if (msgBox.exec() != QMessageBox::Ok) {
            ui->stackedWidget->setCurrentIndex(0);
            ui->buttonStart->setEnabled(true);
            this->show();
            return;
        }
        int ans = QMessageBox::question(this, tr("Final chance"),
                              tr("Snapshot now has all the information it needs to create an ISO from your running system.") + "\n\n" +
                              tr("It will take some time to finish, depending on the size of the installed system and the capacity of your computer.") + "\n\n" +
                              tr("OK to start?"), QMessageBox::Ok | QMessageBox::Cancel);
        if (ans == QMessageBox::Cancel) {
            return qApp->quit();
        }
        this->show();
        copyNewIso();
        copyFileSystem();
        QString filename = getFilename();
        removeOldPackageDirectory(); // removes the directory of the old package-list directories (that use the same basename)
        ui->outputLabel->clear();
        mkDir(filename);
        savePackageList(filename);

        if (edit_boot_menu == "yes") {
            ans = QMessageBox::question(this, tr("Edit Boot Menu"),
                                  tr("The program will now pause to allow you to edit any files in the work directory. Select Yes to edit the boot menu or select No to bypass this step and continue creating the snapshot."),
                                     QMessageBox::Yes | QMessageBox::No);
            if (ans == QMessageBox::Yes) {
                this->hide();
                QString cmd = gui_editor.fileName() + " " + work_dir.absolutePath() + "/new-iso/boot/isolinux/isolinux.cfg";
                system(cmd.toAscii());
                this->show();
            }
        }
        createIso(filename);
        cleanUp();
        this->hide();
        QMessageBox::information(this, tr("Success"),tr("All finished!"), QMessageBox::Ok);
        this->show();
        ui->buttonStart->setText(tr("< Back"));
        ui->buttonStart->setEnabled(true);

    // on output page
    } else if (ui->stackedWidget->currentWidget() == ui->outputPage) {
        ui->stackedWidget->setCurrentIndex(0);
        // restore Start button
        ui->buttonStart->setText(tr("Next"));
        ui->buttonStart->setIcon(QIcon("/usr/share/mx-snapshot/icons/dialog-ok.png"));
        ui->outputBox->clear();
    } else {
        return qApp->quit();
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
