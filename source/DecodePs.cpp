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

#include<cwchar>
#include <algorithm>
#include <vector>

#include "DecodePs.h"
#include "General.h"
#include "snoop.h"
//#include "JPEGsnoop.h"          // for m_pAppConfig get
#include "SnoopConfig.h"

#include "WindowBuf.h"

// Forward declarations
//struct tsIptcField    asIptcFields[];
//struct tsBimRecord    asBimRecords[];
//struct tsBimEnum      asBimEnums[];

// ===============================================================================
// CONSTANTS
// ===============================================================================

// Define all of the currently-known IPTC fields, used when we
// parse the APP13 marker
//
// - Reference: IPTC-NAA Information Interchange Model Version 4
// - See header for struct encoding
struct tsIptcField asIptcFields[] = {
  {1, 0, IPTC_T_NUM2, "Model Version"},
  {1, 5, IPTC_T_STR, "Destination"},
  {1, 20, IPTC_T_NUM2, "File Format"},
  {1, 22, IPTC_T_NUM2, "File Format Version"},
  {1, 30, IPTC_T_STR, "Service Identifier"},
  {1, 40, IPTC_T_STR, "Envelope Number"},
  {1, 50, IPTC_T_STR, "Product I.D."},
  {1, 60, IPTC_T_STR, "Envelope Priority"},
  {1, 70, IPTC_T_STR, "Date Sent"},
  {1, 80, IPTC_T_STR, "Time Sent"},
  {1, 90, IPTC_T_STR, "Coded Character Set"},
  {1, 100, IPTC_T_STR, "UNO"},
  {1, 120, IPTC_T_NUM2, "ARM Identifier"},
  {1, 122, IPTC_T_NUM2, "ARM Version"},
  {2, 0, IPTC_T_NUM2, "Record Version"},
  {2, 3, IPTC_T_STR, "Object Type Reference"},
  {2, 4, IPTC_T_STR, "Object Attrib Reference"},
  {2, 5, IPTC_T_STR, "Object Name"},
  {2, 7, IPTC_T_STR, "Edit Status"},
  {2, 8, IPTC_T_STR, "Editorial Update"},
  {2, 10, IPTC_T_STR, "Urgency"},
  {2, 12, IPTC_T_STR, "Subject Reference"},
  {2, 15, IPTC_T_STR, "Category"},
  {2, 20, IPTC_T_STR, "Supplemental Category"},
  {2, 22, IPTC_T_STR, "Fixture Identifier"},
  {2, 25, IPTC_T_STR, "Keywords"},
  {2, 26, IPTC_T_STR, "Content Location Code"},
  {2, 27, IPTC_T_STR, "Content Location Name"},
  {2, 30, IPTC_T_STR, "Release Date"},
  {2, 35, IPTC_T_STR, "Release Time"},
  {2, 37, IPTC_T_STR, "Expiration Date"},
  {2, 38, IPTC_T_STR, "Expiration Time"},
  {2, 40, IPTC_T_STR, "Special Instructions"},
  {2, 42, IPTC_T_STR, "Action Advised"},
  {2, 45, IPTC_T_UNK, "Reference Service"},
  {2, 47, IPTC_T_UNK, "Reference Date"},
  {2, 50, IPTC_T_UNK, "Reference Number"},
  {2, 55, IPTC_T_STR, "Date Created"},
  {2, 60, IPTC_T_STR, "Time Created"},
  {2, 62, IPTC_T_STR, "Digital Creation Date"},
  {2, 63, IPTC_T_STR, "Digital Creation Time"},
  {2, 65, IPTC_T_STR, "Originating Program"},
  {2, 70, IPTC_T_STR, "Program Version"},
  {2, 75, IPTC_T_STR, "Object Cycle"},
  {2, 80, IPTC_T_STR, "By-line"},
  {2, 85, IPTC_T_STR, "By-line Title"},
  {2, 90, IPTC_T_STR, "City"},
  {2, 92, IPTC_T_STR, "Sub-location"},
  {2, 95, IPTC_T_STR, "Province/State"},
  {2, 100, IPTC_T_STR, "Country/Primary Location Code"},
  {2, 101, IPTC_T_STR, "Country/Primary Location Name"},
  {2, 103, IPTC_T_STR, "Original Transmission Reference"},
  {2, 105, IPTC_T_STR, "Headline"},
  {2, 110, IPTC_T_STR, "Credit"},
  {2, 115, IPTC_T_STR, "Source"},
  {2, 116, IPTC_T_STR, "Copyright Notice"},
  {2, 118, IPTC_T_STR, "Contact"},
  {2, 120, IPTC_T_STR, "Caption/Abstract"},
  {2, 122, IPTC_T_STR, "Writer/Editor"},
  {2, 125, IPTC_T_UNK, "Rasterized Caption"},
  {2, 130, IPTC_T_STR, "Image Type"},
  {2, 131, IPTC_T_STR, "Image Orientation"},
  {2, 135, IPTC_T_STR, "Language Identifier"},
  {2, 150, IPTC_T_STR, "Audio Type"},
  {2, 151, IPTC_T_STR, "Audio Sampling Rate"},
  {2, 152, IPTC_T_STR, "Audio Sampling Resolution"},
  {2, 153, IPTC_T_STR, "Audio Duration"},
  {2, 154, IPTC_T_STR, "Audio Outcue"},
  {2, 200, IPTC_T_NUM2, "ObjectData Preview File Format"},
  {2, 201, IPTC_T_NUM2, "ObjectData Preview File Format Version"},
  {2, 202, IPTC_T_UNK, "ObjectData Preview Data"},
  {7, 10, IPTC_T_NUM1, "Size Mode"},
  {7, 20, IPTC_T_NUM, "Max Subfile Size"},
  {7, 90, IPTC_T_NUM1, "ObjectData Size Announced"},
  {7, 95, IPTC_T_NUM, "Maximum ObjectData Size"},
  {8, 10, IPTC_T_UNK, "Subfile"},
  {9, 10, IPTC_T_NUM, "Confirmed ObjectData Size"},
  {0, 0, IPTC_T_END, "DONE"},
};

// Adobe Photoshop File Formats Specification (October 2013)
// - Image Resource Blocks IRB (8BIM)
// - See header for struct encoding
struct tsBimRecord asBimRecords[] = {
  {0x03E8, 0x0000, BIM_T_UNK, "-"},
  {0x03E9, 0x0000, BIM_T_HEX, "Macintosh print manager print info record"},
  {0x03EB, 0x0000, BIM_T_HEX, "Indexed color table"},
  {0x03ED, 0x0000, BIM_T_PS_RESOLUTION_INFO, "ResolutionInfo structure"},
  {0x03EE, 0x0000, BIM_T_HEX, "Names of alpha channels"},
  {0x03EF, 0x0000, BIM_T_HEX, "DisplayInfo structure"},
  {0x03F0, 0x0000, BIM_T_HEX, "Caption"},
  {0x03F1, 0x0000, BIM_T_HEX, "Border information"},
  {0x03F2, 0x0000, BIM_T_HEX, "Background color"},
  {0x03F3, 0x0000, BIM_T_PS_PRINT_FLAGS, "Print flags"},
  {0x03F4, 0x0000, BIM_T_HEX, "Grayscale and multichannel halftoning information"},
  {0x03F5, 0x0000, BIM_T_HEX, "Color halftoning information"},
  {0x03F6, 0x0000, BIM_T_HEX, "Duotone halftoning information"},
  {0x03F7, 0x0000, BIM_T_HEX, "Grayscale and multichannel transfer function"},
  {0x03F8, 0x0000, BIM_T_HEX, "Color transfer functions"},
  {0x03F9, 0x0000, BIM_T_HEX, "Duotone transfer functions"},
  {0x03FA, 0x0000, BIM_T_HEX, "Duotone image information"},
  {0x03FC, 0x0000, BIM_T_HEX, "-"},
  {0x03FD, 0x0000, BIM_T_HEX, "EPS options"},
  {0x03FE, 0x0000, BIM_T_HEX, "Quick Mask information"},
  {0x03FF, 0x0000, BIM_T_HEX, "-"},
  {0x0400, 0x0000, BIM_T_PS_LAYER_STATE_INFO, "Layer state information"},
  {0x0401, 0x0000, BIM_T_HEX, "Working path"},
  {0x0402, 0x0000, BIM_T_PS_LAYER_GROUP_INFO, "Layers group information"},
  {0x0403, 0x0000, BIM_T_HEX, "-"},
  {0x0404, 0x0000, BIM_T_IPTC_NAA, "IPTC-NAA record"},
  {0x0405, 0x0000, BIM_T_HEX, "Image mode (raw format files)"},
  {0x0406, 0x0000, BIM_T_JPEG_QUAL, "JPEG quality"},
  {0x0408, 0x0000, BIM_T_PS_GRID_GUIDES, "Grid and guides information"},
  {0x0409, 0x0000, BIM_T_HEX, "Thumbnail resource (PS 4.0)"},
  {0x040A, 0x0000, BIM_T_PS_COPYRIGHT_FLAG, "Copyright flag"},
  {0x040B, 0x0000, BIM_T_HEX, "URL"},
  {0x040C, 0x0000, BIM_T_PS_THUMB_RES, "Thumbnail resources"},
  {0x040D, 0x0000, BIM_T_PS_GLOBAL_ANGLE, "Global Angle"},
  {0x040E, 0x0000, BIM_T_HEX, "Color samplers resource"},
  {0x040F, 0x0000, BIM_T_HEX, "ICC Profile"},
  {0x0410, 0x0000, BIM_T_HEX, "Watermark"},
  {0x0411, 0x0000, BIM_T_HEX, "ICC Untagged Profile"},
  {0x0412, 0x0000, BIM_T_HEX, "Effects visible"},
  {0x0413, 0x0000, BIM_T_HEX, "Spot Halftone"},
  {0x0414, 0x0000, BIM_T_PS_DOC_SPECIFIC_SEED, "Document-specific IDs seed number"},
  {0x0415, 0x0000, BIM_T_PS_STR_UNI, "Unicode Alpha Names"},
  {0x0416, 0x0000, BIM_T_HEX, "Indexed Color Table Count"},
  {0x0417, 0x0000, BIM_T_HEX, "Transparency Index"},
  {0x0419, 0x0000, BIM_T_PS_GLOBAL_ALTITUDE, "Global Altitude"},
  {0x041A, 0x0000, BIM_T_PS_SLICES, "Slices"},
  {0x041B, 0x0000, BIM_T_PS_STR_UNI, "Workflow URL"},
  {0x041C, 0x0000, BIM_T_HEX, "Jump to XPEP"},
  {0x041D, 0x0000, BIM_T_HEX, "Alpha Identifiers"},
  {0x041E, 0x0000, BIM_T_HEX, "URL List"},
  {0x0421, 0x0000, BIM_T_PS_VER_INFO, "Version Info"},
  {0x0422, 0x0000, BIM_T_HEX, "Exif data 1"},
  {0x0423, 0x0000, BIM_T_HEX, "Exif data 3"},
  {0x0424, 0x0000, BIM_T_PS_STR_ASC_LONG, "XMP metadata"},
  {0x0425, 0x0000, BIM_T_HEX, "Caption digest"},
  {0x0426, 0x0000, BIM_T_PS_PRINT_SCALE, "Print scale"},
  {0x0428, 0x0000, BIM_T_PS_PIXEL_ASPECT_RATIO, "Pixel Aspect Ratio"},
  {0x0429, 0x0000, BIM_T_HEX, "Layer Comps"},
  {0x042A, 0x0000, BIM_T_HEX, "Alternate Duotone Colors"},
  {0x042B, 0x0000, BIM_T_HEX, "Alternate Spot Colors"},
  {0x042D, 0x0000, BIM_T_PS_LAYER_SELECT_ID, "Layer Selection IDs"},
  {0x042E, 0x0000, BIM_T_HEX, "HDR Toning information"},
  {0x042F, 0x0000, BIM_T_HEX, "Print info"},
  {0x0430, 0x0000, BIM_T_PS_LAYER_GROUP_ENABLED, "Layer Groups Enabled ID"},
  {0x0431, 0x0000, BIM_T_HEX, "Color samplers resource"},
  {0x0432, 0x0000, BIM_T_HEX, "Measurement Scale"},
  {0x0433, 0x0000, BIM_T_HEX, "Timeline Information"},
  {0x0434, 0x0000, BIM_T_HEX, "Sheet Disclosure"},
  {0x0435, 0x0000, BIM_T_HEX, "DisplayInfo structure (FloatPt colors)"},
  {0x0436, 0x0000, BIM_T_HEX, "Onion Skins"},
  {0x0438, 0x0000, BIM_T_HEX, "Count Information"},
  {0x043A, 0x0000, BIM_T_HEX, "Print Information"},
  {0x043B, 0x0000, BIM_T_HEX, "Print Style"},
  {0x043C, 0x0000, BIM_T_HEX, "Macintosh NSPrintInfo"},
  {0x043D, 0x0000, BIM_T_HEX, "Windows DEVMODE"},
  {0x043E, 0x0000, BIM_T_HEX, "Auto Save File Path"},
  {0x043F, 0x0000, BIM_T_HEX, "Auto Save Format"},
  {0x0440, 0x0000, BIM_T_HEX, "Path Selection State"},
  {0x07D0, 0x0BB6, BIM_T_HEX, "Path Information (saved paths)"},
  {0x0BB7, 0x0000, BIM_T_HEX, "Name of clipping path"},
  {0x0BB8, 0x0000, BIM_T_HEX, "Origin Path Info"},
  {0x0FA0, 0x1387, BIM_T_HEX, "Plug-In resources"},
  {0x1B58, 0x0000, BIM_T_HEX, "Image Ready variables"},
  {0x1B59, 0x0000, BIM_T_HEX, "Image Ready data sets"},
  {0x1F40, 0x0000, BIM_T_HEX, "Lightroom workflow"},
  {0x2710, 0x0000, BIM_T_PS_PRINT_FLAGS_INFO, "Print flags information"},
  {0x0000, 0x0000, BIM_T_END, ""},
};

// Adobe Photoshop enumerated constants
// - See header for struct encoding
struct tsBimEnum asBimEnums[] = {
  {BIM_T_ENUM_FILE_HDR_COL_MODE, 0, "Bitmap"},
  {BIM_T_ENUM_FILE_HDR_COL_MODE, 1, "Grayscale"},
  {BIM_T_ENUM_FILE_HDR_COL_MODE, 2, "Indexed"},
  {BIM_T_ENUM_FILE_HDR_COL_MODE, 3, "RGB"},
  {BIM_T_ENUM_FILE_HDR_COL_MODE, 4, "CMYK"},
  {BIM_T_ENUM_FILE_HDR_COL_MODE, 7, "Multichannel"},
  {BIM_T_ENUM_FILE_HDR_COL_MODE, 8, "Duotone"},
  {BIM_T_ENUM_FILE_HDR_COL_MODE, 9, "Lab"},
  {BIM_T_ENUM_RESOLUTION_INFO_RES_UNIT, 1, "pixels per inch"},
  {BIM_T_ENUM_RESOLUTION_INFO_RES_UNIT, 2, "pixels per cm"},
  {BIM_T_ENUM_RESOLUTION_INFO_WIDTH_UNIT, 1, "inch"},
  {BIM_T_ENUM_RESOLUTION_INFO_WIDTH_UNIT, 2, "cm"},
  {BIM_T_ENUM_RESOLUTION_INFO_WIDTH_UNIT, 3, "picas"},
  {BIM_T_ENUM_RESOLUTION_INFO_WIDTH_UNIT, 4, "columns"},
  {BIM_T_ENUM_PRINT_SCALE_STYLE, 0, "centered"},
  {BIM_T_ENUM_PRINT_SCALE_STYLE, 1, "size to fit"},
  {BIM_T_ENUM_PRINT_SCALE_STYLE, 2, "user defined"},
  {BIM_T_ENUM_GRID_GUIDE_DIR, 0, "vertical"},
  {BIM_T_ENUM_GRID_GUIDE_DIR, 1, "horizontal"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'pass', "pass through"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'norm', "normal"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'diss', "dissolve"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'dark', "darken"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'mul ', "multiply"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'idiv', "color burn"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'lbrn', "linear burn"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'dkCl', "darker color"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'lite', "lighten"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'scrn', "screen"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'div ', "color dodge"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'lddg', "linear dodge"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'lgCl', "lighter color"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'over', "overlay"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'sLit', "soft light"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'hLit', "hard light"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'vLit', "vivid light"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'lLit', "linear light"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'pLit', "pin light"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'hMix', "hard mix"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'diff', "difference"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'smud', "exclusion"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'fsub', "subtract"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'fdiv', "divide"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'hue ', "hue"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'sat ', "saturation"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'colr', "color"},
  {BIM_T_ENUM_BLEND_MODE_KEY, 'lum ', "luminosity"},
  {BIM_T_ENUM_END, 0, "???"},
};

CDecodePs::CDecodePs(CwindowBuf *pWBuf, CDocLog *pLog, CSnoopConfig *pAppConfig) : m_pWBuf(pWBuf), m_pLog(pLog), m_pAppConfig(pAppConfig)
{
  // HACK
  // Only select a single layer to decode into the DIB
  // FIXME: Need to allow user control over this setting
  m_bDisplayLayer = false;
  m_nDisplayLayerInd = 0;
  m_bDisplayImage = true;

  Reset();
}

