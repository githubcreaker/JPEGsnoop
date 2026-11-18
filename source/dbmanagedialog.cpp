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

#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "dbmanagedialog.h"

namespace
{

// Each row keeps its entry's place in the file
constexpr int kEntryRole = Qt::UserRole;

QString sourceText(teEditor eEditor)
{
  if(eEditor == ENUM_EDITOR_CAM)
    return CDbManageDialog::tr("Camera");
  if(eEditor == ENUM_EDITOR_SW)
    return CDbManageDialog::tr("Software");
  if(eEditor == ENUM_EDITOR_UNSURE)
    return CDbManageDialog::tr("Unknown");
  // (A number other than 1 to 3 in the file)
  return CDbManageDialog::tr("Not set");
}

// A field as listed: a very long one (a pasted name) by its start
QString shownText(const QString &str)
{
  return (str.size() <= 200) ? str : (str.left(200) + QChar(0x2026));
}

QString entriesText(int nNum)
{
  return (nNum == 1) ? CDbManageDialog::tr("1 entry") : CDbManageDialog::tr("%1 entries").arg(nNum);
}

} // namespace

// The Windows dialog had a list of each entry's make, model and quality (a
// software name was listed as the make), Remove, Remove All, OK and Cancel.
// This one lists every field but the rotated signature, can remove several
// entries at once, and says where the database is. Its widgets have object
// names for the test driver.
CDbManageDialog::CDbManageDialog(const QList<CompSig> &aEntries, const QString &strDbPath,
                                 const QString &strLoadError, QWidget *pParent) :
  QDialog(pParent),
  m_aEntries(aEntries),
  m_strNoEntries(strDbPath.isEmpty()       ? tr("There are no entries.")
                 : strLoadError.isEmpty() ? tr("There are no entries. Tools > Add Camera/SW to DB adds them.")
                                          : tr("No entries could be read.")),
  m_pList(new QTreeWidget),
  m_pRemove(new QPushButton(tr("Remove"))),
  m_pRemoveAll(new QPushButton(tr("Remove All"))),
  m_pCount(new QLabel)
{
  setObjectName(QStringLiteral("manageDbDialog"));
  setWindowTitle(tr("Manage User Database"));

  m_pList->setObjectName(QStringLiteral("entries"));
  m_pList->setHeaderLabels({ tr("Source"), tr("Make"), tr("Model"), tr("Software"), tr("Quality"),
                             tr("Subsampling"), tr("Signature") });
  m_pList->setRootIsDecorated(false);
  m_pList->setUniformRowHeights(true);
  m_pList->setAllColumnsShowFocus(true);
  m_pList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  for(qsizetype nInd = 0; nInd < m_aEntries.size(); nInd++)
  {
    const CompSig &sEntry = m_aEntries.at(nInd);
    QTreeWidgetItem *pItem = new QTreeWidgetItem(
      m_pList, { sourceText(sEntry.eEditor), shownText(sEntry.strXMake), shownText(sEntry.strXModel),
                 shownText(sEntry.strMSwDisp), shownText(sEntry.strUmQual), shownText(sEntry.strXSubsamp),
                 shownText(sEntry.strCSig) });
    pItem->setData(0, kEntryRole, static_cast<qlonglong>(nInd));
  }

  int nWidth = 0;
  for(int nCol = 0; nCol < m_pList->columnCount(); nCol++)
  {
    m_pList->resizeColumnToContents(nCol);
    nWidth += m_pList->columnWidth(nCol);
  }
  m_pList->setMinimumSize(
    qMin(nWidth + (2 * m_pList->frameWidth()) + m_pList->verticalScrollBar()->sizeHint().width(), 900), 200);
  m_pList->installEventFilter(this);

  m_pRemove->setObjectName(QStringLiteral("remove"));
  m_pRemoveAll->setObjectName(QStringLiteral("removeAll"));
  m_pCount->setObjectName(QStringLiteral("count"));
  // Return is OK, wherever the focus is
  m_pRemove->setAutoDefault(false);
  m_pRemoveAll->setAutoDefault(false);
  connect(m_pRemove, &QPushButton::clicked, this, &CDbManageDialog::onRemove);
  connect(m_pRemoveAll, &QPushButton::clicked, this, &CDbManageDialog::onRemoveAll);
  connect(m_pList, &QTreeWidget::itemSelectionChanged, this, &CDbManageDialog::updateState);

  QHBoxLayout *pRemoveRow = new QHBoxLayout;
  pRemoveRow->addWidget(m_pRemove);
  pRemoveRow->addWidget(m_pRemoveAll);
  pRemoveRow->addSpacing(12);
  pRemoveRow->addWidget(m_pCount, 1);

  QString strNote;
  if(strDbPath.isEmpty())
  {
    strNote = tr("No folder is set for the user database (Options > Configuration), so it has no entries.");
  }
  else
  {
    strNote = tr("Remove takes the entries chosen out of this list, and OK takes them out of the file. The "
                 "signatures built into JPEGsnoop aren't listed.\n\nUser database: %1")
                .arg(QDir::toNativeSeparators(strDbPath));
    if(!strLoadError.isEmpty())
    {
      strNote += "\n\n" + tr("NOTE: User database %1. Only the entries read are listed, and if any are removed, "
                             "the file is kept as %2 first.")
                            .arg(strLoadError, QDir::toNativeSeparators(strDbPath + QStringLiteral(".bak")));
    }
  }
  QLabel *pNote = new QLabel(strNote);
  pNote->setObjectName(QStringLiteral("dbNote"));
  pNote->setWordWrap(true);
  pNote->setTextInteractionFlags(Qt::TextSelectableByMouse);

  QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pButtons->setObjectName(QStringLiteral("buttonBox"));
  connect(pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  QVBoxLayout *pLayout = new QVBoxLayout(this);
  pLayout->addWidget(m_pList, 1);
  pLayout->addLayout(pRemoveRow);
  pLayout->addWidget(pNote);
  pLayout->addWidget(pButtons);

  updateState();
  m_pList->setFocus();
}

QList<CompSig> CDbManageDialog::removed() const
{
  QList<bool> abListed(m_aEntries.size(), false);
  for(int nRow = 0; nRow < m_pList->topLevelItemCount(); nRow++)
    abListed[m_pList->topLevelItem(nRow)->data(0, kEntryRole).toLongLong()] = true;

  QList<CompSig> aRemoved;
  for(qsizetype nInd = 0; nInd < m_aEntries.size(); nInd++)
  {
    if(!abListed.at(nInd))
      aRemoved.append(m_aEntries.at(nInd));
  }
  return aRemoved;
}

// Delete or Backspace in the list is Remove
bool CDbManageDialog::eventFilter(QObject *pObj, QEvent *pEvent)
{
  if((pObj == m_pList) && (pEvent->type() == QEvent::KeyPress))
  {
    const int nKey = static_cast<QKeyEvent *>(pEvent)->key();
    if((nKey == Qt::Key_Delete) || (nKey == Qt::Key_Backspace))
    {
      onRemove();
      return true;
    }
  }
  return QDialog::eventFilter(pObj, pEvent);
}

// The entries chosen; the row after them is chosen next (Windows chose none)
void CDbManageDialog::onRemove()
{
  const QList<QTreeWidgetItem *> apChosen = m_pList->selectedItems();
  if(apChosen.isEmpty())
    return;

  int nLast = 0;
  for(QTreeWidgetItem *pItem : apChosen)
    nLast = qMax(nLast, m_pList->indexOfTopLevelItem(pItem));
  const int nNext = nLast + 1 - static_cast<int>(apChosen.size());

  qDeleteAll(apChosen);
  const int nRows = m_pList->topLevelItemCount();
  if(nRows > 0)
    m_pList->setCurrentItem(m_pList->topLevelItem(qMin(nNext, nRows - 1)));
  m_pList->setFocus();
  updateState();
}

void CDbManageDialog::onRemoveAll()
{
  m_pList->clear();
  updateState();
}

void CDbManageDialog::updateState()
{
  const int nRows = m_pList->topLevelItemCount();
  const int nRemoved = static_cast<int>(m_aEntries.size()) - nRows;

  m_pRemove->setEnabled(!m_pList->selectedItems().isEmpty());
  m_pRemoveAll->setEnabled(nRows > 0);

  if(m_aEntries.isEmpty())
    m_pCount->setText(m_strNoEntries);
  else if(nRemoved == 0)
    m_pCount->setText(entriesText(nRows));
  else
    m_pCount->setText(tr("%1, %2 removed: OK takes %3 out of the file.")
                        .arg(entriesText(nRows))
                        .arg(nRemoved)
                        .arg((nRemoved == 1) ? tr("it") : tr("them")));
}
