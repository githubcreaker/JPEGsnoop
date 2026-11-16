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

#include <QDebug>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QVector>
#include "DocLog.h"

//
// Initialize the log
//
CDocLog::CDocLog(void)
{
  m_pDoc = nullptr;
  m_bEn = true;     // master enabled the log by default; was left uninitialised
}

CDocLog::CDocLog(QPlainTextEdit *pDoc)
{
  m_pDoc = pDoc;
  m_bEn = true;
}

CDocLog::~CDocLog(void)
{
}

void CDocLog::Clear()
{
  m_pDoc->clear();
}

void CDocLog::setDoc(QPlainTextEdit *pDoc)
{
  m_pDoc = pDoc;
}

// Colour for each kind of line. Normal text has no colour of its own: it uses
// the report view's palette, so it follows the light/dark appearance. The
// others have one colour for light backgrounds (master's colours, with the
// error red darkened for contrast) and one for dark backgrounds. All give at
// least 4.5:1 contrast against a white or near-black (#1E1E1E) background.
QColor CDocLog::lineColor(teLineKind eKind, bool bDark)
{
  switch(eKind)
  {
    case LINE_HDR:
      return bDark ? QColor(108, 160, 255) : QColor(1, 1, 255);
    case LINE_HDR_DESC:
      return bDark ? QColor(138, 180, 255) : QColor(32, 32, 255);
    case LINE_WARN:
      return bDark ? QColor(255, 138, 128) : QColor(128, 1, 1);
    case LINE_ERR:
      return bDark ? QColor(255, 92, 92) : QColor(204, 0, 0);
    case LINE_GOOD:
      return bDark ? QColor(107, 217, 107) : QColor(16, 128, 16);
    case LINE_NORMAL:
    default:
      return QColor();
  }
}

// Is the report view currently showing a dark background?
bool CDocLog::isDark() const
{
  return m_pDoc && (m_pDoc->palette().color(QPalette::Base).lightness() < 128);
}

void CDocLog::appendToLog(QString strTxt, teLineKind eKind)
{
  if(m_bEn)
  {
    QTextCharFormat format;

    // (Normal text used to be RGB(1,1,1): invisible on a Dark Mode background)
    if(eKind != LINE_NORMAL)
    {
      format.setForeground(QBrush(lineColor(eKind, isDark())));
    }

    m_pDoc->setCurrentCharFormat(format);
//    m_pDoc->appendPlainText(rTrim(strTxt));
    m_pDoc->appendPlainText(strTxt);
//    qDebug() << strTxt;
  }
}

// After a light/dark change, swap each coloured line to the colour for the
// new appearance. Normal text has no colour of its own and follows by itself.
void CDocLog::RecolorForPalette()
{
  if(m_pDoc)
  {
    RecolorDocument(m_pDoc->document(), isDark());
  }
}

// Give each coloured line of pDoc its colour for a dark (bDark) or light
// background
void CDocLog::RecolorDocument(QTextDocument *pDoc, bool bDark)
{
  // Find the runs first: changing a format can merge fragments, which would
  // upset an iteration over them
  struct sRecolor
  {
    int nPos;
    int nLen;
    QColor col;
  };
  QVector<sRecolor> runs;

  for(QTextBlock block = pDoc->begin(); block.isValid(); block = block.next())
  {
    for(QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
    {
      const QTextFragment frag = it.fragment();
      const QBrush brush = frag.charFormat().foreground();

      if(!frag.isValid() || (brush.style() == Qt::NoBrush))
      {
        continue;
      }

      for(teLineKind eKind : { LINE_HDR, LINE_HDR_DESC, LINE_WARN, LINE_ERR, LINE_GOOD })
      {
        if(brush.color() == lineColor(eKind, !bDark))
        {
          runs.append({ frag.position(), frag.length(), lineColor(eKind, bDark) });
          break;
        }
      }
    }
  }

  if(runs.isEmpty())
  {
    return;
  }

  // Recolouring isn't an edit, so keep it off the undo stack. (Turning undo
  // off clears the stack, which only holds lines the report appended.)
  const bool bUndo = pDoc->isUndoRedoEnabled();
  pDoc->setUndoRedoEnabled(false);

  QTextCursor cursor(pDoc);
  cursor.beginEditBlock();

  for(const sRecolor &run : runs)
  {
    QTextCharFormat format;
    format.setForeground(QBrush(run.col));
    cursor.setPosition(run.nPos);
    cursor.setPosition(run.nPos + run.nLen, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(format);
  }

  cursor.endEditBlock();
  pDoc->setUndoRedoEnabled(bUndo);
}

// Add a basic text line to the log
void CDocLog::AddLine(QString strTxt)
{
  appendToLog(strTxt, LINE_NORMAL);
}

// Add a header text line to the log
void CDocLog::AddLineHdr(QString strTxt)
{
  appendToLog(strTxt, LINE_HDR);
}

// Add a header description text line to the log
void CDocLog::AddLineHdrDesc(QString strTxt)
{
  appendToLog(strTxt, LINE_HDR_DESC);
}

// Add a warning text line to the log
void CDocLog::AddLineWarn(QString strTxt)
{
  appendToLog(strTxt, LINE_WARN);
}

// Add an error text line to the log
void CDocLog::AddLineErr(QString strTxt)
{
  appendToLog(strTxt, LINE_ERR);
}

// Add a "good" indicator text line to the log
void CDocLog::AddLineGood(QString strTxt)
{
  appendToLog(strTxt, LINE_GOOD);
}

QString CDocLog::rTrim(const QString& str)
{
  int n = str.size() - 1;

  for (; n >= 0; --n)
  {
    if (!str.at(n).isSpace())
    {
      return str.left(n + 1);
    }
  }

  return "";
}