void CDecodePs::Reset()
{
  m_bPsd = false;
  m_nQualitySaveAs = 0;
  m_nQualitySaveForWeb = 0;
  m_bAbort = false;
  m_nPosBlockEnd = 0;
  m_bInResBlock = false;
  m_bBlockOverrun = false;
  m_strBlockKind = "8BIM resource";
  m_nDescriptorDepth = 0;
  m_bDibGray = false;
  m_nDepthBpp = 8;
}

CDecodePs::~CDecodePs(void)
{
}

// Provide a short-hand alias for the m_pWBuf buffer
// INPUT:
// - offset                                     File offset to read from
// - bClean                                     Forcibly disables any redirection to Fake DHT table
//
// POST:
// - m_pLog
//
// RETURN:
// - Byte from file
//
uint8_t CDecodePs::Buf(uint32_t offset, bool bClean = false)
{
  return m_pWBuf->Buf(offset, bClean);
}

// Determine if the file is a Photoshop PSD
// If so, parse the headers. Generally want to start at start of file (nPos=0).
// - pDibTemp: where to draw the composite image (nullptr for none). It is left
//   null when the image can't be shown.
bool CDecodePs::DecodePsd(uint32_t nPos, QImage * pDibTemp, uint32_t &nWidth, uint32_t &nHeight)
{
  QString strTmp;
  QString strSig;

  m_bPsd = false;

  uint32_t nVer;

  strSig = m_pWBuf->BufReadStrn(nPos, 4);
  nPos += 4;
  nVer = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);

  if((strSig == "8BPS") && (nVer == 1))
  {
    m_bPsd = true;
    m_pLog->AddLine("");
    m_pLog->AddLineHdr("*** Photoshop PSD File Decoding ***");
    m_pLog->AddLine("Decoding Photoshop format...");
    m_pLog->AddLine("");
  }
  else
  {
    return false;
  }

  // No image unless the image data is decoded
  if(pDibTemp)
  {
    *pDibTemp = QImage();
  }

  // Rewind back to header (signature)
  nPos -= 6;

  bool bDecOk = true;

  tsImageInfo sImageInfo;

  PhotoshopParseFileHeader(nPos, 3, &sImageInfo);
  nWidth = sImageInfo.nImageWidth;
  nHeight = sImageInfo.nImageHeight;
  m_nDepthBpp = sImageInfo.nDepthBpp;

  bDecOk &= PhotoshopParseColorModeSection(nPos, 3);

  if(bDecOk)
    bDecOk &= PhotoshopParseImageResourcesSection(nPos, 3);

  if(bDecOk)
    bDecOk &= PhotoshopParseLayerMaskInfo(nPos, 3);

  if(bDecOk)
  {
    bDecOk &= PhotoshopParseImageData(nPos, 3, &sImageInfo, m_bDisplayImage ? pDibTemp : nullptr);
    PhotoshopParseReportFldOffset(3, "Image data decode complete:", nPos);
  }

  PhotoshopParseReportNote(3, "");

  if(!bDecOk)
  {
    m_pLog->AddLineErr("ERROR: There was a problem during decode. Aborting.");
    return false;
  }

  return true;
}

// Does a field of nLen bytes at nPos end within the file? If not, report it.
// Lengths in a PSD file are 32-bit, so without this a bad one ran the parse
// far past the end (reading zeros, slowly), allocated gigabytes for a row
// table, or wrapped the 32-bit position back to the same block forever.
bool CDecodePs::PsdFits(uint32_t nPos, uint64_t nLen, const QString &strWhat)
{
  const uint64_t nPosEof = m_pWBuf->GetPosEof();

  // (Written so that it can't overflow, whatever nLen is)
  if((nPos <= nPosEof) && (nLen <= nPosEof - nPos))
  {
    return true;
  }

  m_pLog->AddLineErr(QString("ERROR: %1 at [0x%2] runs past the end of the file. Stopping.")
                       .arg(strWhat).arg(nPos, 8, 16, QChar('0')));
  return false;
}

// Does a field of nLen bytes at nPos end within the part of the file that holds
// it (strIn, ending at nPosEnd, exclusive)? If not, report it. Lengths were only
// compared with the file's end, so a part was read on into the one after it:
// a layer's name and tagged blocks from the next layer record.
bool CDecodePs::PsdFitsIn(uint32_t nPos, uint64_t nLen, uint64_t nPosEnd, const QString &strWhat, const QString &strIn)
{
  // (Where the file ends first, as before)
  if(nPosEnd >= m_pWBuf->GetPosEof())
  {
    return PsdFits(nPos, nLen, strWhat);
  }

  if((nPos <= nPosEnd) && (nLen <= nPosEnd - nPos))
  {
    return true;
  }

  m_pLog->AddLineErr(QString("ERROR: %1 at [0x%2] runs past the end of %3, at [0x%4]. Stopping.")
                       .arg(strWhat)
                       .arg(nPos, 8, 16, QChar('0'))
                       .arg(strIn)
                       .arg(nPosEnd, 8, 16, QChar('0')));
  return false;
}

// The size of nCount items of nSize bytes, or UINT64_MAX if that overflows (it
// can't fit in a file then). Sizes from 32-bit fields multiplied in 64 bits
// could still wrap to a small number: a small file with a huge raw image
// passed the size check and was then stepped through billions of rows.
static uint64_t PsdSize(uint64_t nCount, uint64_t nSize)
{
  if((nSize != 0) && (nCount > UINT64_MAX / nSize))
  {
    return UINT64_MAX;
  }

  return nCount * nSize;
}

// Bytes in one row of a channel: a 1-bit image packs 8 pixels in a byte, and
// 16- and 32-bit samples take 2 and 4 bytes. (Counting a byte per pixel made
// a 16-bit RLE image fail on its first row.)
uint64_t CDecodePs::PsdRowBytes(uint32_t nWidth) const
{
  return (static_cast<uint64_t>(nWidth) * m_nDepthBpp + 7) / 8;
}

// Locate a field ID in the constant 8BIM / Image Resource array
// 
// INPUT:
// - nBimId                             = Image Resource ID (16-bit unsigned)
// OUTPUT:
// - nFldInd                    = Index into asBimRecords[] array
// RETURN:
// - Returns true if ID was found in array
// NOTE:
// - The constant struct array depends on BIM_T_END as a terminator for the list
//
bool CDecodePs::FindBimRecord(uint32_t nBimId, uint32_t &nFldInd)
{
  uint32_t nInd = 0;

  bool bDone = false;
  bool bFound = false;

  while(!bDone)
  {
    if(asBimRecords[nInd].eBimType == BIM_T_END)
    {
      bDone = true;
    }
    else
    {
      // Support detection of code ranges
      uint32_t nCodeStart = asBimRecords[nInd].nCode;

      uint32_t nCodeEnd = asBimRecords[nInd].nCodeEnd;

      if((nCodeEnd == 0) && (nCodeStart == nBimId))
      {
        // Match with single code (ie. not ranged entry)
        bDone = true;
        bFound = true;
      }
      else if((nCodeEnd != 0) && (nCodeStart <= nBimId) && (nCodeEnd >= nBimId))
      {
        // Match with code in ranged entry
        bDone = true;
        bFound = true;
      }
      else
      {
        nInd++;
      }
    }
  }

  if(bFound)
  {
    nFldInd = nInd;
  }

  return bFound;
}

// Search the constant array of IPTC fields for the Record:DataSet index
//
// OUTPUT:
// - nFldInd  = The index into the array of the located entry (if found)
// RETURN
// - Successfully found the Record:DataSet value
//
bool CDecodePs::LookupIptcField(uint32_t nRecord, uint32_t nDataSet, uint32_t &nFldInd)
{
  uint32_t nInd = 0;

  bool bDone = false;
  bool bFound = false;

  while(!bDone)
  {
    if(asIptcFields[nInd].eFldType == IPTC_T_END)
    {
      bDone = true;
    }
    else
    {
      if((asIptcFields[nInd].nRecord == nRecord) && (asIptcFields[nInd].nDataSet == nDataSet))
      {
        bDone = true;
        bFound = true;
      }
      else
      {
        nInd++;
      }
    }
  }

  if(bFound)
  {
    nFldInd = nInd;
  }

  return bFound;
}

// Generate the custom-formatted string representing the IPTC field name and value
//
// INPUT:
// - eIptcType                          = Enumeration representing the IPTC field type
// - nFldCnt                            = The number of elements in this field to report
// - nPos                                       = The starting file offset for this field
// RETURN:
// - The IPTC formattted string
// NOTE:
// - The IPTC field type is used to determine how to represent the input values
//
QString CDecodePs::DecodeIptcValue(teIptcType eIptcType, uint32_t nFldCnt, uint32_t nPos)
{
  //uint32_t      nFldInd = 0;
  uint32_t nInd;
  uint32_t nVal;

  QString strField = "";
  QString strByte = "";
  QString strVal = "";

  switch (eIptcType)
  {
    case IPTC_T_NUM:
    case IPTC_T_NUM1:
    case IPTC_T_NUM2:
      if((nFldCnt == 1) || (nFldCnt == 2) || (nFldCnt == 4))
      {
        nVal = m_pWBuf->BufX(nPos, nFldCnt, PS_BSWAP);
        nPos += nFldCnt;
        nPos += nFldCnt;
        strVal = QString("%1").arg(nVal);
      }
      else
      {
        // The size comes from the file; BufX() only reads 1, 2 or 4 bytes.
        // Show other sizes as hex (at most 16 bytes) instead.
        strVal = "[";

        for(nInd = 0; (nInd < nFldCnt) && (nInd < 16); nInd++)
        {
          strVal += QString("0x%1 ").arg(Buf(nPos + nInd), 2, 16, QChar('0'));
        }

        strVal += (nFldCnt > 16) ? "...]" : "]";
        nPos += nFldCnt;
      }
      break;

    case IPTC_T_HEX:
      strVal = "[";

      for(nInd = 0; nInd < nFldCnt; nInd++)
      {
        strByte = QString("0x%1 ").arg(Buf(nPos++), 2, 16, QChar('0'));
        strVal += strByte;
      }

      strVal += "]";
      break;

    case IPTC_T_STR:
      strVal = "\"";

      for(nInd = 0; nInd < nFldCnt; nInd++)
      {
        strVal += QChar(Buf(nPos++));     // the character, not its code
      }

      strVal += "\"";
      break;

    case IPTC_T_UNK:
      strVal = "???";

    default:
      break;
  }

  return strVal;
}

// Decode the IPTC metadata segment
// INPUT:
//      nLen    : Length of the 8BIM:IPTC resource data
void CDecodePs::DecodeIptc(uint32_t &nPos, uint32_t nLen, uint32_t nIndent)
{
  QString strIndent;
  QString strTmp;
  QString strIptcTypeName;
  QString strIptcField;
  QString strIptcVal;
  QString strByte;

  uint32_t nPosStart;

  bool bDone;

  uint32_t nTagMarker, nRecordNumber, nDataSetNumber, nDataFieldCnt;

  strIndent = PhotoshopParseIndent(nIndent);
  nPosStart = nPos;

  // Where the resource ends: a field's header and its data are kept inside it.
  // (Its 16-bit length was used as it was: a 6-byte resource read a caption of
  // 65,535 bytes from past its end.)
  const quint64 nPosEnd = quint64(nPosStart) + nLen;

  bDone = (nPos >= nPosEnd);

  while(!bDone)
  {
    nTagMarker = Buf(nPos + 0);

    if(nTagMarker != 0x1C)
    {
      // Unknown Tag Marker
      // I have seen at least one JPEG file that had an IPTC block with all zeros.
      // In this example, the TagMarker check for 0x1C would fail.
      // Since we don't know how to parse it, abort now.
      strTmp = QString("ERROR: Unknown IPTC TagMarker [0x%1] @ 0x%2. Skipping parsing.")
          .arg(nTagMarker, 2, 16, QChar('0'))
          .arg(nPos, 8, 16, QChar('0'));
      m_pLog->AddLineErr(strTmp);

#ifdef DEBUG_LOG
      QString strDebug;

      strDebug = QString("## File=[%1] Block=[%2] Error=[%3]\n")
          .arg(m_pAppConfig->strCurFname, -100)
          .arg("PsDecode", -10)
          .arg(strTmp);
      qDebug() << (strDebug);
#endif

      // Adjust length to end, and abort
      nPos = static_cast<uint32_t>(nPosEnd);
      break;
    }

    if(nPosEnd - nPos < 5)
    {
      strTmp = QString("ERROR: IPTC field header at [0x%1] runs past the end of its 8BIM resource, at [0x%2]. Stopping.")
          .arg(nPos, 8, 16, QChar('0'))
          .arg(nPosEnd, 8, 16, QChar('0'));
      m_pLog->AddLineErr(strTmp);
      nPos = static_cast<uint32_t>(nPosEnd);
      break;
    }

    nRecordNumber = Buf(nPos + 1);
    nDataSetNumber = Buf(nPos + 2);
    nDataFieldCnt = Buf(nPos + 3) * 256 + Buf(nPos + 4);
    nPos += 5;

    if(nDataFieldCnt > nPosEnd - nPos)
    {
      const uint32_t nAvail = static_cast<uint32_t>(nPosEnd - nPos);

      strTmp = QString("ERROR: IPTC field length [%1] at [0x%2] runs past the end of its 8BIM resource, at [0x%3]; using [%4]")
          .arg(nDataFieldCnt)
          .arg(nPos, 8, 16, QChar('0'))
          .arg(nPosEnd, 8, 16, QChar('0'))
          .arg(nAvail);
      m_pLog->AddLineErr(strTmp);
      nDataFieldCnt = nAvail;
    }

    uint32_t nFldInd;

    teIptcType eIptcType;

    if(LookupIptcField(nRecordNumber, nDataSetNumber, nFldInd))
    {
      strIptcField = QString("%1").arg(asIptcFields[nFldInd].strFldName, -35);
      eIptcType = asIptcFields[nFldInd].eFldType;
    }
    else
    {
      strIptcField = QString("%1").arg("?", -35);
      eIptcType = IPTC_T_UNK;
    }

    strIptcVal = DecodeIptcValue(eIptcType, nDataFieldCnt, nPos);
    // The indent has its own placeholder (it took %1 and dropped the value)
    strTmp = QString("%1IPTC [%2:%3] %4 = %5").arg(strIndent)
        .arg(nRecordNumber, 3, 10, QChar('0'))
        .arg(nDataSetNumber, 3, 10, QChar('0'))
        .arg(strIptcField)
        .arg(strIptcVal);
    m_pLog->AddLine(strTmp);
    nPos += nDataFieldCnt;

    bDone = (nPos >= nPosEnd);
  }
}

// Create indent string
// - Returns a sequence of spaces according to the indent level
QString CDecodePs::PhotoshopParseIndent(uint32_t nIndent)
{
  QString strIndent = "";

  for(uint32_t nInd = 0; nInd < nIndent; nInd++)
  {
    strIndent += "  ";
  }

  return strIndent;
}

// Get ASCII string according to Photoshop format
// - String length prefix
// - If length is 0 then fixed 4-character string
// - Otherwise it is defined length string (no terminator required)
// How far a length read from the file may move the cursor at nPos: while a
// resource block is being parsed, no further than its end (nothing once nPos
// is at or past it), and never so far that the 32-bit position wraps. Lengths
// were added unchecked, so 0xFFFFFFF8 moved the cursor back 8 bytes and a
// descriptor repeated the same item for every one of its 2^28 declared items.
uint32_t CDecodePs::LimitAdvance(uint32_t nPos, uint64_t nLen) const
{
  uint64_t nMax = 0xFFFFFFFFull - nPos;

  if(m_bInResBlock)
  {
    nMax = (nPos < m_nPosBlockEnd) ? (m_nPosBlockEnd - nPos) : 0;
  }

  return static_cast<uint32_t>((nLen < nMax) ? nLen : nMax);
}

// Whether the resource block being parsed has nLen more bytes at nPos
// (always true outside a block)
bool CDecodePs::BlockRoom(uint32_t nPos, uint64_t nLen) const
{
  return !m_bInResBlock || ((nPos <= m_nPosBlockEnd) && (nLen <= m_nPosBlockEnd - nPos));
}

// A field at nPos doesn't fit in its resource block: report it (once per
// block) and move nPos to the block's end, which also ends the parsing loops.
// (Reading on used to take the field from whatever followed the block.)
void CDecodePs::BlockOverrun(uint32_t &nPos, const QString &strWhat)
{
  if(!m_bInResBlock)
  {
    return;
  }

  if(!m_bBlockOverrun)
  {
    m_pLog->AddLineErr(QString("ERROR: %1 at [0x%2] runs past the end of its %3, at [0x%4]. Stopping.")
                         .arg(strWhat)
                         .arg(nPos, 8, 16, QChar('0'))
                         .arg(m_strBlockKind)
                         .arg(m_nPosBlockEnd, 8, 16, QChar('0')));
    m_bBlockOverrun = true;
  }

  if(nPos < m_nPosBlockEnd)
  {
    nPos = m_nPosBlockEnd;
  }
}

// Whether a field of nLen bytes at nPos is in the block. If not, it is reported
// and nPos moved to the block's end (see BlockOverrun()), and the parser stops:
// fixed-size fields were read from whatever followed the block (the next
// resource's "8BIM", as a thumbnail 943,868,237 pixels wide).
bool CDecodePs::PsNeed(uint32_t &nPos, uint64_t nLen, const QString &strWhat)
{
  if(BlockRoom(nPos, nLen))
  {
    return true;
  }

  BlockOverrun(nPos, strWhat);
  return false;
}

