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
// - The report in the main window: read-only text that can be selected and
//   copied (Edit > Select All and Copy), as on Windows
//
// ==========================================================================

#ifndef REPORTVIEW_H
#define REPORTVIEW_H

#include <QPlainTextEdit>

class CReportView : public QPlainTextEdit
{
  Q_OBJECT

public:
  explicit CReportView(QWidget *pParent = nullptr);

  // Edit > Find and Find Next: selects the next match of strFind after the
  // selection's start (or before it, with bDown false) and returns true, or
  // returns false if there is none. As in MFC's rich edit view on Windows,
  // the first search that finds nothing goes on from the other end of the
  // report, as far as where the search started; after that, reaching the end
  // returns false once and the next search goes round again.
  bool findText(const QString &strFind, bool bDown, bool bCase, bool bWord);

  // Starts a new search from the selection: Find... does this, and so does a
  // change to the report
  void startNewSearch() { m_bFirstSearch = true; }

protected:
  // The selection as plain text and HTML, the HTML with the coloured lines in
  // their colours for a light background
  QMimeData *createMimeDataFromSelection() const override;

private:
  bool m_bFirstSearch = true;     // the next search starts a new one
  bool m_bWrapAllowed = false;    // it may go on from the other end
  int m_nSearchStart = 0;         // where the search started
};

#endif // REPORTVIEW_H
