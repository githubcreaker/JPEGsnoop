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

#ifndef _GENERAL_H_
#define _GENERAL_H_

#include <QString>
#include <QStringList>

// General Global Functions
QString Dec2Bin(uint32_t nVal, uint32_t nLen, bool bSpace = true);
uint16_t Swap16(uint16_t nVal);
QString Uint2Chars(uint32_t nVal);
QString Uint2DotByte(uint32_t nVal);
bool TestBit(uint32_t nVal, uint32_t nBit);
//QString ByteStr2Unicode(quint8* pBuf, uint32_t nBufLen);
bool Str2Uint32(QString strVal, uint32_t nBase, uint32_t & nVal);
bool SameFile(const QString &strPath1, const QString &strPath2);

// What a file written by strPath would replace (for one renamed into place,
// as a QSaveFile is, with a link of that name removed first): the entry of
// that name in its folder, as an id to compare with PathEntries()'. Empty if
// there is none.
QString EntryId(const QString &strPath);
// The entries that what strPath leads to depends on, as EntryId()'s: each
// link it goes through, in its folders' names too, and the file (or folder)
// at the end. A file written by one of them changes what strPath leads to;
// by any other name, it doesn't. For a path that leads nowhere, the links up
// to where it stops.
QStringList PathEntries(const QString &strPath);

// Lets the window repaint during long work, such as decoding a file, without
// handling user input, which could start other work in the middle of it.
// WorkEventsRunning() is true meanwhile: a file handed to the app from outside
// it (dropped on the report, or sent by Finder) waits until the work is done.
void ProcessWorkEvents();
bool WorkEventsRunning();

// Printable ASCII (0x20-0x7E). Use this rather than isprint(), which accepts
// bytes above 0x7F in a UTF-8 locale (Qt sets the user's locale at startup);
// master's Windows build showed those bytes as '.' or left them out.
inline bool IsPrintAscii(uint32_t nVal)
{
  return (nVal >= 0x20) && (nVal < 0x7F);
}
//bool Uni2AscBuf(quint8* pBuf,QString strIn,uint32_t nMaxBytes,uint32_t &nOffsetBytes);

// General Global Constants
extern const uint32_t glb_anZigZag[64];
extern const uint32_t glb_anUnZigZag[64];
extern const uint32_t glb_anQuantRotate[64];
extern const uint32_t glb_anStdQuantLum[64];
extern const uint32_t glb_anStdQuantChr[64];

#endif
