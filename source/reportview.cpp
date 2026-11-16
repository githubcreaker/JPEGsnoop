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

#include <QMimeData>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>

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
}

QMimeData *CReportView::createMimeDataFromSelection() const
{
  return new CReportMimeData(textCursor().selection(), document()->defaultFont());
}
