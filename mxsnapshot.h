/**********************************************************************
 *  mxsnapshot.h
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


#ifndef MXSNAPSHOT_H
#define MXSNAPSHOT_H

#include <QMessageBox>
#include <QProcess>
#include <QTimer>

#include <QDir>
#include <QTime>

namespace Ui {
class mxsnapshot;
}

class mxsnapshot : public QDialog
{
    Q_OBJECT
    
protected:
    QProcess *proc;
    QTimer *timer;

public:
    explicit mxsnapshot(QWidget *parent = 0);
    ~mxsnapshot();

    QString getCmdOut(QString cmd);
    QString getVersion(QString name);
    void setup();

    QDir error_log;
    QDir work_dir;
    QDir snapshot_dir;
    QString save_work;
    QFile snapshot_excludes;
    QFile initrd_modules_file;
    QString snapshot_persist;
    QString kernel_image;
    QString initrd_image;
    QTime stamp;
    QString snapshot_basename;
    QString make_md5sum;
    QString make_isohybrid;
    QString edit_boot_menu;
    QString iso_dir;
    QDir lib_mod_dir;
    QDir ata_dir;


public slots:
    void procStart();
    void procTime();
    void procDone(int exitCode);
    void setConnections(QTimer* timer, QProcess* proc);
    void onStdoutAvailable();

private slots:
    void on_buttonStart_clicked();
    void on_buttonAbout_clicked();
    void on_buttonHelp_clicked();

private:
    Ui::mxsnapshot *ui;    
};

#endif // MXSNAPSHOT_H