QString CDecodePs::PhotoshopParseGetLStrAsc(uint32_t &nPos)
{
  uint32_t nStrLen;

  QString strVal = "";

  // The length prefix has to be inside the resource too
  if(!BlockRoom(nPos, 4))
  {
    BlockOverrun(nPos, "String");
    return strVal;
  }

  nStrLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  if(nStrLen != 0)
  {
    // The length comes from the file (see LimitAdvance). Show the part that
    // is inside the resource.
    const uint32_t nStrLenAvail = LimitAdvance(nPos, nStrLen);

    strVal = m_pWBuf->BufReadStrn(nPos, nStrLenAvail);
    if(BlockRoom(nPos, nStrLen))
    {
      nPos += nStrLenAvail;
    }
    else
    {
      // To the block's end
      BlockOverrun(nPos, QString("String of length %1").arg(nStrLen));
    }
  }
  else if(!BlockRoom(nPos, 4))
  {
    BlockOverrun(nPos, "String");
  }
  else
  {
    // 4 byte string
    // Four characters (master: %c%c%c%c), not their codes
    strVal = QString(QChar(Buf(nPos + 0))) + QChar(Buf(nPos + 1)) + QChar(Buf(nPos + 2)) + QChar(Buf(nPos + 3));
    nPos += 4;
  }

  return strVal;
}

// Read a UNICODE string starting at nPos according to Photoshop Unicode string format
// - Start with 4 byte length
// - Follow with UNICODE string
// OUTPUT:
// - The byte offset to advance the file pointer
// RETURN:
// - Unicode string
QString CDecodePs::PhotoshopParseGetBimLStrUni(uint32_t nPos, uint32_t &nPosOffset)
{
  QString strVal;

  uint32_t nStrLenActual, nStrLenTrunc;

  // Initialize return
  strVal = "";

  // The length prefix has to be inside the resource too
  nPosOffset = 0;
  if(!BlockRoom(nPos, 4))
  {
    uint32_t nPosEnd = nPos;

    BlockOverrun(nPosEnd, "Unicode string");
    nPosOffset = nPosEnd - nPos;
    return strVal;
  }

  nStrLenActual = m_pWBuf->BufX(nPos + nPosOffset, 4, PS_BSWAP);
  nPosOffset += 4;

  // The length comes from the file, and doubling it could wrap too (see
  // LimitAdvance). Read only the characters inside the resource.
  const uint32_t nStrBytesAvail = LimitAdvance(nPos + nPosOffset, static_cast<uint64_t>(nStrLenActual) * 2);

  nStrLenTrunc = nStrBytesAvail / 2;

  if(nStrLenTrunc > 0)
  {
    // Truncate overly long strings
    if(nStrLenTrunc > PS_MAX_UNICODE_STRLEN)
    {
      nStrLenTrunc = PS_MAX_UNICODE_STRLEN;
    }

    // Big-endian UTF-16 code units, up to a NUL terminator. (Building the
    // QString from the bytes as a C string stopped at the first zero byte,
    // so ASCII text came out as its first character.)
    for(uint32_t nInd = 0; nInd < nStrLenTrunc; nInd++)
    {
      const char16_t nUnit = static_cast<char16_t>((Buf(nPos + nPosOffset + (nInd * 2) + 0) << 8) |
                                                   Buf(nPos + nPosOffset + (nInd * 2) + 1));

      if(nUnit == 0)
      {
        break;
      }

      strVal += QChar(nUnit);
    }
  }

  // Update the file position offset
  if(BlockRoom(nPos + nPosOffset, static_cast<uint64_t>(nStrLenActual) * 2))
  {
    nPosOffset += nStrBytesAvail;
  }
  else
  {
    // To the block's end
    uint32_t nPosEnd = nPos + nPosOffset;

    BlockOverrun(nPosEnd, QString("Unicode string of length %1").arg(nStrLenActual));
    nPosOffset = nPosEnd - nPos;
  }
  // Return
  return strVal;
}

// Display a formatted note line
// - Apply the current indent level (nIndent)
void CDecodePs::PhotoshopParseReportNote(uint32_t nIndent, QString strNote)
{
  QString strIndent;
  QString strLine;

  strIndent = PhotoshopParseIndent(nIndent);
  strLine = QString("%1%2")
      .arg(strIndent)
      .arg(strNote, -50);
  m_pLog->AddLine(strLine);
}

// Display a formatted numeric field with optional units string
// - Report the value with the field name (strField) and current indent level (nIndent)
void CDecodePs::PhotoshopParseReportFldNum(uint32_t nIndent, QString strField, uint32_t nVal, QString strUnits)
{
  QString strIndent;
  QString strVal;
  QString strLine;

  strIndent = PhotoshopParseIndent(nIndent);
  strVal = QString("%1").arg(nVal);
  strLine = QString("%1%2 = %3 %4")
      .arg(strIndent)
      .arg(strField, -50)
      .arg(strVal).arg(strUnits);
  m_pLog->AddLine(strLine);
}

// Display a formatted boolean field
// - Report the value with the field name (strField) and current indent level (nIndent)
void CDecodePs::PhotoshopParseReportFldBool(uint32_t nIndent, QString strField, uint32_t nVal)
{
  QString strIndent;
  QString strVal;
  QString strLine;

  strIndent = PhotoshopParseIndent(nIndent);
  strVal = (nVal != 0) ? "true" : "false";
  strLine = QString("%1%2 = %3")
      .arg(strIndent)
      .arg(strField, -50)
      .arg(strVal);
  m_pLog->AddLine(strLine);
}

//
// Display a formatted hexadecimal field
// - Report the value with the field name (strField) and current indent level (nIndent)
// - Depending on the length of the buffer, either report the hex value in-line with
//   the field name or else start a new row and begin a hex dump
// - Also report the ASCII representation alongside the hex dump
//
// INPUT:
// - nIndent            = Current indent level for reporting
// - strField           = Field name
// - nPosStart          = Field byte array file position start
// - nLen                       = Field byte array length
//
void CDecodePs::PhotoshopParseReportFldHex(uint32_t nIndent, QString strField, uint32_t nPosStart, uint32_t nLen)
{
  QString strIndent;
  QString strPrefix;
  QString strByteHex;
  QString strByteAsc;
  QString strValHex;
  QString strValAsc;
  QString strLine;

  int32_t nByte;

  strIndent = PhotoshopParseIndent(nIndent);

  if(nLen == 0)
  {
    // Print out the header row, but no data will be shown
    strLine = QString("%1%2 = ").arg(strIndent).arg(strField, -50);
    m_pLog->AddLine(strLine);
    // Nothing to report, exit now
    return;
  }
  else if(nLen <= PS_HEX_MAX_INLINE)
  {
    // Define prefix for row
    strPrefix = QString("%1%2 = ").arg(strIndent).arg(strField, -50);
  }
  else
  {
    // Print out header row
    strLine = QString("%1%2 =").arg(strIndent).arg(strField, -50);
    m_pLog->AddLine(strLine);
    // Define prefix for next row
    strPrefix = QString("%1").arg(strIndent);
  }

  // Build up the hex string
  // Limit to PS_HEX_TOTAL bytes
  uint32_t nRowOffset = 0;

  bool bDone = false;

  uint32_t nLenClip;

  nLenClip = qMin(nLen, PS_HEX_TOTAL);
  strValHex = "";
  strValAsc = "";

  while(!bDone)
  {
    // Have we reached the end of the data we wish to display?
    if(nRowOffset >= nLenClip)
    {
      bDone = true;
    }
    else
    {
      // Reset the cumulative hex/ascii value strings
      strValHex = "";
      strValAsc = "";

      // Build the hex/ascii value strings
      for(uint32_t nInd = 0; nInd < PS_HEX_MAX_ROW; nInd++)
      {
        if((nRowOffset + nInd) < nLenClip)
        {
          nByte = m_pWBuf->Buf(nPosStart + nRowOffset + nInd);
          strByteHex = QString("%1 ").arg(nByte, 2, 16, QChar('0'));

          // Only display printable characters
          if(IsPrintAscii(nByte))
          {
            strByteAsc = QChar(nByte);   // the character, not its code
          }
          else
          {
            strByteAsc = ".";
          }
        }
        else
        {
          // Pad out to end of row
          strByteHex = QString("   ");
          strByteAsc = " ";
        }

        // Build up the strings
        strValHex += strByteHex;
        strValAsc += strByteAsc;
      }

      // Generate the line with Hex and ASCII representations
      strLine = QString("%1 | 0x%2 | %3").arg(strPrefix).arg(strValHex).arg(strValAsc);
      m_pLog->AddLine(strLine);

      // Now increment file offset
      nRowOffset += PS_HEX_MAX_ROW;
    }
  }

  // If we had to clip the display length, then show ellipsis now
  if(nLenClip < nLen)
  {
    strLine = QString("%1 | ...").arg(strPrefix);
    m_pLog->AddLine(strLine);
  }

}

QString CDecodePs::PhotoshopDispHexWord(uint32_t nVal)
{
  QString strValHex;
  QString strValAsc;
  QString strByteHex;
  QString strByteAsc;
  QString strLine;

  uint32_t nByte;

  // Reset the cumulative hex/ascii value strings
  strValHex = "";
  strValAsc = "";

  // Build the hex/ascii value strings
  for(uint32_t nInd = 0; nInd <= 3; nInd++)
  {
    nByte = (nVal & 0xFF000000) >> 24;
    nVal <<= 8;

    strByteHex = QString("%1 ").arg(nByte, 2, 16, QChar('0'));

    // Only display printable characters
    if(IsPrintAscii(nByte))
    {
      strByteAsc = QChar(nByte);   // the character, not its code
    }
    else
    {
      strByteAsc = ".";
    }

    // Build up the strings
    strValHex += strByteHex;
    strValAsc += strByteAsc;
  }

  // Generate the line with Hex and ASCII representations
  strLine = QString("0x%1 | %2").arg(strValHex).arg(strValAsc);

  return strLine;
}

// Look up the enumerated constant (nVal) for the specified field (eEnumField)
// - Returns "" if the field wasn't found
QString CDecodePs::PhotoshopParseLookupEnum(teBimEnumField eEnumField, uint32_t nVal)
{
  QString strVal = "";

  // Find the enum value
  bool bDone = false;

  bool bFound = false;

  uint32_t nEnumInd = 0;

  while(!bDone)
  {
    if(asBimEnums[nEnumInd].eBimEnum == eEnumField)
    {
      if(asBimEnums[nEnumInd].nVal == nVal)
      {
        bDone = true;
        bFound = true;
        strVal = asBimEnums[nEnumInd].strVal;
      }
    }

    if(asBimEnums[nEnumInd].eBimEnum == BIM_T_ENUM_END)
    {
      bDone = true;
    }

    if(!bDone)
    {
      nEnumInd++;
    }
  }

  // If enum wasn't found, then replace by hex value
  if(!bFound)
  {
    QString strWord = PhotoshopDispHexWord(nVal);

    strVal = QString("? [%1]").arg(strWord);
  }
  return strVal;
}

// Display a formatted enumerated type field
// - Look up enumerated constant (nVal) for the given field (eEnumField)
// - Report the value with the field name (strField) and current indent level (nIndent)
void CDecodePs::PhotoshopParseReportFldEnum(uint32_t nIndent, QString strField, teBimEnumField eEnumField, uint32_t nVal)
{
  QString strIndent;
  QString strVal;
  QString strLine;

  strIndent = PhotoshopParseIndent(nIndent);
  strVal = PhotoshopParseLookupEnum(eEnumField, nVal);
  strLine = QString("%1%2 = %3").arg(strIndent).arg(strField, -50).arg(strVal);
  m_pLog->AddLine(strLine);
}

// Display a formatted fixed-point field
// - Convert a 32-bit unsigned integer into Photoshop fixed point
// - Assume fixed point representation is 16-bit integer : 16-bit fraction
// - Report the value with the field name (strField) and current indent level (nIndent)
void CDecodePs::PhotoshopParseReportFldFixPt(uint32_t nIndent, QString strField, uint32_t nVal, QString strUnits)
{
  QString strIndent;
  QString strVal;
  QString strLine;

  double fVal;

  fVal = (static_cast<double>(nVal) / 65536.0);
  strIndent = PhotoshopParseIndent(nIndent);
  strVal = QString("%1").arg(fVal);
  strLine = QString("%1%2 = %3 %4")
      .arg(strIndent)
      .arg(strField, -50)
      .arg(strVal)
      .arg(strUnits);
  m_pLog->AddLine(strLine);
}

// Display a formatted fixed-point field
// - Convert a 32-bit unsigned integer into Photoshop floating point
// - Report the value with the field name (strField) and current indent level (nIndent)
void CDecodePs::PhotoshopParseReportFldFloatPt(uint32_t nIndent, QString strField, uint32_t nVal, QString strUnits)
{
  QString strIndent;
  QString strVal;
  QString strLine;

  float fVal;

  // Convert 4 byte unsigned int to floating-point
  // TODO: Need to check this since Photoshop web spec doesn't
  // indicate how floating points are stored.
  union tUnionTmp
  {
    uint8_t nVal[4];
    float fVal;
  };

  tUnionTmp myUnion;

  // NOTE: Empirically determined the byte order for double representation
  myUnion.nVal[3] = static_cast <uint8_t> ((nVal & 0xFF000000) >> 24);
  myUnion.nVal[2] = static_cast <uint8_t> ((nVal & 0x00FF0000) >> 16);
  myUnion.nVal[1] = static_cast <uint8_t> ((nVal & 0x0000FF00) >> 8);
  myUnion.nVal[0] = static_cast <uint8_t> ((nVal & 0x000000FF) >> 0);
  fVal = myUnion.fVal;

  strIndent = PhotoshopParseIndent(nIndent);
  strVal = QString("%1").arg(fVal, 0, 'f', 5);
  strLine = QString("%1%2 = %3 %4")
      .arg(strIndent)
      .arg(strField, -50)
      .arg(strVal).arg(strUnits);
  m_pLog->AddLine(strLine);
}

// Display a formatted double-precision floating-point field
// - Convert two 32-bit unsigned integers into Photoshop double-precision floating point
// - Report the value with the field name (strField) and current indent level (nIndent)
void CDecodePs::PhotoshopParseReportFldDoublePt(uint32_t nIndent, QString strField, uint32_t nVal1, uint32_t nVal2, QString strUnits)
{
  QString strIndent;
  QString strVal;
  QString strLine;

  double dVal;

  // Convert 2x4 byte unsigned int to double-point
  // TODO: Need to check this since Photoshop web spec doesn't
  // indicate how double points are stored.
  union tUnionTmp
  {
    uint8_t nVal[8];
    double dVal;
  };

  tUnionTmp myUnion;

  // NOTE: Empirically determined the byte order for double representation
  myUnion.nVal[7] = static_cast <uint8_t> ((nVal1 & 0xFF000000) >> 24);
  myUnion.nVal[6] = static_cast <uint8_t> ((nVal1 & 0x00FF0000) >> 16);
  myUnion.nVal[5] = static_cast <uint8_t> ((nVal1 & 0x0000FF00) >> 8);
  myUnion.nVal[4] = static_cast <uint8_t> ((nVal1 & 0x000000FF) >> 0);
  myUnion.nVal[3] = static_cast <uint8_t> ((nVal2 & 0xFF000000) >> 24);
  myUnion.nVal[2] = static_cast <uint8_t> ((nVal2 & 0x00FF0000) >> 16);
  myUnion.nVal[1] = static_cast <uint8_t> ((nVal2 & 0x0000FF00) >> 8);
  myUnion.nVal[0] = static_cast <uint8_t> ((nVal2 & 0x000000FF) >> 0);
  dVal = myUnion.dVal;

  strIndent = PhotoshopParseIndent(nIndent);
  strVal = QString("%1").arg(dVal, 0, 'f', 5);
  strLine = QString("%1%2 = %3 %4")
      .arg(strIndent)
      .arg(strField, -50)
      .arg(strVal)
      .arg(strUnits);
  m_pLog->AddLine(strLine);
}

// Display a formatted string field
// - Report the value with the field name (strField) and current indent level (nIndent)
void CDecodePs::PhotoshopParseReportFldStr(uint32_t nIndent, QString strField, QString strVal)
{
  QString strIndent;
  QString strLine;

  strIndent = PhotoshopParseIndent(nIndent);
  strLine = QString("%1%2 = \"%3\"").arg(strIndent).arg(strField, -50).arg(strVal);
  m_pLog->AddLine(strLine);
}

// Display a formatted file offset field
// - Report the offset with the field name (strField) and current indent level (nIndent)
void CDecodePs::PhotoshopParseReportFldOffset(uint32_t nIndent, QString strField, uint32_t nOffset)
{
  QString strIndent;
  QString strLine;

  strIndent = PhotoshopParseIndent(nIndent);
  strLine = QString("%1%2 @ 0x%3").arg(strIndent).arg(strField, -50).arg(nOffset, 8, 16, QChar('0'));
  m_pLog->AddLine(strLine);
}

// Parse the Photoshop IRB Thumbnail Resource
// - NOTE: Returned nPos doesn't take into account JFIF data
void CDecodePs::PhotoshopParseThumbnailResource(uint32_t &nPos, uint32_t nIndent)
{
  uint32_t nVal;

  if(!PsNeed(nPos, 28, "Thumbnail header"))
  {
    return;
  }

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Format", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Width of thumbnail", nVal, "pixels");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Height of thumbnail", nVal, "pixels");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Widthbytes", nVal, "bytes");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Total size", nVal, "bytes");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Size after compression", nVal, "bytes");
  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Bits per pixel", nVal, "bits");
  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Number of planes", nVal, "");

  PhotoshopParseReportFldOffset(nIndent, "JFIF data", nPos);

  // NOTE: nPos doesn't take into account JFIF data!
}

