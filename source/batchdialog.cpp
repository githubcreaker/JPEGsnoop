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

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "batchdialog.h"

namespace
{

// A folder field's text as a path: "~" is the home folder
QString folderPath(const QString &strText)
{
  QString strPath = strText.trimmed();
  if((strPath == QLatin1String("~")) || strPath.startsWith(QLatin1String("~/")))
    strPath = QDir::homePath() + strPath.mid(1);
  return strPath.isEmpty() ? strPath : QDir::cleanPath(strPath);
}

} // namespace

// The Windows dialog's fields and wording, with "folder" for "directory" as
// macOS says. Its widgets have object names for the test driver.
CBatchDialog::CBatchDialog(const QString &strInput, const QString &strOutput, QWidget *pParent) :
  QDialog(pParent),
  m_pInput(new QLineEdit(QDir::toNativeSeparators(strInput))),
  m_pOutput(new QLineEdit(QDir::toNativeSeparators(strOutput))),
  m_pSubfolders(new QCheckBox(tr("Process subfolders?"))),
  m_pExtractAll(new QCheckBox(tr("Extract all JPEGs")))
{
  setObjectName(QStringLiteral("batchDialog"));
  setWindowTitle(tr("Batch Processing"));
  m_pInput->setObjectName(QStringLiteral("inputFolder"));
  m_pOutput->setObjectName(QStringLiteral("outputFolder"));
  m_pSubfolders->setObjectName(QStringLiteral("processSubfolders"));
  m_pExtractAll->setObjectName(QStringLiteral("extractAll"));
  m_pInput->setMinimumWidth(m_pInput->fontMetrics().horizontalAdvance(QLatin1Char('x')) * 50);

  QLabel *pNote1 = new QLabel(tr("NOTE 1: Recommended for use on a backup copy of the files only"));
  QLabel *pNote2 = new QLabel(tr("NOTE 2: For faster operation, turn off Options > Scan Segment > Decode Image "
                                 "before use"));
  pNote2->setWordWrap(true);

  QPushButton *pBrowseInput = new QPushButton(tr("Choose..."));
  pBrowseInput->setObjectName(QStringLiteral("browseInput"));
  pBrowseInput->setAutoDefault(false);
  QPushButton *pBrowseOutput = new QPushButton(tr("Choose..."));
  pBrowseOutput->setObjectName(QStringLiteral("browseOutput"));
  pBrowseOutput->setAutoDefault(false);

  QGridLayout *pFolders = new QGridLayout;
  pFolders->addWidget(new QLabel(tr("Input Image Folder:")), 0, 0, 1, 2);
  pFolders->addWidget(m_pInput, 1, 0);
  pFolders->addWidget(pBrowseInput, 1, 1);
  pFolders->addWidget(new QLabel(tr("Output Log Folder:")), 2, 0, 1, 2);
  pFolders->addWidget(m_pOutput, 3, 0);
  pFolders->addWidget(pBrowseOutput, 3, 1);

  QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pButtons->setObjectName(QStringLiteral("buttonBox"));

  QVBoxLayout *pLayout = new QVBoxLayout(this);
  pLayout->addWidget(pNote1);
  pLayout->addWidget(pNote2);
  pLayout->addSpacing(8);
  pLayout->addLayout(pFolders);
  pLayout->addSpacing(8);
  pLayout->addWidget(m_pSubfolders);
  pLayout->addWidget(m_pExtractAll);
  pLayout->addWidget(pButtons);
  pLayout->setSizeConstraint(QLayout::SetFixedSize);

  connect(pBrowseInput, &QPushButton::clicked, this, [this]() { browse(true); });
  connect(pBrowseOutput, &QPushButton::clicked, this, [this]() { browse(false); });
  connect(pButtons, &QDialogButtonBox::accepted, this, [this]() {
    if(acceptFields())
      accept();
  });
  connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// Choose a folder. As on Windows, choosing the input folder also makes it the
// output folder, so the logs go beside the images unless another is chosen.
void CBatchDialog::browse(bool bInput)
{
  QLineEdit *pEdit = bInput ? m_pInput : m_pOutput;
  QString strStart = folderPath(pEdit->text());
  if(!QFileInfo(strStart).isDir())
    strStart = QDir::homePath();

  QFileDialog dlg(this, bInput ? tr("Select input image folder") : tr("Select output log folder"), strStart);
  dlg.setObjectName(QStringLiteral("batchFolderDialog"));
  dlg.setFileMode(QFileDialog::Directory);
  dlg.setOption(QFileDialog::ShowDirsOnly, true);

  if((dlg.exec() != QDialog::Accepted) || dlg.selectedFiles().isEmpty())
    return;

  const QString strPath = QDir::toNativeSeparators(dlg.selectedFiles().first());
  pEdit->setText(strPath);
  if(bInput)
    m_pOutput->setText(strPath);
}

// Checks the fields and keeps them for settings(); says what is wrong and
// returns false if they can't be used. (Master accepted anything: with an
// input folder that didn't exist it found nothing and said it was done.)
// The output folder is made when the batch starts, if it doesn't exist.
bool CBatchDialog::acceptFields()
{
  const QString strInput = folderPath(m_pInput->text());
  const QString strOutput = folderPath(m_pOutput->text());
  const QFileInfo fiInput(strInput);
  const QFileInfo fiOutput(strOutput);
  const QString strReadError = fiInput.isDir() ? folderReadError(strInput) : QString();
  QString strError;

  if(strInput.isEmpty())
    strError = tr("Choose the folder of images to process.");
  else if(fiInput.isRelative())
    strError = tr("The input folder must be a full path, such as ~/Pictures.");
  else if(!fiInput.exists())
    strError = tr("The input folder doesn't exist: \"%1\".").arg(QDir::toNativeSeparators(strInput));
  else if(!fiInput.isDir())
    strError = tr("The input folder is a file: \"%1\".").arg(QDir::toNativeSeparators(strInput));
  else if(!strReadError.isEmpty())
    strError = tr("Couldn't read the input folder \"%1\": %2.").arg(QDir::toNativeSeparators(strInput), strReadError);
  else if(strOutput.isEmpty())
    strError = tr("Choose a folder for the logs.");
  else if(fiOutput.isRelative())
    strError = tr("The output folder must be a full path, such as ~/Pictures.");
  else if(fiOutput.exists() && !fiOutput.isDir())
    strError = tr("The output folder is a file: \"%1\".").arg(QDir::toNativeSeparators(strOutput));

  if(!strError.isEmpty())
  {
    QMessageBox::warning(this, windowTitle(), strError);
    return false;
  }

  m_sSettings.strInput = strInput;
  m_sSettings.strOutput = strOutput;
  m_sSettings.bSubfolders = m_pSubfolders->isChecked();
  m_sSettings.bExtractAll = m_pExtractAll->isChecked();
  return true;
}
