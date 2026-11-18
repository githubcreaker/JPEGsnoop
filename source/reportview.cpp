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

#include <algorithm>

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QUrl>

#include "DocLog.h"
#include "reportview.h"

namespace
{

// A copy of the report on the clipboard, as plain text and as HTML, each made
// only when an app asks for it (as Qt's own copy does: a long report's HTML
// takes a while). As in the RTF Windows copies, the HTML keeps the report's
// fixed-width font, so its columns stay lined up when pasted into a mail or
// document, and gives the coloured lines their colours for a light background
// whatever the appearance: Dark Mode's paler colours are hard to read on a
// white page.
class CReportMimeData : public QMimeData
{
public:
  CReportMimeData(const QTextDocumentFragment &fragment, const QFont &font) :
    m_fragment(fragment),
    m_font(font)
  {
  }

  QStringList formats() const override
  {
    return { QStringLiteral("text/plain"), QStringLiteral("text/html") };
  }

protected:
  QVariant retrieveData(const QString &strType, QMetaType eType) const override
  {
    QString strData;

    if(strType == QLatin1String("text/plain"))
    {
      strData = m_fragment.toPlainText();
    }
    else if(strType == QLatin1String("text/html"))
    {
      QTextDocument doc;
      QTextCursor cursor(&doc);
      cursor.insertFragment(m_fragment);
      CDocLog::RecolorDocument(&doc, false);

      // The font's family on the text itself: the HTML of a part of a
      // document leaves out the document's own font
      QTextCharFormat format;
      format.setFontFamilies(QStringList(m_font.family()));
      cursor.select(QTextCursor::Document);
      cursor.mergeCharFormat(format);

      strData = QTextDocumentFragment(&doc).toHtml();
    }
    else
    {
      return QMimeData::retrieveData(strType, eType);
    }

    if(eType.id() == QMetaType::QByteArray)
    {
      return strData.toUtf8();
    }
    return strData;
  }

private:
  const QTextDocumentFragment m_fragment;
  const QFont m_font;
};

// The first file in a drag, or "" if it has none. On Windows the report took
// files dropped from Explorer (DragAcceptFiles()), not text. It is taken only
// as a copy, as on Windows, so whatever it came from never moves the file.
QString draggedFile(const QDropEvent *pEvent)
{
  if(!(pEvent->possibleActions() & Qt::CopyAction))
  {
    return QString();
  }
  const QMimeData *pData = pEvent->mimeData();
  const QList<QUrl> aUrls = pData ? pData->urls() : QList<QUrl>();
  const auto itFile = std::find_if(aUrls.cbegin(), aUrls.cend(), [](const QUrl &url) { return url.isLocalFile(); });
  return (itFile != aUrls.cend()) ? itFile->toLocalFile() : QString();
}

} // namespace

// Read-only, as on Windows: it can be selected and copied but not typed into.
// (Editable, a key pressed after Select All replaced the whole report, and
// typing, pasting or Undo changed what Save Log and Print wrote.) The caret and
// keyboard selection stay, and no undo history is kept for the lines the
// decoder adds.
CReportView::CReportView(QWidget *pParent) :
  QPlainTextEdit(pParent)
{
  setReadOnly(true);
  setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  setUndoRedoEnabled(false);

  // Files can be dropped on it (see dropEvent())
  setAcceptDrops(true);

  // A new report gets a new search (on Windows a search went on into the
  // next file's report, where its starting point meant nothing)
  connect(this, &QPlainTextEdit::textChanged, this, &CReportView::startNewSearch);
}

// CRichEditView::FindTextSimple in MFC 11 (Visual Studio 2012, which the
// Windows version is built with), with the same rules:
// - A search starts at the selection: forwards, at the character after the
//   selection's start (so the next match can overlap the one selected), or
//   at the caret; backwards, at the last match that starts before it. (MFC
//   asks Rich Edit to search backwards from the character before the
//   selection, and Rich Edit doesn't document which matches that includes;
//   this mirrors the forward rule.)
// - If the first search after Find... (or after one that found nothing)
//   finds nothing, it goes on from the other end of the report as far as
//   where it started, so it can find the match the search started on.
// - Otherwise a search that finds nothing returns false (Windows beeps), and
//   the next one starts again from the selection, so it goes round.
// QTextDocument's search, like Rich Edit's, stays within a line.
bool CReportView::findText(const QString &strFind, bool bDown, bool bCase, bool bWord)
{
  QTextDocument *pDoc = document();
  const QTextCursor cursor = textCursor();
  const int nSelStart = cursor.selectionStart();
  const int nEnd = pDoc->characterCount() - 1;

  if(m_bFirstSearch)
  {
    m_nSearchStart = nSelStart;
    m_bFirstSearch = false;
    m_bWrapAllowed = true;
  }

  QTextDocument::FindFlags eFlags;
  if(bCase)
    eFlags |= QTextDocument::FindCaseSensitively;
  if(bWord)
    eFlags |= QTextDocument::FindWholeWords;
  if(!bDown)
    eFlags |= QTextDocument::FindBackward;

  QTextCursor found;
  if(!strFind.isEmpty())
  {
    // Backwards, find() returns the last match that starts before the position
    if(bDown)
      found = pDoc->find(strFind, cursor.hasSelection() ? qMin(nSelStart + 1, nEnd) : nSelStart, eFlags);
    else
      found = pDoc->find(strFind, nSelStart, eFlags);

    if(found.isNull() && m_bWrapAllowed)
    {
      found = pDoc->find(strFind, bDown ? 0 : nEnd, eFlags);
      if(!found.isNull() && (bDown ? (found.selectionStart() > m_nSearchStart)
                                   : (found.selectionStart() < m_nSearchStart)))
        found = QTextCursor();
    }
  }
  m_bWrapAllowed = false;

  if(found.isNull())
  {
    m_bFirstSearch = true;
    return false;
  }

  setTextCursor(found);
  ensureCursorVisible();
  return true;
}

QMimeData *CReportView::createMimeDataFromSelection() const
{
  return new CReportMimeData(textCursor().selection(), document()->defaultFont());
}

//-----------------------------------------------------------------------------
// A file dropped on the report is opened, as on Windows
// (CJPEGsnoopView::OnDropFiles()): the first one, if several are dropped.
// Text and other drags go to the text edit, which refuses them in a report
// that can't be changed.

void CReportView::dragEnterEvent(QDragEnterEvent *pEvent)
{
  if(draggedFile(pEvent).isEmpty())
  {
    QPlainTextEdit::dragEnterEvent(pEvent);
    return;
  }
  pEvent->setDropAction(Qt::CopyAction);
  pEvent->accept();
}

void CReportView::dragMoveEvent(QDragMoveEvent *pEvent)
{
  if(draggedFile(pEvent).isEmpty())
  {
    QPlainTextEdit::dragMoveEvent(pEvent);
    return;
  }
  pEvent->setDropAction(Qt::CopyAction);
  pEvent->accept();
}

void CReportView::dropEvent(QDropEvent *pEvent)
{
  const QString strFile = draggedFile(pEvent);
  if(strFile.isEmpty())
  {
    QPlainTextEdit::dropEvent(pEvent);
    return;
  }
  pEvent->setDropAction(Qt::CopyAction);
  pEvent->accept();
  emit fileDropped(strFile);
}