// Parse the Photoshop IRB Version Info
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseVersionInfo(uint32_t &nPos, uint32_t nIndent)
{
  uint32_t nVal;

  QString strVal;

  uint32_t nPosOffset;

  if(!PsNeed(nPos, 5, "Version"))
  {
    return;
  }

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Version", nVal, "");
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "hasRealMergedData", nVal, "");
  strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
  nPos += nPosOffset;
  PhotoshopParseReportFldStr(nIndent, "Writer name", strVal);
  strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
  nPos += nPosOffset;
  PhotoshopParseReportFldStr(nIndent, "Reader name", strVal);

  if(!PsNeed(nPos, 4, "File version"))
  {
    return;
  }

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "File version", nVal, "");
}

// Parse the Photoshop IRB Print Scale
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParsePrintScale(uint32_t &nPos, uint32_t nIndent)
{
  if(!PsNeed(nPos, 14, "Print scale"))
  {
    return;
  }

  uint32_t nVal;

  QString strVal;

  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldEnum(nIndent, "Style", BIM_T_ENUM_PRINT_SCALE_STYLE, nVal);
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldFloatPt(nIndent, "X location", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldFloatPt(nIndent, "Y location", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldFloatPt(nIndent, "Scale", nVal, "");
}

// Parse the Photoshop IRB Global Angle
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseGlobalAngle(uint32_t &nPos, uint32_t nIndent)
{
  if(!PsNeed(nPos, 4, "Global Angle"))
  {
    return;
  }

  uint32_t nVal;

  QString strVal;

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Global Angle", nVal, "degrees");
}

// Parse the Photoshop IRB Global Altitude
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseGlobalAltitude(uint32_t &nPos, uint32_t nIndent)
{
  if(!PsNeed(nPos, 4, "Global Altitude"))
  {
    return;
  }

  uint32_t nVal;

  QString strVal;

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Global Altitude", nVal, "");
}

// Parse the Photoshop IRB Print Flags
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParsePrintFlags(uint32_t &nPos, uint32_t nIndent)
{
  if(!PsNeed(nPos, 9, "Print flags"))
  {
    return;
  }

  uint32_t nVal;

  QString strVal;

  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Labels", nVal);
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Crop marks", nVal);
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Color bars", nVal);
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Registration marks", nVal);
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Negative", nVal);
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Flip", nVal);
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Interpolate", nVal);
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Caption", nVal);
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Print flags", nVal);
}

// Parse the Photoshop IRB Print Flags Information
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParsePrintFlagsInfo(uint32_t &nPos, uint32_t nIndent)
{
  if(!PsNeed(nPos, 10, "Print flags information"))
  {
    return;
  }

  uint32_t nVal;

  QString strVal;

  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Version", nVal, "");
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Center crop marks", nVal, "");
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Reserved", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Bleed width value", nVal, "");
  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Bleed width scale", nVal, "");
}

// Parse the Photoshop IRB Copyright Flag
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseCopyrightFlag(uint32_t &nPos, uint32_t nIndent)
{
  if(!PsNeed(nPos, 1, "Copyright flag"))
  {
    return;
  }

  uint32_t nVal;

  QString strVal;

  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Copyright flag", nVal);
}

// Parse the Photoshop IRB Aspect Ratio
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParsePixelAspectRatio(uint32_t &nPos, uint32_t nIndent)
{
  if(!PsNeed(nPos, 12, "Pixel aspect ratio"))
  {
    return;
  }

  uint32_t nVal, nVal1, nVal2;

  QString strVal;

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Version", nVal, "");
  nVal1 = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  nVal2 = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldDoublePt(nIndent, "X/Y Ratio", nVal1, nVal2, "");
}

// Parse the Photoshop IRB Document-Specific Seed
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseDocSpecificSeed(uint32_t &nPos, uint32_t nIndent)
{
  if(!PsNeed(nPos, 4, "Document-specific IDs seed"))
  {
    return;
  }

  uint32_t nVal;

  QString strVal;

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Base value", nVal, "");
}

// Parse the Photoshop IRB Grid and Guides
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseGridGuides(uint32_t &nPos, uint32_t nIndent)
{
  uint32_t nVal;

  QString strVal;

  if(!PsNeed(nPos, 16, "Grid and guides header"))
  {
    return;
  }

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Version", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Grid Horizontal", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Grid Vertical", nVal, "");
  uint32_t nNumGuides = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Number of Guide Resources", nNumGuides, "");

  if(nNumGuides > 0)
    PhotoshopParseReportNote(nIndent, "-----");

  // The count comes from the file; also stop at the end of the block
  for(uint32_t nInd = 0; (nInd < nNumGuides) && (nPos < m_nPosBlockEnd); nInd++)
  {
    // A guide (5 bytes) is all in the block: one starting 1 to 4 bytes before
    // its end was read on past it
    if(!PsNeed(nPos, 5, QString("Guide #%1").arg(nInd)))
    {
      break;
    }

    strVal = QString("Guide #%1:").arg(nInd);
    PhotoshopParseReportNote(nIndent, strVal);

    nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent + 1, "Location", nVal, "");
    nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
    PhotoshopParseReportFldEnum(nIndent + 1, "Direction", BIM_T_ENUM_GRID_GUIDE_DIR, nVal);
  }

  if(nNumGuides > 0)
    PhotoshopParseReportNote(nIndent, "-----");
}

// Parse the Photoshop IRB Resolution Info
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseResolutionInfo(uint32_t &nPos, uint32_t nIndent)
{
  if(!PsNeed(nPos, 16, "Resolution info"))
  {
    return;
  }

  uint32_t nVal, nUnit;

  QString strVal, strUnit;

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  nUnit = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  strUnit = PhotoshopParseLookupEnum(BIM_T_ENUM_RESOLUTION_INFO_RES_UNIT, nUnit);
  PhotoshopParseReportFldFixPt(nIndent, "Horizontal resolution", nVal, strUnit);
  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldEnum(nIndent, "Width unit", BIM_T_ENUM_RESOLUTION_INFO_WIDTH_UNIT, nVal);
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  nUnit = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  strUnit = PhotoshopParseLookupEnum(BIM_T_ENUM_RESOLUTION_INFO_RES_UNIT, nUnit);
  PhotoshopParseReportFldFixPt(nIndent, "Vertical resolution", nVal, strUnit);
  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldEnum(nIndent, "Height unit", BIM_T_ENUM_RESOLUTION_INFO_WIDTH_UNIT, nVal);
}

// Parse the Photoshop IRB Layer State Info
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseLayerStateInfo(uint32_t &nPos, uint32_t nIndent)
{
  if(!PsNeed(nPos, 2, "Target layer"))
  {
    return;
  }

  uint32_t nVal;

  QString strVal;

  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Target layer", nVal, "");
}

// Parse the Photoshop IRB Layer Group Info
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// - nLen               = Length of IRB entry (to determine number of layers)
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseLayerGroupInfo(uint32_t &nPos, uint32_t nIndent, uint32_t nLen)
{
  uint32_t nVal;

  QString strVal;

  // NOTE:
  // According to the Photoshop documentation:
  // - 2 bytes per layer containing a group ID for the dragging groups.
  // - Layers in a group have the same group ID.
  // It was not clear whether there was a separate indication of the number of layers
  // For now, assume we can derive it from the total IRB length
  uint32_t nNumLayers;

  nNumLayers = nLen / 2;

  for(uint32_t nInd = 0; nInd < nNumLayers; nInd++)
  {
    strVal = QString("Layer #%1:").arg(nInd);
    PhotoshopParseReportNote(nIndent, strVal);
    nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent + 1, "Layer Group", nVal, "");
  }
}

// Parse the Photoshop IRB Layer Groups Enabled
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// - nLen               = Length of IRB entry (to determine number of layers)
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseLayerGroupEnabled(uint32_t &nPos, uint32_t nIndent, uint32_t nLen)
{
  uint32_t nVal;

  QString strVal;

  // NOTE:
  // According to the Photoshop documentation:
  // - 1 byte for each layer in the document, repeated by length of the resource.
  // - NOTE: Layer groups have start and end markers
  // For now, assume we can derive it from the total IRB length
  uint32_t nNumLayers;

  nNumLayers = nLen / 1;

  for(uint32_t nInd = 0; nInd < nNumLayers; nInd++)
  {
    strVal = QString("Layer #%1:").arg(nInd);
    PhotoshopParseReportNote(nIndent, strVal);
    nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent + 1, "Layer Group Enabled ID", nVal, "");
  }
}

// Parse the Photoshop IRB Layer Select IDs
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseLayerSelectId(uint32_t &nPos, uint32_t nIndent)
{
  uint32_t nVal;

  QString strVal;

  if(!BlockRoom(nPos, 2))
  {
    BlockOverrun(nPos, "Layer count");
    return;
  }

  uint32_t nNumLayer = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Num selected", nNumLayer, "");

  // The count comes from the file: stop at the end of the block (a 2-byte
  // block said 65,535, and as many IDs were read from past it)
  for(uint32_t nInd = 0; nInd < nNumLayer; nInd++)
  {
    if(!BlockRoom(nPos, 4))
    {
      BlockOverrun(nPos, "Layer ID");
      break;
    }

    nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent + 1, "Layer ID", nVal, "");
  }
}

// Parse the Photoshop IRB File Header
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseFileHeader(uint32_t &nPos, uint32_t nIndent, tsImageInfo * psImageInfo)
{
  Q_ASSERT(psImageInfo);

  uint32_t nVal;

  QString strVal;

  PhotoshopParseReportNote(nIndent, "File Header Section:");
  nIndent++;

  QString strSig = m_pWBuf->BufReadStrn(nPos, 4);

  nPos += 4;
  PhotoshopParseReportFldStr(nIndent, "Signature", strSig);

  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Version", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Reserved1", nVal, "");
  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Reserved2", nVal, "");
  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Num channels in image", nVal, "");
  psImageInfo->nNumChans = nVal;
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Image height", nVal, "pixels");
  psImageInfo->nImageHeight = nVal;
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Image width", nVal, "pixels");
  psImageInfo->nImageWidth = nVal;
  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Depth", nVal, "bits per pixel");
  psImageInfo->nDepthBpp = nVal;
  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  PhotoshopParseReportFldEnum(nIndent, "Color mode", BIM_T_ENUM_FILE_HDR_COL_MODE, nVal);
  psImageInfo->nColorMode = nVal;
}

// Parse the Photoshop IRB Color Mode Section (from File Header)
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseColorModeSection(uint32_t &nPos, uint32_t nIndent)
{
  QString strVal;

  PhotoshopParseReportNote(nIndent, "Color Mode Data Section:");
  nIndent++;

  uint32_t nColModeLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Length", nColModeLen, "");

  if(!PsdFits(nPos, nColModeLen, "Color mode data"))
  {
    return false;
  }

  if(nColModeLen != 0)
  {
    PhotoshopParseReportFldOffset(nIndent, "Color data", nPos);
  }

  // Skip data
  nPos += nColModeLen;
  return true;
}

// -------------

// Parse the Photoshop IRB Layer and Mask Info Section (from File Header)
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseLayerMaskInfo(uint32_t &nPos, uint32_t nIndent)
{
  QString strVal;

  bool bDecOk = true;

  PhotoshopParseReportNote(nIndent, "Layer and Mask Information Section:");
  nIndent++;

  uint32_t nLayerMaskLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  uint32_t nPosStart = nPos;

  PhotoshopParseReportFldNum(nIndent, "Length", nLayerMaskLen, "");

  // (The end can't wrap past 32 bits once it is within the file)
  if(!PsdFits(nPosStart, nLayerMaskLen, "Layer and mask information"))
  {
    return false;
  }

  uint32_t nPosEnd = nPosStart + nLayerMaskLen;

  if(nLayerMaskLen == 0)
  {
    return true;
  }
  else
  {
    // Its parts are in the section, not just in the file
    if(bDecOk)
      bDecOk &= PhotoshopParseLayerInfo(nPos, nIndent, nPosEnd);

    // The global layer mask info is optional: the layer info can fill the
    // section (as in files from macOS's sips), or tagged blocks can follow it
    // directly (psd-tools reads it the same way). It used to be read from
    // whatever followed, such as the image data.
    if(bDecOk && (nPos < nPosEnd) && (nPosEnd - nPos >= 4))
    {
      const QString strSig = m_pWBuf->BufReadStrn(nPos, 4);

      if((strSig != "8BIM") && (strSig != "8B64"))
        bDecOk &= PhotoshopParseGlobalLayerMaskInfo(nPos, nIndent, nPosEnd);
    }

    if(bDecOk)
    {
      // Additional layer info blocks fill the rest of the section (each is
      // at least 12 bytes). This compared unsigned values, so a position past
      // the end looked like 4 GB to go.
      while((bDecOk) && (nPos < nPosEnd) && (nPosEnd - nPos > 12))
      {
        bDecOk &= PhotoshopParseAddtlLayerInfo(nPos, nIndent, 4, nPosEnd, "the layer and mask information");
      }
    }
  }

  // The image data follows this section, whose length is known, so a problem
  // with the layers no longer stops the decode. (One file written by macOS's
  // sips had a layer channel whose rows didn't match its row lengths.)
  if(!bDecOk)
  {
    m_pLog->AddLineWarn("NOTE: Skipping the rest of the layer and mask information, to decode the image data after it");
    bDecOk = true;
  }

  // Skip data
  nPos = nPosEnd;

  return bDecOk;
}

// Parse the Photoshop IRB Layer Info Section
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseLayerInfo(uint32_t &nPos, uint32_t nIndent, uint32_t nPosLimit)
{
  QString strVal;

  bool bDecOk = true;

  PhotoshopParseReportNote(nIndent, "Layer Info:");
  nIndent++;

  if(!PsdFitsIn(nPos, 4, nPosLimit, "Layer info length", "the layer and mask information"))
  {
    return false;
  }

  uint32_t nLayerLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Length", nLayerLen, "");

  if(nLayerLen == 0)
  {
    return bDecOk;
  }

  if(!PsdFitsIn(nPos, nLayerLen, nPosLimit, "Layer info", "the layer and mask information"))
  {
    return false;
  }

  // Save file position
  uint32_t nPosStart = nPos;

  // Round up length to multiple of 2 (at most the end of the section)
  const uint64_t nPosOuter = std::min<uint64_t>(nPosLimit, m_pWBuf->GetPosEof());
  const uint32_t nPosEnd = static_cast<uint32_t>(std::min<uint64_t>(
      static_cast<uint64_t>(nPosStart) + nLayerLen + (nLayerLen % 2), nPosOuter));

  if(!PsdFitsIn(nPos, 2, nPosEnd, "Layer count", "the layer info"))
  {
    return false;
  }

  // According to Adobe, "Layer count" is defined as follows:
  // - If it is a negative number, its absolute value is the number of layers and the
  //   first alpha channel contains the transparency data for the merged result.
  // Therefore, we'll treat it as signed short and take absolute value
  uint16_t nLayerCountU = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  int16_t nLayerCountS = static_cast<int16_t>(nLayerCountU);

  uint32_t nLayerCount = abs(nLayerCountS);

  PhotoshopParseReportFldNum(nIndent, "Layer count", nLayerCount, "");

  if(nLayerCountU & 0x8000)
  {
    PhotoshopParseReportNote(nIndent, "First alpha channel contains transparency for merged result");
  }

  tsLayerAllInfo sLayerAllInfo;

  // Value-initialised, so the clean-up below can free every layer's arrays
  // (the records not reached, or cut short, have none)
  sLayerAllInfo.nNumLayers = nLayerCount;
  sLayerAllInfo.psLayers = new tsLayerInfo[nLayerCount]();

  for(uint32_t nLayerInd = 0; (bDecOk) && (nLayerInd < nLayerCount); nLayerInd++)
  {
    QString strTmp;

    strTmp = QString("Layer #%1").arg(nLayerInd);
    PhotoshopParseReportFldOffset(nIndent, strTmp, nPos);

    // A record is at least its rectangle: stop at the end of the section
    // rather than parse up to 32767 records of zeros past it
    if((nPos >= nPosEnd) || !PsdFitsIn(nPos, 16, nPosEnd, strTmp, "the layer info"))
    {
      if(nPos >= nPosEnd)
      {
        m_pLog->AddLineErr(QString("ERROR: %1 at [0x%2] is past the end of the layer info. Stopping.")
                             .arg(strTmp).arg(nPos, 8, 16, QChar('0')));
      }
      bDecOk = false;
      break;
    }

    bDecOk &= PhotoshopParseLayerRecord(nPos, nIndent, &sLayerAllInfo.psLayers[nLayerInd], nPosEnd);
  }

  PhotoshopParseReportNote(nIndent, "Channel Image Data:");
  QString strLine;

  uint32_t nNumChans;
  uint32_t nWidth;
  uint32_t nHeight;
  uint32_t nPosLastLayer = 0;
  uint32_t nPosLastChan = 0;

  for(uint32_t nLayerInd = 0; (bDecOk) && (nLayerInd < nLayerCount); nLayerInd++)
  {
    nNumChans = sLayerAllInfo.psLayers[nLayerInd].nNumChans;
    nWidth = sLayerAllInfo.psLayers[nLayerInd].nWidth;
    nHeight = sLayerAllInfo.psLayers[nLayerInd].nHeight;

    // Layers aren't drawn: the Windows version could draw one into the
    // preview (m_bDisplayLayer and m_nDisplayLayerInd), but nothing turned
    // that on, so only the composite image is shown (PhotoshopParseImageData)
    unsigned char *pDibBits = nullptr;

    nPosLastLayer = nPos;

    for(uint32_t nChanInd = 0; (bDecOk) && (nChanInd < nNumChans); nChanInd++)
    {
      nPosLastChan = nPos;
      strLine = QString("Layer %1/%2, Channel %3/%4")
          .arg(nLayerInd + 1, 3)
          .arg(nLayerCount, 3)
          .arg(nChanInd + 1, 2).arg(nNumChans, 2);
      //m_pLog->AddLine(strLine);
      PhotoshopParseReportNote(nIndent + 1, strLine);

      if(bDecOk)
      {
        // Fetch the channel ID to determine what RGB channel to map to
        // - Layer 0 (background):           0->R, 1->G, 2->B
        // - Layer n (other):      65535->A, 0->R, 1->G, 2->B
        const tsLayerInfo &sLayer = sLayerAllInfo.psLayers[nLayerInd];
        uint32_t nChanID = sLayer.pnChanID[nChanInd];

        // The layer record gives each channel's data length: the channel
        // must be in the file, and the next one starts after it, so a
        // problem with one channel doesn't lose the rest of the layers
        const uint32_t nChanLen = sLayer.pnChanLen[nChanInd];

        // (In the layer info: it was read on into the image data after it)
        if(!PsdFitsIn(nPos, nChanLen, nPosEnd, "Channel image data", "the layer info"))
        {
          bDecOk = false;
          break;
        }

        const uint32_t nPosChanEnd = nPos + nChanLen;

        // A user mask (-2) or real user mask (-3) has its own size, from the
        // layer mask data. (Every channel got the layer's size, so a 4x4
        // layer's 2x2 mask gave an RLE error and lost the rest.)
        uint32_t nChanWidth = nWidth;
        uint32_t nChanHeight = nHeight;

        if(nChanID == 0xFFFE)
        {
          nChanWidth = sLayer.nMaskWidth;
          nChanHeight = sLayer.nMaskHeight;
        }
        else if(nChanID == 0xFFFD)
        {
          nChanWidth = sLayer.nRealMaskWidth;
          nChanHeight = sLayer.nRealMaskHeight;
        }

        // Parse, and optionally paint the DIB
        // NOTE: For uncompressed channels, the width & height generally appear to be zero
        //       According to the spec:
        //       If the compression code is 0, the image data is just the raw image data,
        //       whose size is calculated as (LayerBottom-LayerTop)* (LayerRight-LayerLeft)
        if(!PhotoshopParseChannelImageData(nPos, nIndent + 1, nChanWidth, nChanHeight, nChanID, nPosChanEnd, pDibBits))
        {
          m_pLog->AddLineWarn("NOTE: Skipping to the next channel");
        }

        nPos = nPosChanEnd;
      }

      strLine = QString("CurPos @ 0x%1, bDecOk=%2, LastLayer @ 0x%3, LastChan @ 0x%4")
          .arg(nPos, 8, 16, QChar('0'))
          .arg(bDecOk).arg(nPosLastLayer, 8, 16, QChar('0'))
          .arg(nPosLastChan, 8, 16, QChar('0'));
      //m_pLog->AddLine(strLine);
      //PhotoshopParseReportNote(nIndent+1,strLine);
    }
  }

  // Pad out to specified length
  if(nPos < nPosEnd)
  {
    nPos = nPosEnd;
  }

  // Deallocate (this used to free a layer's arrays only after a successful
  // decode, and never for a layer with no channels)
  for(uint32_t nLayerInd = 0; (nLayerInd < sLayerAllInfo.nNumLayers); nLayerInd++)
  {
    delete[]sLayerAllInfo.psLayers[nLayerInd].pnChanLen;
    delete[]sLayerAllInfo.psLayers[nLayerInd].pnChanID;
  }

  delete[]sLayerAllInfo.psLayers;
  sLayerAllInfo.psLayers = nullptr;

  return bDecOk;
}

