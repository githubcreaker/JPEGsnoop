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

// ==========================================================================
// CLASS DESCRIPTION:
// - Tools > Manage Local DB's dialog, as on Windows ("Manage User Database"):
//   the user database's entries, which Remove and Remove All take out of the
//   list. The caller takes the entries removed out of the file on OK.
//
// ==========================================================================

#ifndef DBMANAGEDIALOG_H
#define DBMANAGEDIALOG_H

#include <QDialog>
#include <QList>

#include "DbSigs.h"

class QLabel;
class QPushButton;
class QTreeWidget;

class CDbManageDialog : public QDialog
{
  Q_OBJECT

public:
  // aEntries: the user database's entries, in the file's order; strDbPath:
  // its file (empty if no folder is set); strLoadError: why it couldn't be
  // read in full, if it couldn't
  CDbManageDialog(const QList<CompSig> &aEntries, const QString &strDbPath, const QString &strLoadError,
                  QWidget *pParent = nullptr);

  // The entries taken out of the list, in the file's order
  QList<CompSig> removed() const;

protected:
  bool eventFilter(QObject *pObj, QEvent *pEvent) override;

private slots:
  void onRemove();
  void onRemoveAll();
  void updateState();

private:
  QList<CompSig> m_aEntries;
  QString m_strNoEntries;       // what the count says when there are none
  QTreeWidget *m_pList;
  QPushButton *m_pRemove;
  QPushButton *m_pRemoveAll;
  QLabel *m_pCount;
};

#endif // DBMANAGEDIALOG_H
