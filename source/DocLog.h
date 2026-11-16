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
// - Simple wrapper for document log routines
//
// ==========================================================================

#ifndef DOCLOG_H
#define DOCLOG_H

#include <QString>
#include <QPlainTextEdit>

class CDocLog
{
public:
  CDocLog();
  CDocLog(QPlainTextEdit *pDoc);
  ~CDocLog(void);

  void setDoc(QPlainTextEdit *pDoc);

  void AddLine(QString str);
  void AddLineHdr(QString str);
  void AddLineHdrDesc(QString str);
  void AddLineWarn(QString str);
  void AddLineErr(QString str);
  void AddLineGood(QString str);

  void Clear();

  void Enable() { m_bEn = true; }
  void Disable() { m_bEn = false; }

  // Re-colour the report after a light/dark appearance change
  void RecolorForPalette();

  // Give the coloured lines of a report document their colours for a dark or
  // light background (also used for a copy of the report on the clipboard)
  static void RecolorDocument(QTextDocument *pDoc, bool bDark);

private:
  CDocLog &operator = (const CDocLog&);
  CDocLog(CDocLog&);

  // What a line is; its colour depends on the current light/dark appearance
  enum teLineKind
  {
    LINE_NORMAL,
    LINE_HDR,
    LINE_HDR_DESC,
    LINE_WARN,
    LINE_ERR,
    LINE_GOOD
  };

  static QColor lineColor(teLineKind eKind, bool bDark);
  bool isDark() const;
  void appendToLog(QString strTxt, teLineKind eKind);
  QString rTrim(const QString& str);

  QPlainTextEdit *m_pDoc;
  bool m_bEn;
};

#endif