// Parse the Photoshop IRB Layer Record
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseLayerRecord(uint32_t &nPos, uint32_t nIndent, tsLayerInfo * pLayerInfo, uint32_t nPosLimit)
{
  QString strVal;

  bool bDecOk = true;

  Q_ASSERT(pLayerInfo);

  PhotoshopParseReportNote(nIndent, "Layer Record:");
  nIndent++;

  uint32_t nRect1, nRect2, nRect3, nRect4;

  nRect1 = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  nRect2 = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  nRect3 = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  nRect4 = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Rect Top", nRect1, "");
  PhotoshopParseReportFldNum(nIndent, "Rect Left", nRect2, "");
  PhotoshopParseReportFldNum(nIndent, "Rect Bottom", nRect3, "");
  PhotoshopParseReportFldNum(nIndent, "Rect Right", nRect4, "");

  pLayerInfo->nHeight = nRect3 - nRect1;
  pLayerInfo->nWidth = nRect4 - nRect2;
  //nHeight = nRect3-nRect1;

  // Each part of the record is in the layer info, and those after the extra
  // data's length in the extra data (they were read on into the next record)
  if(!PsdFitsIn(nPos, 2, nPosLimit, "Number of channels", "the layer info"))
  {
    return false;
  }

  uint32_t nNumChans = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Number of channels", nNumChans, "");

  // 6 bytes per channel (ID and data length)
  if(!PsdFitsIn(nPos, PsdSize(nNumChans, 6), nPosLimit, "Layer channel list", "the layer info"))
  {
    return false;
  }

  //nChans = nNumChans;
  pLayerInfo->nNumChans = nNumChans;

  Q_ASSERT(pLayerInfo->pnChanLen == nullptr);
  pLayerInfo->pnChanLen = new uint32_t[nNumChans];
  pLayerInfo->pnChanID = new uint32_t[nNumChans];

  for(uint32_t nChanInd = 0; nChanInd < nNumChans; nChanInd++)
  {
    uint32_t nChanID = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);

    uint32_t nChanDataLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

    QString strField;

    strField = QString("Channel index #%1").arg(nChanInd);
    strVal = QString("ID=%1 DataLength=0x%2").arg(nChanID, 5).arg(nChanDataLen, 8, 16, QChar('0'));
    PhotoshopParseReportFldStr(nIndent, strField, strVal);
    pLayerInfo->pnChanID[nChanInd] = nChanID;
    pLayerInfo->pnChanLen[nChanInd] = nChanDataLen;
  }

  // Blend mode signature and key, opacity, clipping, flags, filler and the
  // extra data's length
  if(!PsdFitsIn(nPos, 16, nPosLimit, "Blend mode signature", "the layer info"))
  {
    return false;
  }

  QString strBlendModeSig = m_pWBuf->BufReadStrn(nPos, 4);

  nPos += 4;
  PhotoshopParseReportFldStr(nIndent, "Blend mode signature", strBlendModeSig);
  uint32_t nBlendModeKey = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  PhotoshopParseReportFldEnum(nIndent, "Blend mode key", BIM_T_ENUM_BLEND_MODE_KEY, nBlendModeKey);
  uint32_t nOpacity = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Opacity", nOpacity, "(0=transparent ... 255=opaque)");
  m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);   // unsigned nClipping
  m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);   // unsigned nFlags
  m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);   // unsigned nFiller
  uint32_t nExtraDataLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  uint32_t nPosExtra = nPos;

  if(!PsdFitsIn(nPosExtra, nExtraDataLen, nPosLimit, "Layer extra data", "the layer info"))
  {
    return false;
  }

  const uint32_t nPosExtraEnd = nPosExtra + nExtraDataLen;

  // The real user mask's rectangle is only there with a real user mask channel
  bool bRealMask = false;

  for(uint32_t nChanInd = 0; nChanInd < nNumChans; nChanInd++)
  {
    bRealMask |= (pLayerInfo->pnChanID[nChanInd] == 0xFFFD);
  }

  // The mask, blending ranges, name and tagged blocks are in the extra data,
  // whose length was checked: a problem with one of them is reported, and the
  // record still ends where its extra data does, so the next layers are read
  bool bExtraOk = PhotoshopParseLayerMask(nPos, nIndent, pLayerInfo, bRealMask, nPosExtraEnd);

  if(bExtraOk)
    bExtraOk = PhotoshopParseLayerBlendingRanges(nPos, nIndent, nPosExtraEnd);

  // The name (its length, then the name) is in the extra data too. (Its pad
  // to a multiple of 4 is left out of the check: a writer that doesn't pad,
  // with nothing after the name, is still read.)
  if(bExtraOk && !PsdFitsIn(nPos, 1, nPosExtraEnd, "Layer name", "the layer's extra data"))
  {
    bExtraOk = false;
  }
  else if(bExtraOk && !PsdFitsIn(nPos, 1 + static_cast<uint64_t>(Buf(nPos)), nPosExtraEnd, "Layer name",
                                 "the layer's extra data"))
  {
    bExtraOk = false;
  }

  if(bExtraOk)
  {
    uint32_t nLayerNameLen = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);

    QString strLayerName = m_pWBuf->BufReadStrn(nPos, nLayerNameLen);

    nPos += nLayerNameLen;
    // According to spec, length is padded to multiple of 4 bytes,
    uint32_t nSkip = 0;

    nSkip = (4 - ((1 + nLayerNameLen) % 4)) % 4;
    nPos += nSkip;
  }

  // Now use up the "extra bytes" with "Additional Layer Info"
  while(bExtraOk && (nPos < nPosExtraEnd))
  {
    bExtraOk = PhotoshopParseAddtlLayerInfo(nPos, nIndent, 2, nPosExtraEnd, "the layer's extra data");
  }

  // The record ends with its extra data
  nPos = nPosExtraEnd;

  return bDecOk;
}

// Parse the Photoshop IRB Layer Mask
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseLayerMask(uint32_t &nPos, uint32_t nIndent, tsLayerInfo * pLayerInfo, bool bRealMask,
                                        uint32_t nPosLimit)
{
  PhotoshopParseReportNote(nIndent, "Layer Mask / Adjustment layer data:");

  // In the layer's extra data (nPosLimit, its end)
  if(!PsdFitsIn(nPos, 4, nPosLimit, "Layer mask data length", "the layer's extra data"))
  {
    return false;
  }

  uint32_t nLayerMaskLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  if(!PsdFitsIn(nPos, nLayerMaskLen, nPosLimit, "Layer mask data", "the layer's extra data"))
  {
    return false;
  }

  // Keep the rectangles, which give the mask channels' sizes: the user mask's
  // first (top, left, bottom, right), then the default colour and flags; with
  // a real user mask channel, its flags, background and rectangle follow (in
  // that order, before any mask parameters, as psd-tools reads them). The
  // rest isn't reported, so the data is then skipped by its length. (Reading
  // the fields one by one relied on guesses about the layout, and on a wrong
  // guess the record went out of step.)
  if(nLayerMaskLen >= 16)
  {
    const uint32_t nTop = m_pWBuf->BufX(nPos, 4, PS_BSWAP);
    const uint32_t nLeft = m_pWBuf->BufX(nPos + 4, 4, PS_BSWAP);

    pLayerInfo->nMaskHeight = m_pWBuf->BufX(nPos + 8, 4, PS_BSWAP) - nTop;
    pLayerInfo->nMaskWidth = m_pWBuf->BufX(nPos + 12, 4, PS_BSWAP) - nLeft;
  }

  if(bRealMask && (nLayerMaskLen >= 36))
  {
    const uint32_t nTop = m_pWBuf->BufX(nPos + 20, 4, PS_BSWAP);
    const uint32_t nLeft = m_pWBuf->BufX(nPos + 24, 4, PS_BSWAP);

    pLayerInfo->nRealMaskHeight = m_pWBuf->BufX(nPos + 28, 4, PS_BSWAP) - nTop;
    pLayerInfo->nRealMaskWidth = m_pWBuf->BufX(nPos + 32, 4, PS_BSWAP) - nLeft;
  }

  nPos += nLayerMaskLen;
  return true;
}

// Parse the Photoshop IRB Layer blending ranges data
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseLayerBlendingRanges(uint32_t &nPos, uint32_t nIndent, uint32_t nPosLimit)
{
  PhotoshopParseReportNote(nIndent, "Layer blending ranges data:");

  // In the layer's extra data (nPosLimit, its end)
  if(!PsdFitsIn(nPos, 4, nPosLimit, "Layer blending range data length", "the layer's extra data"))
  {
    return false;
  }

  uint32_t nLayerBlendLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  // The ranges aren't reported: skip them. (Reading them in a loop of
  // (length - 8) / 8 pairs ran 500 million times for a bad length.)
  if(!PsdFitsIn(nPos, nLayerBlendLen, nPosLimit, "Layer blending range data", "the layer's extra data"))
  {
    return false;
  }

  nPos += nLayerBlendLen;
  return true;
}

// Does a field of nLen bytes at nPos end within its channel's data, whose
// length the layer record gives? If not, report it.
bool CDecodePs::ChanFits(uint32_t nPos, uint64_t nLen, uint32_t nPosChanEnd, const QString &strWhat)
{
  if((nPos <= nPosChanEnd) && (nLen <= nPosChanEnd - nPos))
  {
    return true;
  }

  m_pLog->AddLineErr(QString("ERROR: %1 at [0x%2] runs past the end of the channel's data, at [0x%3].")
                       .arg(strWhat).arg(nPos, 8, 16, QChar('0')).arg(nPosChanEnd, 8, 16, QChar('0')));
  return false;
}

// Parse the Photoshop IRB Channel image data
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// - nWidth, nHeight = The channel's size (a mask's, or else the layer's)
// - nChan      = Channel being parsed (used for DIB mapping)
// - nPosChanEnd = End of the channel's data (from the layer record), which
//                the caller has checked is in the file
// - pDibBits   = Preview pixels to draw into (nullptr if disabled)
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseChannelImageData(uint32_t &nPos, uint32_t nIndent, uint32_t nWidth, uint32_t nHeight,
                                               uint32_t nChan, uint32_t nPosChanEnd, unsigned char *pDibBits)
{
  bool bDecOk = true;

  if(!ChanFits(nPos, 2, nPosChanEnd, "Compression method"))
  {
    return false;
  }

  uint32_t nCompressionMethod = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent + 1, "Compression method", nCompressionMethod, "");

  // Rows are counted in bytes (see PsdRowBytes), and only 8-bit samples
  // can be drawn
  const uint64_t nRowBytes64 = PsdRowBytes(nWidth);

  if(nRowBytes64 > 0xFFFFFFFFull)
  {
    m_pLog->AddLineErr(QString("ERROR: Channel rows of %1 bytes are too long.").arg(nRowBytes64));
    return false;
  }

  const uint32_t nRowBytes = static_cast<uint32_t>(nRowBytes64);

  if(m_nDepthBpp != 8)
  {
    pDibBits = nullptr;
  }

  if(nCompressionMethod == 1)
  {
    // *** RLE Compression ***
    if(nHeight == 0)
    {
      return true;
    }

    // Read the line lengths into an array
    // -   LOOP [nHeight]
    // -     16-bit: row length
    // -   ENDLOOP
    // The table, and then the rows, must be in the channel's data. (A layer
    // whose bottom is above its top has a height near 4 billion: its table
    // took 16 GB.)
    if(!ChanFits(nPos, PsdSize(nHeight, 2), nPosChanEnd, "Channel row length table"))
    {
      return false;
    }

    std::vector<uint16_t> anRowLen(nHeight);
    uint64_t nDataLen = 0;

    for(uint32_t nRow = 0; nRow < nHeight; nRow++)
    {
      anRowLen[nRow] = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
      nDataLen += anRowLen[nRow];
    }

    if(!ChanFits(nPos, nDataLen, nPosChanEnd, "RLE channel data"))
    {
      return false;
    }

    // Read the compressed data
    for(uint32_t nRow = 0; (bDecOk) && (nRow < nHeight); nRow++)
    {
      bDecOk = PhotoshopDecodeRowRle(nPos, nRowBytes, nRow, anRowLen[nRow], nChan, pDibBits);
    }                           //nRow
  }
  else if(nCompressionMethod == 0)
  {
    // *** RAW (no compression)
    // (Empty rows would pass the size check below whatever the height)
    if((nHeight == 0) || (nRowBytes == 0))
    {
      return true;
    }

    if(!ChanFits(nPos, PsdSize(nHeight, nRowBytes), nPosChanEnd, "Channel image data"))
    {
      return false;
    }

    for(uint32_t nRow = 0; (bDecOk) && (nRow < nHeight); nRow++)
    {
      bDecOk = PhotoshopDecodeRowUncomp(nPos, nRowBytes, nRow, nChan, pDibBits);
    }
  }
  else
  {
    // (Such as ZIP, which Photoshop can use for layers)
    m_pLog->AddLineWarn("Unsupported compression method.");
    bDecOk = false;
    return bDecOk;
  }

  return bDecOk;
}

// Put one channel's sample into the preview (QImage::Format_RGB32, filled
// with opaque black first)
// - The caller keeps nRow and nCol within the image
// - RGB images: channels 0, 1 and 2 are R, G and B; any others (alpha,
//   spot colours) aren't drawn
// - Grayscale images: channel 0 sets R, G and B
// - (The Windows DIB was stored bottom row first, so the rows used to be
//   flipped: a QImage starts at the top.)
void CDecodePs::PhotoshopSetPixel(unsigned char *pDibBits, uint32_t nWidth, uint32_t nRow, uint32_t nCol, uint32_t nChanID,
                                  unsigned char nVal)
{
  // Byte offsets of R, G and B in a 0xffRRGGBB word
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
  static const uint32_t anRgbOff[3] = { 2, 1, 0 };
#else
  static const uint32_t anRgbOff[3] = { 1, 2, 3 };
#endif

  unsigned char *pPix = pDibBits + (static_cast<size_t>(nRow) * nWidth + nCol) * sizeof(QRgb);

  if(m_bDibGray)
  {
    if(nChanID == 0)
    {
      pPix[anRgbOff[0]] = nVal;
      pPix[anRgbOff[1]] = nVal;
      pPix[anRgbOff[2]] = nVal;
    }
  }
  else if(nChanID < 3)
  {
    pPix[anRgbOff[nChanID]] = nVal;
  }
}

