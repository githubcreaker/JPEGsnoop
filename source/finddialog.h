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
// - Edit > Find's dialog, as on Windows (the common Find dialog): the text to
//   find, Match whole word only, Match case and the direction. It stays open
//   while searching (it is modeless); Find Next searches and Cancel closes it.
//
// ==========================================================================

#ifndef FINDDIALOG_H
#define FINDDIALOG_H

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QRadioButton;

class CFindDialog : public QDialog
{
  Q_OBJECT

public:
  explicit CFindDialog(QWidget *pParent = nullptr);

  // Fills in the fields, with the text selected and focused
  void setOptions(const QString &strFind, bool bDown, bool bCase, bool bWord);

signals:
  // Find Next was pressed (it can't be while the text is empty)
  void findNext(const QString &strFind, bool bDown, bool bCase, bool bWord);

private:
  QLineEdit *m_pFind;
  QCheckBox *m_pWord;
  QCheckBox *m_pCase;
  QRadioButton *m_pUp;
  QRadioButton *m_pDown;
  QPushButton *m_pFindNext;
};

#endif // FINDDIALOG_H
