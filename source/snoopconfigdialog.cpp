// JPEGsnoop - JPEG Image Decoder & Analysis Utility
// Copyright (C) 2018 - Calvin Hass
// http://www.impulseadventure.com/photo/jpeg-snoop.html
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <QFileDialog>
#include <QIntValidator>
#include <QStandardPaths>

#include "SnoopConfig.h"

#include "snoopconfigdialog.h"
#include "ui_snoopconfigdialog.h"

SnoopConfigDialog::SnoopConfigDialog(CSnoopConfig *pAppConfig, QWidget *parent) : QDialog(parent), ui(new Ui::SnoopConfigDialog), m_pAppConfig(pAppConfig)
{
  ui->setupUi(this);
  connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(onSave()));
  connect(ui->buttonBox, SIGNAL(rejected()), this, SLOT(onCancel()));

  connect(ui->btnBrowse, SIGNAL(released()), this, SLOT(onBrowse()));
  connect(ui->btnDefault, SIGNAL(released()), this, SLOT(onDefault()));
  // The coach-message Reset button (onReset used to be wired to Default)
  connect(ui->btnReset, SIGNAL(released()), this, SLOT(onReset()));

  // Whole numbers only
  ui->leMaxErrors->setValidator(new QIntValidator(0, 99999, this));

  ui->leDbPath->setText(m_pAppConfig->dbPath());

  bAutoReprocess = m_pAppConfig->autoReprocess();
  ui->cbAutoReprocess->setChecked(bAutoReprocess);
  maxDecodeErrors = m_pAppConfig->maxDecodeError();
  ui->leMaxErrors->setText(QString("%1").arg(maxDecodeErrors));
}

SnoopConfigDialog::~SnoopConfigDialog()
{
  delete ui;
}

void SnoopConfigDialog::onBrowse()
{
  // A folder picker (ShowDirsOnly alone still asked for a file)
  QString strStart = ui->leDbPath->text();

  if(strStart.isEmpty())
  {
    strStart = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  }

  const QString strDir = QFileDialog::getExistingDirectory(this, tr("Directory for User Database"), strStart);

  if(!strDir.isEmpty())
  {
    dbPath = strDir;
    ui->leDbPath->setText(dbPath);
  }
}

void SnoopConfigDialog::onDefault()
{
  ui->leDbPath->setText(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}

void SnoopConfigDialog::onReset()
{
  // Re-enable all coach messages
  m_pAppConfig->CoachReset();
}

void SnoopConfigDialog::onSave()
{
  bool bOk;
  int32_t nVal;

  m_pAppConfig->setDbPath(ui->leDbPath->text());
  m_pAppConfig->setAutoReprocess(ui->cbAutoReprocess->isChecked());

  nVal = ui->leMaxErrors->text().toInt(&bOk);

  if(bOk)
  {
    m_pAppConfig->setMaxErrors(nVal);
  }

  // Save now rather than only when the main window closes
  m_pAppConfig->Dirty();
  m_pAppConfig->RegistryStore();

  accept();
}

void SnoopConfigDialog::onCancel()
{
  reject();
}