bool CDecodePs::PhotoshopDecodeRowUncomp(uint32_t &nPos, uint32_t nRowBytes, uint32_t nRow, uint32_t nChanID,
                                         unsigned char *pDibBits)
{
  // The caller has checked that the row is in the file
  if(!pDibBits)
  {
    nPos += nRowBytes;
    return true;
  }

  for(uint32_t nCol = 0; nCol < nRowBytes; nCol++)
  {
    PhotoshopSetPixel(pDibBits, nRowBytes, nRow, nCol, nChanID, m_pWBuf->BufRdAdv1(nPos, PS_BSWAP));
  }

  return true;
}

// Decode one PackBits (RLE) row of nRowLen bytes into nRowBytes bytes
// - Each run starts with a signed count n: 0..127 copies the next n+1 bytes,
//   -1..-127 repeats the next byte 1-n times, and -128 does nothing
// - The row must decode to exactly nRowBytes bytes (for an 8-bit image, the
//   only kind drawn into pDibBits, its width). A run that went past the row
//   used to be written past it (and past the end of the image).
bool CDecodePs::PhotoshopDecodeRowRle(uint32_t &nPos, uint32_t nRowBytes, uint32_t nRow, uint32_t nRowLen,
                                      uint32_t nChanID, unsigned char *pDibBits)
{
  const uint32_t nPosRow = nPos;
  uint32_t nRowOffsetComp = 0;      // Row offset (compressed size)
  uint32_t nCol = 0;                // Row offset (decompressed size)
  bool bDecOk = true;

  while((bDecOk) && (nRowOffsetComp < nRowLen))
  {
    // (A plain char is unsigned on some platforms, such as Linux on ARM)
    const int8_t nRleRunS = static_cast<int8_t>(m_pWBuf->BufRdAdv1(nPos, PS_BSWAP));

    nRowOffsetComp++;

    if(nRleRunS == -128)
    {
      continue;
    }

    const bool bRepeat = (nRleRunS < 0);
    const uint32_t nRleRunCnt = static_cast<uint32_t>(bRepeat ? (1 - nRleRunS) : (1 + nRleRunS));

    if(nRleRunCnt > nRowBytes - nCol)
    {
      bDecOk = false;
      break;
    }

    unsigned char nRleVal = 0;

    if(bRepeat)
    {
      // Replicate the next byte
      nRleVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
      nRowOffsetComp++;
    }

    for(uint32_t nRunInd = 0; nRunInd < nRleRunCnt; nRunInd++)
    {
      if(!bRepeat)
      {
        // Copy the next bytes as-is
        nRleVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
        nRowOffsetComp++;
      }

      if(pDibBits)
      {
        PhotoshopSetPixel(pDibBits, nRowBytes, nRow, nCol, nChanID, nRleVal);
      }

      nCol++;
    }
  }

  // Now that we've finished the row, compare the decompressed size
  // to the expected width, and the bytes used to the row's length (a last
  // run can go past it)
  if((!bDecOk) || (nCol != nRowBytes) || (nRowOffsetComp > nRowLen))
  {
    m_pLog->AddLineErr(QString("ERROR: RLE row %1 at [0x%2] doesn't decode to %3 bytes.")
                         .arg(nRow).arg(nPosRow, 8, 16, QChar('0')).arg(nRowBytes));
    return false;
  }

  return true;
}

// Parse the Photoshop Image Data Section (the composite image)
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// - pDibTemp   = Where to draw the composite image (nullptr if disabled)
// OUTPUT:
// - nPos               = File position after reading the block
//
// NOTE:
// - Only 8-bit RGB and grayscale images are drawn
//
bool CDecodePs::PhotoshopParseImageData(uint32_t &nPos, uint32_t nIndent, tsImageInfo * psImageInfo, QImage *pDibTemp)
{
  Q_ASSERT(psImageInfo);

  QString strVal;

  //PhotoshopParseReportNote(nIndent,"Image data section:");
  PhotoshopParseReportFldOffset(nIndent, "Image data section:", nPos);

  bool bDecOk = true;

  //----
  uint32_t nWidth = psImageInfo->nImageWidth;
  uint32_t nHeight = psImageInfo->nImageHeight;
  uint32_t nNumChans = psImageInfo->nNumChans;

  //----

  if(!PsdFits(nPos, 2, "Image data"))
  {
    return false;
  }

  uint32_t nCompressionMethod = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent + 1, "Compression method", nCompressionMethod, "");

  if((nCompressionMethod != 0) && (nCompressionMethod != 1))
  {
    m_pLog->AddLineWarn("Unsupported compression method. Stopping.");
    return false;
  }

  // Photoshop's limits for a PSD file: up to 56 channels and 30,000 pixels a
  // side, with 1, 8, 16 or 32 bits a sample. (The sizes below are also
  // checked for overflow: a small file whose header gave 32768 channels of
  // 2^20 rows, 2^29 bytes each, passed the size check and then stepped
  // through billions of rows.)
  const uint32_t nDepth = psImageInfo->nDepthBpp;

  if((nNumChans > 56) || (nWidth > 30000) || (nHeight > 30000) ||
     ((nDepth != 1) && (nDepth != 8) && (nDepth != 16) && (nDepth != 32)))
  {
    m_pLog->AddLineErr(QString("ERROR: An image of %1x%2 pixels, %3 channels and %4 bits a sample is outside "
                               "the PSD limits. Stopping.").arg(nWidth).arg(nHeight).arg(nNumChans).arg(nDepth));
    return false;
  }

  // The channels' rows, one channel after another (64-bit: the product of
  // the 32-bit header fields used to wrap, so a 2-channel image 2^31 rows
  // tall got an empty row table and read past it)
  const uint64_t nRows = static_cast<uint64_t>(nNumChans) * nHeight;

  if(nRows == 0)
  {
    return true;
  }

  // Rows are counted in bytes (see PsdRowBytes)
  const uint64_t nRowBytes64 = PsdRowBytes(nWidth);

  if(nRowBytes64 > 0xFFFFFFFFull)
  {
    m_pLog->AddLineErr(QString("ERROR: Image rows of %1 bytes are too long. Stopping.").arg(nRowBytes64));
    return false;
  }

  const uint32_t nRowBytes = static_cast<uint32_t>(nRowBytes64);

  // RLE: a 16-bit compressed length per row comes first. The table and the
  // data it describes must be in the file, before anything is allocated.
  std::vector<uint16_t> anRowLen;

  if(nCompressionMethod == 1)
  {
    if(!PsdFits(nPos, PsdSize(nRows, 2), "Image data row length table"))
    {
      return false;
    }

    anRowLen.resize(nRows);
    uint64_t nDataLen = 0;

    for(uint64_t nRow = 0; nRow < nRows; nRow++)
    {
      anRowLen[nRow] = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
      nDataLen += anRowLen[nRow];
    }

    if(!PsdFits(nPos, nDataLen, "RLE image data"))
    {
      return false;
    }
  }
  else if(nRowBytes == 0)
  {
    // Empty raw rows (a width of 0) would pass the size check whatever the
    // height, and then be stepped through one by one
    return true;
  }
  else if(!PsdFits(nPos, PsdSize(nRows, nRowBytes), "Image data"))
  {
    return false;
  }

  // Allocate the preview. Only 8-bit RGB and grayscale images are drawn (a
  // 16-bit sample is two bytes, and the other colour modes need converting).
  unsigned char *pDibBits = nullptr;

#ifdef PS_IMG_DEC_EN
  m_bDibGray = (psImageInfo->nColorMode == 1);

  if(pDibTemp)
  {
    const bool bRgb = (psImageInfo->nColorMode == 3) && (nNumChans >= 3);

    if((psImageInfo->nDepthBpp != 8) || (!bRgb && !m_bDibGray))
    {
      PhotoshopParseReportNote(nIndent + 1, "Image not shown: only 8-bit RGB and grayscale images are drawn");
    }
    else if(nWidth == 0)
    {
      PhotoshopParseReportNote(nIndent + 1, "Image not shown: it has no pixels");
    }
    else
    {
      *pDibTemp = QImage(static_cast<int>(nWidth), static_cast<int>(nHeight), QImage::Format_RGB32);

      // PhotoshopSetPixel() assumes rows of exactly nWidth pixels
      if(pDibTemp->isNull() || (pDibTemp->bytesPerLine() != static_cast<qsizetype>(nWidth) * 4))
      {
        *pDibTemp = QImage();
        PhotoshopParseReportNote(nIndent + 1, "Image not shown: not enough memory");
      }
      else
      {
        pDibTemp->fill(Qt::black);
        pDibBits = pDibTemp->bits();
      }
    }
  }
#endif

  for(uint32_t nChan = 0; (bDecOk) && (nChan < nNumChans); nChan++)
  {
    // Channels that aren't drawn (alpha, spot colours) are still decoded if
    // RLE, which checks them, while their raw rows are skipped unread
    unsigned char *pDibChan = ((m_bDibGray && (nChan == 0)) || (!m_bDibGray && (nChan < 3))) ? pDibBits : nullptr;

    for(uint32_t nRow = 0; (bDecOk) && (nRow < nHeight); nRow++)
    {
      if(nCompressionMethod == 1)
      {
        uint32_t nRowLen = anRowLen[static_cast<uint64_t>(nChan) * nHeight + nRow];

        bDecOk = PhotoshopDecodeRowRle(nPos, nRowBytes, nRow, nRowLen, nChan, pDibChan);
      }
      else
      {
        bDecOk = PhotoshopDecodeRowUncomp(nPos, nRowBytes, nRow, nChan, pDibChan);
      }
    }
  }

  // A partly drawn image isn't shown
  if(!bDecOk && pDibBits)
  {
    *pDibTemp = QImage();
  }

  return bDecOk;
}

// Parse the Photoshop IRB Global layer mask info
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseGlobalLayerMaskInfo(uint32_t &nPos, uint32_t nIndent, uint32_t nPosLimit)
{
  QString strVal;

  bool bDecOk = true;

  PhotoshopParseReportNote(nIndent, "Global layer mask info:");
  nIndent++;

  uint32_t nInfoLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  if(nInfoLen == 0)
  {
    return bDecOk;
  }

  if(!PsdFitsIn(nPos, nInfoLen, nPosLimit, "Global layer mask info", "the layer and mask information"))
  {
    return false;
  }

  uint32_t nPosStart = nPos;

  m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);   // unsigned nOverlayColorSpace
  m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);   // unsigned nColComp1
  m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);   // unsigned nColComp2
  m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);   // unsigned nColComp3
  m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);   // unsigned nColComp4
  m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);   // unsigned nOpacity
  m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);   // unsigned nKind

  // Variable Filler: zeros
  //nPos += nInfoLen - 17;
  nPos = nPosStart + nInfoLen;

  return bDecOk;
}

// Parse the Photoshop IRB Additional layer info
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// - nAlign     = What the length is padded to: 2 in a layer record (Adobe's
//                specification rounds the length up to an even number), and
//                4 at the end of the layer and mask section (as psd-tools
//                reads Photoshop's files)
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseAddtlLayerInfo(uint32_t &nPos, uint32_t nIndent, uint32_t nAlign, uint32_t nPosLimit,
                                             const QString &strIn)
{
  bool bDecOk = true;

  uint32_t nPosStart;

  PhotoshopParseReportNote(nIndent, "Additional layer info:");
  nIndent++;

  // Signature, key and length, in what holds the block (strIn, ending at
  // nPosLimit), as is the block
  if(!PsdFitsIn(nPos, 12, nPosLimit, "Additional layer info", strIn))
  {
    return false;
  }

  QString strSig = m_pWBuf->BufReadStrn(nPos, 4);

  nPos += 4;

  // Photoshop also writes "8B64" (Adobe's specification allows both)
  if((strSig != "8BIM") && (strSig != "8B64"))
  {
    // Signature did not match!
    QString strError;

    strError = QString("ERROR: Addtl Layer Info signature unknown [%1] @ 0x%2").arg(strSig).arg(nPos - 4, 8, 16, QChar('0'));
    PhotoshopParseReportNote(nIndent, strError);

#ifdef DEBUG_LOG
    QString strDebug;

    strDebug =
      QString("## File=[%1] Block=[%2] Error=[%3]\n").arg(m_pAppConfig->strCurFname, -100).arg("PsDecode", -10).arg(strError);
    qDebug() << (strDebug);
#else
    Q_ASSERT(false);
#endif
    bDecOk = false;
    return false;
  }

  QString strKey = m_pWBuf->BufReadStrn(nPos, 4);

  nPos += 4;
  PhotoshopParseReportFldStr(nIndent, "Key", strKey);
  bool bLen8B = false;

  // FIXME: "Lr16" (the layers of a 16-bit image) is only shown in hex.
  // (A Q_ASSERT here stopped a debug build on any 16-bit image with layers.)
  /*
     //CAL! FIXME: Empirically, I didn't see 8B length being used by "FMsk" in psd-flame-icon.psd @ 0x7A458
     if (strKey == "LMsk") { bLen8B = true; }
     if (strKey == "Lr16") { bLen8B = true; }
     if (strKey == "Lr32") { bLen8B = true; }
     if (strKey == "Layr") { bLen8B = true; }
     if (strKey == "Mt16") { bLen8B = true; }
     if (strKey == "Mt32") { bLen8B = true; }
     if (strKey == "Mtrn") { bLen8B = true; }
     if (strKey == "Alph") { bLen8B = true; }
     if (strKey == "FMsk") { bLen8B = true; }
     if (strKey == "lnk2") { bLen8B = true; }
     if (strKey == "FEid") { bLen8B = true; }
     if (strKey == "FXid") { bLen8B = true; }
     if (strKey == "PxSD") { bLen8B = true; }
   */

  uint32_t nLen;

  if(!bLen8B)
  {
    nLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  }
  else
  {
    // DUMMY for now
    //FIXME: Untested
    nLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
    Q_ASSERT(nLen == 0);
    nLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  }
  PhotoshopParseReportFldNum(nIndent, "Length", nLen, "");

  // A length near 4 GB wrapped the position back to this block, so the
  // caller's loop never ended
  if(!PsdFitsIn(nPos, nLen, nPosLimit, QString("Additional layer info [%1]").arg(strKey), strIn))
  {
    return false;
  }

  if(nLen > 0)
  {
    PhotoshopParseReportFldHex(nIndent, strKey, nPos, nLen);
  }

  // The values below are in the block, as for a resource's (see
  // BlockOverrun()): they were read past its length, from the next block
  {
    uint32_t nVal;
    QString sStr;
    uint32_t nPosOffset = 0;

    struct TaggedScope
    {
      CDecodePs &rPs;
      explicit TaggedScope(CDecodePs &rDec, uint32_t nPosEnd) : rPs(rDec)
      {
        rPs.m_bInResBlock = true;
        rPs.m_nPosBlockEnd = nPosEnd;
        rPs.m_bBlockOverrun = false;
        rPs.m_strBlockKind = "additional layer info";
      }
      ~TaggedScope()
      {
        rPs.m_bInResBlock = false;
        rPs.m_strBlockKind = "8BIM resource";
      }
      TaggedScope(const TaggedScope &) = delete;
      TaggedScope &operator = (const TaggedScope &) = delete;
    } sScope(*this, nPos + nLen);

    uint32_t nPosVal = nPos;
    const bool bFourBytes = (strKey == "lnsr") || (strKey == "lyid") || (strKey == "clbl") || (strKey == "infx") ||
                            (strKey == "knko");

    if(strKey == "luni")
    {
      sStr = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
      PhotoshopParseReportFldStr(nIndent, "Layer Name (Unicode)", sStr);
    }
    else if(bFourBytes && !PsNeed(nPosVal, 4, QString("[%1] value").arg(strKey)))
    {
      // (Reported)
    }
    else if(strKey == "lnsr")
    {
      nVal = m_pWBuf->BufX(nPos, 4, PS_BSWAP);
      PhotoshopParseReportFldNum(nIndent, "Layer Name Source ID", nVal, "");
    }
    else if(strKey == "lyid")
    {
      nVal = m_pWBuf->BufX(nPos, 4, PS_BSWAP);
      PhotoshopParseReportFldNum(nIndent, "Layer ID", nVal, "");
    }
    else if(strKey == "clbl")
    {
      nVal = m_pWBuf->BufX(nPos, 4, PS_BSWAP);
      PhotoshopParseReportFldBool(nIndent, "Blend clipped elements", nVal);
    }
    else if(strKey == "infx")
    {
      nVal = m_pWBuf->BufX(nPos, 4, PS_BSWAP);
      PhotoshopParseReportFldBool(nIndent, "Blend interior elements", nVal);
    }
    else if(strKey == "knko")
    {
      nVal = m_pWBuf->BufX(nPos, 4, PS_BSWAP);
      PhotoshopParseReportFldBool(nIndent, "Knockout", nVal);
    }
  }

  nPosStart = nPos;

  // Skip data
  nPos = nPosStart + nLen;

  // Pad the length to a multiple of nAlign. (It was always 4, so after a
  // 10-byte "luni" name, such as "Cat", the next block was read 2 bytes late
  // and its signature reported as unknown.)
  uint32_t nMod;

  nMod = (nLen % nAlign);
  if(nMod != 0)
  {
    nPos += (nAlign - nMod);
  }

  return bDecOk;
}

