/**********************************************************************
 *  mxsnapshot.cpp
 **********************************************************************
 * Copyright (C) 2015 MX Authors
 *
 * Authors: Adrian
 *          MX & MEPIS Community <http://forum.mepiscommunity.org>
 *
 * This file is part of MX Snapshot.
 *
 * MX Snapshot is free software: you can redistribute it and/or modify
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

//#include <QDebug>

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
    this->setWindowTitle(tr("MX Snapshot"));
    ui->buttonBack->setHidden(true);
    config_file.setFileName("/etc/mx-snapshot.conf");
    QSettings settings(config_file.fileName(), QSettings::IniFormat);

    proc = new QProcess(this);
    timer = new QTimer(this);
    proc->setReadChannel(QProcess::StandardOutput);
    proc->setReadChannelMode(QProcess::MergedChannels);
    ui->stackedWidget->setCurrentIndex(0);
    ui->buttonCancel->setEnabled(true);
    ui->buttonNext->setEnabled(true);
    session_excludes = "";

    // Load settings or use the default value
    snapshot_dir = settings.value("snapshot_dir", "/home/snapshot").toString();    
    snapshot_excludes.setFileName(settings.value("snapshot_excludes", "/usr/lib/mx-snapshot/snapshot_exclude.list").toString());
    initrd_modules_file.setFileName(settings.value("initrd_modules_file", "/usr/lib/mx-snapshot/initrd_modules.list").toString());
    snapshot_persist = settings.value("snapshot_persist", "no").toString();
    snapshot_basename = settings.value("snapshot_basename", "snapshot").toString();
    make_md5sum = settings.value("make_md5sum", "no").toString();
    make_isohybrid = settings.value("make_isohybrid", "yes").toString();
    mksq_opt = settings.value("mksq_opt", "-comp xz").toString();
    edit_boot_menu = settings.value("edit_boot_menu", "no").toString();
    iso_dir = settings.value("iso_dir", "/usr/lib/mx-snapshot/new-iso").toString();
    lib_mod_dir = settings.value("lib_mod_dir", "/lib/modules/").toString();
    gui_editor.setFileName(settings.value("gui_editor", "/usr/bin/leafpad").toString());
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
QString mxsnapshot::getSnapshotSize()
{
    QString size;
    if (snapshot_dir.exists()) {
        QString cmd = QString("find %1 -maxdepth 1 -type f -name '*.iso' -exec du -shc {} + | tail -1 | awk '{print $1}'").arg(snapshot_dir.absolutePath());
        size = getCmdOut(cmd);
        if (size != "" ) {
            return size;
        }
    }
    return "0";
}

// List the info regarding the free space on drives
void mxsnapshot::listDiskSpace()
{
    QString out = QString("Current space on the partition* holding the snapshot directory %1:").arg(snapshot_dir.absolutePath());
    ui->labelCurrentSpace->setText(out);

    QString path;
    if (snapshot_dir.absolutePath() == "/home/snapshot") {
        path = "/home";
    } else {
        path = snapshot_dir.absolutePath();
    }
    QString cmd = QString("df -h  %1 | awk '{printf \"%-8s\\t%-8s\\t%-8s\\t%-8s\\n\",$2,$3,$5,$4}'").arg(path);
    out = "\n" + getCmdOut(cmd) + "\n";
    ui->labelDiskSpace->setText(out);
    ui->labelDiskSpaceHelp->setText(tr("It is recommended that free space ('Avail') be at least equal to the total installed system size ('Used').\n\n"
                                       "      If necessary, you can create more available space\n"
                                       "      by removing previous snapshots and saved copies:\n"
                                       "      %1 snapshots are taking up %2 of disk space.\n").arg(QString::number(getSnapshotCount())).arg(getSnapshotSize()));
}

// Checks if the editor listed in the config file is present
void mxsnapshot::checkEditor()
{
    if (gui_editor.exists()) {
        return;
    }
    QString msg = tr("The graphical text editor is set to %1, but it is not installed. Edit %2 "
                     "and set the gui_editor variable to the editor of your choice. "
                     "(examples: /usr/bin/gedit, /usr/bin/leafpad)\n\n"
                     "Will install leafpad and use it this time.").arg(gui_editor.fileName()).arg(config_file.fileName());
    QMessageBox::information(0, QString::null, msg);
    if (installLeafpad()) {
        gui_editor.setFileName("/usr/bin/leafpad");
    }
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
    ui->outputBox->clear();
    ui->buttonNext->setDisabled(true);
    ui->buttonBack->setDisabled(true);
    this->show();
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

// Installs live-init-mx package
bool mxsnapshot::installLeafpad()
{
    QEventLoop loop;
    ui->outputBox->clear();
    ui->buttonNext->setDisabled(true);
    ui->buttonBack->setDisabled(true);
    this->show();
    ui->stackedWidget->setCurrentWidget(ui->outputPage);
    setConnections(timer, proc);
    connect(proc, SIGNAL(finished(int)), &loop, SLOT(quit()));
    proc->start("apt-get update");
    loop.exec();
    proc->start("apt-get install leafpad");
    loop.exec();
    this->hide();
    if (proc->exitCode() != 0) {
        QMessageBox::critical(0, tr("Error"), tr("Count not install leafpad"));
        return false;
    }
    ui->stackedWidget->setCurrentWidget(ui->settingsPage);
    return true;
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
    work_dir.setPath(getCmdOut("mktemp -d " + snapshot_dir.absolutePath() + "/work-XXXXXXXX"));
}

void mxsnapshot::detectKernels()
{
    kernel_used = getCmdOut("uname -r");
    QString cmd = QString("ls /boot/vmlinuz-* | sed 's|/boot/vmlinuz-||g'| grep -v %1").arg(kernel_used);
    kernels_avail = getCmdOut(cmd);
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
    if (initrd_dir != "") {
        cmd = "rm -r " + mod_dir + "/*";
        getCmdOut2(cmd);
        copyModules(mod_dir + "/" + kernel_used, "/lib/modules/" + kernel_used);
        closeInitrd(initrd_dir, work_dir.absolutePath() + "/new-iso/antiX/initrd.gz");
    }
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
        cleanUp();
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
        file_name = getCmdOut(cmd.toAscii());
        QFile::copy(*it, sub_dir + "/" + file_name);
    }
}

// Create the output filename
QString mxsnapshot::getFilename()
{
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
void mxsnapshot::removeOldPackageDirectory()
{
    QString dir =  work_dir.absolutePath() + "/new-iso/" + snapshot_basename;
    QString cmd = "rm -r " + dir + "*";
    getCmdOut2(cmd);
    ui->outputLabel->setText(tr("Removing old package-list directory: ") + dir);
}

// make working directory using the base filename
void mxsnapshot::mkDir(QString filename)
{
    QDir dir;
    filename.chop(4); //remove ".iso" string
    dir.setPath(work_dir.absolutePath() + "/new-iso/" + filename);
    dir.mkpath(dir.absolutePath());
}

// save package list in working directory
void mxsnapshot::savePackageList(QString filename)
{
    filename.chop(4); //remove .iso
    filename = work_dir.absolutePath() + "/new-iso/" + filename + "/package_list";
    QString cmd = "dpkg -l | grep \"ii\" | awk '{ print $2 }' >" + filename;
    system(cmd.toAscii());
}

void mxsnapshot::createIso(QString filename)
{
    // add exclusions snapshot dir
    addRemoveExclusion(true, snapshot_dir.absolutePath());

    // create an empty fstab file
    QString cmd = QString("touch %1/fstabdummy").arg(snapshot_dir.absolutePath());
    system(cmd.toAscii());
    // mount empty fstab file
    cmd = QString("mount --bind %1/fstabdummy /etc/fstab").arg(snapshot_dir.absolutePath());
    system(cmd.toAscii());

    // squash the filesystem copy
    QDir::setCurrent(work_dir.absolutePath());
    cmd = "mksquashfs / new-iso/antiX/linuxfs " + mksq_opt + " -wildcards -ef " + snapshot_excludes.fileName() + " " + session_excludes;
    ui->outputLabel->setText(tr("Squashing filesystem..."));
    getCmdOut2(cmd);

    // create the iso file
    QDir::setCurrent(work_dir.absolutePath() + "/new-iso");
    cmd = "genisoimage -l -V MX-14live -R -J -pad -no-emul-boot -boot-load-size 4 -boot-info-table -b boot/isolinux/isolinux.bin -c boot/isolinux/isolinux.cat -o " + snapshot_dir.absolutePath() + "/" + filename + " .";
    ui->outputLabel->setText(tr("Creating CD/DVD image file..."));
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

    // umount empty fstab file
    cmd = QString("umount /etc/fstab");
    system(cmd.toAscii());
    // remove dummy fstab file
    cmd = "rm " + snapshot_dir.absolutePath() + "/fstabdummy";
    system(cmd.toAscii());

}

// clean up changes before exit
void mxsnapshot::cleanUp()
{
    ui->stackedWidget->setCurrentWidget(ui->outputPage);
    QDir::setCurrent("/");
    ui->outputLabel->setText(tr("Cleaning..."));
    if (work_dir.exists() && work_dir.absolutePath() != "/") {
        system("rm -rf " + work_dir.absolutePath().toAscii());
    }

    // remove linux-init-mx
    if (snapshot_persist == "yes") {
        ui->outputLabel->setText(tr("Removing live-init-mx"));
        getCmdOut2("apt-get purge linux-init-mx");
    }
    ui->outputLabel->clear();
}

void mxsnapshot::addRemoveExclusion(bool add, QString exclusion)
{
    exclusion.remove(0, 1); // remove training slash
    if (add) {
        if ( session_excludes == "" ) {
            session_excludes.append("-e " + exclusion + " ");
        } else {
            session_excludes.append(" " + exclusion + " ");
        }
    } else {
        session_excludes.remove(" " + exclusion + " ");
        if ( session_excludes == "-e" ) {
            session_excludes = "";
        }
    }
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


// Next button clicked
void mxsnapshot::on_buttonNext_clicked()
{
    // on first page
    if (ui->stackedWidget->currentIndex() == 0) {
        this->setWindowTitle(tr("Settings"));
        ui->stackedWidget->setCurrentWidget(ui->settingsPage);
        ui->buttonBack->setHidden(false);
        ui->buttonBack->setEnabled(true);
        if (edit_boot_menu == "yes") {
            checkEditor();
        }
        if (snapshot_persist == "yes") {
              if (!checkInstalled("live-init-mx")) {
                ui->stackedWidget->setCurrentWidget(ui->outputPage);
                installLiveInitMx();
                ui->buttonNext->setEnabled(true);
                ui->buttonBack->setEnabled(true);
                ui->stackedWidget->setCurrentWidget(ui->settingsPage);
              }
        }
        detectKernels();
        checkInitrdModules();
        ui->stackedWidget->setCurrentWidget(ui->settingsPage);
        ui->label_1->setText(tr("Snapshot will use the following settings:*"));

        ui->label_2->setText(QString("\n" + tr("- Snapshot directory:") + " %1\n" +
                       tr("- Kernel to be used:") + " %2\n").arg(snapshot_dir.absolutePath()).arg(kernel_used));
        ui->label_3->setText(tr("*These settings can be changed by editing: ") + config_file.fileName());

    // on settings page
    } else if (ui->stackedWidget->currentWidget() == ui->settingsPage) {
        int ans = QMessageBox::question(this, tr("Final chance"),
                              tr("Snapshot now has all the information it needs to create an ISO from your running system.") + "\n\n" +
                              tr("It will take some time to finish, depending on the size of the installed system and the capacity of your computer.") + "\n\n" +
                              tr("OK to start?"), QMessageBox::Ok | QMessageBox::Cancel);
        if (ans == QMessageBox::Cancel) {
            return;
        }
        checkDirectories();
        ui->buttonNext->setEnabled(false);
        ui->buttonBack->setEnabled(false);
        ui->stackedWidget->setCurrentWidget(ui->outputPage);
        this->setWindowTitle(tr("Output"));
        copyNewIso();
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
        QMessageBox::information(this, tr("Success"),tr("All finished!"), QMessageBox::Ok);
        ui->buttonBack->setEnabled(true);
    } else {
        return qApp->quit();
    }
}

void mxsnapshot::on_buttonBack_clicked()
{
    this->setWindowTitle(tr("MX Snapshot"));
    ui->stackedWidget->setCurrentIndex(0);
    ui->buttonNext->setEnabled(true);
    ui->buttonBack->setDisabled(true);
    ui->outputBox->clear();
}

void mxsnapshot::on_buttonEditConfig_clicked()
{
    this->hide();
    checkEditor();
    system((gui_editor.fileName() + " /etc/mx-snapshot.conf").toAscii());
    setup();
    this->show();
}

void mxsnapshot::on_buttonEditExclude_clicked()
{
    this->hide();
    checkEditor();
    system((gui_editor.fileName() + " /usr/lib/mx-snapshot/snapshot_exclude.list").toAscii());
    this->show();
}

void mxsnapshot::on_excludeDocuments_toggled(bool checked)
{
    QString exclusion = "/home/*/Documents/*";
    addRemoveExclusion(checked, exclusion);
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void mxsnapshot::on_excludeDownloads_toggled(bool checked)
{
    QString exclusion = "/home/*/Downloads/*";
    addRemoveExclusion(checked, exclusion);
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void mxsnapshot::on_excludePictures_toggled(bool checked)
{
    QString exclusion = "/home/*/Pictures/*";
    addRemoveExclusion(checked, exclusion);
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void mxsnapshot::on_excludeMusic_toggled(bool checked)
{
    QString exclusion = "/home/*/Music/*";
    addRemoveExclusion(checked, exclusion);
    if (!checked) {
        ui->excludeAll->setChecked(false);
    }
}

void mxsnapshot::on_excludeVideos_toggled(bool checked)
{
    QString exclusion = "/home/*/Videos/*";
    addRemoveExclusion(checked, exclusion);
    if (!checked) {
        ui->excludeAll->setChecked(false);
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
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>", 0, this);
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

// Select snapshot directory
void mxsnapshot::on_buttonSelectSnapshot_clicked()
{
    QFileDialog dialog;
    QDir selected = dialog.getExistingDirectory(0, tr("Select Snapshot Directory"), QString(), QFileDialog::ShowDirsOnly);
    if (selected.exists()) {
        snapshot_dir.setPath(selected.absolutePath() + "/snapshot");
        ui->labelSnapshot->setText(tr("The snapshot will be placed in ") + snapshot_dir.absolutePath());
        listDiskSpace();
    }
}
