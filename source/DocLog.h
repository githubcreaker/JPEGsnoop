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

#include <cstdint>
#include <functional>
#include <QString>
#include <QVector>
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

  // As AddLine() and the others, but the line's text is made by fnTxt only if
  // the line is shown: one held back (HoldStart()) or past the limit
  // (LimitStart()) mostly isn't, and making their text was most of the time
  // past the limit (DHTs of empty tables: 7.9 billion instructions a MB, and
  // 0.51 with the text not made). fnTxt is called later, so it must
  // not read anything that can change before then: it captures values, never
  // `this`, by reference or by default.
  using TxtFn = std::function<QString()>;

  void AddLineF(TxtFn fnTxt);
  void AddLineHdrF(TxtFn fnTxt);
  void AddLineHdrDescF(TxtFn fnTxt);
  void AddLineWarnF(TxtFn fnTxt);
  void AddLineErrF(TxtFn fnTxt);

  void Clear();

  void Enable() { m_bEn = true; }
  void Disable() { m_bEn = false; }

  // Hold back the lines added from now on instead of showing them, until
  // HoldStop(); then show them, or drop them, counting the errors and warnings
  // among them. (Apart from Enable() and Disable(): while the log is
  // disabled, no line is held. See CjfifDecode::LengthRunBefore().)
  void HoldStart();
  void HoldStop() { m_bHold = false; }
  void HeldShow();
  void HeldDrop(uint32_t &nErr, uint32_t &nWarn);

  // Show at most nMax of the lines added from now on, and nMaxChars of their
  // characters, until LimitStop(). The rest aren't shown: the last of them
  // since the last LimitMark() after which any weren't, at most LIMIT_TAIL,
  // are kept for LimitTailShow(), and the others counted. LimitPause() stops
  // it as LimitStop() does, and LimitResume() goes on with what was left to
  // show. (A line held back by HoldStart() counts when it is shown. See
  // CjfifDecode::MarkerListEnd().)
  static constexpr uint32_t LIMIT_TAIL = 50;

  void LimitStart(uint32_t nMax, qsizetype nMaxChars);
  void LimitMark();
  void LimitStop(uint32_t &nCut, uint32_t &nErr, uint32_t &nWarn, uint32_t &nTail);
  void LimitPause(uint32_t &nCut, uint32_t &nErr, uint32_t &nWarn, uint32_t &nTail);
  void LimitResume();
  void LimitTailShow();

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

  // A line, with its text, or what makes it (fnTxt) if it isn't made yet
  struct CLine
  {
    QString strTxt;
    TxtFn fnTxt;
    teLineKind eKind = LINE_NORMAL;
  };

  static QColor lineColor(teLineKind eKind, bool bDark);
  bool isDark() const;
  void appendLine(CLine line);
  QString rTrim(const QString& str);

  QPlainTextEdit *m_pDoc;
  bool m_bEn;

  bool m_bHold;
  QVector<CLine> m_aHeld;

  void LimitCount(teLineKind eKind);
  void LimitDrop();

  bool m_bLimit;
  bool m_bLimitPaused;        // By LimitPause(), until LimitResume()
  uint32_t m_nLimitLeft;      // Lines still to be shown, and characters
  qsizetype m_nLimitCharsLeft;
  uint32_t m_nLimitCut;       // Lines not shown, nor kept: all, errors, warnings
  uint32_t m_nLimitCutErr;
  uint32_t m_nLimitCutWarn;
  bool m_bLimitMark;          // LimitMark() was called: the next line not shown drops m_aTail
  QVector<CLine> m_aTail;     // The last lines not shown, since LimitMark()
};

#endif