// -------------

// Parse the Photoshop IRB Image Resources Section
// - Iteratively works through the individual Image Resource Blocks (IRB)
//
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseImageResourcesSection(uint32_t &nPos, uint32_t nIndent)
{
  QString strVal;

  bool bDecOk = true;

  PhotoshopParseReportNote(nIndent, "Image Resources Section:");
  nIndent++;

  uint32_t nPosSectionStart = 0;

  uint32_t nPosSectionEnd = 0;

  uint32_t nImgResLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Length", nImgResLen, "");

  // (A length past the end of the file could also wrap the section's end)
  if(!PsdFits(nPos, nImgResLen, "Image resources section"))
  {
    return false;
  }

  nPosSectionStart = nPos;
  nPosSectionEnd = nPos + nImgResLen;

  while(nPos < nPosSectionEnd)
  {
    bDecOk &= PhotoshopParseImageResourceBlock(nPos, nIndent, nPosSectionEnd);
    if(!bDecOk)
    {
      // As for the layers: the rest of the file can still be decoded
      m_pLog->AddLineWarn("NOTE: Skipping the rest of the image resources, to decode the layers and image data after them");
      nPos = nPosSectionEnd;
      return true;
    }
  }
  return bDecOk;
}

// Parse the Photoshop IRB (Image Resource Block)
// - Decode the 8BIM ID and invoke the appropriate IRB parsing handler
//
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
bool CDecodePs::PhotoshopParseImageResourceBlock(uint32_t &nPos, uint32_t nIndent, uint32_t nPosLimit)
{
  QString strVal;

  //bool          bDecOk = true;

  //PhotoshopParseReportNote(nIndent,"Image Resource Block:");
  //nIndent++;

  //
  // - Signature  (4B)                                            "8BIM"
  // - NameLen    (2B)                                            Generally 0
  // - Name               (NameLen Bytes, min 1 Byte)     Usually empty (NULL)
  // - BimLen             (4B)                                            Length of 8BIM data
  // - Data               (1..BimLen)                                     Field data
  //

  QString strSig = m_pWBuf->BufReadStrn(nPos, 4);

  nPos += 4;

  if(strSig != "8BIM")
  {
    // Signature did not match!
    QString strError;

    strError = QString("ERROR: IRB signature unknown [%1]").arg(strSig);
    PhotoshopParseReportNote(nIndent, strError);

#ifdef DEBUG_LOG
    QString strDebug;

    strDebug =
      QString("## File=[%1] Block=[%2] Error=[%3]\n")
        .arg(m_pAppConfig->strCurFname, -100)
        .arg("PsDecode", -10)
        .arg(strError);
    qDebug() << (strDebug);
#else
    Q_ASSERT(false);
#endif

    return false;;
  }

  // The header (ID, name and length) is in the segment or section too: past
  // it the name was read, up to 255 bytes, and a length of 0 from past the
  // file's end was said to run past the segment. (Every byte past the end
  // reads as 0.)
  const uint64_t nPosHeaderLimit = std::min<uint64_t>(nPosLimit, m_pWBuf->GetPosEof());
  const uint32_t nPosHeader = nPos - 4;
  const uint32_t nResNameLen = ((nPos <= nPosHeaderLimit) && (nPosHeaderLimit - nPos >= 3)) ? Buf(nPos + 2) : 0;
  const uint64_t nHeaderLen = 2 + 1 + nResNameLen + (((1 + nResNameLen) % 2 != 0) ? 1 : 0) + 4;

  if((nPos > nPosHeaderLimit) || (nHeaderLen > nPosHeaderLimit - nPos))
  {
    m_pLog->AddLineErr(QString("ERROR: 8BIM header at [0x%1] runs past the end of its segment, at [0x%2]. Stopping.")
                         .arg(nPosHeader, 8, 16, QChar('0'))
                         .arg(nPosHeaderLimit, 8, 16, QChar('0')));
    nPos = static_cast<uint32_t>(nPosHeaderLimit);
    return false;
  }

  uint32_t nBimId = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);
  m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);   // (nResNameLen)

  // Its name: a byte that doesn't print is shown as a dot (as in an APPn
  // identifier), rather than left out ("Caf\xe9" as "Caf")
  QString strResName;

  for(uint32_t nInd = 0; nInd < nResNameLen; nInd++)
  {
    const uint8_t nChar = Buf(nPos + nInd);

    strResName += IsPrintAscii(nChar) ? QChar(nChar) : QChar('.');
  }

  nPos += nResNameLen;

  // If size wasn't even, pad here
  if((1 + nResNameLen) % 2 != 0)
  {
    nPos += 1;
  }

  uint32_t nBimLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  QString strTmp;
  QString strByte;

  // Lookup 8BIM defined name
  QString strBimDefName = "";

  bool bBimKnown = false;

  uint32_t nFldInd = 0;

  bBimKnown = FindBimRecord(nBimId, nFldInd);

  if(bBimKnown)
  {
    strBimDefName = asBimRecords[nFldInd].strRecordName;
  }

  // (The resource's own name: this showed a name never set, so always "")
  strTmp = QString("8BIM: [0x%1] Name=\"%2\" Len=[0x%3] DefinedName=\"%4\"")
      .arg(nBimId, 4, 16, QChar('0'))
      .arg(strResName)
      .arg(nBimLen, 4, 16, QChar('0'))
      .arg(strBimDefName);
  PhotoshopParseReportNote(nIndent, strTmp);

  nIndent++;

  // A length that runs past the containing segment, or wraps the 32-bit
  // position back to this block, would make the caller loop forever or parse
  // gigabytes past the end. Clamp it to the data actually available: a
  // segment cut short by the end of the file ends there (past it every byte
  // reads as 0, and a count of 0xFFFF gave 16,000 Layer IDs of 0).
  nPosLimit = qMin(nPosLimit, m_pWBuf->GetPosEof());

  if((nPos > nPosLimit) || (nBimLen > nPosLimit - nPos))
  {
    const uint32_t nBimLenAvail = (nPos < nPosLimit) ? (nPosLimit - nPos) : 0;

    strTmp = QString("ERROR: 8BIM length [0x%1] runs past the end of its segment; using [0x%2]")
        .arg(nBimLen, 8, 16, QChar('0'))
        .arg(nBimLenAvail, 8, 16, QChar('0'));
    m_pLog->AddLineErr(strTmp);
    nBimLen = nBimLenAvail;
  }

  m_nPosBlockEnd = nPos + nBimLen;

  // The block's end limits the string readers while it is parsed, on every
  // return path (the PSD layer records use the readers outside any block)
  struct BlockScope
  {
    bool &bInBlock;
    explicit BlockScope(bool &bFlag) : bInBlock(bFlag) { bInBlock = true; }
    ~BlockScope() { bInBlock = false; }
  } sBlockScope(m_bInResBlock);
  m_bBlockOverrun = false;

  if(nBimLen == 0)
  {
    // If length is zero, then skip this block
    PhotoshopParseReportNote(nIndent, "Length is zero. Skipping.");
  }
  else if(bBimKnown)
  {
    // Save the file pointer
    uint32_t nPosSaved;

    nPosSaved = nPos;

    // Calculate the end of the record
    // - This is used for parsing records that have a conditional parsing
    //   of additional fields "if length permits".
    uint32_t nPosEnd;

    nPosEnd = nPos + nBimLen - 1;

    switch (asBimRecords[nFldInd].eBimType)
    {

      case BIM_T_STR:
        strVal = DecodeIptcValue(IPTC_T_STR, nBimLen, nPos);
        PhotoshopParseReportFldStr(nIndent, strBimDefName, strVal);
        break;
      case BIM_T_HEX:
        PhotoshopParseReportFldHex(nIndent, strBimDefName, nPos, nBimLen);
        nPos += nBimLen;
        break;

      case BIM_T_PS_THUMB_RES:
        PhotoshopParseThumbnailResource(nPos, nIndent);
        // Since this block has image data that we are not going to parse,
        // force the end position to match the length
        nPos = nPosSaved + nBimLen;
        break;
      case BIM_T_PS_SLICES:
        PhotoshopParseSliceHeader(nPos, nIndent, nPosEnd);
        break;
      case BIM_T_PS_DESCRIPTOR:
        PhotoshopParseDescriptor(nPos, nIndent);
        break;
      case BIM_T_PS_VER_INFO:
        PhotoshopParseVersionInfo(nPos, nIndent);
        break;
      case BIM_T_PS_PRINT_SCALE:
        PhotoshopParsePrintScale(nPos, nIndent);
        break;
      case BIM_T_PS_PIXEL_ASPECT_RATIO:
        PhotoshopParsePixelAspectRatio(nPos, nIndent);
        break;
      case BIM_T_PS_DOC_SPECIFIC_SEED:
        PhotoshopParseDocSpecificSeed(nPos, nIndent);
        break;
      case BIM_T_PS_RESOLUTION_INFO:
        PhotoshopParseResolutionInfo(nPos, nIndent);
        break;
      case BIM_T_PS_GRID_GUIDES:
        PhotoshopParseGridGuides(nPos, nIndent);
        break;
      case BIM_T_PS_GLOBAL_ANGLE:
        PhotoshopParseGlobalAngle(nPos, nIndent);
        break;
      case BIM_T_PS_GLOBAL_ALTITUDE:
        PhotoshopParseGlobalAltitude(nPos, nIndent);
        break;
      case BIM_T_PS_PRINT_FLAGS:
        PhotoshopParsePrintFlags(nPos, nIndent);
        break;
      case BIM_T_PS_PRINT_FLAGS_INFO:
        PhotoshopParsePrintFlagsInfo(nPos, nIndent);
        break;
      case BIM_T_PS_COPYRIGHT_FLAG:
        PhotoshopParseCopyrightFlag(nPos, nIndent);
        break;
      case BIM_T_PS_LAYER_STATE_INFO:
        PhotoshopParseLayerStateInfo(nPos, nIndent);
        break;
      case BIM_T_PS_LAYER_GROUP_INFO:
        PhotoshopParseLayerGroupInfo(nPos, nIndent, nBimLen);
        break;
      case BIM_T_PS_LAYER_GROUP_ENABLED:
        PhotoshopParseLayerGroupEnabled(nPos, nIndent, nBimLen);
        break;
      case BIM_T_PS_LAYER_SELECT_ID:
        PhotoshopParseLayerSelectId(nPos, nIndent);
        break;

      case BIM_T_PS_STR_UNI:
        PhotoshopParseStringUni(nPos, nIndent);
        break;
      case BIM_T_PS_STR_ASC:
        strVal += m_pWBuf->BufReadStrn(nPos, nBimLen);
        nPos += nBimLen;
        PhotoshopParseReportFldStr(nIndent, strBimDefName, strVal);
        break;
      case BIM_T_PS_STR_ASC_LONG:
        strVal = "\n";
        strVal += m_pWBuf->BufReadStrn(nPos, nBimLen);
        nPos += nBimLen;
        PhotoshopParseReportFldStr(nIndent, strBimDefName, strVal);
        break;

      case BIM_T_JPEG_QUAL:    // (0x0406)
        // JPEG Quality
        PhotoshopParseJpegQuality(nPos, nIndent, nPosEnd);
        break;

      case BIM_T_IPTC_NAA:     // (0x0404)
        // IPTC-NAA
        DecodeIptc(nPos, nBimLen, nIndent);
        break;

      default:
        // Not decoded (0x03E8): skipped whole. (Parsing went on inside it, so
        // an "8BIM" in its data was reported as the next resource.)
        nPos = nPosSaved + nBimLen;
        break;
    }                           // switch

    // Check to see if we ended up with a length mismatch after decoding
    if(nPos > nPosEnd + 1)
    {
      // Length mismatch detected: we read too much (versus length)
      strTmp = QString("ERROR: Parsing exceeded expected length. Stopping decode. BIM=[%1], CurPos=[0x%2], ExpPosEnd=[0x%3], ExpLen=[%4]")
          .arg(strBimDefName)
          .arg(nPos, 8, 16, QChar('0'))
          .arg(nPosEnd + 1, 8, 16, QChar('0'))
          .arg(nBimLen);
      m_pLog->AddLineErr(strTmp);
#ifdef DEBUG_LOG
      QString strDebug;

      strDebug = QString("## File=[%1] Block=[%2] Error=[%3]\n")
          .arg(m_pAppConfig->strCurFname, -100)
          .arg("PsDecode", -10)
          .arg(strTmp);
      qDebug() << (strDebug);
#else
      Q_ASSERT(false);
#endif
      // TODO: Add interactive error message here

      // Now we should roll-back the file position to the position indicated
      // by the length. This will help us stay on track for next decoding.
      // (Then on after the block's pad byte, below: this left the position a
      // byte early after an odd length, and every resource after it was lost.)
    }
    else if(nPos != nPosEnd + 1)
    {
      // Length mismatch detected: we read too little (versus length)
      // This is generally an indication that either I haven't accurately captured the
      // specific block parsing format or else the specification is loose.
      strTmp = QString("WARNING: Parsing offset length mismatch. Current pos=[0x%1], expected end pos=[0x%2], expect length=[%3]")
          .arg(nPos, 8, 16, QChar('0'))
          .arg(nPosEnd + 1, 8, 16, QChar('0'))
          .arg(nBimLen);
      m_pLog->AddLineWarn(strTmp);
#ifdef DEBUG_LOG
      QString strDebug;

      strDebug = QString("## File=[%1] Block=[%2] Error=[%3]\n")
          .arg(m_pAppConfig->strCurFname, -100)
          .arg("PsDecode", -10)
          .arg(strTmp);
      qDebug() << (strDebug);
#endif
      // The block is skipped by its length below: parsing went on from where
      // it stopped, inside the block, and lost the resources after it. Unless
      // no resource starts where the length ends, and one does where the
      // fields did: then the length is taken to be wrong, as it always was.
      const uint32_t nPosByLen = nPosSaved + nBimLen + (nBimLen % 2);

      if((m_pWBuf->BufReadStrn(nPosByLen, 4) != "8BIM") && (m_pWBuf->BufReadStrn(nPos, 4) == "8BIM"))
      {
        m_pLog->AddLineWarn(QString("NOTE: No 8BIM resource where its length ends, at [0x%1], but one where its fields "
                                    "do, at [0x%2]: going on there")
                              .arg(nPosByLen, 8, 16, QChar('0'))
                              .arg(nPos, 8, 16, QChar('0')));
        return true;
      }
    }

    // Reset the file marker
    nPos = nPosSaved;
  }                             // bBimKnown

  // Skip rest of 8BIM
  nPos += nBimLen;

  // FIXME: correct to make for even parity?
  if((nBimLen % 2) != 0)
    nPos++;

  return true;
}

// Parse the Photoshop IRB Slice Header
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// - nPosEnd    = File position at end of block (last byte address)
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseSliceHeader(uint32_t &nPos, uint32_t nIndent, uint32_t nPosEnd)
{
  uint32_t nVal;

  QString strVal;

  uint32_t nPosOffset;

  PhotoshopParseReportNote(nIndent, "Slice Header:");
  nIndent++;

  // Each fixed-size part is in the block (they were read on past it)
  if(!PsNeed(nPos, 4, "Slice header version"))
  {
    return;
  }

  uint32_t nSliceVer = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Version", nSliceVer, "");

  if((nSliceVer == 6) && !PsNeed(nPos, 16, "Bound Rect"))
  {
    return;
  }

  if(nSliceVer == 6)
  {
    nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent, "Bound Rect (top)", nVal, "");
    nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent, "Bound Rect (left)", nVal, "");
    nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent, "Bound Rect (bottom)", nVal, "");
    nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent, "Bound Rect (right)", nVal, "");
    strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
    nPos += nPosOffset;
    PhotoshopParseReportFldStr(nIndent, "Name of group of slices", strVal);

    if(!PsNeed(nPos, 4, "Number of slices"))
    {
      return;
    }

    uint32_t nNumSlices = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

    PhotoshopParseReportFldNum(nIndent, "Number of slices", nNumSlices, "");

    // Slice resource block
    if(nNumSlices > 0)
      PhotoshopParseReportNote(nIndent, "-----");
    for(uint32_t nSliceInd = 0; (nSliceInd < nNumSlices) && (nPos < m_nPosBlockEnd); nSliceInd++)
    {
      const uint32_t nPosSlice = nPos;
      QString strSliceInd;

      strSliceInd = QString("Slice #%1:").arg(nSliceInd);
      PhotoshopParseReportNote(nIndent, strSliceInd);
      PhotoshopParseSliceResource(nPos, nIndent + 1, nPosEnd);

      // Every slice must move forward (as for descriptor items)
      if(nPos <= nPosSlice)
      {
        PhotoshopParseReportNote(nIndent, "ERROR: Slice didn't advance. Stopping.");
        nPos = m_nPosBlockEnd;
        break;
      }
    }
    if(nNumSlices > 0)
      PhotoshopParseReportNote(nIndent, "-----");
  }
  else if(((nSliceVer == 7) || (nSliceVer == 8)) && PsNeed(nPos, 4, "Descriptor version"))
  {
    nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent, "Descriptor version", nVal, "");
    PhotoshopParseDescriptor(nPos, nIndent);
  }
}

// Parse the Photoshop IRB Slice Resource
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// - nPosEnd    = File position at end of block (last byte address)
// OUTPUT:
// - nPos               = File position after reading the block
// NOTE:
// - The nPosEnd is supplied as this resource block depends on some
//   conditional field parsing that is "as length allows"
//
void CDecodePs::PhotoshopParseSliceResource(uint32_t &nPos, uint32_t nIndent, uint32_t nPosEnd)
{
  uint32_t nVal;

  QString strVal;

  uint32_t nPosOffset;

  PhotoshopParseReportNote(nIndent, "Slice Resource:");
  nIndent++;

  if(!PsNeed(nPos, 12, "Slice ID"))
  {
    return;
  }

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "ID", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Group ID", nVal, "");
  uint32_t nOrigin = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Origin", nOrigin, "");

  if((nOrigin == 1) && !PsNeed(nPos, 4, "Associated Layer ID"))
  {
    return;
  }

  if(nOrigin == 1)
  {
    nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent, "Associated Layer ID", nVal, "");
  }

  strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
  nPos += nPosOffset;
  PhotoshopParseReportFldStr(nIndent, "Name", strVal);

  if(!PsNeed(nPos, 20, "Slice type"))
  {
    return;
  }

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Type", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Position (top)", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Position (left)", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Position (bottom)", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Position (right)", nVal, "");
  strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
  nPos += nPosOffset;
  PhotoshopParseReportFldStr(nIndent, "URL", strVal);
  strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
  nPos += nPosOffset;
  PhotoshopParseReportFldStr(nIndent, "Target", strVal);
  strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
  nPos += nPosOffset;
  PhotoshopParseReportFldStr(nIndent, "Message", strVal);
  strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
  nPos += nPosOffset;
  PhotoshopParseReportFldStr(nIndent, "Alt Tag", strVal);

  if(!PsNeed(nPos, 1, "Cell text is HTML"))
  {
    return;
  }

  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Cell text is HTML", nVal);
  strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
  nPos += nPosOffset;
  PhotoshopParseReportFldStr(nIndent, "Cell text", strVal);

  if(!PsNeed(nPos, 12, "Horizontal alignment"))
  {
    return;
  }

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Horizontal alignment", nVal, "");
  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Vertical alignment", nVal, "");
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Alpha color", nVal, "");
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Red", nVal, "");
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Green", nVal, "");
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Blue", nVal, "");

  // Per standard, we are only supposed to parse additional fields
  // "as length allows". This is not a particularly good format
  // definition. At this point we'll perform a check to see if
  // we have exhausted the length of the block. If not, continue
  // to parse the next fields.
  // TODO: Do we need to be any more precise than this? (ie. check
  // after reading the descriptor version?)
  // (Its 4 bytes have to be there: with 1 to 3 left they were read past it)
  if((nPos <= nPosEnd) && PsNeed(nPos, 4, "Descriptor version"))
  {
    nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
    PhotoshopParseReportFldNum(nIndent, "Descriptor version", nVal, "");

    PhotoshopParseDescriptor(nPos, nIndent);
  }
  else
  {
    // We ran out of space in the block so end now
  }
}

// Parse the Photoshop IRB JPEG Quality
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// - nPosEnd    = File position at end of block (last byte address)
// OUTPUT:
// - nPos               = File position after reading the block
// NOTE:
// - This IRB is private, so reverse-engineered and may not be per spec
//
void CDecodePs::PhotoshopParseJpegQuality(uint32_t &nPos, uint32_t nIndent, uint32_t)
{
  uint32_t nVal;

  QString strVal;

  uint32_t nSaveFormat;

  // Save As Quality
  // Index 0: Quality level
  if(!PsNeed(nPos, 2, "Photoshop Save As Quality"))
  {
    return;
  }

  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);

  switch (nVal)
  {
    case 0xFFFD:
      m_nQualitySaveAs = 1;
      break;
    case 0xFFFE:
      m_nQualitySaveAs = 2;
      break;
    case 0xFFFF:
      m_nQualitySaveAs = 3;
      break;
    case 0x0000:
      m_nQualitySaveAs = 4;
      break;
    case 0x0001:
      m_nQualitySaveAs = 5;
      break;
    case 0x0002:
      m_nQualitySaveAs = 6;
      break;
    case 0x0003:
      m_nQualitySaveAs = 7;
      break;
    case 0x0004:
      m_nQualitySaveAs = 8;
      break;
    case 0x0005:
      m_nQualitySaveAs = 9;
      break;
    case 0x0006:
      m_nQualitySaveAs = 10;
      break;
    case 0x0007:
      m_nQualitySaveAs = 11;
      break;
    case 0x0008:
      m_nQualitySaveAs = 12;
      break;
    default:
      m_nQualitySaveAs = 0;
      break;
  }

  if(m_nQualitySaveAs != 0)
  {
    PhotoshopParseReportFldNum(nIndent, "Photoshop Save As Quality", m_nQualitySaveAs, "");
  }

  // TODO: I have observed a file that had this block with length=2, which would end here
  // - Perhaps we should check against length in case some older files had a shorter format
  //   to this IRB?

  // The fields after it are read as far as the block goes (one of 2 bytes has
  // been seen, so older versions may have had fewer): they were read from the
  // next resource
  if(!BlockRoom(nPos, 2))
  {
    return;
  }

  // Index 1: Format
  nSaveFormat = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);

  switch (nSaveFormat)
  {
    case 0x0000:
      strVal = "Standard";
      break;
    case 0x0001:
      strVal = "Optimized";
      break;
    case 0x0101:
      strVal = "Progressive";
      break;
    default:
      strVal = "???";
      break;
  }

  PhotoshopParseReportFldStr(nIndent, "Photoshop Save Format", strVal);

  if(!BlockRoom(nPos, 2))
  {
    return;
  }

  // Index 2: Progressive Scans
  // - Only meaningful if Progressive mode
  nVal = m_pWBuf->BufRdAdv2(nPos, PS_BSWAP);

  switch (nVal)
  {
    case 0x0001:
      strVal = "3 Scans";
      break;
    case 0x0002:
      strVal = "4 Scans";
      break;
    case 0x0003:
      strVal = "5 Scans";
      break;
    default:
      strVal = "???";
      break;
  }

  PhotoshopParseReportFldStr(nIndent, "Photoshop Save Progressive Scans", strVal);

  if(!BlockRoom(nPos, 1))
  {
    return;
  }

  // Not sure what this byte is for
  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "???", nVal, "");
}

// Decode the OSType field and invoke the appropriate IRB parsing handler
//
// INPUT:
// - nPos               = File position at start of block
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the block
//
void CDecodePs::PhotoshopParseHandleOsType(QString strOsType, uint32_t &nPos, uint32_t nIndent)
{
  // An item's value is read, or at least skipped, so that the next item is
  // read where it starts. (Types that weren't decoded left the value unread,
  // which was then taken for the next key's length.) A reference ("obj ") or a
  // type not known can't be skipped: the rest of the block isn't decoded.
  uint32_t nVal1;
  uint32_t nVal2;
  QString strVal;

  if(strOsType == "doub")
  {
    if(PsNeed(nPos, 8, "Double"))
    {
      nVal1 = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
      nVal2 = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
      PhotoshopParseReportFldDoublePt(nIndent, "Value", nVal1, nVal2, "");
    }
  }
  else if(strOsType == "UntF")
  {
    // A unit (an OSType: "#Ang", "#Pxl", ...) and a double
    if(PsNeed(nPos, 12, "Unit float"))
    {
      strVal = QString(QChar(Buf(nPos + 0))) + QChar(Buf(nPos + 1)) + QChar(Buf(nPos + 2)) + QChar(Buf(nPos + 3));
      nPos += 4;
      PhotoshopParseReportFldStr(nIndent, "Units", strVal);
      nVal1 = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
      nVal2 = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
      PhotoshopParseReportFldDoublePt(nIndent, "Value", nVal1, nVal2, "");
    }
  }
  else if((strOsType == "type") || (strOsType == "GlbC"))
  {
    // A class: its name, then its ID
    uint32_t nPosOffset;

    strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
    nPos += nPosOffset;
    PhotoshopParseReportFldStr(nIndent, "Name from classID", strVal);
    strVal = PhotoshopParseGetLStrAsc(nPos);
    PhotoshopParseReportFldStr(nIndent, "classID", strVal);
  }
  else if((strOsType == "alis") || (strOsType == "tdta"))
  {
    // Data of a given length, not decoded
    if(PsNeed(nPos, 4, "Data length"))
    {
      const uint32_t nDataLen = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

      if(PsNeed(nPos, nDataLen, QString("Data of length %1").arg(nDataLen)))
      {
        PhotoshopParseReportFldNum(nIndent, "Data", nDataLen, "bytes");
        nPos += nDataLen;
      }
    }
  }
  else if(strOsType == "Objc")
  {
    PhotoshopParseDescriptor(nPos, nIndent);
  }
  else if(strOsType == "VlLs")
  {
    PhotoshopParseList(nPos, nIndent);
  }
  else if(strOsType == "TEXT")
  {
    PhotoshopParseStringUni(nPos, nIndent);
  }
  else if(strOsType == "enum")
  {
    PhotoshopParseEnum(nPos, nIndent);
  }
  else if(strOsType == "long")
  {
    // The fixed-size values have to be inside the resource too
    if(BlockRoom(nPos, 4))
    {
      PhotoshopParseInteger(nPos, nIndent);
    }
    else
    {
      BlockOverrun(nPos, "Integer");
    }
  }
  else if(strOsType == "bool")
  {
    if(BlockRoom(nPos, 1))
    {
      PhotoshopParseBool(nPos, nIndent);
    }
    else
    {
      BlockOverrun(nPos, "Boolean");
    }
  }
  else if(strOsType == "GlbO")
  {
    PhotoshopParseDescriptor(nPos, nIndent);
  }
  else
  {
    // A reference ("obj ") or a type not known: its length can't be told
    m_pLog->AddLineWarn(QString("NOTE: Descriptor item type [%1] at [0x%2] isn't decoded, so the rest of its %3 "
                                "isn't either")
                          .arg(strOsType)
                          .arg(nPos - 4, 8, 16, QChar('0'))
                          .arg(m_strBlockKind));
    nPos = m_nPosBlockEnd;
  }
}

// Parse the Photoshop IRB OSType Descriptor
// INPUT:
// - nPos               = File position at start of entry
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the entry
//
void CDecodePs::PhotoshopParseDescriptor(uint32_t &nPos, uint32_t nIndent)
{
  QString strVal;

  uint32_t nPosOffset;

  // Descriptors and lists nest through OSTypes; limit the depth so a crafted
  // file can't recurse until the stack runs out. Jumping to the block end also
  // ends the enclosing loops.
  if(m_nDescriptorDepth >= 32)
  {
    PhotoshopParseReportNote(nIndent, "ERROR: Descriptor nesting too deep. Stopping.");
    nPos = m_nPosBlockEnd;
    return;
  }

  m_nDescriptorDepth++;

  PhotoshopParseReportNote(nIndent, "Descriptor:");
  nIndent++;

  strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
  nPos += nPosOffset;
  PhotoshopParseReportFldStr(nIndent, "Name from classID", strVal);

  strVal = PhotoshopParseGetLStrAsc(nPos);
  PhotoshopParseReportFldStr(nIndent, "classID", strVal);

  // The count, and each item's type, have to be inside the resource too
  if(!BlockRoom(nPos, 4))
  {
    BlockOverrun(nPos, "Descriptor item count");
    m_nDescriptorDepth--;
    return;
  }

  uint32_t nDescNumItems = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Num items in descriptor", nDescNumItems, "");

  if(nDescNumItems > 0)
    PhotoshopParseReportNote(nIndent, "-----");

  for(uint32_t nDescInd = 0; (nDescInd < nDescNumItems) && (nPos < m_nPosBlockEnd); nDescInd++)
  {
    const uint32_t nPosItem = nPos;
    QString strDescInd;

    strDescInd = QString("Descriptor item #%1:").arg(nDescInd);
    PhotoshopParseReportNote(nIndent, strDescInd);

    strVal = PhotoshopParseGetLStrAsc(nPos);
    PhotoshopParseReportFldStr(nIndent + 1, "Key", strVal);

    if(!BlockRoom(nPos, 4))
    {
      BlockOverrun(nPos, "Descriptor item type");
      break;
    }

    QString strOsType;

    // Four characters (master: %c%c%c%c), not their codes
    strOsType = QString(QChar(Buf(nPos + 0))) + QChar(Buf(nPos + 1)) + QChar(Buf(nPos + 2)) + QChar(Buf(nPos + 3));
    nPos += 4;
    PhotoshopParseReportFldStr(nIndent + 1, "OSType key", strOsType);

    PhotoshopParseHandleOsType(strOsType, nPos, nIndent + 1);

    // Every item must move forward, or the count could keep it going
    if(nPos <= nPosItem)
    {
      PhotoshopParseReportNote(nIndent, "ERROR: Descriptor item didn't advance. Stopping.");
      nPos = m_nPosBlockEnd;
      break;
    }
  }

  if(nDescNumItems > 0)
    PhotoshopParseReportNote(nIndent, "-----");

  m_nDescriptorDepth--;
}

// Parse the Photoshop IRB OSType List
// INPUT:
// - nPos               = File position at start of entry
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the entry
//
void CDecodePs::PhotoshopParseList(uint32_t &nPos, uint32_t nIndent)
{
  QString strVal;
  QString strLine;

  // Same nesting limit as PhotoshopParseDescriptor()
  if(m_nDescriptorDepth >= 32)
  {
    PhotoshopParseReportNote(nIndent, "ERROR: List nesting too deep. Stopping.");
    nPos = m_nPosBlockEnd;
    return;
  }

  m_nDescriptorDepth++;

  // As for descriptors: the count and item types have to be inside the resource
  if(!BlockRoom(nPos, 4))
  {
    BlockOverrun(nPos, "List item count");
    m_nDescriptorDepth--;
    return;
  }

  uint32_t nNumItems = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);

  PhotoshopParseReportFldNum(nIndent, "Num items in list", nNumItems, "");

  if(nNumItems > 0)
    PhotoshopParseReportNote(nIndent, "-----");

  for(uint32_t nItemInd = 0; (nItemInd < nNumItems) && (nPos < m_nPosBlockEnd); nItemInd++)
  {
    const uint32_t nPosItem = nPos;
    QString strItemInd;

    strItemInd = QString("Item #%1:").arg(nItemInd);
    PhotoshopParseReportNote(nIndent, strItemInd);

    if(!BlockRoom(nPos, 4))
    {
      BlockOverrun(nPos, "List item type");
      break;
    }

    QString strOsType;

    // Four characters (master: %c%c%c%c), not their codes
    strOsType = QString(QChar(Buf(nPos + 0))) + QChar(Buf(nPos + 1)) + QChar(Buf(nPos + 2)) + QChar(Buf(nPos + 3));
    nPos += 4;
    PhotoshopParseReportFldStr(nIndent + 1, "OSType key", strOsType);

    PhotoshopParseHandleOsType(strOsType, nPos, nIndent + 1);

    // Every item must move forward (as for descriptor items)
    if(nPos <= nPosItem)
    {
      PhotoshopParseReportNote(nIndent, "ERROR: List item didn't advance. Stopping.");
      nPos = m_nPosBlockEnd;
      break;
    }
  }

  if(nNumItems > 0)
    PhotoshopParseReportNote(nIndent, "-----");

  m_nDescriptorDepth--;
}

// Parse the Photoshop IRB OSType Integer
// INPUT:
// - nPos               = File position at start of entry
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the entry
//
void CDecodePs::PhotoshopParseInteger(uint32_t &nPos, uint32_t nIndent)
{
  uint32_t nVal;

  QString strVal;
  QString strLine;

  nVal = m_pWBuf->BufRdAdv4(nPos, PS_BSWAP);
  PhotoshopParseReportFldNum(nIndent, "Value", nVal, "");
}

// Parse the Photoshop IRB OSType Boolean
// INPUT:
// - nPos               = File position at start of entry
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the entry
//
void CDecodePs::PhotoshopParseBool(uint32_t &nPos, uint32_t nIndent)
{
  uint32_t nVal;

  QString strVal;
  QString strLine;

  nVal = m_pWBuf->BufRdAdv1(nPos, PS_BSWAP);
  PhotoshopParseReportFldBool(nIndent, "Value", nVal);
}

// Parse the Photoshop IRB OSType Enumerated type
// INPUT:
// - nPos               = File position at start of entry
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the entry
//
void CDecodePs::PhotoshopParseEnum(uint32_t &nPos, uint32_t nIndent)
{
  QString strVal;
  QString strLine;

  strVal = PhotoshopParseGetLStrAsc(nPos);
  PhotoshopParseReportFldStr(nIndent, "Type", strVal);

  strVal = PhotoshopParseGetLStrAsc(nPos);
  PhotoshopParseReportFldStr(nIndent, "Enum", strVal);
}

// Parse the Photoshop IRB Unicode String
// INPUT:
// - nPos               = File position at start of entry
// - nIndent    = Indent level for formatted output
// OUTPUT:
// - nPos               = File position after reading the entry
// NOTE:
// - The string is in Photoshop Unicode format (length first)
//
void CDecodePs::PhotoshopParseStringUni(uint32_t &nPos, uint32_t nIndent)
{
  QString strVal;

  uint32_t nPosOffset;

  strVal = PhotoshopParseGetBimLStrUni(nPos, nPosOffset);
  nPos += nPosOffset;
  PhotoshopParseReportFldStr(nIndent, "String", strVal);
}
