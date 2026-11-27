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

#include <math.h>

#include <cmath>
#include <algorithm>
#include <limits>

#include <QtDebug>
#include <QtWidgets>

#include "SnoopConfig.h"

#include "ImgDecode.h"


// ------------------------------------------------------
// Settings

// Flag: Use fixed point arithmetic for IDCT?
//#define IDCT_FIXEDPT

// Flag: Do we stop during scan decode if 0xFF (but not pad)?
// TODO: Make this a config option
//#define SCAN_BAD_MARKER_STOP

// ------------------------------------------------------
// Main code

// Convert to int16_t, saturating at its limits (NaN gives the minimum).
// Corrupt coefficients can make IDCT results far larger than int16_t holds,
// and converting an out-of-range double to an integer type is undefined.
static int16_t SaturateInt16(double fVal)
{
  constexpr int16_t nMin = std::numeric_limits<int16_t>::min();
  constexpr int16_t nMax = std::numeric_limits<int16_t>::max();

  if(!(fVal > nMin))
  {
    return nMin;
  }

  if(!(fVal < nMax))
  {
    return nMax;
  }

  return static_cast<int16_t>(fVal);
}

// Clear the min/max/average pixel statistics. Each minimum starts at the
// largest value and each maximum at the smallest, so the first pixel sets
// both. (They started at 0: a white image showed min=0, a black one max=0.)
static void ResetHistoStats(PixelCcHisto &sHisto)
{
  memset(&sHisto, 0, sizeof(sHisto));

  for(int32_t *pnMin : { &sHisto.nPreclipYMin, &sHisto.nPreclipCbMin, &sHisto.nPreclipCrMin,
                         &sHisto.nClipYMin, &sHisto.nClipCbMin, &sHisto.nClipCrMin,
                         &sHisto.nClipRMin, &sHisto.nClipGMin, &sHisto.nClipBMin,
                         &sHisto.nPreclipRMin, &sHisto.nPreclipGMin, &sHisto.nPreclipBMin })
  {
    *pnMin = std::numeric_limits<int32_t>::max();
  }

  for(int32_t *pnMax : { &sHisto.nPreclipYMax, &sHisto.nPreclipCbMax, &sHisto.nPreclipCrMax,
                         &sHisto.nClipYMax, &sHisto.nClipCbMax, &sHisto.nClipCrMax,
                         &sHisto.nClipRMax, &sHisto.nClipGMax, &sHisto.nClipBMax,
                         &sHisto.nPreclipRMax, &sHisto.nPreclipGMax, &sHisto.nPreclipBMax })
  {
    *pnMax = std::numeric_limits<int32_t>::min();
  }
}

// One "component histo" line of the pixel statistics (zeros if no pixel
// was counted, instead of the starting extremes and a NaN average)
static QString HistoStatLine(const QString &strComp, int32_t nMin, int32_t nMax, int64_t nSum, uint64_t nCount)
{
  const bool bAny = (nCount != 0);
  const double dAvg = bAny ? static_cast<double>(nSum) / static_cast<double>(nCount) : 0.0;

  return QString("    %1 component histo: [min=%2 max=%3 avg=%4]")
      .arg(strComp, -2)
      .arg(bAny ? nMin : 0, 5)
      .arg(bAny ? nMax : 0, 5)
      .arg(dAvg, 7, 'f', 1);
}

// Constructor for the Image Decoder
// - This constructor is called only once by Document class
CimgDecode::CimgDecode(CDocLog *pLog, CwindowBuf *pWBuf, CSnoopConfig *pAppConfig, QObject *_parent) : QObject(_parent), m_pLog(pLog), m_pWBuf(pWBuf),
  m_pAppConfig(pAppConfig)
{
  m_bVerbose = false;

  ScanFillForget();

  m_pStatBar = 0;
  m_bImgDecoded = false;        // Image has not been decoded yet
  m_bDibTempReady = false;
  m_bPreviewIsJpeg = false;

  // Preview and histogram images (owned; replaced on each render)
  m_pDibTemp = nullptr;
  m_pDibHistRgb = nullptr;
  m_pDibHistY = nullptr;
  m_bDibHistRgbReady = false;
  m_bDibHistYReady = false;

  m_bHistEn = false;
  m_bStatClipEn = false;        // UNUSED

  m_pMcuFileMap = 0;
  m_nMcuPosPending = -1;
  m_nScanDataEnd = UINT32_MAX;
  m_pBlkDcValY = 0;
  m_pBlkDcValCb = 0;
  m_pBlkDcValCr = 0;
  m_pPixValY = 0;
  m_pPixValCb = 0;
  m_pPixValCr = 0;

  // Read or copied by Reset() before anything else sets them: master read
  // m_bScanErrorsDisable before ResetState() set it (UBSan saw a value of
  // 254 in a decoder on the stack), and then left m_nWarnBadScanNum unset
  m_bScanErrorsDisable = false;
  m_nWarnBadScanNum = 0;
  m_nWarnRstIndexNum = 0;
  m_nWarnRstCutNum = 0;
  m_nWarnRstShortNum = 0;
  m_nWarnRstSyncNum = 0;
  m_nRestartInterval = 0;

  // Reset the image decoding state
  Reset();

  m_nImgSizeXPartMcu = 0;
  m_nImgSizeYPartMcu = 0;
  m_nImgSizeX = 0;
  m_nImgSizeY = 0;

  // FIXME: Temporary hack to avoid divide-by-0 when displaying PSD (instead of JPEG)
  m_nMcuWidth = 1;
  m_nMcuHeight = 1;

  // Detailed VLC Decode mode
  m_bDetailVlc = false;
  m_nDetailVlcX = 0;
  m_nDetailVlcY = 0;
  m_nDetailVlcLen = 1;
  m_bDetailRgbDump = false;

  ResetHistoStats(m_sHisto);

  connect(this, SIGNAL(updateImage()), _parent, SLOT(updateImage()));

  // Set up the IDCT lookup tables
  PrecalcIdct();

  GenLookupHuffMask();

  // The following contain information that is set by
  // the JFIF Decoder. We can only reset them here during
  // the constructor and later by explicit call by JFIF Decoder.
  ResetState();

  // We don't call SetPreviewMode() here because it would
  // automatically try to recalculate the view (but nothing ready yet)
  m_nPreviewMode = PREVIEW_RGB;
  SetPreviewZoom(false, false, true, PRV_ZOOM_12);

  m_bDecodeScanAc = true;

  m_bViewOverlaysMcuGrid = false;

  // Start off with no YCC offsets for CalcChannelPreview()
  SetPreviewYccOffset(0, 0, 0, 0, 0);

  SetPreviewMcuInsert(0, 0, 0);

//  setStyleSheet("background-color: white");
}

// Reset decoding state for start of new decode
// Note that we don't touch the DQT or DHT entries as
// those are set at different times versus reset (sometimes
// before Reset() ).
void CimgDecode::Reset()
{
  qDebug() << "CimgDecode::Reset() Start";

  DecodeRestartScanBuf(0, false);
  DecodeRestartDcState();

  m_bRestartRead = false;       // No restarts seen yet
  m_nRestartRead = 0;
  m_nRestartWantInd = 0;
  m_bRestartLost = false;
  m_bRestartNoMore = false;

  m_nImgSizeXPartMcu = 0;
  m_nImgSizeYPartMcu = 0;
  m_nImgSizeX = 0;
  m_nImgSizeY = 0;
  m_nMcuXMax = 0;
  m_nMcuYMax = 0;
  m_nBlkXMax = 0;
  m_nBlkYMax = 0;

  m_bBrightValid = false;
  m_nBrightY = -32768;
  m_nBrightCb = -32768;
  m_nBrightCr = -32768;
  m_nBrightR = 0;
  m_nBrightG = 0;
  m_nBrightB = 0;
  m_ptBrightMcu.setX(0);
  m_ptBrightMcu.setY(0);

  m_bAvgYValid = false;
  m_nAvgY = 0;

  // Image has not been decoded yet
  m_bImgDecoded = false;

  // If a DIB has been generated, release it!
  if(m_bDibTempReady)
  {
    m_bDibTempReady = false;
  }

  if(m_bDibHistRgbReady)
  {
    m_bDibHistRgbReady = false;
  }

  if(m_bDibHistYReady)
  {
    m_bDibHistYReady = false;
  }

  if(m_pMcuFileMap)
  {
    delete[]m_pMcuFileMap;
    m_pMcuFileMap = 0;
  }

  if(m_pBlkDcValY)
  {
    delete[]m_pBlkDcValY;
    m_pBlkDcValY = 0;
  }

  if(m_pBlkDcValCb)
  {
    delete[]m_pBlkDcValCb;
    m_pBlkDcValCb = 0;
  }

  if(m_pBlkDcValCr)
  {
    delete[]m_pBlkDcValCr;
    m_pBlkDcValCr = 0;
  }

  if(m_pPixValY)
  {
    delete[]m_pPixValY;
    m_pPixValY = 0;
  }
  if(m_pPixValCb)
  {
    delete[]m_pPixValCb;
    m_pPixValCb = 0;
  }
  if(m_pPixValCr)
  {
    delete[]m_pPixValCr;
    m_pPixValCr = 0;
  }

  // Haven't warned about anything yet
  if(!m_bScanErrorsDisable)
  {
    m_nWarnBadScanNum = 0;
  }
  m_nWarnYccClipNum = 0;
  m_nWarnRstIndexNum = 0;
  m_nWarnRstCutNum = 0;
  m_nWarnRstShortNum = 0;
  m_nWarnRstSyncNum = 0;

  // Reset the view
  m_nPreviewPosX = 0;
  m_nPreviewPosY = 0;
  m_nPreviewSizeX = 0;
  m_nPreviewSizeY = 0;
}

// Destructor for Image Decode class
// - Deallocate any image-related dynamic storage
CimgDecode::~CimgDecode()
{
  if(m_pMcuFileMap)
  {
    delete[]m_pMcuFileMap;
    m_pMcuFileMap = 0;
  }

  if(m_pBlkDcValY)
  {
    delete[]m_pBlkDcValY;
    m_pBlkDcValY = 0;
  }
  if(m_pBlkDcValCb)
  {
    delete[]m_pBlkDcValCb;
    m_pBlkDcValCb = 0;
  }

  if(m_pBlkDcValCr)
  {
    delete[]m_pBlkDcValCr;
    m_pBlkDcValCr = 0;
  }

  if(m_pPixValY)
  {
    delete[]m_pPixValY;
    m_pPixValY = 0;
  }

  if(m_pPixValCb)
  {
    delete[]m_pPixValCb;
    m_pPixValCb = 0;
  }

  if(m_pPixValCr)
  {
    delete[]m_pPixValCr;
    m_pPixValCr = 0;
  }

  delete m_pDibTemp;
  m_pDibTemp = nullptr;
  delete m_pDibHistRgb;
  m_pDibHistRgb = nullptr;
  delete m_pDibHistY;
  m_pDibHistY = nullptr;
}

// Reset the major parameters
// - Called by JFIF Decoder when we begin a new file
// TODO: Consider merging with Reset()
// POST:
// - m_anSofSampFactH[]
// - m_anSofSampFactV[]
// - m_bImgDetailsSet
// - m_nNumSofComps
// - m_nPrecision
// - m_bScanErrorsDisable
// - m_nMarkersBlkNum
//
// The image source changed (a new file, or Reprocess): the decoded image,
// preview and histograms are the previous file's until the next decode. An
// empty file or a failed open never reaches a decode, so without this a zoom
// or channel change drew the previous file's image again.
void CimgDecode::ClearDecodedImage()
{
  m_bImgDecoded = false;
  m_bDibTempReady = false;
  m_bPreviewIsJpeg = false;
  m_bDibHistRgbReady = false;
  m_bDibHistYReady = false;
  m_bBrightValid = false;
  m_bAvgYValid = false;

  // Positions marked on the previous image (for Detailed Decode's Load
  // Coords) don't apply to the next
  m_nMarkersBlkNum = 0;
}

void CimgDecode::ResetState()
{
  ResetDhtLookup();
  ResetDqtTables();

  // Set by JfifDecode at the first SOS (not in Reset(), which DecodeScanImg() calls)
  m_bRgbComps = false;

  for(uint32_t nCompInd = 0; nCompInd < MAX_SOF_COMP_NF; nCompInd++)
  {
    m_anSofSampFactH[nCompInd] = 0;
    m_anSofSampFactV[nCompInd] = 0;
  }

  m_bImgDetailsSet = false;
  m_nNumSofComps = 0;

  m_nPrecision = 0;             // Default to "precision not set"

  m_bScanErrorsDisable = false;

  // Reset the markers
  m_nMarkersBlkNum = 0;
}

//void CimgDecode::paintEvent(QPaintEvent *)
//{
//  int32_t top = 0;

//  QRect bRect;

//  QStyleOption opt;
//  opt.init(this);
//  QPainter p(this);
//  style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
//  p.setFont(QFont("Courier New", 14));

//  p.fillRect(0, 0, width(), height(), QColor(Qt::white));

//  if(m_bDibTempReady)
//  {
//    if(!m_bPreviewIsJpeg)
//    {
//      // For all non-JPEG images, report with simple title
//      m_strTitle = "Image";
//    }
//    else
//    {
//      m_strTitle = "Image (";

//      switch (m_nPreviewMode)
//      {
//        case PREVIEW_RGB:
//          m_strTitle += "RGB";
//          break;

//        case PREVIEW_YCC:
//          m_strTitle += "YCC";
//          break;

//        case PREVIEW_R:
//          m_strTitle += "R";
//          break;

//        case PREVIEW_G:
//          m_strTitle += "G";
//          break;

//        case PREVIEW_B:
//          m_strTitle += "B";
//          break;

//        case PREVIEW_Y:
//          m_strTitle += "Y";
//          break;

//        case PREVIEW_CB:
//          m_strTitle += "Cb";
//          break;

//        case PREVIEW_CR:
//          m_strTitle += "Cr";
//          break;

//        default:
//          m_strTitle += "???";
//          break;
//      }

//      if(m_bDecodeScanAc)
//      {
//        m_strTitle += ", DC+AC)";
//      }
//      else
//      {
//        m_strTitle += ", DC)";
//      }
//    }

//    switch (m_nZoomMode)
//    {
//      case PRV_ZOOM_12:
//        m_strTitle += " @ 12.5% (1/8)";
//        break;

//      case PRV_ZOOM_25:
//        m_strTitle += " @ 25% (1/4)";
//        break;

//      case PRV_ZOOM_50:
//        m_strTitle += " @ 50% (1/2)";
//        break;

//      case PRV_ZOOM_100:
//        m_strTitle += " @ 100% (1:1)";
//        break;

//      case PRV_ZOOM_150:
//        m_strTitle += " @ 150% (3:2)";
//        break;

//      case PRV_ZOOM_200:
//        m_strTitle += " @ 200% (2:1)";
//        break;

//      case PRV_ZOOM_300:
//        m_strTitle += " @ 300% (3:1)";
//        break;

//      case PRV_ZOOM_400:
//        m_strTitle += " @ 400% (4:1)";
//        break;

//      case PRV_ZOOM_800:
//        m_strTitle += " @ 800% (8:1)";
//        break;

//      default:
//        m_strTitle += "";
//        break;
//    }

//    // Draw image title
//    bRect = p.boundingRect(QRect(nTitleIndent, top, width(), 100), Qt::AlignLeft | Qt::AlignTop, m_strTitle);
//    p.drawText(bRect, Qt::AlignLeft | Qt::AlignCenter, m_strTitle);

//    top = bRect.height() + nTitleLowGap;
////    p.drawImage(nTitleIndent, top, m_pDibTemp->scaled(m_pDibTemp->width() / 4, m_pDibTemp->height() / 4, Qt::KeepAspectRatio));

////    top += m_pDibTemp->height() + nTitleLowGap;
//    top += nTitleLowGap;
//  }

//  if(m_bHistEn)
//  {
//    if(m_bDibHistRgbReady)
//    {
//      bRect = p.boundingRect(QRect(nTitleIndent, top, width(), 100), Qt::AlignLeft | Qt::AlignTop, "Histogram (RGB)");
//      p.drawText(bRect, Qt::AlignLeft | Qt::AlignCenter, "Histogram (RGB)");
      
//      top += bRect.height() + nTitleLowGap;
//      p.drawImage(nTitleIndent, top, *m_pDibHistRgb);
//      top += m_pDibHistRgb->height() + nTitleLowGap;
//    }

//    if(m_bDibHistYReady)
//    {
//      bRect = p.boundingRect(QRect(nTitleIndent, top, width(), 100), Qt::AlignLeft | Qt::AlignTop, "Histogram (Y)");
//      p.drawText(bRect, Qt::AlignLeft | Qt::AlignCenter, "Histogram (Y)");

//      top += bRect.height() + nTitleLowGap;
//      p.drawImage(nTitleIndent, top, *m_pDibHistY);
//    }
//  }
//}

// Save a copy of the status bar control
//
// INPUT:
// - pStatBar                   = Pointer to status bar
// POST:
// - m_pStatBar
//
void CimgDecode::SetStatusBar(QStatusBar * pStatBar)
{
  m_pStatBar = pStatBar;
}

// Update the status bar text
//
// INPUT:
// - str                                = New text to display on status bar
// PRE:
// - m_pStatBar
//
void CimgDecode::SetStatusText(QString str)
{
  // Make sure that we have been connected to the status
  // bar of the main window first! Note that it is jpegsnoopDoc
  // that sets this variable.
  if(m_pStatBar)
  {
    m_pStatBar->showMessage(str);
  }

  // MainWindow connects updateStatus() to its status bar (m_pStatBar is never
  // set). Let it repaint at most every 100 ms (this is called once per MCU
  // row), without handling user input, which could re-enter the decoder.
  emit updateStatus(str, 0);

  static QElapsedTimer s_tmRepaint;

  if(!s_tmRepaint.isValid() || (s_tmRepaint.elapsed() >= 100))
  {
    ProcessWorkEvents();
    s_tmRepaint.start();
  }
}

// Clears the DQT entries
// POST:
// - m_anDqtTblSel[]
// - m_anDqtCoeff[][]
// - m_anDqtCoeffZz[][]
void CimgDecode::ResetDqtTables()
{
  for(uint32_t nDqtComp = 0; nDqtComp < MAX_DQT_COMP; nDqtComp++)
  {
    // Force entries to an invalid value. This makes
    // sure that we have to get a valid SetDqtTables() call
    // from JfifDecode first.
    m_anDqtTblSel[nDqtComp] = -1;
  }

  for(uint32_t nDestId = 0; nDestId < MAX_DQT_DEST_ID; nDestId++)
  {
    m_abDqtDefined[nDestId] = false;

    for(uint32_t nCoeff = 0; nCoeff < MAX_DQT_COEFF; nCoeff++)
    {
      m_anDqtCoeff[nDestId][nCoeff] = 0;
      m_anDqtCoeffZz[nDestId][nCoeff] = 0;
    }
  }

  m_nNumSofComps = 0;
}

// Reset the DHT lookup tables
// - These tables are used to speed up VLC lookups
// - This should be called by the JFIF decoder any time we start a new file
// POST:
// - m_anDhtLookupSetMax[]
// - m_anDhtLookupSize[][]
// - m_anDhtLookup_bitlen[][][]
// - m_anDhtLookup_bits[][][]
// - m_anDhtLookup_code[][][]
// - m_anDhtLookupfast[][][]
//
void CimgDecode::ResetDhtLookup()
{
  memset(m_anDhtHisto, 0, sizeof(m_anDhtHisto));

  // Use explicit loop ranges instead of memset
  for(uint32_t nClass = DHT_CLASS_DC; nClass <= DHT_CLASS_AC; nClass++)
  {
    m_anDhtLookupSetMax[nClass] = 0;
    // DHT table destination ID is range 0..3
    for(uint32_t nDestId = 0; nDestId < MAX_DHT_DEST_ID; nDestId++)
    {
      m_anDhtLookupSize[nClass][nDestId] = 0;

      for(uint32_t nCodeInd = 0; nCodeInd < MAX_DHT_CODES; nCodeInd++)
      {
        m_anDhtLookup_bitlen[nClass][nDestId][nCodeInd] = 0;
        m_anDhtLookup_bits[nClass][nDestId][nCodeInd] = 0;
        m_anDhtLookup_mask[nClass][nDestId][nCodeInd] = 0;
        m_anDhtLookup_code[nClass][nDestId][nCodeInd] = 0;
      }

      for(uint32_t nElem = 0; nElem < (2 << DHT_FAST_SIZE); nElem++)
      {
        // Mark with invalid value
        m_anDhtLookupfast[nClass][nDestId][nElem] = DHT_CODE_UNUSED;
      }
    }

    for(uint32_t nCompInd = 0; nCompInd < 1 + MAX_SOS_COMP_NS; nCompInd++)
    {
      // Force entries to an invalid value. This makes
      // sure that we have to get a valid SetDhtTables() call
      // from JfifDecode first.
      // Even though nCompInd is supposed to be 1-based numbering,
      // we start at index 0 to ensure it is marked as invalid.
      m_anDhtTblSel[nClass][nCompInd] = -1;
    }
  }

  m_nNumSosComps = 0;
}

// Configure an entry in a quantization table
//
// INPUT:
// - nSet                       = Quant table dest ID (from DQT:Tq)
// - nInd                       = Coeff index (normal order)
// - nIndzz                     = Coeff index (zigzag order)
// - nCoeff                     = Coeff value
// POST:
// - m_anDqtCoeff[]
// - m_anDqtCoeffZz[]
// RETURN:
// - True if params in range, false otherwise
//
// NOTE: Asynchronously called by JFIF Decoder
//
bool CimgDecode::SetDqtEntry(uint32_t nTblDestId, uint32_t nCoeffInd, uint32_t nCoeffIndZz, uint16_t nCoeffVal)
{
  if((nTblDestId < MAX_DQT_DEST_ID) && (nCoeffInd < MAX_DQT_COEFF))
  {
    m_anDqtCoeff[nTblDestId][nCoeffInd] = nCoeffVal;
    m_abDqtDefined[nTblDestId] = true;

    // Save a copy that represents the original zigzag order
    // This is used by the IDCT logic
    m_anDqtCoeffZz[nTblDestId][nCoeffIndZz] = nCoeffVal;

  }
  else
  {
    // Should never get here!
    QString strTmp;

    strTmp = QString("ERROR: Attempt to set DQT entry out of range (nTblDestId = %1, nCoeffInd = %2, nCoeffVal = %3")
        .arg(nTblDestId)
        .arg(nCoeffInd)
        .arg(nCoeffVal);

#ifdef DEBUG_LOG
    QString strDebug;

    strDebug = QString("## File = %1 Block = %2 Error = %3")
        .arg(m_pAppConfig->strCurFname, -100)
        .arg("ImgDecode", -10)
        .arg(strTmp);
    qDebug() << strDebug;
#else
    Q_ASSERT(false);
#endif

    if(m_pAppConfig->interactive())
    {
      msgBox.setText(strTmp);
      msgBox.exec();
    }

    return false;
  }

  return true;
}

// Fetch a DQT table entry
//
// INPUT:
// - nTblDestId                         = DQT Table Destination ID
// - nCoeffInd                          = Coefficient index in 8x8 matrix
// PRE:
// - m_anDqtCoeff[][]
// RETURN:
// - Returns the indexed DQT matrix entry
//
uint32_t CimgDecode::GetDqtEntry(uint32_t nTblDestId, uint32_t nCoeffInd)
{
  if((nTblDestId < MAX_DQT_DEST_ID) && (nCoeffInd < MAX_DQT_COEFF))
  {
    return m_anDqtCoeff[nTblDestId][nCoeffInd];
  }
  else
  {
    // Should never get here!
    QString strTmp;

    strTmp = QString("ERROR: GetDqtEntry(nTblDestId = %1, nCoeffInd = %2").arg(nTblDestId).arg(nCoeffInd);
    m_pLog->AddLineErr(strTmp);

    if(m_pAppConfig->interactive())
    {
      msgBox.setText(strTmp);
      msgBox.exec();
    }

#ifdef DEBUG_LOG
    qDebug() << QString("## File = %1 Block = %2 Error = %3").arg(m_pAppConfig->strCurFname).arg("ImgDecode").arg(strTmp);
#else
    Q_ASSERT(false);
#endif

    return 0;
  }
}

// Set a DQT table for a frame image component identifier
//
// INPUT:
// - nCompInd                   = Component index. Based on m_nSofNumComps_Nf-1 (ie. 0..254)
// - nTbl                               = DQT Table number. Based on SOF:Tqi (ie. 0..3)
// POST:
// - m_anDqtTblSel[]
// RETURN:
// - Success if index and table are in range
// NOTE:
// - Asynchronously called by JFIF Decoder
//
bool CimgDecode::SetDqtTables(uint32_t nCompId, uint32_t nTbl)
{
  QString strTmp;

  if((nCompId < MAX_SOF_COMP_NF) && (nTbl < MAX_DQT_DEST_ID))
  {
    m_anDqtTblSel[nCompId] = static_cast<int32_t>(nTbl);
  }
  else
  {
    // Should never get here unless the JFIF SOF table has a bad entry!
    strTmp = QString("ERROR: SetDqtTables(Comp ID = %1, Table = %2")
        .arg(nCompId)
        .arg(nTbl);
    m_pLog->AddLineErr(strTmp);

    if(m_pAppConfig->interactive())
    {
      msgBox.setText(strTmp);
      msgBox.exec();
    }

    return false;
  }

  return true;
}

// Set a DHT table for a scan image component index
// - The DHT Table select array is stored as:
//   m_anDhtTblSel[0][1,2,3] for DC
//   m_anDhtTblSel[1][1,2,3] for AC
//
// INPUT:
// - nCompInd                   = Component index (1-based). Range 1..4
// - nTblDc                             = DHT table index for DC elements of component
// - nTblAc                             = DHT table index for AC elements of component
// POST:
// - m_anDhtTblSel[][]
// RETURN:
// - Success if indices are in range
//
bool CimgDecode::SetDhtTables(uint32_t nCompInd, uint32_t nTblDc, uint32_t nTblAc)
{
  QString strTmp;

  // Note use of (nCompInd < MAX_SOS_COMP_NS+1) as nCompInd is 1-based notation
  if((nCompInd >= 1) && (nCompInd < MAX_SOS_COMP_NS + 1) && (nTblDc < MAX_DHT_DEST_ID) && (nTblAc < MAX_DHT_DEST_ID))
  {
    m_anDhtTblSel[DHT_CLASS_DC][nCompInd] = static_cast<int32_t>(nTblDc);
    m_anDhtTblSel[DHT_CLASS_AC][nCompInd] = static_cast<int32_t>(nTblAc);
  }
  else
  {
    // Should never get here!
    strTmp = QString("ERROR: SetDhtTables(comp = %1, TblDC = %2 TblAC = %3) out of indexed range")
        .arg(nCompInd)
        .arg(nTblDc)
        .arg(nTblAc);
    m_pLog->AddLineErr(strTmp);

    if(m_pAppConfig->interactive())
    {
      msgBox.setText(strTmp);
      msgBox.exec();
    }

    return false;
  }

  return true;
}

// Get the precision field
//
// INPUT:
// - nPrecision                 = DCT sample precision (typically 8 or 12)
// POST:
// - m_nPrecision
//
void CimgDecode::SetPrecision(uint32_t nPrecision)
{
  m_nPrecision = nPrecision;
}

// Set the general image details for the image decoder
//
// INPUT:
// - nDimX                              = Image dimension (X)
// - nDimY                              = Image dimension (Y)
// - nCompsSOF                  = Number of components in Frame
// - nCompsSOS                  = Number of components in Scan
// - bRstEn                             = Restart markers present?
// - nRstInterval               = Restart marker interval
// POST:
// - m_bImgDetailsSet
// - m_nDimX
// - m_nDimY
// - m_nNumSofComps
// - m_nNumSosComps
// - m_bRestartEn
// - m_nRestartInterval
// NOTE:
// - Called asynchronously by the JFIF decoder
//
void CimgDecode::SetImageDetails(uint32_t nDimX, uint32_t nDimY, uint32_t nCompsSOF, uint32_t nCompsSOS, bool bRstEn,
                                 uint32_t nRstInterval)
{
  m_bImgDetailsSet = true;
  m_nDimX = nDimX;
  m_nDimY = nDimY;
  m_nNumSofComps = nCompsSOF;
  m_nNumSosComps = nCompsSOS;
  m_bRestartEn = bRstEn;
  m_nRestartInterval = nRstInterval;
}

// Reset the image content to prepare it for the upcoming scans
// - TODO: Migrate pixel bitmap allocation / clearing from DecodeScanImg() to here
void CimgDecode::ResetImageContent()
{
}

// Set the sampling factor for an image component
//
// INPUT:
// - nCompInd                   = Component index from Nf (ie. 1..255)
// - nSampFactH                 = Sampling factor in horizontal direction
// - nSampFactV                 = Sampling factor in vertical direction
// POST:
// - m_anSofSampFactH[]
// - m_anSofSampFactV[]
// NOTE:
// - Called asynchronously by the JFIF decoder in SOF
//
void CimgDecode::SetSofSampFactors(uint32_t nCompInd, uint32_t nSampFactH, uint32_t nSampFactV)
{
  // TODO: Check range
  m_anSofSampFactH[nCompInd] = nSampFactH;
  m_anSofSampFactV[nCompInd] = nSampFactV;
}

// Update the preview mode (affects channel display)
//
// INPUT:
// - nMode                              = Mode used in channel display (eg. NONE, RGB, YCC)
//                        See PREVIEW_* constants
//
void CimgDecode::setPreviewMode(QAction *action)
{
  // Need to check to see if mode has changed. If so, we need to recalculate the temporary preview.
  m_nPreviewMode = action->data().toInt();
  qDebug() << QString("CimgDecode::setPreviewMode(%1)").arg(m_nPreviewMode);
  CalcChannelPreview();
  emit updateImage();
}

// Update any level shifts for the preview display
//
// INPUT:
// - nMcuX                              = MCU index in X direction
// - nMcuY                              = MCU index in Y direction
// - nY                                 = DC shift in Y component
// - nCb                                = DC shift in Cb component
// - nCr                                = DC shift in Cr component
//
void CimgDecode::SetPreviewYccOffset(uint32_t nMcuX, uint32_t nMcuY, int32_t nY, int32_t nCb, int32_t nCr)
{
  m_nPreviewShiftY = nY;
  m_nPreviewShiftCb = nCb;
  m_nPreviewShiftCr = nCr;
  m_nPreviewShiftMcuX = nMcuX;
  m_nPreviewShiftMcuY = nMcuY;

  CalcChannelPreview();
}

// Fetch the current level shift setting for the preview display
//
// OUTPUT:
// - nMcuX                              = MCU index in X direction
// - nMcuY                              = MCU index in Y direction
// - nY                                 = DC shift in Y component
// - nCb                                = DC shift in Cb component
// - nCr                                = DC shift in Cr component
//
void CimgDecode::GetPreviewYccOffset(uint32_t &nMcuX, uint32_t &nMcuY, int32_t &nY, int32_t &nCb, int32_t &nCr)
{
  nY = m_nPreviewShiftY;
  nCb = m_nPreviewShiftCb;
  nCr = m_nPreviewShiftCr;
  nMcuX = m_nPreviewShiftMcuX;
  nMcuY = m_nPreviewShiftMcuY;
}

// Set the Preview MCU insert
// UNUSED
void CimgDecode::SetPreviewMcuInsert(uint32_t nMcuX, uint32_t nMcuY, int32_t nLen)
{

  m_nPreviewInsMcuX = nMcuX;
  m_nPreviewInsMcuY = nMcuY;
  m_nPreviewInsMcuLen = nLen;

  CalcChannelPreview();
}

// Get the Preview MCU insert
// UNUSED
void CimgDecode::GetPreviewMcuInsert(uint32_t &nMcuX, uint32_t &nMcuY, uint32_t &nLen)
{
  nMcuX = m_nPreviewInsMcuX;
  nMcuY = m_nPreviewInsMcuY;
  nLen = m_nPreviewInsMcuLen;
}

// Fetch the coordinate of the top-left corner of the preview image
//
// OUTPUT:
// - nX                         = X coordinate of top-left corner
// - nY                         = Y coordinate of top-left corner
//
void CimgDecode::GetPreviewPos(uint32_t &nX, uint32_t &nY)
{
  nX = m_nPreviewPosX;
  nY = m_nPreviewPosY;
}

// Fetch the dimensions of the preview image
//
// OUTPUT:
// - nX                         = X dimension of image
// - nY                         = Y dimension of iamge
//
void CimgDecode::GetPreviewSize(uint32_t &nX, uint32_t &nY)
{
  nX = m_nPreviewSizeX;
  nY = m_nPreviewSizeY;
}

// Set a DHT table entry and associated lookup table
// - Configures the fast lookup table for short code bitstrings
//
// INPUT:
// - nDestId                    = DHT destination table ID (0..3)
// - nClass                             = Select between DC and AC tables (0=DC, 1=AC)
// - nInd                               = Index into table
// - nLen                               = Huffman code length
// - nBits                              = Huffman code bitstring (left justified)
// - nMask                              = Huffman code bit mask (left justified)
// - nCode                              = Huffman code value
// POST:
// - m_anDhtLookup_bitlen[][][]
// - m_anDhtLookup_bits[][][]
// - m_anDhtLookup_mask[][][]
// - m_anDhtLookup_code[][][]
// - m_anDhtLookupSetMax[]
// - m_anDhtLookupfast[][][]
// RETURN:
// - Success if indices are in range
// NOTE:
// - Asynchronously called by JFIF Decoder
//
bool CimgDecode::SetDhtEntry(uint32_t nDestId, uint32_t nClass, uint32_t nInd, uint32_t nLen,
                             uint32_t nBits, uint32_t nMask, uint32_t nCode)
{
  if((nDestId >= MAX_DHT_DEST_ID) || (nClass >= MAX_DHT_CLASS) || (nInd >= MAX_DHT_CODES))
  {
    QString strTmp = "ERROR: Attempt to set DHT entry out of range";

    m_pLog->AddLineErr(strTmp);

    if(m_pAppConfig->interactive())
    {
      msgBox.setText(strTmp);
      msgBox.exec();
    }
#ifdef DEBUG_LOG
    QString strDebug;

    strDebug = QString("## File = %1 Block = %2 Error = %3")
        .arg(m_pAppConfig->strCurFname, -100)
        .arg("ImgDecode", -10)
        .arg(strTmp);
    qDebug() << strDebug;
#else
    Q_ASSERT(false);
#endif
    return false;
  }

  m_anDhtLookup_bitlen[nClass][nDestId][nInd] = nLen;
  m_anDhtLookup_bits[nClass][nDestId][nInd] = nBits;
  m_anDhtLookup_mask[nClass][nDestId][nInd] = nMask;
  m_anDhtLookup_code[nClass][nDestId][nInd] = nCode;

  // Record the highest numbered DHT set.
  // TODO: Currently assuming that there are no missing tables in the sequence
  if(nDestId > m_anDhtLookupSetMax[nClass])
  {
    m_anDhtLookupSetMax[nClass] = nDestId;
  }

  uint32_t nBitsMsb;
  uint32_t nBitsExtraLen;
  uint32_t nBitsExtraVal;
  uint32_t nBitsMax;
  uint32_t nFastVal;

  // If the variable-length code is short enough, add it to the
  // fast lookup table.
  if(nLen <= DHT_FAST_SIZE)
  {
    // nBits is a left-justified number (assume right-most bits are zero)
    // nLen is number of leading bits to compare

    //   nLen     = 5
    //   nBits    = 32'b1011_1xxx_xxxx_xxxx_xxxx_xxxx_xxxx_xxxx  (0xB800_0000)
    //   nBitsMsb =  8'b1011_1xxx (0xB8)
    //   nBitsMsb =  8'b1011_1000 (0xB8)
    //   nBitsMax =  8'b1011_1111 (0xBF)

    //   nBitsExtraLen = 8-len = 8-5 = 3

    //   nBitsExtraVal = (1<<nBitsExtraLen) -1 = 1<<3 -1 = 8'b0000_1000 -1 = 8'b0000_0111
    //
    //   nBitsMax = nBitsMsb + nBitsExtraVal
    //   nBitsMax =  8'b1011_1111
    nBitsMsb = (nBits & nMask) >> (32 - DHT_FAST_SIZE);
    nBitsExtraLen = DHT_FAST_SIZE - nLen;
    nBitsExtraVal = (1 << nBitsExtraLen) - 1;
    nBitsMax = nBitsMsb + nBitsExtraVal;

    for(uint32_t ind1 = nBitsMsb; ind1 <= nBitsMax; ind1++)
    {
      // The arrangement of the dht_lookupfast[] values:
      //                         0xFFFF_FFFF = No entry, resort to slow search
      //    m_anDhtLookupfast[nClass][nDestId] [15: 8] = nLen
      //    m_anDhtLookupfast[nClass][nDestId] [ 7: 0] = nCode
      //
      //Q_ASSERT(code <= 0xFF);
      //Q_ASSERT(ind1 <= 0xFF);
      nFastVal = nCode + (nLen << 8);
      m_anDhtLookupfast[nClass][nDestId][ind1] = nFastVal;
    }
  }

  return true;
}

// Assign the size of the DHT table
//
// INPUT:
// - nDestId                    = Destination DHT table ID (From DHT:Th, range 0..3)
// - nClass                             = Select between DC and AC tables (0=DC, 1=AC) (From DHT:Tc, range 0..1)
// - nSize                              = Number of entries in the DHT table
// POST:
// - m_anDhtLookupSize[][]
// RETURN:
// - Success if indices are in range
//
bool CimgDecode::SetDhtSize(uint32_t nDestId, uint32_t nClass, uint32_t nSize)
{
  if((nDestId >= MAX_DHT_DEST_ID) || (nClass >= MAX_DHT_CLASS) || (nSize >= MAX_DHT_CODES))
  {
    QString strTmp = "ERROR: Attempt to set DHT table size out of range";

    m_pLog->AddLineErr(strTmp);

    if(m_pAppConfig->interactive())
    {
      msgBox.setText(strTmp);
      msgBox.exec();
    }

    return false;
  }
  else
  {
    m_anDhtLookupSize[nClass][nDestId] = nSize;
  }

  return true;
}

// Forget one DHT table before it is (re)defined
// - A DHT segment may redefine a table (T.81 B.2.4.2). Its fast-lookup
//   entries were only cleared once per file, so the new table's codes longer
//   than DHT_FAST_SIZE bits found the old table's entries for their prefixes.
//
// INPUT:
// - nDestId                    = Destination DHT table ID (From DHT:Th, range 0..3)
// - nClass                     = Select between DC and AC tables (0=DC, 1=AC) (From DHT:Tc)
// POST:
// - m_anDhtLookupSize[][]
// - m_anDhtLookupfast[][][]
//
void CimgDecode::ClearDhtTable(uint32_t nDestId, uint32_t nClass)
{
  // Out-of-range IDs are reported by SetDhtEntry()
  if((nDestId >= MAX_DHT_DEST_ID) || (nClass >= MAX_DHT_CLASS))
  {
    return;
  }

  m_anDhtLookupSize[nClass][nDestId] = 0;

  for(uint32_t nElem = 0; nElem < (2 << DHT_FAST_SIZE); nElem++)
  {
    m_anDhtLookupfast[nClass][nDestId][nElem] = DHT_CODE_UNUSED;
  }
}

// Convert huffman code (DC) to signed value
// - Convert according to ITU-T.81 Table 5
//
// INPUT:
// - nVal                               = Huffman DC value (left justified)
// - nBits                              = Bitstring length of value
// RETURN:
// - Signed integer representing Huffman DC value
//
int32_t CimgDecode::HuffmanDc2Signed(uint32_t nVal, uint32_t nBits)
{
  if(nVal >= (1 << (nBits - 1)))
  {
    return static_cast<int32_t>(nVal);
  }
  else
  {
    return static_cast<int32_t>(nVal - ((1 << nBits) - 1));
  }
}

// Generate the Huffman code lookup table mask
//
// POST:
// - m_anHuffMaskLookup[]
//
void CimgDecode::GenLookupHuffMask()
{
  uint32_t nMask;

  // Mask of the top nLen bits. Length 0 is special-cased: shifting a 32-bit
  // value by 32 is undefined behaviour.
  for(uint32_t nLen = 0; nLen < 32; nLen++)
  {
    nMask = (nLen == 0) ? 0 : (0xFFFFFFFFu << (32 - nLen));
    m_anHuffMaskLookup[nLen] = nMask;
  }
}

// Extract a specified number of bits from a 32-bit holding register
//
// INPUT:
// - nWord                              = The 32-bit holding register
// - nBits                              = Number of bits (leftmost) to extract from the holding register
// PRE:
// - m_anHuffMaskLookup[]
// RETURN:
// - The subset of bits extracted from the holding register
// NOTE:
// - This routine is inlined for speed
//
inline uint32_t CimgDecode::ExtractBits(uint32_t nWord, uint32_t nBits)
{
  uint32_t nVal;

  nVal = (nWord & m_anHuffMaskLookup[nBits]) >> (32 - nBits);
  return nVal;
}

// Move the scan buffer's file pointer on by nBytes, stopping at the last
// position, which is past the end of any file. (In a file of 0xFFFFFFFF
// bytes, the position after the end wrapped around to 0, and the decoder went
// on to read the start of the file as scan data.)
//
// POST:
// - m_nScanBuffPtr
//
inline void CimgDecode::ScanBuffPtrAdvance(uint32_t nBytes)
{
  m_nScanBuffPtr = (m_nScanBuffPtr > UINT32_MAX - nBytes) ? UINT32_MAX : (m_nScanBuffPtr + nBytes);
}

// Removes bits from the holding buffer
// - Discard the leftmost "nNumBits" of m_nScanBuff
// - And then realign file position pointers
//
// INPUT:
// - nNumBits                           = Number of left-most bits to consume
// POST:
// - m_nScanBuff
// - m_nScanBuff_vacant
// - m_nScanBuffPtr_align
// - m_anScanBuffPtr_pos[]
// - m_anScanBuffPtr_err[]
// - m_nScanBuffLatchErr
// - m_nScanBuffPtr_num
//
inline void CimgDecode::ScanBuffConsume(uint32_t nNumBits)
{
  m_nScanBuff <<= nNumBits;
  m_nScanBuff_vacant += nNumBits;

  // Need to latch any errors during consumption of multi-bytes
  // otherwise we'll miss the error notification if we skip over
  // it before we exit this routine! e.g if num_bytes=2 and error
  // appears on first byte, then we want to retain it in pos[0]

  // Now realign the file position pointers if necessary
  uint32_t nNumBytes = (m_nScanBuffPtr_align + nNumBits) / 8;

  for(uint32_t nInd = 0; nInd < nNumBytes; nInd++)
  {
    m_anScanBuffPtr_pos[0] = m_anScanBuffPtr_pos[1];
    m_anScanBuffPtr_pos[1] = m_anScanBuffPtr_pos[2];
    m_anScanBuffPtr_pos[2] = m_anScanBuffPtr_pos[3];
    // Don't clear the last ptr position because during an overread
    // this will be the only place that the last position was preserved
    //m_anScanBuffPtr_pos[3] = 0;

    m_anScanBuffPtr_err[0] = m_anScanBuffPtr_err[1];
    m_anScanBuffPtr_err[1] = m_anScanBuffPtr_err[2];
    m_anScanBuffPtr_err[2] = m_anScanBuffPtr_err[3];
    m_anScanBuffPtr_err[3] = SCANBUF_OK;

    if(m_anScanBuffPtr_err[0] != SCANBUF_OK)
    {
      m_nScanBuffLatchErr = m_anScanBuffPtr_err[0];
    }

    m_nScanBuffPtr_num--;
  }
  
  m_nScanBuffPtr_align = (m_nScanBuffPtr_align + nNumBits) % 8;
}

// Augment the current scan buffer with another byte
// - Extra bits are added to right side of existing bitstring
//
// INPUT:
// - nNewByte                 = 8-bit byte to add to buffer
// - nPtr                               = UNUSED
// PRE:
// - m_nScanBuff
// - m_nScanBuff_vacant
// - m_nScanBuffPtr_num
// POST:
// - m_nScanBuff
// - m_nScanBuff_vacant
// - m_anScanBuffPtr_err[]
// - m_anScanBuffPtr_pos[]
//
inline void CimgDecode::ScanBuffAdd(uint32_t nNewByte, uint32_t nPtr)
{
  // Add the new byte to the buffer
  // Assume that m_nScanBuff has already been shifted to be
  // aligned to bit 31 as first bit.
  m_nScanBuff += (nNewByte << (m_nScanBuff_vacant - 8));
  m_nScanBuff_vacant -= 8;

  Q_ASSERT(m_nScanBuffPtr_num < 4);
  
  if(m_nScanBuffPtr_num >= 4)
  {
    return;
  }                             // Unexpected by design
  
  m_anScanBuffPtr_err[m_nScanBuffPtr_num] = SCANBUF_OK;
  m_anScanBuffPtr_pos[m_nScanBuffPtr_num++] = nPtr;

  // m_nScanBuffPtr_align stays the same as we add 8 bits
}

// Augment the current scan buffer with another byte (but mark as error)
//
// INPUT:
// - nNewByte                 = 8-bit byte to add to buffer
// - nPtr                               = UNUSED
// - nErr                               = Error code to associate with this buffer byte
// POST:
// - m_anScanBuffPtr_err[]
//
inline void CimgDecode::ScanBuffAddErr(uint32_t nNewByte, uint32_t nPtr, uint32_t nErr)
{
  ScanBuffAdd(nNewByte, nPtr);
  m_anScanBuffPtr_err[m_nScanBuffPtr_num - 1] = nErr;

  // Added to an empty buffer (after a restart, or at the scan's start), the
  // byte holds the next bit to read, so its error is latched now.
  // ScanBuffConsume() latches a byte's error only when the bits read move into
  // it, which they never do into this one: its 8 bits were decoded as data with
  // no error. Past the end of the file (an RST that ends it), the MCUs they made
  // were decoded from zeros, and a bad marker there wasn't reported.
  if(m_nScanBuffPtr_num == 1)
  {
    m_nScanBuffLatchErr = nErr;
  }
}

// A bad marker (not an RST) in the scan data where a code starts: the MCU and
// the scan are marked bad, and it is reported, up to the scan's error limit
//
// INPUT:
// - nPos, nAlign                       = Where the code starts
//
void CimgDecode::ReportScanBadMarker(uint32_t nPos, uint32_t nAlign)
{
  // Mark as scan error
  m_nScanCurErr = true;

  m_bScanBad = true;

  if(m_nWarnBadScanNum < m_nScanErrMax)
  {
    m_pLog->AddLineErr(QString("*** ERROR: Bad marker @ %1").arg(GetScanBufPos(nPos, nAlign)));

    m_nWarnBadScanNum++;

    if(m_nWarnBadScanNum >= m_nScanErrMax)
    {
      m_pLog->AddLineErr(QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax));
    }
  }
}

// Disable any further reporting of scan errors
//
// PRE:
// - m_nScanErrMax
// POST:
// - m_nWarnBadScanNum
// - m_bScanErrorsDisable
//
void CimgDecode::ScanErrorsDisable()
{
  m_nWarnBadScanNum = m_nScanErrMax;
  m_bScanErrorsDisable = true;
}

// Enable reporting of scan errors
//
// POST:
// - m_nWarnBadScanNum
// - m_nWarnRstIndexNum
// - m_nWarnRstCutNum
// - m_nWarnRstShortNum
// - m_nWarnRstSyncNum
// - m_bScanErrorsDisable
//
void CimgDecode::ScanErrorsEnable()
{
  m_nWarnBadScanNum = 0;
  m_nWarnRstIndexNum = 0;
  m_nWarnRstCutNum = 0;
  m_nWarnRstShortNum = 0;
  m_nWarnRstSyncNum = 0;
  m_bScanErrorsDisable = false;
}

// Read in bits from the buffer and find matching huffman codes
// - Input buffer is m_nScanBuff
// - Perform shift in buffer afterwards
// - Abort if there aren't enough bits (note that the scan buffer
//   is almost empty when we reach the end of a scan segment)
//
// INPUT:
// - nClass                                     = DHT Table class (0..1)
// - nTbl                                       = DHT Destination ID (0..3)
// PRE:
// - Assume that dht_lookup_size[nTbl] != 0 (already checked)
// - m_bRestartRead
// - m_nScanBuff_vacant
// - m_nScanErrMax
// - m_nScanBuff
// - m_anDhtLookupFast[][][]
// - m_anDhtLookupSize[][]
// - m_anDhtLookup_mask[][][]
// - m_anDhtLookup_bits[][][]
// - m_anDhtLookup_bitlen[][][]
// - m_anDhtLookup_code[][][]
// - m_nPrecision
// POST:
// - m_nScanBitsUsed# is calculated
// - m_bScanEnd
// - m_bScanBad
// - m_nWarnBadScanNum
// - m_anDhtHisto[][][]
// OUTPUT:
// - rZrl                               = Zero run amount (if any)
// - rVal                               = Coefficient value
// RETURN:
// - Status from attempting to decode the current value
//
// PERFORMANCE:
// - Tried to unroll all function calls listed below,
//   but performance was same before & after (27sec).
// - Calls unrolled: BuffTopup(), ScanBuffConsume(),
//                   ExtractBits(), HuffmanDc2Signed()
teRsvRet CimgDecode::ReadScanVal(uint32_t nClass, uint32_t nTbl, uint32_t &rZrl, int32_t &rVal)
{
  bool bDone = false;

  uint32_t nInd = 0;

  uint32_t nCode = DHT_CODE_UNUSED;     // Not a valid nCode

  uint32_t nVal;

  Q_ASSERT(nClass < MAX_DHT_CLASS);
  Q_ASSERT(nTbl < MAX_DHT_DEST_ID);

  m_nScanBitsUsed1 = 0;         // bits consumed for nCode
  m_nScanBitsUsed2 = 0;

  rZrl = 0;
  rVal = 0;

  // First check to see if we've entered here with a completely empty
  // scan buffer with a restart marker already observed. In that case
  // we want to exit with condition 3 (restart terminated)
  if((m_nScanBuff_vacant == 32) && (m_bRestartRead))
  {
    return RSV_RST_TERM;
  }

  // Has the scan buffer been depleted?
  if(m_nScanBuff_vacant >= 32)
  {
    // Trying to overread end of scan segment

    if(m_nWarnBadScanNum < m_nScanErrMax)
    {
      QString strTmp;

      strTmp = QString("*** ERROR: Overread scan segment (before nCode)! @ Offset: %1").arg(GetScanBufPos());
      m_pLog->AddLineErr(strTmp);

      m_nWarnBadScanNum++;

      if(m_nWarnBadScanNum >= m_nScanErrMax)
      {
        strTmp = QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax);
        m_pLog->AddLineErr(strTmp);
      }
    }

    m_bScanEnd = true;
    m_bScanBad = true;
    return RSV_UNDERFLOW;
  }

  // Top up the buffer just in case
  BuffTopup();

  bDone = false;
  bool bFound = false;

  // Fast search for variable-length huffman nCode
  // Do direct lookup for any codes DHT_FAST_SIZE bits or shorter

  uint32_t nCodeMsb;

  uint32_t nCodeFastSearch;

  // Only enable this fast search if m_nScanBuff_vacant implies
  // that we have at least DHT_FAST_SIZE bits available in the buffer!
  if((32 - m_nScanBuff_vacant) >= DHT_FAST_SIZE)
  {
    nCodeMsb = m_nScanBuff >> (32 - DHT_FAST_SIZE);
    nCodeFastSearch = m_anDhtLookupfast[nClass][nTbl][nCodeMsb];
    
    if(nCodeFastSearch != DHT_CODE_UNUSED)
    {
      // We found the code!
      m_nScanBitsUsed1 += (nCodeFastSearch >> 8);
      nCode = (nCodeFastSearch & 0xFF);
      bDone = true;
      bFound = true;
    }
  }

  // Slow search for variable-length huffman nCode
  while(!bDone)
  {
    uint32_t nBitLen;

    if((m_nScanBuff & m_anDhtLookup_mask[nClass][nTbl][nInd]) == m_anDhtLookup_bits[nClass][nTbl][nInd])
    {

      nBitLen = m_anDhtLookup_bitlen[nClass][nTbl][nInd];
      
      // Just in case this VLC bit string is longer than the number of
      // bits we have left in the buffer (due to restart marker or end
      // of scan data), we need to double-check
      if(nBitLen <= 32 - m_nScanBuff_vacant)
      {
        nCode = m_anDhtLookup_code[nClass][nTbl][nInd];
        m_nScanBitsUsed1 += nBitLen;
        bDone = true;
        bFound = true;
      }
    }
    
    nInd++;
    
    if(nInd >= m_anDhtLookupSize[nClass][nTbl])
    {
      bDone = true;
    }
  }

  // Could not find huffman nCode in table!
  if(!bFound)
  {
    // If we didn't find the huffman nCode, it might be due to a
    // restart marker that prematurely stopped the scan buffer filling.
    // If so, return with an indication so that DecodeScanComp() can
    // handle the restart marker, refill the scan buffer and then
    // re-do ReadScanVal()
    if(m_bRestartRead)
    {
      return RSV_RST_TERM;
    }

    // FIXME:
    // What should we be consuming here? we need to make forward progress
    // in file. Options:
    // 1) Move forward to next byte in file
    // 2) Move forward to next bit in file
    // Currently moving 1 bit so that we have slightly more opportunities
    // to re-align earlier.
    m_nScanBitsUsed1 = 1;
    nCode = DHT_CODE_UNUSED;
  }

  // Log an entry into a histogram
  if(m_nScanBitsUsed1 < MAX_DHT_CODELEN + 1)
  {
    m_anDhtHisto[nClass][nTbl][m_nScanBitsUsed1]++;
  }
  else
  {
    Q_ASSERT(false);
    // Somehow we got out of range
  }

  ScanBuffConsume(m_nScanBitsUsed1);

  // Did we overread the scan buffer?
  if(m_nScanBuff_vacant > 32)
  {
    // The nCode consumed more bits than we had!
    QString strTmp;

    strTmp = QString("*** ERROR: Overread scan segment (after nCode)! @ Offset: %1").arg(GetScanBufPos());
    m_pLog->AddLineErr(strTmp);
    m_bScanEnd = true;
    m_bScanBad = true;
    return RSV_UNDERFLOW;
  }

  // Replenish buffer after nCode extraction and before variable extra bits
  // This is required because we may have a 12 bit nCode followed by a 16 bit var length bitstring
  BuffTopup();

  // Did we find the nCode?
  if(nCode != DHT_CODE_UNUSED)
  {
    rZrl = (nCode & 0xF0) >> 4;
    m_nScanBitsUsed2 = nCode & 0x0F;
    
    if((rZrl == 0) && (m_nScanBitsUsed2 == 0))
    {
      // EOB (was bits_extra=0)
      return RSV_EOB;
    }
    else if(m_nScanBitsUsed2 == 0)
    {
      // Zero rValue
      rVal = 0;
      return RSV_OK;

    }
    else
    {
      // Normal nCode

      // Its value bits run on past a restart marker: the interval's data
      // ends inside the code. It is dropped, as the bits before a marker
      // that make no code are, and read again after the marker; nor is it
      // counted. (Taken whole, with zeros for the bits past the marker, it
      // ended the scan with "Overread scan segment": the rest of the image
      // was grey, and the errors were at 0x00000000.2.)
      if(m_bRestartRead && (m_nScanBitsUsed2 > 32 - m_nScanBuff_vacant))
      {
        if(m_nScanBitsUsed1 < MAX_DHT_CODELEN + 1)
        {
          m_anDhtHisto[nClass][nTbl][m_nScanBitsUsed1]--;
        }

        return RSV_RST_CUT;
      }

      nVal = ExtractBits(m_nScanBuff, m_nScanBitsUsed2);
      rVal = HuffmanDc2Signed(nVal, m_nScanBitsUsed2);

      // Now handle the different precision values
      // Treat 12-bit like 8-bit but scale values first (ie. drop precision down to 8-bit)
      int32_t nPrecisionDivider = 1;

      if(m_nPrecision >= 8)
      {
        nPrecisionDivider = 1 << (m_nPrecision - 8);
        rVal /= nPrecisionDivider;
      }
      else
      {
        // Precision value seems out of range!
      }

      ScanBuffConsume(m_nScanBitsUsed2);

      // Did we overread the scan buffer?
      if(m_nScanBuff_vacant > 32)
      {
        // The nCode consumed more bits than we had!
        QString strTmp;

        strTmp = QString("*** ERROR: Overread scan segment (after bitstring)! @ Offset: %1").arg(GetScanBufPos());
        m_pLog->AddLineErr(strTmp);
        m_bScanEnd = true;
        m_bScanBad = true;
        return RSV_UNDERFLOW;
      }

      return RSV_OK;
    }
  }
  else
  {
    // ERROR: Invalid huffman nCode!

    // FIXME: We may also enter here if we are about to encounter a
    // restart marker! Need to see if ScanBuf is terminated by
    // restart marker; if so, then we simply flush the ScanBuf,
    // consume the 2-byte RST marker, clear the ScanBuf terminate
    // reason, then indicate to caller that they need to call ReadScanVal
    // again.

    if(m_nWarnBadScanNum < m_nScanErrMax)
    {
      QString strTmp;

      strTmp =
        QString("*** ERROR: Can't find huffman bitstring @ %1, table %2, value 0x%3").arg(GetScanBufPos()).arg(nTbl).
        arg(m_nScanBuff, 8, 16, QChar('0'));
      m_pLog->AddLineErr(strTmp);

      m_nWarnBadScanNum++;

      if(m_nWarnBadScanNum >= m_nScanErrMax)
      {
        strTmp = QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax);
        m_pLog->AddLineErr(strTmp);
      }
    }

    // TODO: What rValue and ZRL should we return?
    m_bScanBad = true;
    return RSV_UNDERFLOW;
  }

  // NOTE: Can't reach here
  // return RSV_UNDERFLOW;
}

// Refill the scan buffer as needed
//
void CimgDecode::BuffTopup()
{
  uint32_t nRetVal;

  // Do we have space to load another byte?
  bool bDone = (m_nScanBuff_vacant < 8);

  // Have we already reached the end of the scan segment?
  if(m_bScanEnd)
  {
    bDone = true;
  }

  while(!bDone)
  {
    nRetVal = BuffAddByte();

    // NOTE: If we have read in a restart marker, the above call will not
    // read in any more bits into the scan buffer, so we should just simply
    // say that we've done the best we can for the top up.
    if(m_bRestartRead)
    {
      bDone = true;
    }

    if(m_nScanBuff_vacant < 8)
    {
      bDone = true;
    }
    
    // If the buffer read returned an error or end of scan segment
    // then stop filling buffer
    if(nRetVal != 0)
    {
      bDone = true;
    }
  }
}

// Check for restart marker and compare the index against expected
//
// PRE:
// - m_nScanBuffPtr
// RETURN:
// - Restart marker found at the current buffer position
// NOTE:
// - This routine expects that we did not remove restart markers
//   from the bytestream (in BuffAddByte).
//
bool CimgDecode::ExpectRestart()
{
  uint32_t nMarker;
  uint32_t nBuf0 = m_pWBuf->Buf(m_nScanBuffPtr);
  uint32_t nBuf1 = m_pWBuf->Buf(m_nScanBuffPtr + 1);

  // Check for restart marker first. Assume none back-to-back.
  if(nBuf0 == 0xFF)
  {
    nMarker = nBuf1;

    // FIXME: Later, check that we are on the right marker!
    if((nMarker >= JFIF_RST0) && (nMarker <= JFIF_RST7))
    {
      if(m_bVerbose)
      {
        QString strTmp;

        strTmp =
          QString("  RESTART marker: @ 0x%1.0 : RST%2")
            .arg(m_nScanBuffPtr, 8, 16, QChar('0'))
            .arg(nMarker - JFIF_RST0, 2, 10, QChar('0'));
        m_pLog->AddLine(strTmp);
      }

      m_nRestartRead++;

      // FIXME:
      //     Later on, we should be checking for RST out of
      //     sequence!

      // Now we need to skip to the next bytes
      m_nScanBuffPtr += 2;

      // Now refill in the buffer if we need to
      BuffAddByte();

      return true;
    }
  }

  return false;
}

// Forget the runs of 0xFF remembered by ScanFillRun() (at each decode and
// restart)
void CimgDecode::ScanFillForget()
{
  for(uint32_t nInd = 0; nInd < SCAN_FILL_RUNS; nInd++)
  {
    m_anScanFillStart[nInd] = 0;
    m_anScanFillEnd[nInd] = 0;
    m_abScanFillSkipped[nInd] = false;
  }

  m_nScanFillNext = 0;
}

// Where a run of 0xFF bytes in the scan data from nPos ends (nPosEnd: the
// first byte after it, or the end of the file), and whether all but its last
// 0xFF are fill bytes, skipped: those before an RSTn (T.81 B.1.1.2), whose
// last 0xFF is the marker's, and those before the 0x00 of a stuffed 0xFF,
// which T.81 doesn't allow (libjpeg documents reading the run as one 0xFF of
// data). The last runs are remembered with that answer, so a long one isn't
// searched again, nor the byte after it read again, for each of its bytes: a
// crafted 1 MB run took over a minute to decode; that byte, over 112 KB on,
// made the file's window load twice for each (12 to 31 s for 2 MB before an
// EOI, depending on the machine's load, 0.3 to 0.6 s without); and Detailed
// Decode searched one before an RSTn again for each code near it (16 MB took
// 5 times as long). Its bytes for a code (5 at most: a 16-bit code and a
// 15-bit value from bit 7; the last not searched from) reach at most 4 runs,
// and the decoder has read up to 4 bytes past the code, so 8 are kept: with
// one, a short run next to a long one made each code search the long one
// again, and with 4, 16-bit codes made it search each run about once more.
bool CimgDecode::ScanFillRun(uint32_t nPos, uint32_t &nPosEnd)
{
  for(uint32_t nInd = 0; nInd < SCAN_FILL_RUNS; nInd++)
  {
    if((nPos >= m_anScanFillStart[nInd]) && (nPos < m_anScanFillEnd[nInd]))
    {
      nPosEnd = m_anScanFillEnd[nInd];
      return m_abScanFillSkipped[nInd];
    }
  }

  const uint32_t nPosEof = m_pWBuf->GetPosEof();

  nPosEnd = nPos;

  while((nPosEnd < nPosEof) && (m_pWBuf->Buf(nPosEnd) == 0xFF))
  {
    nPosEnd++;
  }

  bool bSkipped = false;

  if(nPosEnd < nPosEof)
  {
    const uint32_t nCode = m_pWBuf->Buf(nPosEnd);

    bSkipped = ((nCode >= JFIF_RST0) && (nCode <= JFIF_RST7)) || (nCode == 0x00);
  }

  if(nPosEnd > nPos)
  {
    m_anScanFillStart[m_nScanFillNext] = nPos;
    m_anScanFillEnd[m_nScanFillNext] = nPosEnd;
    m_abScanFillSkipped[m_nScanFillNext] = bSkipped;
    m_nScanFillNext = (m_nScanFillNext + 1) % SCAN_FILL_RUNS;
  }

  return bSkipped;
}

// Whether the 0xFF at nPos (before the end of the file) is the start of a
// marker that the end of the file cuts off: it is the file's last byte, or
// only 0xFF bytes follow it to the end. In scan data a 0xFF of data has its
// 0x00, and one before a marker is that marker's, or a fill byte before it.
bool CimgDecode::ScanMarkerCut(uint32_t nPos)
{
  const uint32_t nPosEof = m_pWBuf->GetPosEof();
  const uint32_t nPosNext = nPos + 1;   // At most the end of the file
  uint32_t nPosEnd = nPosNext;

  if((nPosNext < nPosEof) && (m_pWBuf->Buf(nPosNext) == 0xFF))
  {
    ScanFillRun(nPosNext, nPosEnd);
  }

  return nPosEnd >= nPosEof;
}

// Add a byte to the scan buffer from the file
// - Handle stuff bytes
// - Handle restart markers
//
// PRE:
// - m_nScanBuffPtr
// POST:
// - m_nRestartRead
// - m_nRestartLastInd
//
uint32_t CimgDecode::BuffAddByte()
{
  uint32_t nMarker = 0x00;
  uint32_t nBuf0, nBuf1;

  // If we have already read in a restart marker but not
  // handled it yet, then simply return without reading any
  // more bytes
  if(m_bRestartRead)
  {
    return 0;
  }

  // Past the end of the file Buf() returns 0, which decoded as scan data
  // without any error. Mark the byte instead: if the decoder uses it, the
  // scan ends with an error. (Valid files only read ahead past the end,
  // after EOI, and never use those bytes.)
  if(m_nScanBuffPtr >= m_pWBuf->GetPosEof())
  {
    m_nScanDataEnd = qMin(m_nScanDataEnd, m_pWBuf->GetPosEof());
    ScanBuffAddErr(0x00, m_nScanBuffPtr, SCANBUF_EOF);
    ScanBuffPtrAdvance(1);
    return 0;
  }

  nBuf0 = m_pWBuf->Buf(m_nScanBuffPtr);
  nBuf1 = m_pWBuf->Buf(m_nScanBuffPtr + 1);

  // A 0xFF that the file ends in, or a run of them to its end, isn't data
  // but a marker the end of the file cuts off, so the scan's data ends before
  // it, as libjpeg reads it: at the end of the file its source supplies an
  // EOI, and its decoder takes 0xFF bytes before a marker for fill bytes.
  // (With the 0 that Buf() returns past the end, it was read as a stuffed
  // 0xFF: 8 bits of 1 decoded as data, which could add codes or errors and
  // change the image, and the end of the file was reported 1 or 2 bytes past
  // it.) Each of its bytes is marked as one past the end of the file.
  if((nBuf0 == 0xFF) && ScanMarkerCut(m_nScanBuffPtr))
  {
    m_nScanDataEnd = qMin(m_nScanDataEnd, m_nScanBuffPtr);
    ScanBuffAddErr(0x00, m_nScanBuffPtr, SCANBUF_EOF);
    ScanBuffPtrAdvance(1);
    return 0;
  }

  // Fill bytes before an RSTn (T.81 B.1.1.2) are skipped, to the marker's own
  // 0xFF. Added as data, they could fill the buffer before the RSTn was
  // reached (3 or more did in every file tried), so the restart wasn't seen
  // in time ("Restart marker not detected") and the rest of the scan was
  // decoded wrongly. (Before any other marker they are still added: where the
  // scan's data ends is found before them, below.) Before the 0x00 of a
  // stuffed 0xFF they aren't allowed, but are skipped likewise, so the run is
  // one 0xFF of data, as libjpeg documents; each 0xFF was data.
  if((nBuf0 == 0xFF) && (nBuf1 == 0xFF))
  {
    uint32_t nPosCode = 0;

    if(ScanFillRun(m_nScanBuffPtr + 1, nPosCode))
    {
      m_nScanBuffPtr = nPosCode - 1;
      nBuf1 = m_pWBuf->Buf(nPosCode);
    }
  }

  // Check for restart marker first. (One is read at a time: the decoder
  // passes over each of several in a row, in DecodeScanComp().)
  if(nBuf0 == 0xFF)
  {
    nMarker = nBuf1;

    if((nMarker >= JFIF_RST0) && (nMarker <= JFIF_RST7))
    {
      if(m_bVerbose)
      {
        QString strTmp;

        strTmp =
          QString("  RESTART marker: @ 0x%1.0 : RST%2")
            .arg(m_nScanBuffPtr, 8, 16, QChar('0'))
            .arg(nMarker - JFIF_RST0, 2, 10, QChar('0'));
        m_pLog->AddLine(strTmp);
      }

      m_nRestartRead++;
      m_nRestartLastInd = nMarker - JFIF_RST0;

      // Reported up to the scan's error limit, as its other errors are, but
      // on a count of its own, so as not to hide them: each RST of a run in a
      // row is read (1 MB of RST0s logged 524,288 of these, in 88 s). Only
      // without a DRI: with one, an RST out of order is reported where the
      // restart rule takes it, by the interval count (DecodeRestartNext()).
      // (Reported here too, each RST skipped was reported twice, and the
      // right RST after a wrong one was reported as out of order.)
      if(!m_bRestartEn && (m_nRestartLastInd != m_nRestartExpectInd))
      {
        if(!m_bScanErrorsDisable && (m_nWarnRstIndexNum < m_nScanErrMax))
        {
          QString strTmp;

          strTmp =
            QString("  ERROR: Expected RST marker index RST%1 got RST%2 @ 0x%3.0")
              .arg(m_nRestartExpectInd)
              .arg(m_nRestartLastInd)
              .arg(m_nScanBuffPtr, 8, 16, QChar('0'));
          m_pLog->AddLineErr(strTmp);

          m_nWarnRstIndexNum++;

          if(m_nWarnRstIndexNum >= m_nScanErrMax)
          {
            strTmp = QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax);
            m_pLog->AddLineErr(strTmp);
          }
        }
      }

      m_nRestartExpectInd = (m_nRestartLastInd + 1) % 8;

      // FIXME: Later on, we should be checking for RST out of sequence

      // END BUFFER READ HERE!
      // Indicate that a Restart marker has been seen, which
      // will prevent further reading of scan buffer until it
      // has been handled.
      m_bRestartRead = true;

      return 0;

/*
      // Now we need to skip to the next bytes
			m_nScanBuffPtr+=2;

			// Update local saved buffer vars
			nBuf0 = m_pWBuf->Buf(m_nScanBuffPtr);
			nBuf1 = m_pWBuf->Buf(m_nScanBuffPtr+1);

      // Use ScanBuffAddErr() to mark this byte as being after Restart marker!
			m_anScanBuffPtr_err[m_nScanBuffPtr_num] = SCANBUF_RST;

			// FIXME: We should probably discontinue filling the scan
			// buffer if we encounter a restart marker. This will stop us
			// from reading past the restart marker and missing the necessary
			// step in resetting the decoder accumulation state.

      // When we stop adding bytes to the buffer, we should also
			// mark this scan buffer with a flag to indicate that it was
			// ended by RST not by EOI or an Invalid Marker.

			// If the main decoder (in ReadScanVal) cannot find a VLC code
			// match with the few bits left in the scan buffer
			// (presumably padded with 1's), then it should check to see
			// if the buffer is terminated by RST. If so, then it
      // purges the scan buffer, advances to the next byte (after the
			// RST marker) and does a fill, then re-invokes the ReadScanVal
			// routine. At the level above, the Decoder that is calling
			// ReadScanVal should be counting MCU rows and expect this error
			// from ReadScanVal (no code match and buf terminated by RST).
			// If it happened out of place, we have corruption somehow!

			// See IJG Code:
			//   jdmarker.c:
			//     read_restart_marker()
			//     jpeg_resync_to_restart()
*/
    }
  }

  // Check for byte Stuff
  if((nBuf0 == 0xFF) && (nBuf1 == 0x00))
  {
    // Add byte to m_nScanBuff & record file position
    ScanBuffAdd(nBuf0, m_nScanBuffPtr);
    ScanBuffPtrAdvance(2);
  }
  else if((nBuf0 == 0xFF) && (nBuf1 == 0xFF))
  {
    // NOTE:
    // We should be checking for a run of 0xFF before EOI marker.
    // It is possible that we could get marker padding on the end
    // of the scan segment, so we'd want to handle it here, otherwise
    // we'll report an error that we got a non-EOI Marker in scan
    // segment.

    // The downside of this is that we don't detect errors if we have
    // a run of 0xFF in the stream, until we leave the string of FF's.
    // If it were followed by an 0x00, then we may not notice it at all.
    // Probably OK.

    /*
       if (m_nWarnBadScanNum < m_nScanErrMax) {
       QString strTmp;
       strTmp = QString("  Scan Data encountered sequence 0xFFFF @ 0x%08X.0 - Assume start of marker pad at end of scan segment",
       m_nScanBuffPtr);
       m_pLog->AddLineWarn(strTmp);

       m_nWarnBadScanNum++;
       if (m_nWarnBadScanNum >= m_nScanErrMax) {
       strTmp = QString("    Only reported first %u instances of this message..."),m_nScanErrMax;
       m_pLog->AddLineErr(strTmp);
       }
       }

       // Treat as single byte of byte stuff for now, since we don't
       // know if FF bytes will arrive in pairs or not.
       m_nScanBuffPtr+=1;
     */

    // NOTE:
    // If I treat the 0xFFFF as a potential marker pad, we may not stop correctly
    // upon error if we see this inside the image somewhere (not at end).
    // Therefore, let's simply add these bytes to the buffer and let the DecodeScanImg()
    // routine figure out when we're at the end, etc.

    ScanBuffAdd(nBuf0, m_nScanBuffPtr);
    ScanBuffPtrAdvance(1);
  }
  else if((nBuf0 == 0xFF) && (nMarker != 0x00))
  {
    // We have read a marker... don't assume that this is bad as it will
    // always happen at the end of the scan segment. Therefore, we will
    // assume this marker is valid (ie. not bit error in scan stream)
    // and mark the end of the scan segment.

    // The decoder reads on through the marker and the bytes after it, but
    // an MCU that starts there has no data in this scan. Nor does one that
    // starts in the marker's fill bytes (T.81 B.1.1.2), the 0xFF bytes just
    // before it, which the branch above added as data: in scan data a 0xFF is
    // followed by 0x00.
    uint32_t nDataEnd = m_nScanBuffPtr;

    while((nDataEnd > m_nScanBuffPtr_first) && (m_pWBuf->Buf(nDataEnd - 1) == 0xFF))
    {
      nDataEnd--;
    }

    // With a DRI, a marker below SOF0 (0xFF01..0xFFBF) doesn't end it: the
    // restart rule passes over it, as libjpeg does, and the decode goes on
    // after it. (Tools > Lookup MCU Offset gave "Not decoded" for every MCU
    // after it, though each was decoded in its place.)
    if(!m_bRestartEn || (nMarker >= 0xC0))
    {
      m_nScanDataEnd = qMin(m_nScanDataEnd, nDataEnd);
    }

    if(m_nWarnBadScanNum < m_nScanErrMax)
    {
      QString strTmp;

      strTmp = QString("  Scan Data encountered marker   0xFF%1 @ 0x%2.0")
          .arg(nMarker, 2, 16, QChar('0'))
          .arg(m_nScanBuffPtr, 8, 16, QChar('0'));
      m_pLog->AddLine(strTmp);

      if(nMarker != JFIF_EOI)
      {
        m_pLog->AddLineErr("  NOTE: Marker wasn't EOI (0xFFD9)");
      }

      m_nWarnBadScanNum++;

      if(m_nWarnBadScanNum >= m_nScanErrMax)
      {
        strTmp = QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax);
        m_pLog->AddLineErr(strTmp);
      }
    }

    // Optionally stop immediately upon a bad marker
#ifdef SCAN_BAD_MARKER_STOP
    m_bScanEnd = true;
    return 1;
#else
    // Add byte to m_nScanBuff & record file position
    ScanBuffAddErr(nBuf0, m_nScanBuffPtr, SCANBUF_BADMARK);

    ScanBuffPtrAdvance(1);
#endif
  }
  else
  {
    // Normal byte
    // Add byte to m_nScanBuff & record file position
    ScanBuffAdd(nBuf0, m_nScanBuffPtr);

    ScanBuffPtrAdvance(1);
  }

  return 0;
}

// Define minimum value before we include DCT entry in
// the IDCT calcs.
// NOTE: Currently disabled
#define IDCT_COEF_THRESH 4

// Decode a single component for one block of an MCU
// - Pull bits from the main buffer
// - Find matching huffman codes
// - Fill in the IDCT matrix (m_anDctBlock[])
// - Perform the IDCT to create spatial domain
//
// INPUT:
// - nTblDhtDc                                  = DHT table ID for DC component
// - nTblDhtAc                                  = DHT talbe ID for AC component
// - nMcuX                                              = UNUSED
// - nMcuY                                              = UNUSED
// RETURN:
// - Indicate if the decode was successful
// POST:
// - m_nScanCurDc = DC component value
// Performance:
// - This routine is called on every block so it must be
//   reasonably efficient. Only call string routines when we
//   are sure that we have an error.
//
// FIXME: Consider adding checks for DHT table like in ReadScanVal()
//
bool CimgDecode::DecodeScanComp(uint32_t nTblDhtDc, uint32_t nTblDhtAc, uint32_t nTblDqt, uint32_t nMcuX, uint32_t nMcuY)
{
  uint32_t nZrl;

  int32_t nVal;

  bool bDone = false;
  bool bDC = true;              // Start with DC coeff

  teRsvRet eRsvRet;             // Return value from ReadScanVal()

  uint32_t nNumCoeffs = 0;

  //uint32_t nDctMax = 0;                 // Maximum DCT coefficient to use for IDCT
  uint32_t nSavedBufPos = 0;

  uint32_t nSavedBufErr = SCANBUF_OK;

  uint32_t nSavedBufAlign = 0;

  // Profiling: No difference noted
  DecodeIdctClear();

  // The data of this block's restart interval has ended: it isn't read
  if(m_bRestartLost)
  {
    DecodeRestartLostBlock();
    return true;
  }

  while(!bDone)
  {
    BuffTopup();

    // Note that once we perform ReadScanVal(), then GetScanBufPos() will be
    // after the decoded VLC
    // Save old file position info in case we want accurate error positioning
    nSavedBufPos = m_anScanBuffPtr_pos[0];
    nSavedBufErr = m_nScanBuffLatchErr;
    nSavedBufAlign = m_nScanBuffPtr_align;

    // ReadScanVal return values:
    // - RSV_OK                     OK
    // - RSV_EOB            End of block
    // - RSV_UNDERFLOW      Ran out of data in buffer
    // - RSV_RST_TERM       No huffman code found, but restart marker seen
    // - RSV_RST_CUT        A code found, but its bits run on past the marker
    // Assume nTblDht just points to DC tables, adjust for AC
    // e.g. nTblDht = 0,2,4
    eRsvRet = ReadScanVal(bDC ? 0 : 1, bDC ? nTblDhtDc : nTblDhtAc, nZrl, nVal);

    // An RST before the restart interval's last MCU is read (each interval's
    // first is passed over before it, in DecodeRestartNext()): its data ends
    // here, and a code cut short is dropped. The block keeps what was read
    // before it; with no DC read, it is grey as the rest of the interval is.
    if(m_bRestartEn && ((eRsvRet == RSV_RST_TERM) || (eRsvRet == RSV_RST_CUT)))
    {
      if(eRsvRet == RSV_RST_CUT)
      {
        ReportRestartCut(nSavedBufPos, nSavedBufAlign, nMcuX, nMcuY);
      }

      DecodeRestartShort(nMcuX, nMcuY);

      if(bDC)
      {
        DecodeRestartLostBlock();
      }

      break;
    }

    // Without a restart interval (no DRI), an RST is passed over where it is
    // found, and the decode goes on. Another may follow it at once: each is
    // passed over in turn, until a code is read. (The decode went on after
    // the second marker, with no code read: the block took that for its
    // first coefficient, a 0 at file position 0, and Detailed Decode and the
    // MCU's position showed position 0.) A code the marker cuts short is
    // read again after it, likewise.
    while((eRsvRet == RSV_RST_TERM) || (eRsvRet == RSV_RST_CUT))
    {
      // Assume that m_bRestartRead is TRUE
      // No huffman code found because either we ran out of bits
      // in the scan buffer or the bits padded with 1's didn't result
      // in a valid VLC code.

      // Or the code found was cut short: reported where it starts
      if(eRsvRet == RSV_RST_CUT)
      {
        ReportRestartCut(nSavedBufPos, nSavedBufAlign, nMcuX, nMcuY);
      }

      // Steps:
      //   1) Reset the decoder state (DC values)
      //   2) Advance the buffer pointer (might need to handle the
      //      case of perfect alignment to byte boundary separately)
      //   3) Flush the Scan Buffer
      //   4) Clear m_bRestartRead
      //   5) Refill Scan Buffer with BuffTopUp()
      //   6) Re-invoke ReadScanVal()

      // A bad marker where the bits before the RST start (they made no
      // code, or one cut short): reported there, as the code is read again
      // after the RST, and its position taken from there. (Reported after
      // the restart, it was given that position.)
      if(nSavedBufErr == SCANBUF_BADMARK)
      {
        ReportScanBadMarker(nSavedBufPos, nSavedBufAlign);
      }

      // Step 1:
      DecodeRestartDcState();

      // Step 2
      ScanBuffPtrAdvance(2);

      // Step 3
      DecodeRestartScanBuf(m_nScanBuffPtr, true);

      // Step 4
      m_bRestartRead = false;

      // Step 5
      BuffTopup();

      // The code is read after the marker: report its position from there
      // (it was the position before the marker, of the padding bits), and
      // the error of the byte it starts in (one past the end of the file, as
      // after an RST that ends it, was reported a code late)
      nSavedBufPos = m_anScanBuffPtr_pos[0];
      nSavedBufAlign = m_nScanBuffPtr_align;
      nSavedBufErr = m_nScanBuffLatchErr;

      // Step 6 (each pass moves 2 bytes on, and no marker is read past the
      // end of the file, so this ends)
      eRsvRet = ReadScanVal(bDC ? 0 : 1, bDC ? nTblDhtDc : nTblDhtAc, nZrl, nVal);
    }

    // The MCU's first code: where the MCU starts
    if(m_nMcuPosPending >= 0)
    {
      RecordMcuPos(nSavedBufPos, nSavedBufAlign);
    }

    // In case we encountered a restart marker or bad scan marker
    if(nSavedBufErr == SCANBUF_BADMARK)
    {
      ReportScanBadMarker(nSavedBufPos, nSavedBufAlign);

      // Reset the latched error now that we've dealt with it
      m_nScanBuffLatchErr = SCANBUF_OK;
    }
    else if(nSavedBufErr == SCANBUF_EOF)
    {
      // The scan data ran out at the end of the file: stop the scan after
      // this MCU instead of decoding the zeros read past it
      m_bScanBad = true;
      m_bScanEnd = true;

      if(!m_bScanEofReported)
      {
        m_bScanEofReported = true;

        if(m_nWarnBadScanNum < m_nScanErrMax)
        {
          m_pLog->AddLineErr(QString("*** ERROR: Scan data ends at the end of the file in MCU(%1,%2) @ %3 (file truncated?)")
                               .arg(nMcuX)
                               .arg(nMcuY)
                               .arg(GetScanBufPos(nSavedBufPos, nSavedBufAlign)));
        }

        // The rest of this MCU decodes nothing but zeros: don't report it
        m_nWarnBadScanNum = m_nScanErrMax;
      }

      m_nScanBuffLatchErr = SCANBUF_OK;
    }

    int16_t nVal2;

    nVal2 = static_cast<int16_t>(nVal & 0xFFFF);

    if(eRsvRet == RSV_OK)
    {
      // DC entry is always one value only
      if(bDC)
      {
        DecodeIdctSet(nTblDqt, nNumCoeffs, nZrl, nVal2);        //CALZ
        bDC = false;            // Now we will be on AC comps
      }
      else
      {
        // We're on AC entry, so keep looping until
        // we have finished up to 63 entries
        // Set entry in table
        // PERFORMANCE:
        //   No noticeable difference if following is skipped
        if(m_bDecodeScanAc)
        {
          DecodeIdctSet(nTblDqt, nNumCoeffs, nZrl, nVal2);
        }
      }
    }
    else if(eRsvRet == RSV_EOB)
    {
      if(bDC)
      {
        DecodeIdctSet(nTblDqt, nNumCoeffs, nZrl, nVal2);        //CALZ
        // Now that we have finished the DC coefficient, start on AC coefficients
        bDC = false;
      }
      else
      {
        // Now that we have finished the AC coefficients, we are done
        bDone = true;
      }
    }
    else if(eRsvRet == RSV_UNDERFLOW)
    {
      // ERROR

      if(m_nWarnBadScanNum < m_nScanErrMax)
      {
        QString strPos = GetScanBufPos(nSavedBufPos, nSavedBufAlign);

        QString strTmp;

        strTmp = QString("*** ERROR: Bad huffman code @ %1").arg(strPos);
        m_pLog->AddLineErr(strTmp);

        m_nWarnBadScanNum++;

        if(m_nWarnBadScanNum >= m_nScanErrMax)
        {
          strTmp = QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax);
          m_pLog->AddLineErr(strTmp);
        }
      }

      m_nScanCurErr = true;
      bDone = true;

      return false;
    }

    // Increment the number of coefficients
    nNumCoeffs += 1 + nZrl;

    // If we have filled out an entire 64 entries, then we move to
    // the next block without an EOB
    // NOTE: This is only 63 entries because we assume that we
    //       are doing the AC (DC was already bDone in a different pass)

    // FIXME: Would like to combine DC & AC in one pass so that
    // we don't end up having to use 2 tables. The check below will
    // also need to be changed to == 64.
    //
    // Currently, we will have to correct AC nNumCoeffs entries (in IDCT) to
    // be +1 to get real index, as we are ignoring DC position 0.

    if(nNumCoeffs == 64)
    {
      bDone = true;
    }
    else if(nNumCoeffs > 64)
    {
      // ERROR

      if(m_nWarnBadScanNum < m_nScanErrMax)
      {
        QString strTmp;

        QString strPos = GetScanBufPos(nSavedBufPos, nSavedBufAlign);

        strTmp = QString("*** ERROR: @ %1, nNumCoeffs>64 [%2]").arg(strPos).arg(nNumCoeffs);
        m_pLog->AddLineErr(strTmp);

        m_nWarnBadScanNum++;

        if(m_nWarnBadScanNum >= m_nScanErrMax)
        {
          strTmp = QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax);
          m_pLog->AddLineErr(strTmp);
        }
      }

      m_nScanCurErr = true;
      m_bScanBad = true;
      bDone = true;

      nNumCoeffs = 64;          // Just to ensure we don't use an overrun value anywhere
    }
  }

  // We finished the MCU
  // Now calc the IDCT matrix

  // The following code needs to be very efficient.
  // A number of experiments have been carried out to determine
  // the magnitude of speed improvements through various settings
  // and IDCT methods:
  //
  // PERFORMANCE:
  //   Example file: canon_1dsmk2_
  //
  //   0:06       Turn off m_bDecodeScanAc (so no array memset, etc.)
  //   0:10   m_bDecodeScanAc=true, but DecodeIdctCalc() skipped
  //   0:26       m_bDecodeScanAc=true and DecodeIdctCalcFixedpt()
  //   0:27       m_bDecodeScanAc=true and DecodeIdctCalcFloat()

  if(m_bDecodeScanAc)
  {
#ifdef IDCT_FIXEDPT
    DecodeIdctCalcFixedpt();
#else

    // TODO: Select appropriate conversion routine based on performance
//              DecodeIdctCalcFloat(nDctMax);
//              DecodeIdctCalcFloat(nNumCoeffs);
//              DecodeIdctCalcFloat(m_nDctCoefMax);
    DecodeIdctCalcFloat(64);
//              DecodeIdctCalcFloat(32);
#endif
  }

  return true;
}

// Decode a single component for one block of an MCU with printing
// used for the Detailed Decode functionality
// - Same as DecodeScanComp() but adds reporting of variable length codes (VLC)
//
// INPUT:
// - nTblDhtDc                                  = DHT table ID for DC component
// - nTblDhtAc                                  = DHT talbe ID for AC component
// - nMcuX                                              = Current MCU X coordinate (for reporting only)
// - nMcuY                                              = Current MCU Y coordinate (for reporting only)
// RETURN:
// - Indicate if the decode was successful
// POST:
// - m_nScanCurDc = DC component value
// PERFORMANCE:
// - As this routine is called for every MCU, it is important
//   for it to be efficient. However, we are in print mode, so
//   we can afford to be slower.
//
// FIXME: need to fix like DecodeScanComp() (ordering of exit conditions, etc.)
// FIXME: Consider adding checks for DHT table like in ReadScanVal()
//
bool CimgDecode::DecodeScanCompPrint(uint32_t nTblDhtDc, uint32_t nTblDhtAc, uint32_t nTblDqt, uint32_t nMcuX, uint32_t nMcuY,
                                     uint32_t nComp)
{
  bool bPrint = true;

  teRsvRet eRsvRet;

  QString strTmp;
  QString strTbl;
  QString strSpecial;
  QString strPos;

  uint32_t nZrl;
  int32_t nVal;

  bool bDone = false;
  bool bDC = true;              // Start with DC component

  if(bPrint)
  {
    // Name the component, not the quantization table: Cb and Cr usually
    // share table 1, so Cr was labelled "Chr(0)" like Cb. An RGB JPEG's
    // components are R, G and B.
    switch (nComp)
    {
      case SCAN_COMP_Y:
        strTbl = m_bRgbComps ? "R" : "Lum";
        break;

      case SCAN_COMP_CB:
        strTbl = m_bRgbComps ? "G" : "Chr(0)";
        break;

      case SCAN_COMP_CR:
        strTbl = m_bRgbComps ? "B" : "Chr(1)";
        break;

      case SCAN_COMP_K:
        strTbl = "K";
        break;

      default:
        strTbl = "???";
        break;
    }

    strTmp = QString("    %1 (Tbl #%2), MCU=[%3,%4]").arg(strTbl).arg(nTblDqt).arg(nMcuX).arg(nMcuY);
    m_pLog->AddLine(strTmp);
  }

  uint32_t nNumCoeffs = 0;
  uint32_t nSavedBufPos = 0;
  uint32_t nSavedBufErr = SCANBUF_OK;
  uint32_t nSavedBufAlign = 0;

  DecodeIdctClear();

  // The data of this block's restart interval has ended: it isn't read
  if(m_bRestartLost)
  {
    DecodeRestartLostBlock();

    if(bPrint)
    {
      m_pLog->AddLine("      Not decoded: the data of its restart interval ended before it");
      m_pLog->AddLine("");
    }

    return true;
  }

  while(!bDone)
  {
    BuffTopup();

    // Note that once we perform ReadScanVal(), then GetScanBufPos() will be
    // after the decoded VLC

    // Save old file position info in case we want accurate error positioning
    nSavedBufPos = m_anScanBuffPtr_pos[0];
    nSavedBufErr = m_nScanBuffLatchErr;
    nSavedBufAlign = m_nScanBuffPtr_align;

    // Return values:
    //      0 - OK
    //  1 - EOB
    //  2 - Overread error
    //  3 - No huffman code found, but restart marker seen
    //  4 - A code found, but its bits run on past the marker
    eRsvRet = ReadScanVal(bDC ? 0 : 1, bDC ? nTblDhtDc : nTblDhtAc, nZrl, nVal);

    // An RST before the restart interval's last MCU is read: its data ends
    // here (as in DecodeScanComp())
    if(m_bRestartEn && ((eRsvRet == RSV_RST_TERM) || (eRsvRet == RSV_RST_CUT)))
    {
      if(eRsvRet == RSV_RST_CUT)
      {
        ReportRestartCut(nSavedBufPos, nSavedBufAlign, nMcuX, nMcuY);
      }

      DecodeRestartShort(nMcuX, nMcuY);

      if(bDC)
      {
        DecodeRestartLostBlock();
      }

      if(bPrint)
      {
        m_pLog->AddLine(QString("      RST%1 @ 0x%2.0: the data of its restart interval ends here")
                          .arg(m_nRestartLastInd)
                          .arg(m_nScanBuffPtr, 8, 16, QChar('0')));
      }

      break;
    }

    // Without a restart interval (no DRI), an RST is passed over where it is
    // found, and each that follows it at once (as DecodeScanComp(): the code
    // read after the second was reported at [0x00000000.0], showing the
    // file's first bytes), and a code it cuts short
    while((eRsvRet == RSV_RST_TERM) || (eRsvRet == RSV_RST_CUT))
    {
      // Assume that m_bRestartRead is TRUE
      // No huffman code found because either we ran out of bits
      // in the scan buffer or the bits padded with 1's didn't result
      // in a valid VLC code.

      // Or the code found was cut short: reported where it starts
      if(eRsvRet == RSV_RST_CUT)
      {
        ReportRestartCut(nSavedBufPos, nSavedBufAlign, nMcuX, nMcuY);
      }

      // Steps:
      //   1) Reset the decoder state (DC values)
      //   2) Advance the buffer pointer (might need to handle the
      //      case of perfect alignment to byte boundary separately)
      //   3) Flush the Scan Buffer
      //   4) Clear m_bRestartRead
      //   5) Refill Scan Buffer with BuffTopUp()
      //   6) Re-invoke ReadScanVal()

      // A bad marker where the bits before the RST start (they made no
      // code, or one cut short): reported there, as the code is read again
      // after the RST, and its position taken from there. (Reported after
      // the restart, it was given that position.)
      if(nSavedBufErr == SCANBUF_BADMARK)
      {
        ReportScanBadMarker(nSavedBufPos, nSavedBufAlign);
      }

      // Step 1:
      DecodeRestartDcState();

      // Step 2
      ScanBuffPtrAdvance(2);

      // Step 3
      DecodeRestartScanBuf(m_nScanBuffPtr, true);

      // Step 4
      m_bRestartRead = false;

      // Step 5
      BuffTopup();

      // The code is read after the marker: report its position from there
      // (it was the position before the marker, of the padding bits), and
      // the error of the byte it starts in (see DecodeScanComp())
      nSavedBufPos = m_anScanBuffPtr_pos[0];
      nSavedBufAlign = m_nScanBuffPtr_align;
      nSavedBufErr = m_nScanBuffLatchErr;

      // Step 6 (this ends, as in DecodeScanComp())
      eRsvRet = ReadScanVal(bDC ? 0 : 1, bDC ? nTblDhtDc : nTblDhtAc, nZrl, nVal);
    }

    // The MCU's first code: where the MCU starts
    if(m_nMcuPosPending >= 0)
    {
      RecordMcuPos(nSavedBufPos, nSavedBufAlign);
    }

    // In case we encountered a restart marker or bad scan marker
    if(nSavedBufErr == SCANBUF_BADMARK)
    {
      ReportScanBadMarker(nSavedBufPos, nSavedBufAlign);

      // Reset the latched error now that we've dealt with it
      m_nScanBuffLatchErr = SCANBUF_OK;
    }
    else if(nSavedBufErr == SCANBUF_EOF)
    {
      // The scan data ran out at the end of the file: stop the scan after
      // this MCU instead of decoding the zeros read past it
      m_bScanBad = true;
      m_bScanEnd = true;

      if(!m_bScanEofReported)
      {
        m_bScanEofReported = true;

        if(m_nWarnBadScanNum < m_nScanErrMax)
        {
          m_pLog->AddLineErr(QString("*** ERROR: Scan data ends at the end of the file in MCU(%1,%2) @ %3 (file truncated?)")
                               .arg(nMcuX)
                               .arg(nMcuY)
                               .arg(GetScanBufPos(nSavedBufPos, nSavedBufAlign)));
        }

        // The rest of this MCU decodes nothing but zeros: don't report it
        m_nWarnBadScanNum = m_nScanErrMax;
      }

      m_nScanBuffLatchErr = SCANBUF_OK;
    }

    // Should this be before or after restart checks?
    uint32_t nCoeffStart = nNumCoeffs;
    uint32_t nCoeffEnd = nNumCoeffs + nZrl;

    int16_t nVal2;

    nVal2 = static_cast<int16_t>(nVal & 0xFFFF);

    if(eRsvRet == RSV_OK)
    {
      strSpecial = "";
      // DC entry is always one value only
      // FIXME: Do I need nTblDqt == 4 as well?
      if(bDC)
      {
        DecodeIdctSet(nTblDqt, nNumCoeffs, nZrl, nVal2);
        bDC = false;            // Now we will be on AC comps
      }
      else
      {
        // We're on AC entry, so keep looping until
        // we have finished up to 63 entries
        // Set entry in table
        DecodeIdctSet(nTblDqt, nNumCoeffs, nZrl, nVal2);
      }
    }
    else if(eRsvRet == RSV_EOB)
    {
      if(bDC)
      {
        // A DC difference of 0 (size 0), not an end of block (it was
        // reported as "EOB")
        DecodeIdctSet(nTblDqt, nNumCoeffs, nZrl, nVal2);
        bDC = false;            // Now we will be on AC comps
        strSpecial = "";
      }
      else
      {
        bDone = true;
        strSpecial = "EOB";
      }
    }
    else if(eRsvRet == RSV_UNDERFLOW)
    {
      if(m_nWarnBadScanNum < m_nScanErrMax)
      {
        strSpecial = "ERROR";
        strPos = GetScanBufPos(nSavedBufPos, nSavedBufAlign);

        strTmp = QString("*** ERROR: Bad huffman code @ %1").arg(strPos);
        m_pLog->AddLineErr(strTmp);

        m_nWarnBadScanNum++;
        
        if(m_nWarnBadScanNum >= m_nScanErrMax)
        {
          strTmp = QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax);
          m_pLog->AddLineErr(strTmp);
        }
      }

      m_nScanCurErr = true;
      bDone = true;

      // Print out before we leave
      if(bPrint)
      {
        ReportVlc(nSavedBufPos, nSavedBufAlign, nZrl, nVal2, nCoeffStart, nCoeffEnd, strSpecial);
      }

      return false;
    }

    // Increment the number of coefficients
    nNumCoeffs += 1 + nZrl;
    // If we have filled out an entire 64 entries, then we move to
    // the next block without an EOB
    // NOTE: This is only 63 entries because we assume that we
    //       are doing the AC (DC was already done in a different pass)
    if(nNumCoeffs == 64)
    {
      strSpecial = "EOB64";
      bDone = true;
    }
    else if(nNumCoeffs > 64)
    {
      // ERROR

      if(m_nWarnBadScanNum < m_nScanErrMax)
      {
        strPos = GetScanBufPos(nSavedBufPos, nSavedBufAlign);
        strTmp = QString("*** ERROR: @ %1, nNumCoeffs>64 [%2]").arg(strPos).arg(nNumCoeffs);
        m_pLog->AddLineErr(strTmp);

        m_nWarnBadScanNum++;

        if(m_nWarnBadScanNum >= m_nScanErrMax)
        {
          strTmp = QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax);
          m_pLog->AddLineErr(strTmp);
        }
      }

      m_nScanCurErr = true;
      m_bScanBad = true;
      bDone = true;

      nNumCoeffs = 64;          // Just to ensure we don't use an overrun value anywhere
    }

    if(bPrint)
    {
      ReportVlc(nSavedBufPos, nSavedBufAlign, nZrl, nVal2, nCoeffStart, nCoeffEnd, strSpecial);
    }
  }

  // We finished the MCU component

  // Now calc the IDCT matrix, as DecodeScanComp() does: only when the image
  // shows the AC. (In No IDCT mode, the MCUs reported were drawn at full
  // resolution and the others from their DC, which also changed the average
  // and brightest pixel.) The report still lists every coefficient.
  if(m_bDecodeScanAc)
  {
#ifdef IDCT_FIXEDPT
    DecodeIdctCalcFixedpt();
#else
//      DecodeIdctCalcFloat(nNumCoeffs);
    DecodeIdctCalcFloat(64);
#endif
  }

  // Now report the coefficient matrix (after zigzag reordering), with the
  // block's DC: the difference decoded here plus the component's predictor
  // (read now, as a restart marker in this block resets it). The caller adds
  // the difference to the predictor afterwards.
  if(bPrint)
  {
    ReportDctMatrix(DcPredictor(nComp));
  }

  return true;
}

// The DC predictor of a component: the previous block's DC (dequantized),
// to which a block's DC difference is added
//
int32_t CimgDecode::DcPredictor(uint32_t nComp) const
{
  switch(nComp)
  {
    case SCAN_COMP_Y:
      return m_nDcLum;

    case SCAN_COMP_CB:
      return m_nDcChrCb;

    case SCAN_COMP_CR:
      return m_nDcChrCr;

    default:
      // K is only decoded in DEBUG_YCCK builds, whose predictor isn't kept here
      return 0;
  }
}

// Print out the DCT matrix for a given block
//
// INPUT:
// - nDcPredictor                       = Added to the DC entry, which holds the
//                                        block's DC difference. (The matrix
//                                        showed the difference, so a flat
//                                        image's blocks after the first had DC 0.)
// PRE:
// - m_anDctBlock[]
//
void CimgDecode::ReportDctMatrix(int32_t nDcPredictor)
{
  QString strTmp;
  QString strLine;

  int32_t nCoefVal;

  for(uint32_t nY = 0; nY < 8; nY++)
  {
    if(nY == 0)
    {
      strLine = "                      DCT Matrix=[";
    }
    else
    {
      strLine = "                                 [";
    }

    for(uint32_t nX = 0; nX < 8; nX++)
    {
      strTmp = "";
      nCoefVal = m_anDctBlock[nY * 8 + nX];

      if((nY * 8 + nX) == DCT_COEFF_DC)
      {
        nCoefVal += nDcPredictor;
      }

      strTmp = QString("%1").arg(nCoefVal, 5);
      strLine.append(strTmp);

      if(nX != 7)
      {
        strLine.append(" ");
      }
    }

    strLine.append("]");
    m_pLog->AddLine(strLine);
  }

  m_pLog->AddLine("");
}

// Report out the variable length codes (VLC)
//
// Need to consider impact of padding bytes... When pads get extracted, they
// will not appear in the binary display shown below. Might want the get buffer
// to do post-pad removal first.
//
// eg. file Dsc0019.jpg
// Overlay 0x4215 = 7FFF0000 len=4
//
// INPUT:
// - nVlcPos                            =
// - nVlcAlign                          =
// - nZrl                                       =
// - nVal                                       =
// - nCoeffStart                        =
// - nCoeffEnd                          =
// - specialStr                         =
//
void CimgDecode::ReportVlc(uint32_t nVlcPos, uint32_t nVlcAlign,
                           uint32_t nZrl, int32_t nVal, uint32_t nCoeffStart, uint32_t nCoeffEnd, QString specialStr)
{
  QString strPos;
  QString strTmp;

  // (In 64 bits: past the end of the largest file it wrapped to its start,
  // which was shown. Past the end each byte is 0, as the decoder read it.)
  quint64 nBufPosInd = nVlcPos;
  const quint64 nPosEof = m_pWBuf->GetPosEof();

  strPos = GetScanBufPos(nVlcPos, nVlcAlign);

  // Show enough bytes for the code and its value from the bit offset, at
  // least 4 as before. A 16-bit code and a 15-bit value from bit 7 take 5:
  // with only 4, such a code lost its last bits from the display.
  const uint32_t nBitsUsed = m_nScanBitsUsed1 + m_nScanBitsUsed2;
  const uint32_t nBytes = std::min<uint32_t>(std::max<uint32_t>(4, (nVlcAlign + nBitsUsed + 7) / 8), 8);

  // Read in the buffer bytes, but skip pad bytes (0xFF00 -> 0xFF)

  // We need to look at previous byte as it might have been
  // start of stuff byte! If so, we need to ignore the byte
  // and advance the pointers.
  uint32_t nBufBytePrev = ((nBufPosInd > 0) && (nBufPosInd - 1 < nPosEof)) ? m_pWBuf->Buf(nVlcPos - 1) : 0;

  QString strHex;
  QString strBytes;

  for(uint32_t nInd = 0; nInd < nBytes; nInd++)
  {
    uint32_t nBufByte = (nBufPosInd < nPosEof) ? m_pWBuf->Buf(static_cast<uint32_t>(nBufPosInd)) : 0;
    nBufPosInd++;

    if((nBufBytePrev == 0xFF) && (nBufByte == 0x00))
    {
      nBufByte = (nBufPosInd < nPosEof) ? m_pWBuf->Buf(static_cast<uint32_t>(nBufPosInd)) : 0;
      nBufPosInd++;
    }

    // A marker that the end of the file cuts off is 0, as the decoder reads
    // it (see BuffAddByte()). (A 0xFF is read only before the end.)
    if((nBufByte == 0xFF) && ScanMarkerCut(static_cast<uint32_t>(nBufPosInd - 1)))
    {
      nBufByte = 0x00;
    }

    // Fill bytes, before an RSTn or a stuffed 0xFF's 0x00, are skipped as the
    // decoder skips them (see BuffAddByte()): each was shown, and the bytes
    // after them later than the decoder read them. On to the RSTn's code, or
    // the 0x00, passed over next as after a 0xFF. (Not for the last byte
    // shown: nothing after it is.)
    if((nInd + 1 < nBytes) && (nBufByte == 0xFF) && (nBufPosInd < nPosEof) &&
       (m_pWBuf->Buf(static_cast<uint32_t>(nBufPosInd)) == 0xFF))
    {
      uint32_t nPosCode = 0;

      if(ScanFillRun(static_cast<uint32_t>(nBufPosInd), nPosCode))
      {
        nBufPosInd = nPosCode;
      }
    }

    strHex += QString(" %1").arg(nBufByte, 2, 16, QChar('0'));
    strBytes += Dec2Bin(nBufByte, 8, true);
    nBufBytePrev = nBufByte;
  }

  // The code and value's bits, the rest shown as "-", in bytes
  QString strBinMarked = QString(nVlcAlign, QChar('-')) + strBytes.mid(nVlcAlign, nBitsUsed);
  strBinMarked = strBinMarked.leftJustified(static_cast<qsizetype>(nBytes * 8), QChar('-'), true);

  for(uint32_t nInd = nBytes - 1; nInd > 0; nInd--)
  {
    strBinMarked.insert(static_cast<qsizetype>(nInd * 8), QChar(' '));
  }

  QString strData = QString("0x%1 = 0b (%2)").arg(strHex, strBinMarked);

  if((nCoeffStart == 0) && (nCoeffEnd == 0))
  {
    strTmp = QString("      [%1]: ZRL=[%2] Val=[%3] Coef=[%4= DC] Data=[%5] %6")
        .arg(strPos)
        .arg(nZrl, 2)
        .arg(nVal, 5)
        .arg(nCoeffStart, 2, 10, QChar('0'))
        .arg(strData)
        .arg(specialStr);
  }
  else
  {
    strTmp = QString("      [%1]: ZRL=[%2] Val=[%3] Coef=[%4..%5] Data=[%6] %7")
        .arg(strPos)
        .arg(nZrl, 2)
        .arg(nVal, 5)
        .arg(nCoeffStart, 2, 10, QChar('0'))
        .arg(nCoeffEnd, 2, 10, QChar('0'))
        .arg(strData)
        .arg(specialStr);
  }

  m_pLog->AddLine(strTmp);
}

// Clear input and output matrix
//
// POST:
// - m_anDctBlock[]
// - m_afIdctBlock[]
// - m_anIdctBlock[]
// - m_nDctCoefMax
//
void CimgDecode::DecodeIdctClear()
{
  memset(m_anDctBlock, 0, sizeof m_anDctBlock);
  memset(m_afIdctBlock, 0, sizeof m_afIdctBlock);
  memset(m_anIdctBlock, 0, sizeof m_anIdctBlock);

  m_nDctCoefMax = 0;
}

// Set the DCT matrix entry
// - Fills in m_anDctBlock[] with the unquantized coefficients
// - Reversing the quantization is done using m_anDqtCoeffZz[][]
//
// INPUT:
// - nDqtTbl                            =
// - num_coeffs                         =
// - zrl                                        =
// - val                                        =
// PRE:
// - glb_anZigZag[]
// - m_anDqtCoeffZz[][]
// - m_nDctCoefMax
// POST:
// - m_anDctBlock[]
// NOTE:
// - We need to convert between the zigzag order and the normal order
//
void CimgDecode::DecodeIdctSet(uint32_t nDqtTbl, uint32_t num_coeffs, uint32_t zrl, int16_t val)
{
  uint32_t ind = num_coeffs + zrl;

  if(ind >= 64)
  {
    // We have an error! Don't set the block. Skip this comp for now
    // After this call, we will likely trap the error.
  }
  else
  {
    uint32_t nDctInd = glb_anZigZag[ind];

    int16_t nValUnquant = val * m_anDqtCoeffZz[nDqtTbl][ind];

    /*
       // NOTE:
       //  To test steganography analysis, we can experiment with dropping
       //  specific components of the image.
       uint32_t nRow = nDctInd/8;
       uint32_t nCol = nDctInd - (nRow*8);
       if ((nRow == 0) && (nCol>=0 && nCol<=7)) {
       nValUnquant = 0;
       }
     */

    m_anDctBlock[nDctInd] = nValUnquant;

    // Update max DCT coef # (after unzigzag) so that we can save
    // some work when performing IDCT.
    // FIXME: The following doesn't seem to work when we later
    // restrict DecodeIdctCalc() to only m_nDctCoefMax coefs!

//              if ( (nDctInd > m_nDctCoefMax) && (abs(nValUnquant) >= IDCT_COEF_THRESH) ) {
    if(nDctInd > m_nDctCoefMax)
    {
      m_nDctCoefMax = nDctInd;
    }
  }
}

// Precalculate the IDCT lookup tables
//
// POST:
// - m_afIdctLookup[]
// - m_anIdctLookup[]
// NOTE:
// - This is 4k entries @ 4B each = 16KB
//
void CimgDecode::PrecalcIdct()
{
  uint32_t nX, nY, nU, nV;

  uint32_t nYX, nVU;

  double fCu, fCv;
  double fCosProd;
  double fInsideProd;
  double fPi = 3.141592654;
  double fSqrtHalf = 0.707106781;

  for(nY = 0; nY < DCT_SZ_Y; nY++)
  {
    for(nX = 0; nX < DCT_SZ_X; nX++)
    {
      nYX = nY * DCT_SZ_X + nX;

      for(nV = 0; nV < DCT_SZ_Y; nV++)
      {
        for(nU = 0; nU < DCT_SZ_X; nU++)
        {

          nVU = nV * DCT_SZ_X + nU;

          fCu = (nU == 0) ? fSqrtHalf : 1;
          fCv = (nV == 0) ? fSqrtHalf : 1;
          fCosProd = cos((2 * nX + 1) * nU * fPi / 16) * cos((2 * nY + 1) * nV * fPi / 16);
          // Note that the only part we are missing from
          // the "Inside Product" is the "m_afDctBlock[nV*8+nU]" term
          fInsideProd = fCu * fCv * fCosProd;

          // Store the Lookup result
          m_afIdctLookup[nYX][nVU] = fInsideProd;

          // Store a fixed point Lookup as well
          m_anIdctLookup[nYX][nVU] = static_cast<int32_t>(fInsideProd * (1 << 10));
        }
      }
    }
  }
}

// Perform IDCT
//
// Formula:
//  See itu-t81.pdf, section A.3.3
//
// s(yx) = 1/4*Sum(u=0..7)[ Sum(v=0..7)[ C(u) * C(v) * S(vu) *
//                     cos( (2x+1)*u*Pi/16 ) * cos( (2y+1)*v*Pi/16 ) ] ]
// Cu, Cv = 1/sqrt(2) for u,v=0
// Cu, Cv = 1 else
//
// INPUT:
// - nCoefMax                           = Maximum number of coefficients to calculate
// PRE:
// - m_afIdctLookup[][]
// - m_anDctBlock[]
// POST:
// - m_afIdctBlock[]
//
void CimgDecode::DecodeIdctCalcFloat(uint32_t nCoefMax)
{
  uint32_t nYX, nVU;

  double fSum;

  for(nYX = 0; nYX < DCT_SZ_ALL; nYX++)
  {
    fSum = 0;

    // Skip DC coefficient!
    for(nVU = 1; nVU < nCoefMax; nVU++)
    {
      fSum += m_afIdctLookup[nYX][nVU] * m_anDctBlock[nVU];
    }
    
    fSum *= 0.25;

    // Store the result
    // FIXME: Note that float->int is very slow!
    //   Should consider using fixed point instead!
    m_afIdctBlock[nYX] = fSum;
  }
}

// Fixed point version of DecodeIdctCalcFloat()
//
// PRE:
// - m_anIdctLookup[][]
// - m_anDctBlock[]
// POST:
// - m_anIdctBlock[]
//
void CimgDecode::DecodeIdctCalcFixedpt()
{
  uint32_t nYX, nVU;

  int32_t nSum;

  for(nYX = 0; nYX < DCT_SZ_ALL; nYX++)
  {
    nSum = 0;
    
    // Skip DC coefficient!
    for(nVU = 1; nVU < DCT_SZ_ALL; nVU++)
    {
      nSum += m_anIdctLookup[nYX][nVU] * m_anDctBlock[nVU];
    }

    nSum /= 4;

    // Store the result
    // FIXME: Note that float->int is very slow!
    //   Should consider using fixed point instead!
    m_anIdctBlock[nYX] = nSum >> 10;
  }
}

// Clear the entire pixel image arrays for all three components (YCC)
//
// INPUT:
// - nWidth                                     = Current allocated image width
// - nHeight                            = Current allocated image height
// POST:
// - m_pPixValY
// - m_pPixValCb
// - m_pPixValCr
//
void CimgDecode::ClrFullRes(int32_t nWidth, int32_t nHeight)
{
  Q_ASSERT(m_pPixValY);
  
  if(m_nNumSosComps == NUM_CHAN_YCC)
  {
    Q_ASSERT(m_pPixValCb);
    Q_ASSERT(m_pPixValCr);
  }
  
  // FIXME: Add in range checking here
  memset(m_pPixValY, 0, (nWidth * nHeight * sizeof(int16_t)));
  
  if(m_nNumSosComps == NUM_CHAN_YCC)
  {
    memset(m_pPixValCb, 0, (nWidth * nHeight * sizeof(int16_t)));
    memset(m_pPixValCr, 0, (nWidth * nHeight * sizeof(int16_t)));
  }
}

// Generate a single component's pixel content for one MCU
// - Fetch content from the 8x8 IDCT block (m_afIdctBlock[])
//   for the specified component (nComp)
// - Transfer the pixel content to the specified component's
//   pixel map (m_pPixValY[],m_pPixValCb[],m_pPixValCr[])
// - DC level shifting is performed (nDcOffset)
// - Replication of pixels according to Chroma Subsampling (sampling factors)
//
// INPUT:
// - nMcuX                                      =
// - nMcuY                                      =
// - nComp                                      = Component index (1,2,3)
// - nCssXInd                           =
// - nCssYInd                           =
// - nDcOffset                          =
// PRE:
// - DecodeIdctCalc() already called on Lum AC, and Lum DC already done
//
void CimgDecode::SetFullRes(int32_t nMcuX, int32_t nMcuY, int32_t nComp, uint32_t nCssXInd, uint32_t nCssYInd, int16_t nDcOffset)
{
  uint32_t nYX;

  double fVal;

  int16_t nVal;

  int32_t nChan;

  // Convert from Component index (1-based) to Channel index (0-based)
  // Component index is direct from SOF/SOS
  // Channel index is used for internal display representation
  if(nComp <= 0)
  {
#ifdef DEBUG_LOG
    QString strTmp;
    QString strDebug;

    strTmp = QString("SetFullRes() with nComp <= 0 [%1]").arg(nComp);
    strDebug = QString("## File=[%1] Block=[%2] Error=[%3]\n")
        .arg(m_pAppConfig->strCurFname, -100)
        .arg("ImgDecode", -10)
        .arg(strTmp);
    qDebug() << strDebug;
#else
    Q_ASSERT(false);
#endif
    return;
  }

  nChan = nComp - 1;

  int32_t nPixMapW = m_nBlkXMax * BLK_SZ_X;    // Width of pixel map
  int32_t nOffsetBlkCorner;    // Linear offset to top-left corner of block
  int32_t nOffsetPixCorner;    // Linear offset to top-left corner of pixel (start point for expansion)

  // Calculate the linear pixel offset for the top-left corner of the block in the MCU.
  // A subsampled block covers 8 pixels times its expansion factor, so that is
  // how far apart the blocks are. (They were placed 8 pixels apart, which put
  // the second chroma block of Y 4x1 / Cb 2x1 at 8 instead of 16.)
  const uint32_t nExpandH = static_cast<uint32_t>(m_anExpandBitsMcuH[nComp]);
  const uint32_t nExpandV = static_cast<uint32_t>(m_anExpandBitsMcuV[nComp]);

  nOffsetBlkCorner = ((nMcuY * m_nMcuHeight) + nCssYInd * BLK_SZ_Y * nExpandV) * nPixMapW +
                     ((nMcuX * m_nMcuWidth) + nCssXInd * BLK_SZ_X * nExpandH);

  // Use the expansion factor to determine how many bits to replicate
  // Typically for luminance (Y) this will be 1 & 1
  // The replication factor is available in m_anExpandBitsMcuH[] and m_anExpandBitsMcuV[]

  // Step through all pixels in the block
  for(uint32_t nY = 0; nY < BLK_SZ_Y; nY++)
  {
    for(uint32_t nX = 0; nX < BLK_SZ_X; nX++)
    {
      nYX = nY * BLK_SZ_X + nX;

      // Fetch the pixel value from the IDCT 8x8 block and perform DC level shift
#ifdef IDCT_FIXEDPT
      nVal = m_anIdctBlock[nYX];
      // TODO: Why do I need AC value x8 multiplier?
      nVal = (nVal * 8) + nDcOffset;
#else
      fVal = m_afIdctBlock[nYX];
      // TODO: Why do I need AC value x8 multiplier?
      // Truncate before adding the DC offset, as before, but saturate instead
      // of an undefined conversion when a corrupt block is out of range
      nVal = SaturateInt16(std::trunc(fVal * 8) + nDcOffset);
#endif

      // NOTE: These range checks were already done in DecodeScanImg()
      Q_ASSERT(nCssXInd < MAX_SAMP_FACT_H);
      Q_ASSERT(nCssYInd < MAX_SAMP_FACT_V);
      Q_ASSERT(nY < BLK_SZ_Y);
      Q_ASSERT(nX < BLK_SZ_X);

      // Set the pixel value for the component

      // We start with the linear offset into the pixel map for the top-left
      // corner of the block. Then we adjust to determine the top-left corner
      // of the pixel that we may optionally expand in subsampling scenarios.

      // Calculate the top-left corner pixel linear offset after taking
      // into account any expansion in the X direction
      nOffsetPixCorner = nOffsetBlkCorner + nX * m_anExpandBitsMcuH[nComp];

      // Replication the pixels as specified in the sampling factor
      // This is typically done for the chrominance channels when
      // chroma subsamping is used.
      for(uint32_t nIndV = 0; nIndV < m_anExpandBitsMcuV[nComp]; nIndV++)
      {
        for(uint32_t nIndH = 0; nIndH < m_anExpandBitsMcuH[nComp]; nIndH++)
        {
          if(nChan == CHAN_Y)
          {
            m_pPixValY[nOffsetPixCorner + (nIndV * nPixMapW) + nIndH] = nVal;
          }
          else if(nChan == CHAN_CB)
          {
            m_pPixValCb[nOffsetPixCorner + (nIndV * nPixMapW) + nIndH] = nVal;
          }
          else if(nChan == CHAN_CR)
          {
            m_pPixValCr[nOffsetPixCorner + (nIndV * nPixMapW) + nIndH] = nVal;
          }
          else
          {
            Q_ASSERT(false);
          }
        }                       // nIndH
      }                         // nIndV
    }                           // nX

    nOffsetBlkCorner += (nPixMapW * m_anExpandBitsMcuV[nComp]);
  }                             // nY
}

// Calculates the actual byte offset (from start of file) for
// the current position in the m_nScanBuff.
//
// PRE:
// - m_anScanBuffPtr_pos[]
// - m_nScanBuffPtr_align
// RETURN:
// - File position
//
QString CimgDecode::GetScanBufPos()
{
  return GetScanBufPos(m_anScanBuffPtr_pos[0], m_nScanBuffPtr_align);
}

// Generate a file position string that also indicates bit alignment
//
// INPUT:
// - pos                        = File position (byte)
// - align                      = File position (bit)
// RETURN:
// - Formatted string
//
QString CimgDecode::GetScanBufPos(uint32_t pos, uint32_t align)
{
  return QString("0x%1.%2")
      .arg(pos, 8, 16, QChar('0'))
      .arg(align);;
}

// Report a code cut short by a restart marker (ReadScanVal() returned
// RSV_RST_CUT), up to the scan's error limit, as its other errors are, but on
// a count of its own, so as not to hide them (on theirs, 20 of these hid the
// error for a file cut short after them), as the RST index error is
// - Called before the marker is passed over: m_nScanBuffPtr is on its 0xFF
//
// INPUT:
// - nPos                       = File position where the code starts (byte)
// - nAlign                     = File position where the code starts (bit)
// - nMcuX                      = MCU coordinate (x)
// - nMcuY                      = MCU coordinate (y)
// PRE:
// - m_nScanBuffPtr
// - m_nRestartLastInd
// - m_bScanErrorsDisable
// POST:
// - m_nWarnRstCutNum
//
void CimgDecode::ReportRestartCut(uint32_t nPos, uint32_t nAlign, uint32_t nMcuX, uint32_t nMcuY)
{
  if(!m_bScanErrorsDisable && (m_nWarnRstCutNum < m_nScanErrMax))
  {
    QString strTmp;

    strTmp = QString("*** ERROR: Huffman code @ %1 in MCU(%2,%3) cut short by RST%4 @ 0x%5.0")
        .arg(GetScanBufPos(nPos, nAlign))
        .arg(nMcuX)
        .arg(nMcuY)
        .arg(m_nRestartLastInd)
        .arg(m_nScanBuffPtr, 8, 16, QChar('0'));
    m_pLog->AddLineErr(strTmp);

    m_nWarnRstCutNum++;

    if(m_nWarnRstCutNum >= m_nScanErrMax)
    {
      strTmp = QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax);
      m_pLog->AddLineErr(strTmp);
    }
  }
}

// Restart intervals (T.81 B.2.4.4, F.2.2.4): each holds the scan data of a
// fixed number of MCUs (the DRI's), and an RST follows each but the last.
// Each interval's data is decoded at the MCUs the restart interval sets, as
// libjpeg does (jdhuff.c process_restart(), jdmarker.c read_restart_marker()
// and jpeg_resync_to_restart()):
// - An RST that comes before the interval's last MCU is read ends its data
//   (DecodeRestartShort()): the rest of the interval isn't decoded, but left
//   grey, and the data after the RST is the next interval's.
// - Data after the interval's last MCU is skipped to the next marker
//   (DecodeRestartNext()), and the RST found there is taken by its number.
// (The decoder went on decoding where an RST was found, continuing the block
// or MCU it was in: when an interval had lost data, the rest of the image was
// decoded one or more MCUs early, and an RST inside an MCU put the data after
// it into the wrong block or component, for the rest of the image. Bits
// before an RST that made no code were dropped with no message.)
//
// Without a DRI, an RST in the scan data is passed over where it is found,
// as before (DecodeScanComp()).

// The MCUs from nMcuFirst on, nMcuCount of them, but not past the image's
// last: "MCU(x,y)", or "MCU(x,y) to MCU(x2,y2)"
//
QString CimgDecode::RestartMcuRange(uint64_t nMcuFirst, uint64_t nMcuCount) const
{
  const uint64_t nMcuX = static_cast<uint64_t>(qMax(m_nMcuXMax, 1));
  const uint64_t nMcuAll = nMcuX * static_cast<uint64_t>(qMax(m_nMcuYMax, 1));

  uint64_t nMcuLast = nMcuFirst + ((nMcuCount > 0) ? (nMcuCount - 1) : 0);

  if(nMcuLast >= nMcuAll)
  {
    nMcuLast = nMcuAll - 1;
  }

  QString strRange = QString("MCU(%1,%2)").arg(nMcuFirst % nMcuX).arg(nMcuFirst / nMcuX);

  if(nMcuLast > nMcuFirst)
  {
    strRange += QString(" to MCU(%1,%2)").arg(nMcuLast % nMcuX).arg(nMcuLast / nMcuX);
  }

  return strRange;
}

// A marker found by RestartSkipToMarker(), for the log
//
// INPUT:
// - nMarker                    = The marker's code (0xD0..0xD7 for an RST), or 0
//                                for the end of the file
// - nMarkerPos                 = File position of its 0xFF (or of the end)
//
QString CimgDecode::RestartMarkerStr(uint32_t nMarker, uint32_t nMarkerPos) const
{
  if(nMarker == 0)
  {
    return QString("the end of the file @ 0x%1.0").arg(nMarkerPos, 8, 16, QChar('0'));
  }

  if((nMarker >= JFIF_RST0) && (nMarker <= JFIF_RST7))
  {
    return QString("RST%1 @ 0x%2.0").arg(nMarker - JFIF_RST0).arg(nMarkerPos, 8, 16, QChar('0'));
  }

  return QString("marker 0xFF%1 @ 0x%2.0").arg(nMarker, 2, 16, QChar('0')).arg(nMarkerPos, 8, 16, QChar('0'));
}

// Whether another message about a missing, late or out of order RST is
// reported: up to the scan's error limit, as its other errors are, but on a
// count of its own, so as not to hide them (one can come at each restart
// interval). Checked before building the message.
//
bool CimgDecode::RestartSyncReportable() const
{
  return !m_bScanErrorsDisable && (m_nWarnRstSyncNum < m_nScanErrMax);
}

// Report a message about a missing, late or out of order RST
// (RestartSyncReportable() said it is reported)
//
// POST:
// - m_nWarnRstSyncNum
//
void CimgDecode::ReportRestartSync(const QString &strMsg)
{
  m_pLog->AddLineErr(strMsg);

  m_nWarnRstSyncNum++;

  if(m_nWarnRstSyncNum >= m_nScanErrMax)
  {
    m_pLog->AddLineErr(QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax));
  }
}

// Pass over the RST that the scan buffer stopped at (m_bRestartRead), and
// start the buffer again after it
//
// PRE:
// - m_nScanBuffPtr             = On the marker's 0xFF
// POST:
// - m_nScanBuffPtr
// - m_bRestartRead
//
void CimgDecode::RestartPassMarker()
{
  ScanBuffPtrAdvance(2);
  DecodeRestartScanBuf(m_nScanBuffPtr, true);
}

// Skip the scan data from nPos to the next marker, as libjpeg's
// next_marker() does (passing over stuffed 0xFF 0x00 and fill bytes), and
// markers below SOF0 (0xFF01..0xFFBF), which its jpeg_resync_to_restart()
// passes over. The scan buffer is started again at the marker found, which
// it reads (BuffAddByte()) if it hadn't: an RST stops the buffer there
// (m_bRestartRead), as at the end of an interval; another marker, or the end
// of the file, ends the scan's data. (The RST the buffer had stopped at is
// left as it is, so it isn't counted or checked twice; a marker the buffer
// had read past was reported then.) Each byte is looked at once, ahead of
// nPos only.
//
// INPUT:
// - nPos                       = File position to search from
// PRE:
// - m_nScanBuffPtr             = Where the scan buffer has read the file to
//                                (on the RST it stopped at, if m_bRestartRead)
// OUTPUT:
// - nMarkerPos                 = File position of the marker's 0xFF (or of the
//                                end of the file)
// RETURN:
// - The marker's code (0xD0..0xD7 for an RST), or 0 at the end of the file
//
uint32_t CimgDecode::RestartSkipToMarker(uint32_t nPos, uint32_t &nMarkerPos)
{
  const uint32_t nPosEof = m_pWBuf->GetPosEof();
  const uint32_t nReadTo = m_nScanBuffPtr;
  const bool bRstRead = m_bRestartRead;

  uint32_t nMarker = 0;

  nMarkerPos = nPosEof;

  while(nPos < nPosEof)
  {
    if(m_pWBuf->Buf(nPos) != 0xFF)
    {
      nPos++;
      continue;
    }

    // A run of 0xFF: the last is a marker's, unless a 0x00 follows (data)
    uint32_t nPosNext = nPos + 1;

    while((nPosNext < nPosEof) && (m_pWBuf->Buf(nPosNext) == 0xFF))
    {
      nPosNext++;
    }

    // A run to the end of the file is a marker the end cuts off, so the data
    // ends at its first 0xFF (see BuffAddByte()). (The end of the file was
    // given, past it, and the 0xFF bytes were counted as scan data.)
    if(nPosNext >= nPosEof)
    {
      nMarkerPos = nPos;
      break;
    }

    const uint32_t nCode = m_pWBuf->Buf(nPosNext);

    // A stuffed 0xFF (0x00 after it) is data; a marker below SOF0 (0xC0:
    // JfifDecode.h's JFIF_SOF0, not included here) is passed over
    if(nCode < 0xC0)
    {
      nPos = nPosNext + 1;
      continue;
    }

    nMarker = nCode;
    nMarkerPos = nPosNext - 1;
    break;
  }

  // The RST the scan buffer stopped at
  if(bRstRead && (nMarkerPos == nReadTo))
  {
    return nMarker;
  }

  // Read it from there, unless the buffer read it already. (The buffer stops
  // at each RST, so one before nReadTo is not this one.)
  DecodeRestartScanBuf(nMarkerPos, true);

  if(nMarkerPos >= nReadTo)
  {
    BuffAddByte();
  }

  return nMarker;
}

// An RST came before the current restart interval's last MCU was read (as
// ReadScanVal() found no code, or a code cut short, before it): the
// interval's data ends there. The rest of the interval isn't decoded (its
// blocks are left grey: DecodeRestartLostBlock()), and the RST is left for
// DecodeRestartNext() to pass over when the next interval starts, at the MCU
// the restart interval sets. (libjpeg decodes the rest of the MCU from zero
// bits, and leaves the interval's MCUs after it grey.) Reported up to the
// scan's error limit, on a count of its own, as each interval can end so.
//
// INPUT:
// - nMcuX                      = MCU coordinate (x)
// - nMcuY                      = MCU coordinate (y)
// PRE:
// - m_nScanBuffPtr             = On the RST's 0xFF
// - m_nRestartLastInd
// - m_nRestartMcusLeft         = MCUs of the interval left, this one included
// POST:
// - m_bRestartLost
// - m_nWarnRstShortNum
//
void CimgDecode::DecodeRestartShort(uint32_t nMcuX, uint32_t nMcuY)
{
  m_bRestartLost = true;

  if(!m_bScanErrorsDisable && (m_nWarnRstShortNum < m_nScanErrMax))
  {
    const uint64_t nMcu = static_cast<uint64_t>(nMcuY) * static_cast<uint64_t>(qMax(m_nMcuXMax, 1)) + nMcuX;

    // Its last MCU (the range's end)
    const QString strRange = RestartMcuRange(nMcu, qMax(m_nRestartMcusLeft, 1u));
    const qsizetype nTo = strRange.indexOf(" to ");

    m_pLog->AddLineErr(QString("*** ERROR: Restart interval ends early, at RST%1 @ 0x%2.0 in MCU(%3,%4): "
                               "the rest of it, to %5, not decoded")
                         .arg(m_nRestartLastInd)
                         .arg(m_nScanBuffPtr, 8, 16, QChar('0'))
                         .arg(nMcuX)
                         .arg(nMcuY)
                         .arg((nTo < 0) ? strRange : strRange.mid(nTo + 4)));

    m_nWarnRstShortNum++;

    if(m_nWarnRstShortNum >= m_nScanErrMax)
    {
      m_pLog->AddLineErr(QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax));
    }
  }
}

// A block of a restart interval whose data has ended (DecodeRestartShort(),
// DecodeRestartNext()) isn't read: it is left grey, its DC 0 and no AC, as
// libjpeg leaves it. The caller adds the block's DC difference (0) to its
// component's predictor, so they are all set to 0; the next interval starts
// them at 0 again.
//
// POST:
// - m_nDcLum
// - m_nDcChrCb
// - m_nDcChrCr
//
void CimgDecode::DecodeRestartLostBlock()
{
  m_nDcLum = 0;
  m_nDcChrCb = 0;
  m_nDcChrCr = 0;
}

// Start the next restart interval, before its first MCU (libjpeg's
// process_restart()): the DC predictors start at 0, and the interval's data
// after the RST that ended the last. The bits left before that RST are
// passed over: padding, or the rest of an interval whose data ended early.
// Data after the last interval's last MCU is skipped to the next marker
// ("Restart marker not detected"). The RST found is taken by its number, as
// libjpeg's jpeg_resync_to_restart() takes it, with the one wanted (the
// interval's, counting from RST0 at the scan's start):
// - The one wanted: passed over, and the interval decoded after it.
// - One of the next two: the RSTs before it were lost, with the data of
//   their intervals. It is left for a later interval, and this interval
//   isn't decoded (its MCUs are left grey).
// - One of the two before: skipped, with the data after it, to the next
//   marker, which is taken likewise.
// - Any other: taken for the one wanted.
// - A marker that isn't an RST, or the end of the file: no MCU from here on
//   is decoded.
// Each is reported up to the scan's error limit, on a count of its own.
//
// INPUT:
// - nMcuX                      = The interval's first MCU (x)
// - nMcuY                      = The interval's first MCU (y)
// PRE:
// - m_nRestartWantInd
// - m_bRestartLost
// - m_bRestartNoMore
// - m_bRestartRead
// - m_nRestartLastInd
// POST:
// - m_nRestartWantInd
// - m_nRestartMcusLeft
// - m_bRestartLost
// - m_bRestartNoMore
// - m_nWarnRstSyncNum
//
void CimgDecode::DecodeRestartNext(uint32_t nMcuX, uint32_t nMcuY)
{
  const uint64_t nMcu = static_cast<uint64_t>(nMcuY) * static_cast<uint64_t>(qMax(m_nMcuXMax, 1)) + nMcuX;
  const uint32_t nWant = m_nRestartWantInd;

  m_nRestartWantInd = (nWant + 1) % 8;
  DecodeRestartDcState();
  m_nRestartMcusLeft = static_cast<uint32_t>(m_nRestartInterval);

  // Nothing is read past a marker that isn't an RST, or the end of the file
  if(m_bRestartNoMore)
  {
    m_bRestartLost = true;
    return;
  }

  // The rest of an interval that ended early is passed over with its RST,
  // which the scan buffer stopped at. Otherwise the last interval's data
  // should end with padding (under a byte) before its RST.
  const bool bWasLost = m_bRestartLost;

  if(!bWasLost)
  {
    BuffTopup();
  }

  // The marker the scan buffer stopped at (or the one found below)
  uint32_t nMarker = JFIF_RST0 + m_nRestartLastInd;
  uint32_t nMarkerPos = m_nScanBuffPtr;

  // Data after the last interval's last MCU: a byte or more before its RST,
  // or no RST within the buffer's reach. It is searched from the byte that
  // holds the next bit, as the buffer may have read on past a marker that
  // isn't an RST, which ends the scan's data here as further on: a data byte
  // partly used, or a stuffed 0xFF, is passed over, and a marker's 0xFF that
  // the last MCU took some of the bits of (as data) is found. (Searched from
  // the byte after it, that 0xFF was missed, and at the end of a file of
  // 4 GiB - 1 bytes the position wrapped around to 0.) One whose 0xFF the
  // MCU took whole is passed, as inside an interval's data.
  if(!m_bRestartRead || (!bWasLost && (m_nScanBuff_vacant <= 32 - 8)))
  {
    const uint32_t nFromPos = m_anScanBuffPtr_pos[0];
    const uint32_t nFromAlign = m_nScanBuffPtr_align;
    const uint32_t nFrom = (m_nScanBuff_vacant >= 32) ? m_nScanBuffPtr : nFromPos;

    nMarker = RestartSkipToMarker(nFrom, nMarkerPos);

    if(!bWasLost && RestartSyncReportable())
    {
      m_pLog->AddLine(QString("  Expect Restart interval elapsed @ %1").arg(GetScanBufPos(nFromPos, nFromAlign)));
      ReportRestartSync(QString("    ERROR: Restart marker not detected: data skipped to %1")
                          .arg(RestartMarkerStr(nMarker, nMarkerPos)));
    }
  }

  m_bRestartLost = false;

  for(;;)
  {
    // A marker that isn't an RST, or the end of the file: libjpeg reads
    // nothing past it, and decodes no more MCUs
    if(!m_bRestartRead)
    {
      if(RestartSyncReportable())
      {
        ReportRestartSync(QString("*** ERROR: No RST found before MCU(%1,%2), but %3: no MCU from there on decoded")
                            .arg(nMcuX)
                            .arg(nMcuY)
                            .arg(RestartMarkerStr(nMarker, nMarkerPos)));
      }

      m_bRestartNoMore = true;
      m_bRestartLost = true;
      break;
    }

    // How far after the one wanted it is (0..7)
    const uint32_t nAhead = (m_nRestartLastInd + 8 - nWant) % 8;

    if(nAhead == 0)
    {
      RestartPassMarker();
      break;
    }

    // The start of its message (made only while they are reported)
    const bool bReport = RestartSyncReportable();
    const QString strFound = !bReport ? QString() :
                             QString("*** ERROR: Expected RST%1 before MCU(%2,%3), found RST%4 @ 0x%5.0")
                               .arg(nWant)
                               .arg(nMcuX)
                               .arg(nMcuY)
                               .arg(m_nRestartLastInd)
                               .arg(m_nScanBuffPtr, 8, 16, QChar('0'));

    if((nAhead == 1) || (nAhead == 2))
    {
      if(bReport)
      {
        ReportRestartSync(strFound + QString(": taken as a later one, so %1 not decoded")
                                       .arg(RestartMcuRange(nMcu, static_cast<uint64_t>(m_nRestartInterval))));
      }

      m_bRestartLost = true;
      break;
    }

    if((nAhead == 6) || (nAhead == 7))
    {
      RestartPassMarker();
      nMarker = RestartSkipToMarker(m_nScanBuffPtr, nMarkerPos);

      if(bReport)
      {
        ReportRestartSync(strFound + QString(", an earlier one: skipped to %1").arg(RestartMarkerStr(nMarker, nMarkerPos)));
      }

      continue;
    }

    if(bReport)
    {
      ReportRestartSync(strFound + ": taken for it");
    }

    RestartPassMarker();
    break;
  }
}

// File position where an MCU's scan data starts, formatted as GetScanBufPos()
// - For notes made after the scan has been decoded, when the scan reader's
//   own position is the end of the scan
//
// INPUT:
// - nMcuX                      = MCU coordinate (x)
// - nMcuY                      = MCU coordinate (y)
// RETURN:
// - Formatted string
//
QString CimgDecode::GetMcuFilePos(uint32_t nMcuX, uint32_t nMcuY)
{
  uint32_t nByte = 0;
  uint32_t nBit = 0;

  LookupFilePosMcu(QPoint(static_cast<int>(nMcuX), static_cast<int>(nMcuY)), nByte, nBit);
  return GetScanBufPos(nByte, nBit);
}

// Test the scan error flag and, if set, report out the position
//
// INPUT:
// - nMcuX                              = MCU x coordinate
// - nMcuY                              = MCU y coordinate
// - nCssIndH                   = Chroma subsampling (horizontal)
// - nCssIndV                   = Chroma subsampling (vertical)
// - nComp                              = Image component
//
void CimgDecode::CheckScanErrors(uint32_t nMcuX, uint32_t nMcuY, uint32_t nCssIndH, uint32_t nCssIndV, uint32_t nComp)
{
  //uint32_t mcu_x_max = (m_nDimX/m_nMcuWidth);
  //uint32_t mcu_y_max = (m_nDimY/m_nMcuHeight);

  // Determine pixel position, taking into account sampling quadrant as well
  // (and the block's expansion factor, as in SetFullRes())
  const uint32_t nExpandH = (nComp < MAX_SOF_COMP_NF) ? static_cast<uint32_t>(m_anExpandBitsMcuH[nComp]) : 1;
  const uint32_t nExpandV = (nComp < MAX_SOF_COMP_NF) ? static_cast<uint32_t>(m_anExpandBitsMcuV[nComp]) : 1;

  uint32_t err_pos_x = m_nMcuWidth * nMcuX + nCssIndH * BLK_SZ_X * nExpandH;

  uint32_t err_pos_y = m_nMcuHeight * nMcuY + nCssIndV * BLK_SZ_Y * nExpandV;

  if(m_nScanCurErr)
  {
    QString strTmp, errStr;

    // Report component and subsampling quadrant
    switch (nComp)
    {
      case SCAN_COMP_Y:
        strTmp = QString("Lum CSS(%1,%2)")
            .arg(nCssIndH)
            .arg(nCssIndV);
        break;

      case SCAN_COMP_CB:
        strTmp = QString("Chr(Cb) CSS(%1,%2)")
            .arg(nCssIndH)
            .arg(nCssIndV);
        break;

      case SCAN_COMP_CR:
        strTmp = QString("Chr(Cr) CSS(%1,%2)")
            .arg(nCssIndH)
            .arg(nCssIndV);
        break;

      default:
        // Unknown component
        strTmp = QString("??? CSS(%1,%2)")
            .arg(nCssIndH)
            .arg(nCssIndV);
        break;
    }

    if(m_nWarnBadScanNum < m_nScanErrMax)
    {
      errStr = QString("*** ERROR: Bad scan data in MCU(%1,%2): %3 @ Offset %4")
          .arg(nMcuX)
          .arg(nMcuY)
          .arg(strTmp)
          .arg(GetScanBufPos());
      m_pLog->AddLineErr(errStr);
      errStr = QString("           MCU located at pixel=(%1, %2)")
          .arg(err_pos_x)
          .arg(err_pos_y);
      m_pLog->AddLineErr(errStr);

      //errStr = QString("*** Resetting Error state to continue ***");
      //m_pLog->AddLineErr(errStr);

      m_nWarnBadScanNum++;

      if(m_nWarnBadScanNum >= m_nScanErrMax)
      {
        strTmp = QString("    Only reported first %1 instances of this message...").arg(m_nScanErrMax);
        m_pLog->AddLineErr(strTmp);
      }
    }

    // TODO: Should we reset m_nScanCurErr?
    m_nScanCurErr = false;

    //errStr = QString("*** Resetting Error state to continue ***");
    //m_pLog->AddLineErr(errStr);
  }                             // Error?
}

// Report the cumulative DC value
//
// INPUT:
// - nMcuX                              = MCU x coordinate
// - nMcuY                              = MCU y coordinate
// - nVal                               = DC value
//
void CimgDecode::PrintDcCumVal(uint32_t, uint32_t, int32_t nVal)
{
  QString strTmp;

/*  strTmp = QString("  MCU [%1,%2] DC Cumulative Val = [%3]")
      .arg(nMcuX, 4)
      .arg(nMcuY, 4)
      .arg(nVal, 5); */
  strTmp = QString("                 Cumulative DC Val=[%1]").arg(nVal, 5);
  m_pLog->AddLine(strTmp);
}

// Reset the DC values in the decoder (e.g. at start and
// after restart markers)
//
// POST:
// - m_nDcLum
// - m_nDcChrCb
// - m_nDcChrCr
// - m_anDcLumCss[]
// - m_anDcChrCbCss[]
// - m_anDcChrCrCss[]
//
void CimgDecode::DecodeRestartDcState()
{
  m_nDcLum = 0;
  m_nDcChrCb = 0;
  m_nDcChrCr = 0;
  
  for(uint32_t nInd = 0; nInd < MAX_SAMP_FACT_V * MAX_SAMP_FACT_H; nInd++)
  {
    m_anDcLumCss[nInd] = 0;
    m_anDcChrCbCss[nInd] = 0;
    m_anDcChrCrCss[nInd] = 0;
  }
}

//TODO
void CimgDecode::SetImageDimensions(uint32_t nWidth, uint32_t nHeight)
{
  m_rectImgBase = QRect(QPoint(0, 0), QSize(nWidth, nHeight));
}

// Process the entire scan segment and optionally render the image
// - Reset and clear the output structures
// - Loop through each MCU and read each component
// - Maintain running DC level accumulator
// - Call SetFullRes() to transfer IDCT output to YCC Pixel Map
//
// INPUT:
// - nStart                                     = File position at start of scan
// - bDisplay                           = Generate a preview image?
// - bQuiet                                     = Disable output of certain messages during decode?
//
void CimgDecode::DecodeScanImg(uint32_t nStart, bool bDisplay, bool bQuiet)
{
  qDebug() << "CimgDecode::DecodeScanImg Start";

  QString strTmp;

  bool bDieOnFirstErr = false;  // FIXME: do we want this? It makes it less useful for corrupt jpegs

  // Fetch configuration values locally
  bool bDumpHistoY = m_pAppConfig->displayYHistogram();
  bool bDecodeScanAc;

  int32_t nScanErrMax = m_pAppConfig->maxDecodeError();

  // Add some extra speed-up in hidden mode (we don't need AC)
  if(bDisplay)
  {
    bDecodeScanAc = m_pAppConfig->decodeAc();
  }
  else
  {
    bDecodeScanAc = false;
  }
  
  m_bHistEn = m_pAppConfig->displayRgbHistogram();
  m_bStatClipEn = m_pAppConfig->clipStats();

  int32_t nPixMapW = 0;
  int32_t nPixMapH = 0;

  // Reset the decoder state variables
  Reset();

  m_nScanErrMax = nScanErrMax;
  m_bDecodeScanAc = bDecodeScanAc;

  // Detect the scenario where the image component details haven't been set yet
  // The image details are set via SetImageDetails()
  if(!m_bImgDetailsSet)
  {
    m_pLog->AddLineErr("*** ERROR: Decoding image before Image components defined ***");
    return;
  }

  // Even though we support decoding of MAX_SOS_COMP_NS we limit the component flexibility further
  if((m_nNumSosComps != NUM_CHAN_GRAYSCALE) && (m_nNumSosComps != NUM_CHAN_YCC))
  {
    strTmp = QString("  NOTE: Number of SOS components not supported [%1]").arg(m_nNumSosComps);
    m_pLog->AddLineWarn(strTmp);
#ifndef DEBUG_YCCK
    return;
#endif
  }

  // Determine the maximum sampling factor and min sampling factor for this scan
  m_nSosSampFactHMax = 0;
  m_nSosSampFactVMax = 0;
  m_nSosSampFactHMin = 0xFF;
  m_nSosSampFactVMin = 0xFF;

  for(uint32_t nComp = 1; nComp <= m_nNumSosComps; nComp++)
  {
    m_nSosSampFactHMax = qMax(m_nSosSampFactHMax, m_anSofSampFactH[nComp]);
    m_nSosSampFactVMax = qMax(m_nSosSampFactVMax, m_anSofSampFactV[nComp]);
    m_nSosSampFactHMin = qMin(m_nSosSampFactHMin, m_anSofSampFactH[nComp]);
    m_nSosSampFactVMin = qMin(m_nSosSampFactVMin, m_anSofSampFactV[nComp]);
    Q_ASSERT(m_nSosSampFactHMin != 0);
    Q_ASSERT(m_nSosSampFactVMin != 0);
  }

  // ITU-T.81 clause A.2.2 "Non-interleaved order (Ns=1)"
  // - In some cases an image may have a single component in a scan but with sampling factors other than 1:
  //     Number of Img components = 1
  //       Component[1]: ID=0x01, Samp Fac=0x22 (Subsamp 1 x 1), Quant Tbl Sel=0x00 (Lum: Y)
  // - This could either be in a 3-component SOF with multiple 1-component SOS or a 1-component SOF (monochrome image)
  // - In general, grayscale images exhibit a sampling factor of 0x11
  // - Per ITU-T.81 A.2.2:
  //     When Ns = 1 (where Ns is the number of components in a scan), the order of data units
  //     within a scan shall be left-to-right and top-to-bottom, as shown in Figure A.2. This
  //     ordering applies whenever Ns = 1, regardless of the values of H1 and V1.
  // - Thus, instead of the usual decoding sequence for 0x22:
  //   [ 0 1 ] [ 4 5 ]
  //   [ 2 3 ] [ 6 7 ]
  // - The sequence for decode should be:
  //   [ 0 ] [ 1 ] [ 2 ] [ 3 ] [ 4 ] ...
  // - Which is equivalent to the non-subsampled ordering (ie. 0x11)
  // - Apply a correction for such images to remove the sampling factor
  if(m_nNumSosComps == 1)
  {
    // TODO: Need to confirm if component index needs to be looked up
    // in the case of multiple SOS or if [1] is the correct index
    if((m_anSofSampFactH[1] != 1) || (m_anSofSampFactV[1] != 1))
    {
      m_pLog->AddLineWarn("    Altering sampling factor for single component scan to 0x11");
    }

    m_anSofSampFactH[1] = 1;
    m_anSofSampFactV[1] = 1;
    m_nSosSampFactHMax = 1;
    m_nSosSampFactVMax = 1;
    m_nSosSampFactHMin = 1;
    m_nSosSampFactVMin = 1;
  }

  // Perform additional range checks
  // (A zero minimum would divide by zero below, e.g. an SOS listing more
  // components than the SOF defined.)
  if((m_nSosSampFactHMin == 0) || (m_nSosSampFactVMin == 0) ||
     (m_nSosSampFactHMax == 0) || (m_nSosSampFactVMax == 0) || (m_nSosSampFactHMax > MAX_SAMP_FACT_H)
     || (m_nSosSampFactVMax > MAX_SAMP_FACT_V))
  {
    strTmp = QString("  NOTE: Degree of subsampling factor not supported [HMax=%1, VMax=%2]")
        .arg(m_nSosSampFactHMax)
        .arg(m_nSosSampFactVMax);
    m_pLog->AddLineWarn(strTmp);
    return;
  }

  // Calculate the MCU size for this scan. We do it here rather
  // than at the time of SOF (ie. SetImageDetails) for the reason
  // that under some circumstances we need to override the sampling
  // factor in single-component scans. This is done earlier.
  m_nMcuWidth = m_nSosSampFactHMax * BLK_SZ_X;
  m_nMcuHeight = m_nSosSampFactVMax * BLK_SZ_Y;

  // Calculate the number of bits to replicate when we generate the pixel map
  for(uint32_t nComp = 1; nComp <= m_nNumSosComps; nComp++)
  {
    m_anExpandBitsMcuH[nComp] = m_nSosSampFactHMax / m_anSofSampFactH[nComp];
    m_anExpandBitsMcuV[nComp] = m_nSosSampFactVMax / m_anSofSampFactV[nComp];
  }

  // Calculate the number of component samples per MCU
  for(uint32_t nComp = 1; nComp <= m_nNumSosComps; nComp++)
  {
    m_anSampPerMcuH[nComp] = m_anSofSampFactH[nComp];
    m_anSampPerMcuV[nComp] = m_anSofSampFactV[nComp];
  }

  // Determine the MCU ranges
  m_nMcuXMax = (m_nDimX / m_nMcuWidth);
  m_nMcuYMax = (m_nDimY / m_nMcuHeight);

  m_nImgSizeXPartMcu = m_nMcuXMax * m_nMcuWidth;
  m_nImgSizeYPartMcu = m_nMcuYMax * m_nMcuHeight;

  // Detect incomplete (partial) MCUs and round-up the MCU ranges if necessary.
  if((m_nDimX % m_nMcuWidth) != 0)
  {
    m_nMcuXMax++;
  }

  if((m_nDimY % m_nMcuHeight) != 0)
  {
    m_nMcuYMax++;
  }

  // Save the maximum 8x8 block dimensions
  m_nBlkXMax = m_nMcuXMax * m_nSosSampFactHMax;
  m_nBlkYMax = m_nMcuYMax * m_nSosSampFactVMax;

  // Ensure the image has a size
  if((m_nBlkXMax == 0) || (m_nBlkYMax == 0))
  {
    return;
  }

  // Refuse images whose pixel maps would be unreasonably large. This also keeps
  // the int32_t sizes and indices used by the pixel maps (width * height) in
  // range: a 65535x65535 SOF would otherwise wrap them and overflow the heap.
  static const quint64 nMaxDecodePixels = 1ull << 28;   // 16384 x 16384
  const quint64 nPixels = static_cast<quint64>(m_nMcuXMax) * static_cast<quint64>(m_nMcuWidth)
                          * static_cast<quint64>(m_nMcuYMax) * static_cast<quint64>(m_nMcuHeight);

  if(nPixels > nMaxDecodePixels)
  {
    strTmp = QString("*** ERROR: Image too large to decode the scan (%1 x %2 pixels); skipping image decode ***")
        .arg(m_nDimX).arg(m_nDimY);
    m_pLog->AddLineErr(strTmp);
    m_nMcuXMax = 0;
    m_nMcuYMax = 0;
    m_nBlkXMax = 0;
    m_nBlkYMax = 0;
    return;
  }

  // Set the decoded size and before scaling
  m_nImgSizeX = m_nMcuXMax * m_nMcuWidth;
  m_nImgSizeY = m_nMcuYMax * m_nMcuHeight;
  qDebug() << QString("CimgDecode::DecodeScanImg ImgSizeX=%1 ImgSizeY=%2").arg(m_nImgSizeX).arg(m_nImgSizeY);
  m_rectImgBase = QRect(QPoint(0, 0), QSize(m_nImgSizeX, m_nImgSizeY));

  // Determine decoding range
  int32_t nDecMcuRowStart;
  int32_t nDecMcuRowEnd;       // End to AC scan decoding
  int32_t nDecMcuRowEndFinal;  // End to general decoding

  nDecMcuRowStart = 0;
  nDecMcuRowEnd = m_nMcuYMax;
  nDecMcuRowEndFinal = m_nMcuYMax;

  // Limit the decoding range to valid range
  nDecMcuRowEnd = qMin(nDecMcuRowEnd, m_nMcuYMax);
  nDecMcuRowEndFinal = qMin(nDecMcuRowEndFinal, m_nMcuYMax);

  // Allocate the MCU File Map
  Q_ASSERT(m_pMcuFileMap == 0);
  m_pMcuFileMap = new uint64_t[m_nMcuYMax * m_nMcuXMax];

  if(!m_pMcuFileMap)
  {
    strTmp = "ERROR: Not enough memory for Image Decoder MCU File Pos Map";
    m_pLog->AddLineErr(strTmp);

    if(m_pAppConfig->interactive())
    {
      msgBox.setText(strTmp);
      msgBox.exec();
    }

    return;
  }
  
  // An MCU the decode doesn't reach (after the scan stopped in its row) has
  // no position. (They were 0, which looked like the start of the file.)
  std::fill_n(m_pMcuFileMap, m_nMcuYMax * m_nMcuXMax, kMcuNotDecoded);

  // Allocate the 8x8 Block DC Map
  m_pBlkDcValY = new int16_t[m_nBlkYMax * m_nBlkXMax];

  if((!m_pBlkDcValY))
  {
    strTmp = "ERROR: Not enough memory for Image Decoder Blk DC Value Map";
    m_pLog->AddLineErr(strTmp);

    if(m_pAppConfig->interactive())
    {
      msgBox.setText(strTmp);
      msgBox.exec();
    }

    return;
  }

  if(m_nNumSosComps == NUM_CHAN_YCC)
  {
    m_pBlkDcValCb = new int16_t[m_nBlkYMax * m_nBlkXMax];
    m_pBlkDcValCr = new int16_t[m_nBlkYMax * m_nBlkXMax];

    if((!m_pBlkDcValCb) || (!m_pBlkDcValCr))
    {
      strTmp = "ERROR: Not enough memory for Image Decoder Blk DC Value Map";
      m_pLog->AddLineErr(strTmp);

      if(m_pAppConfig->interactive())
      {
        msgBox.setText(strTmp);
        msgBox.exec();
      }

      return;
    }
  }

  memset(m_pBlkDcValY, 0, (m_nBlkYMax * m_nBlkXMax * sizeof(int16_t)));

  if(m_nNumSosComps == NUM_CHAN_YCC)
  {
    memset(m_pBlkDcValCb, 0, (m_nBlkYMax * m_nBlkXMax * sizeof(int16_t)));
    memset(m_pBlkDcValCr, 0, (m_nBlkYMax * m_nBlkXMax * sizeof(int16_t)));
  }

  // Allocate the real YCC pixel Map
  nPixMapH = m_nBlkYMax * BLK_SZ_Y;
  nPixMapW = m_nBlkXMax * BLK_SZ_X;

  // Ensure no image allocated yet
  Q_ASSERT(m_pPixValY == 0);

  if(m_nNumSosComps == NUM_CHAN_YCC)
  {
    Q_ASSERT(m_pPixValCb == 0);
    Q_ASSERT(m_pPixValCr == 0);
  }

  // Allocate image (YCC)
  m_pPixValY = new int16_t[nPixMapW * nPixMapH];

  if((!m_pPixValY))
  {
    strTmp = "ERROR: Not enough memory for Image Decoder Pixel YCC Value Map";
    m_pLog->AddLineErr(strTmp);

    if(m_pAppConfig->interactive())
    {
      msgBox.setText(strTmp);
      msgBox.exec();
    }

    return;
  }

  if(m_nNumSosComps == NUM_CHAN_YCC)
  {
    m_pPixValCb = new int16_t[nPixMapW * nPixMapH];
    m_pPixValCr = new int16_t[nPixMapW * nPixMapH];

    if((!m_pPixValCb) || (!m_pPixValCr))
    {
      strTmp = "ERROR: Not enough memory for Image Decoder Pixel YCC Value Map";
      m_pLog->AddLineErr(strTmp);

      if(m_pAppConfig->interactive())
      {
        msgBox.setText(strTmp);
        msgBox.exec();
      }

      return;
    }
  }

  // Reset pixel map
  if(bDisplay)
  {
    ClrFullRes(nPixMapW, nPixMapH);
  }

  // If a previous bitmap was created, deallocate it and start fresh
  m_bDibTempReady = false;
  m_bPreviewIsJpeg = false;
  
  // -------------------------------------

  QString picStr;

  // Reset the DC cumulative state
  DecodeRestartDcState();

  // Reset the scan buffer
  DecodeRestartScanBuf(nStart, false);

  // Load the buffer
  m_pWBuf->BufLoadWindow(nStart);

  // Expect that we start with RST0
  m_nRestartExpectInd = 0;
  m_nRestartLastInd = 0;

  // And that the first restart interval is followed by RST0, and has data
  m_nRestartWantInd = 0;
  m_bRestartLost = false;
  m_bRestartNoMore = false;

  // Load the first data into the scan buffer
  // This is done so that the MCU map will already
  // have access to the data.
  BuffTopup();

  if(!bQuiet)
  {
    m_pLog->AddLineHdr("*** Decoding SCAN Data ***");
    strTmp = QString("  OFFSET: 0x%1").arg(nStart, 8, 16, QChar('0'));
    m_pLog->AddLine(strTmp);
  }

  // TODO: Might be more appropriate to check against m_nNumSosComps instead?
  if((m_nNumSofComps != NUM_CHAN_GRAYSCALE) && (m_nNumSofComps != NUM_CHAN_YCC))
  {
    strTmp = QString("  NOTE: Number of Image Components not supported [%1]").arg(m_nNumSofComps);
    m_pLog->AddLineWarn(strTmp);
#ifndef DEBUG_YCCK
    return;
#endif
  }

  // Check DQT tables
  // We need to ensure that the DQT Table selection has already
  // been done (via a call from JfifDec to SetDqtTables() ).
  uint32_t nDqtTblY = 0;
  uint32_t nDqtTblCr = 0;
  uint32_t nDqtTblCb = 0;

#ifdef DEBUG_YCCK
  uint32_t nDqtTblK = 0;
#endif
  bool bDqtReady = true;

  for(uint32_t ind = 1; ind <= m_nNumSosComps; ind++)
  {
    if(m_anDqtTblSel[ind] < 0)
    {
      bDqtReady = false;
    }
  }

  if(!bDqtReady)
  {
    m_pLog->AddLineErr("*** ERROR: Decoding image before DQT Table Selection via JFIF_SOF ***");
    // TODO: Is more error handling required?
    return;
  }
  else
  {
    // Each selected table must have been defined by a DQT. An undefined one
    // is all zeros and was used as it was, which zeroes every coefficient.
    for(int32_t ind = 1; ind <= m_nNumSosComps; ind++)
    {
      const int32_t nTbl = m_anDqtTblSel[ind];

      if((nTbl >= static_cast<int32_t>(MAX_DQT_DEST_ID)) || !m_abDqtDefined[nTbl])
      {
        m_pLog->AddLineErr(QString("*** ERROR: Component %1 uses quantization table %2, which isn't defined. "
                                   "Skipping image decode ***").arg(ind).arg(nTbl));
        return;
      }
    }

    // FIXME: Not sure that we can always depend on the indices to appear in this order.
    // May need another layer of indirection to get at the frame image component index.
    nDqtTblY = m_anDqtTblSel[DQT_DEST_Y];
    nDqtTblCb = m_anDqtTblSel[DQT_DEST_CB];
    nDqtTblCr = m_anDqtTblSel[DQT_DEST_CR];
#ifdef DEBUG_YCCK
    if(m_nNumSosComps == 4)
    {
      nDqtTblK = m_anDqtTblSel[DQT_DEST_K];
    }
#endif
  }

  // Now check DHT tables
  bool bDhtReady = true;

  uint32_t nDhtTblDcY, nDhtTblDcCb, nDhtTblDcCr;
  uint32_t nDhtTblAcY, nDhtTblAcCb, nDhtTblAcCr;

#ifdef DEBUG_YCCK
  uint32_t nDhtTblDcK, nDhtTblAcK;
#endif
  for(uint32_t nClass = DHT_CLASS_DC; nClass <= DHT_CLASS_AC; nClass++)
  {
    for(uint32_t nCompInd = 1; nCompInd <= m_nNumSosComps; nCompInd++)
    {
      if(m_anDhtTblSel[nClass][nCompInd] < 0)
      {
        bDhtReady = false;
      }
    }
  }

  // Ensure that the table has been defined already!
  // Only index the tables with a valid selector: an SOS with an out-of-range
  // selector leaves -1 here, which would read far outside the arrays.
  for(uint32_t nCompInd = 1; bDhtReady && (nCompInd <= m_nNumSosComps); nCompInd++)
  {
    for(uint32_t nClass = DHT_CLASS_DC; nClass <= DHT_CLASS_AC; nClass++)
    {
      const int32_t nSel = m_anDhtTblSel[nClass][nCompInd];

      if((nSel < 0) || (nSel >= MAX_DHT_DEST_ID) || (m_anDhtLookupSize[nClass][nSel] == 0))
      {
        bDhtReady = false;
      }
    }
  }

  if(!bDhtReady)
  {
    m_pLog->AddLineErr("*** ERROR: Decoding image before DHT Table Selection via JFIF_SOS ***");
    // TODO: Is more error handling required here?
    return;
  }
  else
  {
    // Define the huffman table indices that are selected for each
    // image component index and class (AC,DC).
    //
    // No need to check if a table is valid here since we have
    // previously checked to ensure that all required tables exist.
    // NOTE: If the table has not been defined, then the index
    // will be 0xFFFFFFFF. ReadScanVal() will trap this with Q_ASSERT
    // should it ever be used.
    nDhtTblDcY = m_anDhtTblSel[DHT_CLASS_DC][COMP_IND_YCC_Y];
    nDhtTblAcY = m_anDhtTblSel[DHT_CLASS_AC][COMP_IND_YCC_Y];
    nDhtTblDcCb = m_anDhtTblSel[DHT_CLASS_DC][COMP_IND_YCC_CB];
    nDhtTblAcCb = m_anDhtTblSel[DHT_CLASS_AC][COMP_IND_YCC_CB];
    nDhtTblDcCr = m_anDhtTblSel[DHT_CLASS_DC][COMP_IND_YCC_CR];
    nDhtTblAcCr = m_anDhtTblSel[DHT_CLASS_AC][COMP_IND_YCC_CR];
#ifdef DEBUG_YCCK
    nDhtTblDcK = m_anDhtTblSel[DHT_CLASS_DC][COMP_IND_YCC_K];
    nDhtTblAcK = m_anDhtTblSel[DHT_CLASS_AC][COMP_IND_YCC_K];
#endif
  }

  // Done checks

  // Inform if they are in AC+DC/DC mode
  if(!bQuiet)
  {
    if(m_bDecodeScanAc)
    {
      m_pLog->AddLine("  Scan Decode Mode: Full IDCT (AC + DC)");
    }
    else
    {
      m_pLog->AddLine("  Scan Decode Mode: No IDCT (DC only)");
      m_pLog-> AddLineWarn("    NOTE: Low-resolution DC component shown. Can decode full-res with [Options->Scan Segment->Full IDCT]");
    }

    // Options > Scan Segment > Detailed Decode stays on for every file
    // decoded, so say what it covers
    if(m_bDetailVlc)
    {
      if(DetailVlcInImage())
      {
        m_pLog->AddLine(QString("  Detailed Decode: %1 %2 from MCU [%3,%4]")
                          .arg(m_nDetailVlcLen)
                          .arg((m_nDetailVlcLen == 1) ? "MCU" : "MCUs")
                          .arg(m_nDetailVlcX)
                          .arg(m_nDetailVlcY));
      }
      else
      {
        m_pLog->AddLineWarn(QString("  NOTE: Detailed Decode starts at MCU [%1,%2], outside this image (%3 x %4 MCUs), "
                                    "so nothing is reported")
                              .arg(m_nDetailVlcX)
                              .arg(m_nDetailVlcY)
                              .arg(m_nMcuXMax)
                              .arg(m_nMcuYMax));
      }
    }

    m_pLog->AddLine("");
  }

  // Report any Buffer overlays
  m_pWBuf->ReportOverlays(m_pLog);

  m_nNumPixels = 0;

  // Clear the histogram and color correction clipping stats
  if(bDisplay)
  {
    memset(&m_sStatClip, 0, sizeof(m_sStatClip));
    ResetHistoStats(m_sHisto);

    // FIXME: Histo should now be done after color convert
    memset(&m_anCcHisto_r, 0, sizeof(m_anCcHisto_r));
    memset(&m_anCcHisto_g, 0, sizeof(m_anCcHisto_g));
    memset(&m_anCcHisto_b, 0, sizeof(m_anCcHisto_b));

    memset(&m_anHistoYFull, 0, sizeof(m_anHistoYFull));
    memset(&m_anHistoYSubset, 0, sizeof(m_anHistoYSubset));
  }

  // -----------------------------------------------------------------------
  // Process all scan MCUs
  // -----------------------------------------------------------------------

  for(uint32_t nMcuY = nDecMcuRowStart; nMcuY < nDecMcuRowEndFinal; nMcuY++)
  {
    // Set the statusbar text to Processing...
    strTmp = QString("Decoding Scan Data... Row %1 of %2 (%3%)")
        .arg(nMcuY, 4, 10, QChar('0'))
        .arg(m_nMcuYMax, 4, 10, QChar('0'))
        .arg(nMcuY * 100.0 / m_nMcuYMax, 3, 'f', 0);
    SetStatusText(strTmp);

    // TODO: Trap escape keypress here (or run as thread)

    bool bDscRet;               // Return value for DecodeScanComp()
    bool bScanStop = false;

    for(uint32_t nMcuX = 0; (nMcuX < m_nMcuXMax) && (!bScanStop); nMcuX++)
    {
      // Each restart interval after the first starts after the RST that
      // ends the one before, at the MCU the restart interval sets
      // (DecodeRestartNext())
      if((m_bRestartEn) && (m_nRestartMcusLeft == 0))
      {
        DecodeRestartNext(nMcuX, nMcuY);
      }

      // To support a fast decode mode, allow for a subset of the
      // image to have DC+AC decoding, while the remainder is only DC decoding
      if((nMcuY < nDecMcuRowStart) || (nMcuY > nDecMcuRowEnd))
      {
        m_bDecodeScanAc = false;
      }
      else
      {
        m_bDecodeScanAc = bDecodeScanAc;
      }

      // Precalculate MCU matrix index
      uint32_t nMcuXY = nMcuY * m_nMcuXMax + nMcuX;

      // Mark the start of the MCU in the file map: where DecodeScanComp()
      // reads its first code, after the restart marker before it if there is
      // one. (It was marked here, before the marker: the position of the
      // previous MCU's padding bits.)
      m_nMcuPosPending = static_cast<int64_t>(nMcuXY);

      // Is this an MCU that we want full printing of decode process?
      // (In 64 bits: the range wrapped around, so a large Y or length chose
      // the wrong MCUs. A start beyond the right edge isn't in the image.)
      bool bVlcDump = false;

      if(m_bDetailVlc && DetailVlcInImage())
      {
        const uint64_t nRangeBase = static_cast<uint64_t>(m_nDetailVlcY) * static_cast<uint32_t>(m_nMcuXMax) +
                                    m_nDetailVlcX;
        const uint64_t nRangeCur = nMcuXY;

        if((nRangeCur >= nRangeBase) && (nRangeCur < nRangeBase + m_nDetailVlcLen))
        {
          bVlcDump = true;
        }
      }

      // Luminance
      // If there is chroma subsampling, then this block will have (css_x * css_y) luminance blocks to process
      // We store them all in an array m_anDcLumCss[]

      // Give separator line between MCUs
      if(bVlcDump)
      {
        m_pLog->AddLine("");
      }

      // CSS array indices
      uint32_t nCssIndH;
      uint32_t nCssIndV;
      uint32_t nComp;

      // No need to reset the IDCT output matrix for this MCU
      // since we are going to be generating it here. This help
      // maintain performance.

      // --------------------------------------------------------------
      nComp = SCAN_COMP_Y;

      // Step through the sampling factors per image component
      // TODO: Could rewrite this to use single loop across each image component
      for(nCssIndV = 0; nCssIndV < m_anSampPerMcuV[nComp]; nCssIndV++)
      {
        for(nCssIndH = 0; nCssIndH < m_anSampPerMcuH[nComp]; nCssIndH++)
        {
          if(!bVlcDump)
          {
            bDscRet = DecodeScanComp(nDhtTblDcY, nDhtTblAcY, nDqtTblY, nMcuX, nMcuY);   // Lum DC+AC
          }
          else
          {
            bDscRet = DecodeScanCompPrint(nDhtTblDcY, nDhtTblAcY, nDqtTblY, nMcuX, nMcuY, SCAN_COMP_Y);      // Lum DC+AC
          }
          
          if(m_nScanCurErr)
          {
            CheckScanErrors(nMcuX, nMcuY, nCssIndH, nCssIndV, nComp);
          }
          
          if(!bDscRet && bDieOnFirstErr)
          {
            return;
          }

          // The DCT Block matrix has already been dezigzagged
          // and multiplied against quantization table entry
          m_nDcLum += m_anDctBlock[DCT_COEFF_DC];

          if(bVlcDump)
          {
//                                              PrintDcCumVal(nMcuX,nMcuY,m_nDcLum);
          }

          // Now take a snapshot of the current cumulative DC value
          m_anDcLumCss[nCssIndV * MAX_SAMP_FACT_H + nCssIndH] = m_nDcLum;

          // At this point we have one of the luminance comps
          // fully decoded (with IDCT if enabled). The result is
          // currently in the array: m_afIdctBlock[]
          // The next step would be to move these elements into
          // the 3-channel MCU image map

          // Store the pixels associated with this channel into
          // the full-res pixel map. IDCT has already been computed
          // on the 8x8 (or larger) MCU block region.

#if 1
          if(bDisplay)
          {
            SetFullRes(nMcuX, nMcuY, nComp, nCssIndH, nCssIndV, m_nDcLum);
          }
#else
          // FIXME
          // Temporarily experiment with trying to handle multiple scans
          // by converting sampling factor of luminance scan back to 1x1
          uint32_t nNewMcuX, nNewMcuY, nNewCssX, nNewCssY;

          if(nCssIndV == 0)
          {
            if(nMcuX < m_nMcuXMax / 2)
            {
              nNewMcuX = nMcuX;
              nNewMcuY = nMcuY;
              nNewCssY = 0;
            }
            else
            {
              nNewMcuX = nMcuX - (m_nMcuXMax / 2);
              nNewMcuY = nMcuY;
              nNewCssY = 1;
            }
            nNewCssX = nCssIndH;
            SetFullRes(nNewMcuX, nNewMcuY, SCAN_COMP_Y, nNewCssX, nNewCssY, m_nDcLum);
          }
          else
          {
            nNewMcuX = (nMcuX / 2) + 1;
            nNewMcuY = (nMcuY / 2);
            nNewCssX = nCssIndH;
            nNewCssY = nMcuY % 2;
          }
#endif

          // ---------------

          // TODO: Counting pixels makes assumption that luminance is
          // not subsampled, so we increment by 64.
          m_nNumPixels += BLK_SZ_X * BLK_SZ_Y;
        }
      }

      // In a grayscale image, we don't do this part!
      //if (m_nNumSofComps == NUM_CHAN_YCC) {
      if(m_nNumSosComps == NUM_CHAN_YCC)
      {
        // --------------------------------------------------------------
        nComp = SCAN_COMP_CB;

        // Chrominance Cb
        for(nCssIndV = 0; nCssIndV < m_anSampPerMcuV[nComp]; nCssIndV++)
        {
          for(nCssIndH = 0; nCssIndH < m_anSampPerMcuH[nComp]; nCssIndH++)
          {
            if(!bVlcDump)
            {
              bDscRet = DecodeScanComp(nDhtTblDcCb, nDhtTblAcCb, nDqtTblCb, nMcuX, nMcuY);      // Chr Cb DC+AC
            }
            else
            {
              bDscRet = DecodeScanCompPrint(nDhtTblDcCb, nDhtTblAcCb, nDqtTblCb, nMcuX, nMcuY, SCAN_COMP_CB); // Chr Cb DC+AC
            }
            
            if(m_nScanCurErr)
            {
              CheckScanErrors(nMcuX, nMcuY, nCssIndH, nCssIndV, nComp);
            }
            
            if(!bDscRet && bDieOnFirstErr)
            {
              return;
            }

            m_nDcChrCb += m_anDctBlock[DCT_COEFF_DC];

            if(bVlcDump)
            {
              //PrintDcCumVal(nMcuX,nMcuY,m_nDcChrCb);
            }

            // Now take a snapshot of the current cumulative DC value
            m_anDcChrCbCss[nCssIndV * MAX_SAMP_FACT_H + nCssIndH] = m_nDcChrCb;

            // Store fullres value
            if(bDisplay)
            {
              SetFullRes(nMcuX, nMcuY, nComp, nCssIndH, nCssIndV, m_nDcChrCb);
            }
          }
        }

        // --------------------------------------------------------------
        nComp = SCAN_COMP_CR;

        // Chrominance Cr
        for(nCssIndV = 0; nCssIndV < m_anSampPerMcuV[nComp]; nCssIndV++)
        {
          for(nCssIndH = 0; nCssIndH < m_anSampPerMcuH[nComp]; nCssIndH++)
          {
            if(!bVlcDump)
            {
              bDscRet = DecodeScanComp(nDhtTblDcCr, nDhtTblAcCr, nDqtTblCr, nMcuX, nMcuY);      // Chr Cr DC+AC
            }
            else
            {
              bDscRet = DecodeScanCompPrint(nDhtTblDcCr, nDhtTblAcCr, nDqtTblCr, nMcuX, nMcuY, SCAN_COMP_CR); // Chr Cr DC+AC
            }
            
            if(m_nScanCurErr)
            {
              CheckScanErrors(nMcuX, nMcuY, nCssIndH, nCssIndV, nComp);
            }
            
            if(!bDscRet && bDieOnFirstErr)
            {
             return;
            }

            m_nDcChrCr += m_anDctBlock[DCT_COEFF_DC];

            if(bVlcDump)
            {
              //PrintDcCumVal(nMcuX,nMcuY,m_nDcChrCr);
            }

            // Now take a snapshot of the current cumulative DC value
            m_anDcChrCrCss[nCssIndV * MAX_SAMP_FACT_H + nCssIndH] = m_nDcChrCr;

            // Store fullres value
            if(bDisplay)
            {
              SetFullRes(nMcuX, nMcuY, nComp, nCssIndH, nCssIndV, m_nDcChrCr);
            }
          }
        }
      }

#ifdef DEBUG_YCCK
      else if(m_nNumSosComps == NUM_CHAN_YCCK)
      {
        // --------------------------------------------------------------
        nComp = SCAN_COMP_CB;

        // Chrominance Cb
        for(nCssIndV = 0; nCssIndV < m_anSampPerMcuV[nComp]; nCssIndV++)
        {
          for(nCssIndH = 0; nCssIndH < m_anSampPerMcuH[nComp]; nCssIndH++)
          {
            if(!bVlcDump)
            {
              bDscRet = DecodeScanComp(nDhtTblDcCb, nDhtTblAcCb, nDqtTblCb, nMcuX, nMcuY);      // Chr Cb DC+AC
            }
            else
            {
              bDscRet = DecodeScanCompPrint(nDhtTblDcCb, nDhtTblAcCb, nDqtTblCb, nMcuX, nMcuY, SCAN_COMP_CB); // Chr Cb DC+AC
            }
            
            if(m_nScanCurErr)
            {
              CheckScanErrors(nMcuX, nMcuY, nCssIndH, nCssIndV, nComp);
            }
            
            if(!bDscRet && bDieOnFirstErr)
            {
              return;
            }

            m_nDcChrCb += m_anDctBlock[DCT_COEFF_DC];

            if(bVlcDump)
            {
              //PrintDcCumVal(nMcuX,nMcuY,m_nDcChrCb);
            }

            // Now take a snapshot of the current cumulative DC value
            m_anDcChrCbCss[nCssIndV * MAX_SAMP_FACT_H + nCssIndH] = m_nDcChrCb;

            // Store fullres value
            if(bDisplay)
            {
              SetFullRes(nMcuX, nMcuY, nComp, 0, 0, m_nDcChrCb);
            }
          }
        }

        // --------------------------------------------------------------
        nComp = SCAN_COMP_CR;

        // Chrominance Cr
        for(nCssIndV = 0; nCssIndV < m_anSampPerMcuV[nComp]; nCssIndV++)
        {
          for(nCssIndH = 0; nCssIndH < m_anSampPerMcuH[nComp]; nCssIndH++)
          {
            if(!bVlcDump)
            {
              bDscRet = DecodeScanComp(nDhtTblDcCr, nDhtTblAcCr, nDqtTblCr, nMcuX, nMcuY);      // Chr Cr DC+AC
            }
            else
            {
              bDscRet = DecodeScanCompPrint(nDhtTblDcCr, nDhtTblAcCr, nDqtTblCr, nMcuX, nMcuY, SCAN_COMP_CR); // Chr Cr DC+AC
            }
            
            if(m_nScanCurErr)
              CheckScanErrors(nMcuX, nMcuY, nCssIndH, nCssIndV, nComp);
            
            if(!bDscRet && bDieOnFirstErr)
              return;

            m_nDcChrCr += m_anDctBlock[DCT_COEFF_DC];

            if(bVlcDump)
            {
              //PrintDcCumVal(nMcuX,nMcuY,m_nDcChrCr);
            }

            // Now take a snapshot of the current cumulative DC value
            m_anDcChrCrCss[nCssIndV * MAX_SAMP_FACT_H + nCssIndH] = m_nDcChrCr;

            // Store fullres value
            if(bDisplay)
              SetFullRes(nMcuX, nMcuY, nComp, 0, 0, m_nDcChrCr);
          }
        }

        // --------------------------------------------------------------
        // IGNORED
        nComp = SCAN_COMP_K;

        // Black K
        for(nCssIndV = 0; nCssIndV < m_anSampPerMcuV[nComp]; nCssIndV++)
        {
          for(nCssIndH = 0; nCssIndH < m_anSampPerMcuH[nComp]; nCssIndH++)
          {

            if(!bVlcDump)
            {
              bDscRet = DecodeScanComp(nDhtTblDcK, nDhtTblAcK, nDqtTblK, nMcuX, nMcuY); // K DC+AC
            }
            else
            {
              bDscRet = DecodeScanCompPrint(nDhtTblDcK, nDhtTblAcK, nDqtTblK, nMcuX, nMcuY, SCAN_COMP_K);    // K DC+AC
            }
            
            if(m_nScanCurErr)
              CheckScanErrors(nMcuX, nMcuY, nCssIndH, nCssIndV, nComp);
            
            if(!bDscRet && bDieOnFirstErr)
              return;

/*
						m_nDcChrK += m_anDctBlock[DCT_COEFF_DC];

						if (bVlcDump) {
							//PrintDcCumVal(nMcuX,nMcuY,m_nDcChrCb);
						}

						// Now take a snapshot of the current cumulative DC value
						m_anDcChrKCss[nCssIndV*MAX_SAMP_FACT_H+nCssIndH] = m_nDcChrK;

						// Store fullres value
						if (bDisplay)
							SetFullRes(nMcuX,nMcuY,nComp,0,0,m_nDcChrK);
*/

          }
        }
      }
#endif
      // --------------------------------------------------------------------

      uint32_t nBlkXY;

      // Now save the DC YCC values (expanded per 8x8 block)
      // without ranging or translation into RGB.
      //
      // We enter this code once per MCU so we need to expand
      // out to cover all blocks in this MCU.

      // --------------------------------------------------------------
      nComp = SCAN_COMP_Y;

      // Calculate top-left corner of MCU in block map
      // and then linear offset into block map
      uint32_t nBlkCornerMcuX, nBlkCornerMcuY, nBlkCornerMcuLinear;

      nBlkCornerMcuX = nMcuX * m_anExpandBitsMcuH[nComp];
      nBlkCornerMcuY = nMcuY * m_anExpandBitsMcuV[nComp];
      nBlkCornerMcuLinear = (nBlkCornerMcuY * m_nBlkXMax) + nBlkCornerMcuX;

      // Now step through each block in the MCU per subsampling
      for(nCssIndV = 0; nCssIndV < m_anSampPerMcuV[nComp]; nCssIndV++)
      {
        for(nCssIndH = 0; nCssIndH < m_anSampPerMcuH[nComp]; nCssIndH++)
        {
          // Calculate upper-left Blk index
          // FIXME: According to code analysis the following write assignment
          // to m_pBlkDcValY[] can apparently exceed the buffer bounds (C6386).
          // I have not yet determined what scenario can lead to
          // this. So for now, add in specific clause to trap and avoid.
          nBlkXY = nBlkCornerMcuLinear + (nCssIndV * m_nBlkXMax) + nCssIndH;
          
          // FIXME: Temporarily catch any range issue
          if(nBlkXY >= m_nBlkXMax * m_nBlkYMax)
          {
#ifdef DEBUG_LOG
            QString strDebug;

            strTmp = QString("DecodeScanImg() with nBlkXY out of range. nBlkXY=[%1] m_nBlkXMax=[%2] m_nBlkYMax=[%3]")
                .arg(nBlkXY)
                .arg(m_nBlkXMax)
                .arg(m_nBlkYMax);
            strDebug = QString("## File=[%1] Block=[%2] Error=[%3]\n")
                .arg(m_pAppConfig->strCurFname, -100)
                .arg("ImgDecode", -10).arg(strTmp);
            qDebug() << strDebug;
#else
            Q_ASSERT(false);
#endif
          }
          else
          {
            m_pBlkDcValY[nBlkXY] = m_anDcLumCss[nCssIndV * MAX_SAMP_FACT_H + nCssIndH];
          }
        }
      }

      // Only process the chrominance if it is YCC
      if(m_nNumSosComps == NUM_CHAN_YCC)
      {
        // --------------------------------------------------------------
        nComp = SCAN_COMP_CB;

        for(nCssIndV = 0; nCssIndV < m_anSampPerMcuV[nComp]; nCssIndV++)
        {
          for(nCssIndH = 0; nCssIndH < m_anSampPerMcuH[nComp]; nCssIndH++)
          {
            // Calculate upper-left Blk index
            nBlkXY = (nMcuY * m_anExpandBitsMcuV[nComp] + nCssIndV) * m_nBlkXMax + (nMcuX * m_anExpandBitsMcuH[nComp] + nCssIndH);

            // FIXME: Temporarily catch any range issue
            if(nBlkXY >= m_nBlkXMax * m_nBlkYMax)
            {
#ifdef DEBUG_LOG
              QString strDebug;

              strTmp = QString("DecodeScanImg() with nBlkXY out of range. nBlkXY=[%1] m_nBlkXMax=[%2] m_nBlkYMax=[%3]").arg(nBlkXY).arg(m_nBlkXMax).arg(m_nBlkYMax);
              strDebug = QString("## File=[%1] Block=[%2] Error=[%3]\n").arg(m_pAppConfig->strCurFname, -100).arg("ImgDecode", -10).arg(strTmp);
              qDebug() << strDebug;
#else
              Q_ASSERT(false);
#endif
            }
            else
            {
              m_pBlkDcValCb[nBlkXY] = m_anDcChrCbCss[nCssIndV * MAX_SAMP_FACT_H + nCssIndH];
            }
          }
        }

        // --------------------------------------------------------------
        nComp = SCAN_COMP_CR;

        for(nCssIndV = 0; nCssIndV < m_anSampPerMcuV[nComp]; nCssIndV++)
        {
          for(nCssIndH = 0; nCssIndH < m_anSampPerMcuH[nComp]; nCssIndH++)
          {
            // Calculate upper-left Blk index
            nBlkXY = (nMcuY * m_anExpandBitsMcuV[nComp] + nCssIndV) * m_nBlkXMax + (nMcuX * m_anExpandBitsMcuH[nComp] + nCssIndH);

            // FIXME: Temporarily catch any range issue
            if(nBlkXY >= m_nBlkXMax * m_nBlkYMax)
            {
#ifdef DEBUG_LOG
              QString strDebug;

              strTmp = QString("DecodeScanImg() with nBlkXY out of range. nBlkXY=[%1] m_nBlkXMax=[%2] m_nBlkYMax=[%3]").arg(nBlkXY).arg(m_nBlkXMax).arg(m_nBlkYMax);
              strDebug = QString("## File=[%1] Block=[%2] Error=[%3]\n").arg(m_pAppConfig->strCurFname, -100).arg("ImgDecode", -10).arg(strTmp);
              qDebug() << strDebug;
#else
              Q_ASSERT(false);
#endif
            }
            else
            {
              m_pBlkDcValCr[nBlkXY] = m_anDcChrCrCss[nCssIndV * MAX_SAMP_FACT_H + nCssIndH];
            }
          }
        }
      }

      // Now that we finished an MCU, decrement the restart interval counter
      if(m_bRestartEn)
      {
        m_nRestartMcusLeft--;
      }

      // Check to see if we need to abort for some reason.
      // Note that only check m_bScanEnd if we have a failure.
      // m_bScanEnd is Q_ASSERTed during normal out-of-data when scan
      // segment ends with marker. We don't want to abort early
      // or else we'll not decode the last MCU or two!
      if(m_bScanEnd && m_bScanBad)
      {
        bScanStop = true;

        // With a DRI, no MCU after it is decoded, as after an interval with no
        // RST at its end (DecodeRestartNext()). (The rest of the row was
        // skipped without counting its MCUs, so the restart interval's count
        // was off after it, and the first MCU of each row after it was decoded
        // from zeros.)
        if(m_bRestartEn)
        {
          m_bRestartNoMore = true;
          m_bRestartLost = true;
        }
      }
    }                           // nMcuX
  }                             // nMcuY

  if(!bQuiet)
  {
    m_pLog->AddLine("");
  }

  // Set an indicator that we have completed an image decode
  m_bImgDecoded = true;

  // ---------------------------------------------------------

  // Now we can create the final preview. Since we have just finished
  // decoding a new image, we need to ensure that we invalidate
  // the temporary preview (multi-channel option). Done earlier
  // with PREVIEW_NONE
  if(bDisplay)
  {
    // Report the detailed MCU's RGB here, not again each time the preview
    // is recalculated (such as for View > Image Chan)
    m_bDetailRgbDump = m_bDetailVlc && (m_nDetailVlcLen > 0) && DetailVlcInImage();
    CalcChannelPreview();
    m_bDetailRgbDump = false;
  }

  // DIB is ready for display now
  if(bDisplay)
  {
    m_bDibTempReady = true;
    m_bPreviewIsJpeg = true;
  }

  // ------------------------------------
  // Report statistics

  if(!bQuiet)
  {
    // Report Compression stats
    // TODO: Should we use m_nNumSofComps?
    strTmp = QString("  Compression stats:");
    m_pLog->AddLine(strTmp);
    // Not in 32 bits: the image's bits passed 2^31 at 89.5 megapixels in
    // colour (the ratio went negative), and the scan's 2^32 at 512 MiB.
    // Doubles hold both exactly.
    const double dPixels = static_cast<double>(m_nDimX) * m_nDimY;
    const double dScanBits = static_cast<double>(m_anScanBuffPtr_pos[0] - m_nScanBuffPtr_first) * 8;

    // A scan with no data (its header ends the file) has no bits: the ratio
    // was "inf" (a division by zero)
    if(dScanBits > 0)
    {
      double nCompressionRatio = (dPixels * m_nNumSosComps * 8) / dScanBits;
      strTmp = QString("    Compression Ratio: %1:1").arg(nCompressionRatio, 5, 'f', 2);
    }
    else
    {
      strTmp = QString("    Compression Ratio: - (no scan data)");
    }

    m_pLog->AddLine(strTmp);

    double nBitsPerPixel = dScanBits / dPixels;

    strTmp = QString("    Bits per pixel:    %1:1").arg(nBitsPerPixel, 5, 'f', 2);
    m_pLog->AddLine(strTmp);
    m_pLog->AddLine("");

    // Report Huffman stats
    strTmp = QString("  Huffman code histogram stats:");
    m_pLog->AddLine(strTmp);

    uint32_t nDhtHistoTotal;

    for(uint32_t nClass = DHT_CLASS_DC; nClass <= DHT_CLASS_AC; nClass++)
    {
      for(uint32_t nDhtDestId = 0; nDhtDestId <= m_anDhtLookupSetMax[nClass]; nDhtDestId++)
      {
        nDhtHistoTotal = 0;

        for(uint32_t nBitLen = 1; nBitLen <= MAX_DHT_CODELEN; nBitLen++)
        {
          nDhtHistoTotal += m_anDhtHisto[nClass][nDhtDestId][nBitLen];
        }

        strTmp = QString("    Huffman Table: (Dest ID: %1, Class: %2)").arg(nDhtDestId).arg(nClass?"AC":"DC");
        m_pLog->AddLine(strTmp);

        for(uint32_t nBitLen = 1; nBitLen <= MAX_DHT_CODELEN; nBitLen++)
        {
          // A table with no decoded codes has a total of 0
          const double dPercent = (nDhtHistoTotal == 0) ? 0.0 :
                                  (m_anDhtHisto[nClass][nDhtDestId][nBitLen] * 100.0) / nDhtHistoTotal;

          strTmp = QString("      # codes of length %1 bits: %2 (%3%)")
              .arg(nBitLen, 2, 10, QChar('0'))
              .arg(m_anDhtHisto[nClass][nDhtDestId][nBitLen], 8)
              .arg(dPercent, 3, 'f', 0);
          m_pLog->AddLine(strTmp);
        }

        m_pLog->AddLine("");
      }
    }

    // Report YCC stats
    ReportColorStats();
  }                             // !bQuiet

  // ------------------------------------

  // Display the image histogram if enabled
  if(bDisplay && m_bHistEn)
  {
    DrawHistogram(bQuiet, bDumpHistoY);
  }

  if(bDisplay && m_bAvgYValid)
  {
    m_pLog->AddLine("  Average Pixel Luminance (Y):");
    m_pLog->AddLine(QString("    Y=[%1] (range: 0..255)").arg(m_nAvgY, 3));
    m_pLog->AddLine("");
  }

  if(bDisplay && m_bBrightValid)
  {
    m_pLog->AddLine("  Brightest Pixel Search:");
    strTmp = QString("    YCC=[%1,%2,%3] RGB=[%4,%5,%6] @ MCU[%7,%8]")
        .arg(m_nBrightY, 5)
        .arg(m_nBrightCb, 5)
        .arg(m_nBrightCr, 5)
        .arg(m_nBrightR, 3)
        .arg(m_nBrightG, 3)
        .arg(m_nBrightB, 3)
        .arg(m_ptBrightMcu.x(), 3)
        .arg(m_ptBrightMcu.y(), 3);
    m_pLog->AddLine(strTmp);
    m_pLog->AddLine("");
  }

  // --------------------------------------

  if(!bQuiet)
  {
    m_pLog->AddLine("  Finished Decoding SCAN Data");
    strTmp = QString("    Number of RESTART markers decoded: %1").arg(m_nRestartRead);
    m_pLog->AddLine(strTmp);
    strTmp = QString("    Next position in scan buffer: Offset %1").arg(GetScanBufPos());
    m_pLog->AddLine(strTmp);
    m_pLog->AddLine("");
  }

  // --------------------------------------
  // Write out the full Y histogram if requested!

  QString strFull;

  if(bDisplay && m_bHistEn && bDumpHistoY)
  {
    ReportHistogramY();
  }
}

//
// Report if image preview is ready to display
//
// RETURN:
// - True if image preview is ready
//
bool CimgDecode::IsPreviewReady()
{
  return m_bPreviewIsJpeg;
}

// Report out the color conversion statistics
//
// PRE:
// - m_sStatClip
// - m_sHisto
//
void CimgDecode::ReportColorStats()
{
  QString strTmp;

  // Clipping is only counted by the full colour conversion, which runs when
  // the histograms (or clip stats) are on. Otherwise the totals were all 0
  // however much the image clipped.
  const bool bClipCounted = m_bHistEn || m_bStatClipEn;

  if(!bClipCounted)
  {
    m_pLog->AddLine("  YCC and RGB clipping in DC: not counted (turn on Options > Scan Segment > Histogram RGB)");
    m_pLog->AddLine("");
  }

  // Report YCC stats
  if(CC_CLIP_YCC_EN && bClipCounted)
  {
    strTmp = QString("  YCC clipping in DC:");
    m_pLog->AddLine(strTmp);
    strTmp = QString("    Y  component: [<0=%1] [>255=%2]").arg(m_sStatClip.nClipYUnder, 5).arg( m_sStatClip.nClipYOver, 5);
    m_pLog->AddLine(strTmp);
    strTmp = QString("    Cb component: [<0=%1] [>255=%2]").arg(m_sStatClip.nClipCbUnder, 5).arg( m_sStatClip.nClipCbOver, 5);
    m_pLog->AddLine(strTmp);
    strTmp = QString("    Cr component: [<0=%1] [>255=%2]").arg(m_sStatClip.nClipCrUnder, 5).arg( m_sStatClip.nClipCrOver, 5);
    m_pLog->AddLine(strTmp);
    m_pLog->AddLine("");
  }

  if(m_bHistEn)
  {
    strTmp = QString("  YCC histogram in DC (DCT sums : pre-ranged:");
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("Y", m_sHisto.nPreclipYMin, m_sHisto.nPreclipYMax, m_sHisto.nPreclipYSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("Cb", m_sHisto.nPreclipCbMin, m_sHisto.nPreclipCbMax, m_sHisto.nPreclipCbSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("Cr", m_sHisto.nPreclipCrMin, m_sHisto.nPreclipCrMax, m_sHisto.nPreclipCrSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    m_pLog->AddLine("");

    strTmp = QString("  YCC histogram in DC:");
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("Y", m_sHisto.nClipYMin, m_sHisto.nClipYMax, m_sHisto.nClipYSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("Cb", m_sHisto.nClipCbMin, m_sHisto.nClipCbMax, m_sHisto.nClipCbSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("Cr", m_sHisto.nClipCrMin, m_sHisto.nClipCrMax, m_sHisto.nClipCrSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    m_pLog->AddLine("");

    strTmp = QString("  RGB histogram in DC (before clip):");
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("R", m_sHisto.nPreclipRMin, m_sHisto.nPreclipRMax, m_sHisto.nPreclipRSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("G", m_sHisto.nPreclipGMin, m_sHisto.nPreclipGMax, m_sHisto.nPreclipGSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("B", m_sHisto.nPreclipBMin, m_sHisto.nPreclipBMax, m_sHisto.nPreclipBSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    m_pLog->AddLine("");
  }

  if(!bClipCounted)
  {
    return;
  }

  strTmp = QString("  RGB clipping in DC:");
  m_pLog->AddLine(strTmp);
  strTmp = QString("    R  component: [<0=%1] [>255=%2]")
      .arg(m_sStatClip.nClipRUnder, 5)
      .arg(m_sStatClip.nClipROver, 5);
  m_pLog->AddLine(strTmp);
  strTmp = QString("    G  component: [<0=%1] [>255=%2]")
      .arg(m_sStatClip.nClipGUnder, 5)
      .arg(m_sStatClip.nClipGOver, 5);
  m_pLog->AddLine(strTmp);
  strTmp = QString("    B  component: [<0=%1] [>255=%2]")
      .arg(m_sStatClip.nClipBUnder, 5)
      .arg(m_sStatClip.nClipBOver, 5);
  m_pLog->AddLine(strTmp);
  /*
     strTmp = QString("    White Highlight:         [>255=%5u]")).arg(m_sStatClip.nClipWhiteOver;
     m_pLog->AddLine(strTmp);
   */
  m_pLog->AddLine("");
}

// Report the histogram stats from the Y component
//
// PRE:
// - m_anHistoYFull
//
void CimgDecode::ReportHistogramY()
{
  QString strFull;
  QString strTmp;

  m_pLog->AddLine("  Y Histogram in DC: (DCT sums) Full");

  for(uint32_t row = 0; row < 2048 / 8; row++)
  {
    strFull = QString("    Y=%1..%2: ")
        .arg(-1024 + (static_cast<int32_t>(row) * 8), 5)
        .arg(-1024 + (static_cast<int32_t>(row) * 8) + 7, 5);

    for(uint32_t col = 0; col < 8; col++)
    {
      strTmp = QString("0x%1, ").arg(m_anHistoYFull[col + row * 8], 6, 16, QChar('0'));
      strFull += strTmp;
    }

    m_pLog->AddLine(strFull);
  }
}

// Draw the histograms (RGB and/or Y)
//
// INPUT:
// - bQuiet                                     = Calculate stats without reporting to log?
// - bDumpHistoY                        = Generate the Y histogram?
// PRE:
// - m_sHisto
//
void CimgDecode::DrawHistogram(bool bQuiet, bool bDumpHistoY)
{
  QString strTmp;

  if(!bQuiet)
  {
    strTmp = QString("  RGB histogram in DC (after clip):");
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("R", m_sHisto.nClipRMin, m_sHisto.nClipRMax, m_sHisto.nClipRSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("G", m_sHisto.nClipGMin, m_sHisto.nClipGMax, m_sHisto.nClipGSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    strTmp = HistoStatLine("B", m_sHisto.nClipBMin, m_sHisto.nClipBMax, m_sHisto.nClipBSum, m_sHisto.nCount);
    m_pLog->AddLine(strTmp);
    m_pLog->AddLine("");
  }

  // --------------------------------------
  // Now draw the RGB histogram!

  int32_t nCoordY;
  int32_t rgbHistOffset;
  int32_t nHistoBinHeight;
  int32_t nHistoPeakVal;
  int32_t nHistoX;
  int32_t nHistoCurVal;

  m_bDibHistRgbReady = false;

  // Replace the previous histogram (this used to leak one per decode)
  delete m_pDibHistRgb;
  m_pDibHistRgb = new QImage(HISTO_BINS * HISTO_BIN_WIDTH, 3 * HISTO_BIN_HEIGHT_MAX, QImage::Format_RGB32);
  m_pDibHistRgb->fill(Qt::white);

  // Do peak detect first
  // Don't want to reset peak value to 0 as otherwise we might get
  // division by zero later when we calculate nHistoBinHeight
  nHistoPeakVal = 1;

  // Peak value is across all three channels!
  for(int32_t nHistChan = 0; nHistChan < 3; nHistChan++)
  {
    for(int32_t i = 0; i < HISTO_BINS; i++)
    {
      if(nHistChan == 0)
      {
        nHistoCurVal = m_anCcHisto_r[i];
      }
      else if(nHistChan == 1)
      {
        nHistoCurVal = m_anCcHisto_g[i];
      }
      else
      {
        nHistoCurVal = m_anCcHisto_b[i];
      }

      nHistoPeakVal = (nHistoCurVal > nHistoPeakVal) ? nHistoCurVal : nHistoPeakVal;
    }
  }

  for(int32_t nHistChan = 0; nHistChan < 3; nHistChan++)
  {
    if(nHistChan == CHAN_R)
    {
      rgbHistOffset = HISTO_BIN_HEIGHT_MAX - 1;
    }
    else if(nHistChan == CHAN_G)
    {
      rgbHistOffset = HISTO_BIN_HEIGHT_MAX * 2 - 1;
    }
    else
    {
      rgbHistOffset = HISTO_BIN_HEIGHT_MAX * 3 - 1;
    }

    for(int32_t i = 0; i < HISTO_BINS; i++)
    {
      // Calculate bin's height (max HISTO_BIN_HEIGHT_MAX)
      if(nHistChan == CHAN_R)
      {
        nHistoCurVal = m_anCcHisto_r[i];
      }
      else if(nHistChan == CHAN_G)
      {
        nHistoCurVal = m_anCcHisto_g[i];
      }
      else
      {
        nHistoCurVal = m_anCcHisto_b[i];
      }

      // 64-bit product: a bin of over 35.8 million pixels (a white 6000x6000
      // image) overflowed 32 bits, and its bar went missing
      nHistoBinHeight = static_cast<int32_t>(static_cast<int64_t>(HISTO_BIN_HEIGHT_MAX) * nHistoCurVal / nHistoPeakVal);

      for(int32_t y = 0; y < nHistoBinHeight; y++)
      {
        // Store the RGB triplet
        for(int32_t bin_width = 0; bin_width < HISTO_BIN_WIDTH; bin_width++)
        {
          nHistoX = (i * HISTO_BIN_WIDTH) + bin_width;
          // Bars rise from the bottom row of the channel's band
          // (rgbHistOffset); a full bar fills exactly the band
          nCoordY = rgbHistOffset - y;
          m_pDibHistRgb->setPixelColor(nHistoX, nCoordY, QColor((nHistChan == CHAN_R) ? 255 : 0, (nHistChan == CHAN_G) ? 255 : 0, (nHistChan == CHAN_B) ? 255 : 0));
        }
      }
    }  // i: 0..HISTO_BINS-1

    m_bDibHistRgbReady = true;
  }

  // Only create the Y DC Histogram if requested
  m_bDibHistYReady = false;

  if(bDumpHistoY)
  {
    // Replace the previous histogram (this used to leak one per decode)
    delete m_pDibHistY;
    m_pDibHistY = new QImage(SUBSET_HISTO_BINS * HISTO_BIN_WIDTH, HISTO_BIN_HEIGHT_MAX, QImage::Format_RGB32);
    m_pDibHistY->fill(Qt::white);

    // Do peak detect first
    // Don't want to reset peak value to 0 as otherwise we might get
    // division by zero later when we calculate nHistoBinHeight
    nHistoPeakVal = 1;

    // TODO: Temporarily made quarter width - need to resample instead

    // Peak value
    for(uint32_t i = 0; i < SUBSET_HISTO_BINS; i++)
    {
      nHistoCurVal = m_anHistoYFull[i * 4 + 0];
      nHistoCurVal += m_anHistoYFull[i * 4 + 1];
      nHistoCurVal += m_anHistoYFull[i * 4 + 2];
      nHistoCurVal += m_anHistoYFull[i * 4 + 3];
      nHistoPeakVal = (nHistoCurVal > nHistoPeakVal) ? nHistoCurVal : nHistoPeakVal;
    }

    for(int32_t i = 0; i < SUBSET_HISTO_BINS; i++)
    {
      // Calculate bin's height (max HISTO_BIN_HEIGHT_MAX)
      nHistoCurVal = m_anHistoYFull[i * 4 + 0];
      nHistoCurVal += m_anHistoYFull[i * 4 + 1];
      nHistoCurVal += m_anHistoYFull[i * 4 + 2];
      nHistoCurVal += m_anHistoYFull[i * 4 + 3];
      // 64-bit product, as for the RGB histogram
      nHistoBinHeight = static_cast<int32_t>(static_cast<int64_t>(HISTO_BIN_HEIGHT_MAX) * nHistoCurVal / nHistoPeakVal);

      for(int32_t y = 0; y < nHistoBinHeight; y++)
      {
        // Store the RGB triplet
        for(int32_t bin_width = 0; bin_width < HISTO_BIN_WIDTH; bin_width++)
        {
          nHistoX = (i * HISTO_BIN_WIDTH) + bin_width;
          nCoordY = (HISTO_BIN_HEIGHT_MAX - y) - 1;
          m_pDibHistY->setPixelColor(nHistoX, nCoordY, Qt::gray);
        }
      }
    }  // i: 0..HISTO_BINS-1

    m_bDibHistYReady = true;
  }
}

// Reset the decoder Scan Buff (at start of scan and
// after any restart markers)
//
// INPUT:
// - nFilePos                                   = File position at start of scan
// - bRestart                                   = Is this a reset due to RSTn marker?
// PRE:
// - m_nRestartInterval
// POST:
// - m_bScanEnd
// - m_bScanBad
// - m_nScanBuff
// - m_nScanBuffPtr_first
// - m_nScanBuffPtr_start
// - m_nScanBuffPtr_align
// - m_anScanBuffPtr_pos[]
// - m_anScanBuffPtr_err[]
// - m_nScanBuffLatchErr
// - m_nScanBuffPtr_num
// - m_nScanBuff_vacant
// - m_nScanCurErr
// - m_bRestartRead
// - m_nRestartMcusLeft
//
void CimgDecode::DecodeRestartScanBuf(uint32_t nFilePos, bool bRestart)
{
  // Reset the state
  m_bScanEnd = false;
  m_bScanBad = false;
  m_bScanEofReported = false;
  m_nScanBuff = 0x00000000;
  m_nScanBuffPtr = nFilePos;
  
  if(!bRestart)
  {
    // Only reset the scan buffer pointer at the start of the file,
    // not after any RSTn markers. This is only used for the compression
    // ratio calculations.
    m_nScanBuffPtr_first = nFilePos;

    // Nor where the scan's data ends, or the MCU whose position is wanted
    m_nScanDataEnd = UINT32_MAX;
    m_nMcuPosPending = -1;
  }

  m_nScanBuffPtr_start = nFilePos;
  m_nScanBuffPtr_align = 0;     // Start with byte alignment (0)
  // The next bit's position, before any byte is added: the next byte's. (It
  // was 0, which was reported where a restart interval or the scan ended
  // before a byte was added.)
  m_anScanBuffPtr_pos[0] = nFilePos;
  m_anScanBuffPtr_pos[1] = 0;
  m_anScanBuffPtr_pos[2] = 0;
  m_anScanBuffPtr_pos[3] = 0;
  m_anScanBuffPtr_err[0] = SCANBUF_OK;
  m_anScanBuffPtr_err[1] = SCANBUF_OK;
  m_anScanBuffPtr_err[2] = SCANBUF_OK;
  m_anScanBuffPtr_err[3] = SCANBUF_OK;
  m_nScanBuffLatchErr = SCANBUF_OK;

  m_nScanBuffPtr_num = 0;       // Empty m_nScanBuff
  m_nScanBuff_vacant = 32;

  m_nScanCurErr = false;

  //
  m_nScanBuffPtr = nFilePos;

  // Reset RST Interval checking
  m_bRestartRead = false;
  m_nRestartMcusLeft = m_nRestartInterval;

  ScanFillForget();
}

// Color conversion from YCC to RGB
//
// INPUT:
// - sPix                               = Structure for color conversion
// OUTPUT:
// - sPix                               = Structure for color conversion
//
void CimgDecode::ConvertYCCtoRGBFastFloat(PixelCc & sPix)
{
  int32_t nValY, nValCb, nValCr;

  double nValR, nValG, nValB;

  // Perform ranging to adjust from Huffman sums to reasonable range
  // -1024..+1024 -> -128..127
  sPix.nPreclipY = sPix.nPrerangeY >> 3;
  sPix.nPreclipCb = sPix.nPrerangeCb >> 3;
  sPix.nPreclipCr = sPix.nPrerangeCr >> 3;

  // Limit on YCC input
  // The y/cb/nPreclipCr values should already be 0..255 unless we have a
  // decode error where DC value gets out of range!
  //CapYccRange(nMcuX,nMcuY,sPix);
  nValY = (sPix.nPreclipY < -128) ? -128 : (sPix.nPreclipY > 127) ? 127 : sPix.nPreclipY;
  nValCb = (sPix.nPreclipCb < -128) ? -128 : (sPix.nPreclipCb > 127) ? 127 : sPix.nPreclipCb;
  nValCr = (sPix.nPreclipCr < -128) ? -128 : (sPix.nPreclipCr > 127) ? 127 : sPix.nPreclipCr;

  // Save the YCC values (0..255)
  sPix.nFinalY = static_cast < uint8_t > (nValY + 128);
  sPix.nFinalCb = static_cast < uint8_t > (nValCb + 128);
  sPix.nFinalCr = static_cast < uint8_t > (nValCr + 128);

  // Convert
  // Since the following seems to preserve the multiplies and subtractions
  // we could expand this out manually
  double fConstRed = 0.299f;
  double fConstGreen = 0.587f;
  double fConstBlue = 0.114f;

  // r = cr * 1.402 + y;
  // b = cb * 1.772 + y;
  // g = (y - 0.03409 * r) / 0.587;
  nValR = nValCr * (2 - 2 * fConstRed) + nValY;
  nValB = nValCb * (2 - 2 * fConstBlue) + nValY;
  nValG = (nValY - fConstBlue * nValB - fConstRed * nValR) / fConstGreen;

  // Level shift
  nValR += 128;
  nValB += 128;
  nValG += 128;

  // RGB JPEG: the components were R, G and B; ranged the same way as the YCbCr
  if(sPix.bRgbDirect)
  {
    nValR = (sPix.nDirectR >> 3) + 128;
    nValG = (sPix.nDirectG >> 3) + 128;
    nValB = (sPix.nDirectB >> 3) + 128;
  }

  // --------------- Finshed the color conversion

  // Limit
  //   r/g/nPreclipB -> r/g/b
  //CapRgbRange(nMcuX,nMcuY,sPix);
  sPix.nFinalR = (nValR < 0) ? 0 : (nValR > 255) ? 255 : static_cast<uint8_t>(nValR);
  sPix.nFinalG = (nValG < 0) ? 0 : (nValG > 255) ? 255 : static_cast<uint8_t>(nValG);
  sPix.nFinalB = (nValB < 0) ? 0 : (nValB > 255) ? 255 : static_cast<uint8_t>(nValB);
}

// Color conversion from YCC to RGB
//
// INPUT:
// - sPix                               = Structure for color conversion
// OUTPUT:
// - sPix                               = Structure for color conversion
//
void CimgDecode::ConvertYCCtoRGBFastFixed(PixelCc & sPix)
{
  int32_t nPreclipY, nPreclipCb, nPreclipCr;
  int32_t nValY, nValCb, nValCr;
  int32_t nValR, nValG, nValB;

  // Perform ranging to adjust from Huffman sums to reasonable range
  // -1024..+1024 -> -128..+127
  nPreclipY = sPix.nPrerangeY >> 3;
  nPreclipCb = sPix.nPrerangeCb >> 3;
  nPreclipCr = sPix.nPrerangeCr >> 3;

  // Limit on YCC input
  // The nPreclip* values should already be 0..255 unless we have a
  // decode error where DC value gets out of range!

  //CapYccRange(nMcuX,nMcuY,sPix);
  nValY = (nPreclipY < -128) ? -128 : (nPreclipY > 127) ? 127 : nPreclipY;
  nValCb = (nPreclipCb < -128) ? -128 : (nPreclipCb > 127) ? 127 : nPreclipCb;
  nValCr = (nPreclipCr < -128) ? -128 : (nPreclipCr > 127) ? 127 : nPreclipCr;

  // Save the YCC values (0..255)
  sPix.nFinalY = static_cast < uint8_t > (nValY + 128);
  sPix.nFinalCb = static_cast < uint8_t > (nValCb + 128);
  sPix.nFinalCr = static_cast < uint8_t > (nValCr + 128);

  // --------------

  // Convert
  // Fixed values is x 1024 (10 bits). Leaves 22 bits for integer
  //r2 = 1024*cr1*(2-2*fConstRed)+1024*y1;
  //b2 = 1024*cb1*(2-2*fConstBlue)+1024*y1;
  //g2 = 1024*(y1-fConstBlue*b2/1024-fConstRed*r2/1024)/fConstGreen;

  const int32_t CFIX_R = 306;
  const int32_t CFIX_G = 601;
  const int32_t CFIX_B = 116;
  const int32_t CFIX2_R = 1436;        // 2*(1024-cfix_red)
  const int32_t CFIX2_B = 1816;        // 2*(1024-cfix_blue)
  const int32_t CFIX2_G = 1048576;     // 1024*1024

  nValR = CFIX2_R * nValCr + 1024 * nValY;
  nValB = CFIX2_B * nValCb + 1024 * nValY;
  nValG = (CFIX2_G * nValY - CFIX_B * nValB - CFIX_R * nValR) / CFIX_G;

  nValR >>= 10;
  nValG >>= 10;
  nValB >>= 10;

  // Level shift
  nValR += 128;
  nValB += 128;
  nValG += 128;

  // Limit
  //   r/g/nPreclipB -> r/g/b
  sPix.nFinalR = (nValR < 0) ? 0 : (nValR > 255) ? 255 : static_cast < uint8_t > (nValR);
  sPix.nFinalG = (nValG < 0) ? 0 : (nValG > 255) ? 255 : static_cast < uint8_t > (nValG);
  sPix.nFinalB = (nValB < 0) ? 0 : (nValB > 255) ? 255 : static_cast < uint8_t > (nValB);
}

// Color conversion from YCC to RGB
// - CC: y/cb/cr -> r/g/b
//
// INPUT:
// - nMcuX                              = MCU x coordinate
// - nMcuY                              = MCU y coordinate
// - sPix                               = Structure for color conversion
// OUTPUT:
// - sPix                               = Structure for color conversion
// POST:
// - m_sHisto
// - m_anHistoYFull[]
// - m_anCcHisto_r[]
// - m_anCcHisto_g[]
// - m_anCcHisto_b[]
//
void CimgDecode::ConvertYCCtoRGB(uint32_t nMcuX, uint32_t nMcuY, PixelCc & sPix)
{  
  double fConstRed = 0.299;
  double fConstGreen = 0.587;
  double fConstBlue = 0.114;

  int32_t nByteY, nByteCb, nByteCr;
  int32_t nValY, nValCb, nValCr;

  double nValR, nValG, nValB;

  if(m_bHistEn && sPix.bInImage)
  {
    // Calc stats on preranged YCC (direct from huffman DC sums)
    m_sHisto.nPreclipYMin = (sPix.nPrerangeY < m_sHisto.nPreclipYMin) ? sPix.nPrerangeY : m_sHisto.nPreclipYMin;
    m_sHisto.nPreclipYMax = (sPix.nPrerangeY > m_sHisto.nPreclipYMax) ? sPix.nPrerangeY : m_sHisto.nPreclipYMax;
    m_sHisto.nPreclipYSum += sPix.nPrerangeY;
    m_sHisto.nPreclipCbMin = (sPix.nPrerangeCb < m_sHisto.nPreclipCbMin) ? sPix.nPrerangeCb : m_sHisto.nPreclipCbMin;
    m_sHisto.nPreclipCbMax = (sPix.nPrerangeCb > m_sHisto.nPreclipCbMax) ? sPix.nPrerangeCb : m_sHisto.nPreclipCbMax;
    m_sHisto.nPreclipCbSum += sPix.nPrerangeCb;
    m_sHisto.nPreclipCrMin = (sPix.nPrerangeCr < m_sHisto.nPreclipCrMin) ? sPix.nPrerangeCr : m_sHisto.nPreclipCrMin;
    m_sHisto.nPreclipCrMax = (sPix.nPrerangeCr > m_sHisto.nPreclipCrMax) ? sPix.nPrerangeCr : m_sHisto.nPreclipCrMax;
    m_sHisto.nPreclipCrSum += sPix.nPrerangeCr;
  }

  if(m_bHistEn && sPix.bInImage)
  {
    // Now generate the Y histogram, if requested
    // Add the Y value to the full histogram (for image similarity calcs)
    //if (bDumpHistoY) {
    int32_t histo_index = sPix.nPrerangeY;

    if(histo_index < -1024)
      histo_index = -1024;

    if(histo_index > 1023)
      histo_index = 1023;

    histo_index += 1024;
    m_anHistoYFull[histo_index]++;
    //}
  }

  // Perform ranging to adjust from Huffman sums to reasonable range
  // -1024..+1024 -> 0..255
  // Add 1024 then / 8
  sPix.nPreclipY = (sPix.nPrerangeY + 1024) / 8;
  sPix.nPreclipCb = (sPix.nPrerangeCb + 1024) / 8;
  sPix.nPreclipCr = (sPix.nPrerangeCr + 1024) / 8;

  // Limit on YCC input
  // The y/cb/nPreclipCr values should already be 0..255 unless we have a
  // decode error where DC value gets out of range!
  CapYccRange(nMcuX, nMcuY, sPix);

  // --------------- Perform the color conversion
  nByteY = sPix.nFinalY;
  nByteCb = sPix.nFinalCb;
  nByteCr = sPix.nFinalCr;

  // Level shift
  nValY = nByteY - 128;
  nValCb = nByteCb - 128;
  nValCr = nByteCr - 128;

  // Convert
  nValR = nValCr * (2.0 - 2.0 * fConstRed) + nValY;
  nValB = nValCb * (2.0 - 2.0 * fConstBlue) + nValY;
  nValG = (nValY - fConstBlue * nValB - fConstRed * nValR) / fConstGreen;

  // Level shift
  nValR += 128;
  nValB += 128;
  nValG += 128;

  // RGB JPEG: the components were R, G and B (the YCbCr above only fed the
  // statistics); ranged the same way as the YCbCr
  if(sPix.bRgbDirect)
  {
    nValR = (sPix.nDirectR + 1024) / 8;
    nValG = (sPix.nDirectG + 1024) / 8;
    nValB = (sPix.nDirectB + 1024) / 8;
  }

  sPix.nPreclipR = nValR;
  sPix.nPreclipG = nValG;
  sPix.nPreclipB = nValB;
  // --------------- Finshed the color conversion

  // Limit
  // - Preclip RGB to Final RGB
  CapRgbRange(nMcuX, nMcuY, sPix);

  // Display
  /*
     strTmp = QString("* (YCC->RGB) @ (%03u,%03u): YCC=(%4d,%4d,%4d) RGB=(%03u,%03u,%u)",
     nMcuX,nMcuY,y,cb,cr,r_limb,g_limb,b_limb);
     m_pLog->AddLine(strTmp);
   */

  if(m_bHistEn && sPix.bInImage)
  {
    // Bin the result into a histogram!
    //   value = 0..255
    //   bin   = 0..7, 8..15, ..., 248..255
    // Channel: Red
    uint32_t bin_divider = 256 / HISTO_BINS;

    m_anCcHisto_r[sPix.nFinalR / bin_divider]++;
    m_anCcHisto_g[sPix.nFinalG / bin_divider]++;
    m_anCcHisto_b[sPix.nFinalB / bin_divider]++;
  }
}

// Color conversion clipping
// - Process the pre-clipped YCC values and ensure they
//   have been clipped into the valid region
//
// INPUT:
// - nMcuX                              = MCU x coordinate
// - nMcuY                              = MCU y coordinate
// - sPix                               = Structure for color conversion
// OUTPUT:
// - sPix                               = Structure for color conversion
// POST:
// - m_sHisto
//
void CimgDecode::CapYccRange(uint32_t nMcuX, uint32_t nMcuY, PixelCc & sPix)
{
  // Check the bounds on the YCC value. It should probably be 0..255 unless our DC
  // values got really messed up in a corrupt file. Perhaps it might be best to reset it to 0? Otherwise
  // it will continuously report an out-of-range value.
  int32_t nCurY, nCurCb, nCurCr;

  nCurY = sPix.nPreclipY;
  nCurCb = sPix.nPreclipCb;
  nCurCr = sPix.nPreclipCr;

  if(m_bHistEn && sPix.bInImage)
  {
    m_sHisto.nClipYMin = (nCurY < m_sHisto.nClipYMin) ? nCurY : m_sHisto.nClipYMin;
    m_sHisto.nClipYMax = (nCurY > m_sHisto.nClipYMax) ? nCurY : m_sHisto.nClipYMax;
    m_sHisto.nClipYSum += nCurY;
    m_sHisto.nClipCbMin = (nCurCb < m_sHisto.nClipCbMin) ? nCurCb : m_sHisto.nClipCbMin;
    m_sHisto.nClipCbMax = (nCurCb > m_sHisto.nClipCbMax) ? nCurCb : m_sHisto.nClipCbMax;
    m_sHisto.nClipCbSum += nCurCb;
    m_sHisto.nClipCrMin = (nCurCr < m_sHisto.nClipCrMin) ? nCurCr : m_sHisto.nClipCrMin;
    m_sHisto.nClipCrMax = (nCurCr > m_sHisto.nClipCrMax) ? nCurCr : m_sHisto.nClipCrMax;
    m_sHisto.nClipCrSum += nCurCr;
    m_sHisto.nCount++;
  }

  // Only the first YCC_CLIP_REPORT_MAX clips get a note, but the totals count
  // every clipped pixel inside the image (they counted only the noted ones)
  if(CC_CLIP_YCC_EN)
  {
    if(nCurY > CC_CLIP_YCC_MAX)
    {
      if(sPix.bInImage && YCC_CLIP_REPORT_ERR && (m_nWarnYccClipNum < YCC_CLIP_REPORT_MAX))
      {
        QString strTmp;

        strTmp = QString("*** NOTE: YCC Clipped. MCU=(%1,%2) YCC=(%3,%4,%5) Y Overflow @ Offset %6")
            .arg(nMcuX, 4)
            .arg(nMcuY, 4)
            .arg(nCurY, 5)
            .arg(nCurCb, 5)
            .arg(nCurCr, 5)
            .arg(GetMcuFilePos(nMcuX, nMcuY));
        m_pLog->AddLineWarn(strTmp);
        m_nWarnYccClipNum++;

        if(m_nWarnYccClipNum == YCC_CLIP_REPORT_MAX)
        {
          strTmp = QString("    Only reported first %1 instances of this message...").arg(YCC_CLIP_REPORT_MAX);
          m_pLog->AddLineWarn(strTmp);
        }
      }

      sPix.nClip |= CC_CLIP_Y_OVER;

      if(sPix.bInImage)
      {
        m_sStatClip.nClipYOver++;
      }
      nCurY = CC_CLIP_YCC_MAX;
    }

    if(nCurY < CC_CLIP_YCC_MIN)
    {
      if(sPix.bInImage && YCC_CLIP_REPORT_ERR && (m_nWarnYccClipNum < YCC_CLIP_REPORT_MAX))
      {
        QString strTmp;

        strTmp = QString("*** NOTE: YCC Clipped. MCU=(%1,%2) YCC=(%3,%4,%5) Y Underflow @ Offset %6")
            .arg(nMcuX, 4)
            .arg(nMcuY, 4)
            .arg(nCurY, 5)
            .arg(nCurCb, 5)
            .arg(nCurCr, 5)
            .arg(GetMcuFilePos(nMcuX, nMcuY));
        m_pLog->AddLineWarn(strTmp);
        m_nWarnYccClipNum++;

        if(m_nWarnYccClipNum == YCC_CLIP_REPORT_MAX)
        {
          strTmp = QString("    Only reported first %1 instances of this message...").arg(YCC_CLIP_REPORT_MAX);
          m_pLog->AddLineWarn(strTmp);
        }
      }
      
      sPix.nClip |= CC_CLIP_Y_UNDER;

      if(sPix.bInImage)
      {
        m_sStatClip.nClipYUnder++;
      }
      nCurY = CC_CLIP_YCC_MIN;
    }

    if(nCurCb > CC_CLIP_YCC_MAX)
    {
      if(sPix.bInImage && YCC_CLIP_REPORT_ERR && (m_nWarnYccClipNum < YCC_CLIP_REPORT_MAX))
      {
        QString strTmp;

        strTmp = QString("*** NOTE: YCC Clipped.  MCU=(%1,%2) YCC=(%3,%4,%5) Cb Overflow @ Offset %6")
            .arg(nMcuX, 4)
            .arg(nMcuY, 4)
            .arg(nCurY, 5)
            .arg(nCurCb, 5)
            .arg(nCurCr, 5)
            .arg(GetMcuFilePos(nMcuX, nMcuY));
        m_pLog->AddLineWarn(strTmp);
        m_nWarnYccClipNum++;

        if(m_nWarnYccClipNum == YCC_CLIP_REPORT_MAX)
        {
          strTmp = QString("    Only reported first %1 instances of this message...").arg(YCC_CLIP_REPORT_MAX);
          m_pLog->AddLineWarn(strTmp);
        }
      }

      sPix.nClip |= CC_CLIP_CB_OVER;

      if(sPix.bInImage)
      {
        m_sStatClip.nClipCbOver++;
      }
      nCurCb = CC_CLIP_YCC_MAX;
    }

    if(nCurCb < CC_CLIP_YCC_MIN)
    {
      if(sPix.bInImage && YCC_CLIP_REPORT_ERR && (m_nWarnYccClipNum < YCC_CLIP_REPORT_MAX))
      {
        QString strTmp;

        strTmp = QString("*** NOTE: YCC Clipped.  MCU=(%1,%2) YCC=(%3,%4,%5) Cb Underflow @ Offset %6")
            .arg(nMcuX, 4)
            .arg(nMcuY, 4)
            .arg(nCurY, 5)
            .arg(nCurCb, 5)
            .arg(nCurCr, 5)
            .arg(GetMcuFilePos(nMcuX, nMcuY));
        m_pLog->AddLineWarn(strTmp);
        m_nWarnYccClipNum++;

        if(m_nWarnYccClipNum == YCC_CLIP_REPORT_MAX)
        {
          strTmp = QString("    Only reported first %1 instances of this message...").arg(YCC_CLIP_REPORT_MAX);
          m_pLog->AddLineWarn(strTmp);
        }
      }

      sPix.nClip |= CC_CLIP_CB_UNDER;

      if(sPix.bInImage)
      {
        m_sStatClip.nClipCbUnder++;
      }
      nCurCb = CC_CLIP_YCC_MIN;
    }

    if(nCurCr > CC_CLIP_YCC_MAX)
    {
      if(sPix.bInImage && YCC_CLIP_REPORT_ERR && (m_nWarnYccClipNum < YCC_CLIP_REPORT_MAX))
      {
        QString strTmp;

        strTmp = QString("*** NOTE: YCC Clipped.  MCU=(%1,%2) YCC=(%3,%4,%5) Cr Overflow @ Offset %6")
            .arg(nMcuX, 4)
            .arg(nMcuY, 4)
            .arg(nCurY, 5)
            .arg(nCurCb, 5)
            .arg(nCurCr, 5)
            .arg(GetMcuFilePos(nMcuX, nMcuY));
        m_pLog->AddLineWarn(strTmp);
        m_nWarnYccClipNum++;

        if(m_nWarnYccClipNum == YCC_CLIP_REPORT_MAX)
        {
          strTmp = QString("    Only reported first %1 instances of this message...").arg(YCC_CLIP_REPORT_MAX);
          m_pLog->AddLineWarn(strTmp);
        }
      }

      sPix.nClip |= CC_CLIP_CR_OVER;

      if(sPix.bInImage)
      {
        m_sStatClip.nClipCrOver++;
      }
      nCurCr = CC_CLIP_YCC_MAX;
    }

    if(nCurCr < CC_CLIP_YCC_MIN)
    {
      if(sPix.bInImage && YCC_CLIP_REPORT_ERR && (m_nWarnYccClipNum < YCC_CLIP_REPORT_MAX))
      {
        QString strTmp;

        strTmp = QString("*** NOTE: YCC Clipped.  MCU=(%1,%2) YCC=(%3,%4,%5) Cr Underflow @ Offset %6")
            .arg(nMcuX, 4)
            .arg(nMcuY, 4)
            .arg(nCurY, 5)
            .arg(nCurCb, 5)
            .arg(nCurCr, 5)
            .arg(GetMcuFilePos(nMcuX, nMcuY));
        m_pLog->AddLineWarn(strTmp);
        m_nWarnYccClipNum++;

        if(m_nWarnYccClipNum == YCC_CLIP_REPORT_MAX)
        {
          strTmp = QString("    Only reported first %1 instances of this message...").arg(YCC_CLIP_REPORT_MAX);
          m_pLog->AddLineWarn(strTmp);
        }
      }

      sPix.nClip |= CC_CLIP_CR_UNDER;

      if(sPix.bInImage)
      {
        m_sStatClip.nClipCrUnder++;
      }
      nCurCr = CC_CLIP_YCC_MIN;
    }
  }                             // YCC clip enabled?

  // Perform color conversion: YCC->RGB
  // The nCurY/cb/cr values should already be clipped to byte size
  sPix.nFinalY = static_cast < uint8_t > (nCurY);
  sPix.nFinalCb = static_cast < uint8_t > (nCurCb);
  sPix.nFinalCr = static_cast < uint8_t > (nCurCr);
}

// Color conversion clipping (RGB)
// - Input RGB triplet in floats
// - Expect range to be 0..255
// - Return RGB triplet in byteuis
// - Report if it is out of range
// - Converts from Preclip RGB to Final RGB
//
// INPUT:
// - nMcuX                              = MCU x coordinate
// - nMcuY                              = MCU y coordinate
// - sPix                               = Structure for color conversion
// OUTPUT:
// - sPix                               = Structure for color conversion
// POST:
// - m_sHisto
//
void CimgDecode::CapRgbRange(uint32_t nMcuX, uint32_t nMcuY, PixelCc & sPix)
{
  int32_t nLimitR, nLimitG, nLimitB;

  // Truncate
  nLimitR = static_cast<int32_t>(sPix.nPreclipR);
  nLimitG = static_cast<int32_t>(sPix.nPreclipG);
  nLimitB = static_cast<int32_t>(sPix.nPreclipB);

  if(m_bHistEn && sPix.bInImage)
  {
    m_sHisto.nPreclipRMin = (nLimitR < m_sHisto.nPreclipRMin) ? nLimitR : m_sHisto.nPreclipRMin;
    m_sHisto.nPreclipRMax = (nLimitR > m_sHisto.nPreclipRMax) ? nLimitR : m_sHisto.nPreclipRMax;
    m_sHisto.nPreclipRSum += nLimitR;
    m_sHisto.nPreclipGMin = (nLimitG < m_sHisto.nPreclipGMin) ? nLimitG : m_sHisto.nPreclipGMin;
    m_sHisto.nPreclipGMax = (nLimitG > m_sHisto.nPreclipGMax) ? nLimitG : m_sHisto.nPreclipGMax;
    m_sHisto.nPreclipGSum += nLimitG;
    m_sHisto.nPreclipBMin = (nLimitB < m_sHisto.nPreclipBMin) ? nLimitB : m_sHisto.nPreclipBMin;
    m_sHisto.nPreclipBMax = (nLimitB > m_sHisto.nPreclipBMax) ? nLimitB : m_sHisto.nPreclipBMax;
    m_sHisto.nPreclipBSum += nLimitB;
  }

  if(nLimitR < 0)
  {
    if(m_bVerbose)
    {
      QString strTmp;

      strTmp = QString("  YCC->RGB Clipped. MCU=(%1,%2) RGB=(%3,%4,%5) Red Underflow")
          .arg(nMcuX, 4)
          .arg(nMcuY, 4)
          .arg(nLimitR, 5)
          .arg(nLimitG, 5)
          .arg(nLimitB, 5);
      m_pLog->AddLineWarn(strTmp);
    }

    sPix.nClip |= CC_CLIP_R_UNDER;
    if(sPix.bInImage)
    {
      m_sStatClip.nClipRUnder++;
    }
    nLimitR = 0;
  }

  if(nLimitG < 0)
  {
    if(m_bVerbose)
    {
      QString strTmp;

      strTmp = QString("  YCC->RGB Clipped. MCU=(%1,%2) RGB=(%3,%4,%5) Green Underflow")
          .arg(nMcuX, 4)
          .arg(nMcuY, 4)
          .arg(nLimitR, 5)
          .arg(nLimitG, 5)
          .arg(nLimitB, 5);
      m_pLog->AddLineWarn(strTmp);
    }

    sPix.nClip |= CC_CLIP_G_UNDER;
    if(sPix.bInImage)
    {
      m_sStatClip.nClipGUnder++;
    }
    nLimitG = 0;
  }

  if(nLimitB < 0)
  {
    if(m_bVerbose)
    {
      QString strTmp;

      strTmp = QString("  YCC->RGB Clipped. MCU=(%1,%2) RGB=(%3,%4,%5) Blue Underflow")
          .arg(nMcuX, 4)
          .arg(nMcuY, 4)
          .arg(nLimitR, 5)
          .arg(nLimitG, 5)
          .arg(nLimitB, 5);
      m_pLog->AddLineWarn(strTmp);
    }

    sPix.nClip |= CC_CLIP_B_UNDER;
    if(sPix.bInImage)
    {
      m_sStatClip.nClipBUnder++;
    }
    nLimitB = 0;
  }

  if(nLimitR > 255)
  {
    if(m_bVerbose)
    {
      QString strTmp;

      strTmp = QString("  YCC->RGB Clipped. MCU=(%1,%2) RGB=(%3,%4,%5) Red Overflow")
          .arg(nMcuX, 4)
          .arg(nMcuY, 4)
          .arg(nLimitR, 5)
          .arg(nLimitG, 5)
          .arg(nLimitB, 5);
      m_pLog->AddLineWarn(strTmp);
    }

    sPix.nClip |= CC_CLIP_R_OVER;
    if(sPix.bInImage)
    {
      m_sStatClip.nClipROver++;
    }
    nLimitR = 255;
  }

  if(nLimitG > 255)
  {
    if(m_bVerbose)
    {
      QString strTmp;

      strTmp = QString("  YCC->RGB Clipped. MCU=(%1,%2) RGB=(%3,%4,%5) Green Overflow")
          .arg(nMcuX, 4)
          .arg(nMcuY, 4)
          .arg(nLimitR, 5)
          .arg(nLimitG, 5)
          .arg(nLimitB, 5);
      m_pLog->AddLineWarn(strTmp);
    }

    sPix.nClip |= CC_CLIP_G_OVER;
    if(sPix.bInImage)
    {
      m_sStatClip.nClipGOver++;
    }
    nLimitG = 255;
  }

  if(nLimitB > 255)
  {
    if(m_bVerbose)
    {
      QString strTmp;

      strTmp = QString("  YCC->RGB Clipped. MCU=(%1,%2) RGB=(%3,%4,%5) Blue Overflow")
          .arg(nMcuX, 4)
          .arg(nMcuY, 4)
          .arg(nLimitR, 5)
          .arg(nLimitG, 5)
          .arg(nLimitB, 5);
      m_pLog->AddLineWarn(strTmp);
    }

    sPix.nClip |= CC_CLIP_B_OVER;
    if(sPix.bInImage)
    {
      m_sStatClip.nClipBOver++;
    }
    nLimitB = 255;
  }

  if(m_bHistEn && sPix.bInImage)
  {
    m_sHisto.nClipRMin = (nLimitR < m_sHisto.nClipRMin) ? nLimitR : m_sHisto.nClipRMin;
    m_sHisto.nClipRMax = (nLimitR > m_sHisto.nClipRMax) ? nLimitR : m_sHisto.nClipRMax;
    m_sHisto.nClipRSum += nLimitR;
    m_sHisto.nClipGMin = (nLimitG < m_sHisto.nClipGMin) ? nLimitG : m_sHisto.nClipGMin;
    m_sHisto.nClipGMax = (nLimitG > m_sHisto.nClipGMax) ? nLimitG : m_sHisto.nClipGMax;
    m_sHisto.nClipGSum += nLimitG;
    m_sHisto.nClipBMin = (nLimitB < m_sHisto.nClipBMin) ? nLimitB : m_sHisto.nClipBMin;
    m_sHisto.nClipBMax = (nLimitB > m_sHisto.nClipBMax) ? nLimitB : m_sHisto.nClipBMax;
    m_sHisto.nClipBSum += nLimitB;
  }

  // Now convert to byteui
  sPix.nFinalR = static_cast<uint8_t>(nLimitR);
  sPix.nFinalG = static_cast<uint8_t>(nLimitG);
  sPix.nFinalB = static_cast<uint8_t>(nLimitB);
}

// Recalcs the full image based on the original YCC pixmap
// - Also locate the brightest pixel.
// - Note that we cannot do the brightest pixel search when we called SetFullRes()
//   because we need to have access to all of the channel components at once to do this.
//
// INPUT:
// - pRectView                          = UNUSED. Intended to limit updates to visible region
//                            (Range of real image that is visibile / cropped)
// PRE:
// - m_pPixValY[]
// - m_pPixValCb[]
// - m_pPixValCr[]
// OUTPUT:
// - pTmp                                       = RGB pixel map (32-bit per pixel, [0x00,R,G,B])
//
void CimgDecode::CalcChannelPreviewFull(QRect *, uint8_t *pTmp)
{
  qDebug() << QString("CimgDecode::CalcChannelPreviewFull");

  if (!m_bImgDecoded) {
      qDebug() << QString("CimgDecode::CalcChannelPreviewFull exit as image not decoded");
      return;
  }
  PixelCc sPixSrc = {};
  PixelCc sPixDst = {};

  QString strTmp;

  uint32_t nRowBytes;

  nRowBytes = m_nImgSizeX * sizeof(QRgb);

  // Color conversion process

  uint32_t nPixMapW = m_nBlkXMax * BLK_SZ_X;
  uint32_t nPixmapInd;
  uint32_t nRngX1, nRngX2, nRngY1, nRngY2;
  uint64_t nSumY;               // 32 bits wrapped above 16.8 million white pixels
  uint64_t nNumPixels = 0;

  // TODO: Update ranges to take into account the visible view region
  // The approach might include:
  //   nRngX1 = pRectView->left;
  //   nRngX2 = pRectView->right;
  //   nRngY1 = pRectView->top;
  //   nRngY2 = pRectView->bottom;
  //
  // These co-ords will define range in nPixX,nPixY that get drawn
  // This will help YCC Adjust display react much faster. Only recalc
  // visible portion of image, but then recalc entire one once Adjust
  // dialog is closed.
  //
  // NOTE: if we were to make these ranges a subset of the
  // full image dimensions, then we'd have to determine the best
  // way to handle the brightest pixel search & average luminance logic
  // since those appear in the nRngX/Y loops.

  nRngX1 = 0;
  nRngX2 = m_nImgSizeX;
  nRngY1 = 0;
  nRngY2 = m_nImgSizeY;

  // Brightest pixel values were already reset during Reset() call, but for
  // safety, do it again here.
  m_bBrightValid = false;
  m_nBrightY = -32768;
  m_nBrightCb = -32768;
  m_nBrightCr = -32768;

  // Average luminance calculation
  m_bAvgYValid = false;
  m_nAvgY = 0;
  nSumY = 0;

  // For IDCT RGB Printout:
  bool bRowStart = false;

  QString strLine;

  uint32_t nMcuShiftInd = m_nPreviewShiftMcuY * (m_nImgSizeX / m_nMcuWidth) + m_nPreviewShiftMcuX;

  SetStatusText("Color conversion...");

  // Only for the preview made by a decode (DecodeScanImg() sets this)
  const bool bRgbDump = m_bDetailRgbDump;

  if(bRgbDump)
  {
    m_pLog->AddLine("  Detailed IDCT Dump (RGB):");
    strTmp = QString("    MCU [%1,%2]:")
        .arg(m_nDetailVlcX, 3)
        .arg(m_nDetailVlcY, 3);
    m_pLog->AddLine(strTmp);

    if(!m_bDecodeScanAc)
    {
      m_pLog->AddLine("    (No IDCT mode: each block shows its DC value only)");
    }
  }

  // Determine pixel count: the loops below run up to, not including, nRngX2
  // and nRngY2. (This counted one extra row and column, so a white 32x32
  // image averaged 239.) Only pixels inside the image count: the decoded area
  // is padded to whole MCUs, and a 9x8 image of 8 black columns and 1 white
  // one averaged 127 instead of 28 with the 7 (white) padding columns.
  const uint32_t nImgX2 = std::min(nRngX2, static_cast<uint32_t>(m_nDimX));
  const uint32_t nImgY2 = std::min(nRngY2, static_cast<uint32_t>(m_nDimY));
  nNumPixels = static_cast<uint64_t>((nImgY2 > nRngY1) ? (nImgY2 - nRngY1) : 0) *
               ((nImgX2 > nRngX1) ? (nImgX2 - nRngX1) : 0);

  qDebug() << QString("CimgDecode::CalcChannelPreviewFull() new QImage X=%1 Y=%2").arg(m_nImgSizeX).arg(m_nImgSizeY);
  // Replace the previous preview (this used to leak one per decode and per
  // View > Image Chan change)
  delete m_pDibTemp;
  m_pDibTemp = new QImage(m_nImgSizeX, m_nImgSizeY, QImage::Format_RGB32);

  // The clipping totals describe this pass over the image. (They used to
  // add up over every recalculation, such as each View > Image Chan change.)
  memset(&m_sStatClip, 0, sizeof(m_sStatClip));

  // Step through the image
  for(int32_t nPixY = nRngY1; nPixY < nRngY2; nPixY++)
  {
    uint32_t nMcuY = nPixY / m_nMcuHeight;

    // DIBs appear to be stored up-side down, so correct Y
    uint32_t nCoordYInv = (m_nImgSizeY - 1) - nPixY;

    for(int32_t nPixX = nRngX1; nPixX < nRngX2; nPixX++)
    {
      nPixmapInd = nPixY * nPixMapW + nPixX;
      uint32_t nPixByte = nPixX * 4 + 0 + nCoordYInv * nRowBytes;
      
      uint32_t nMcuX = nPixX / m_nMcuWidth;
      uint32_t nMcuInd = nMcuY * (m_nImgSizeX / m_nMcuWidth) + nMcuX;

      int32_t nTmpY, nTmpCb, nTmpCr;

      nTmpY = m_pPixValY[nPixmapInd];

      if(m_nNumSosComps == NUM_CHAN_YCC)
      {
        nTmpCb = m_pPixValCb[nPixmapInd];
        nTmpCr = m_pPixValCr[nPixmapInd];
      }
      else
      {
        nTmpCb = 0;
        nTmpCr = 0;
      }

      // RGB JPEG: these are R, G and B. The colour conversion uses them as
      // they are; the rest (statistics, channel views, brightest pixel) gets
      // the equivalent YCbCr. (They were converted as if they were YCbCr, so
      // red showed as (75,255,28).) Pre-ranged values are (level - 128) * 8,
      // so the conversion needs no offsets.
      sPixSrc.bRgbDirect = m_bRgbComps && (m_nNumSosComps == NUM_CHAN_YCC);

      if(sPixSrc.bRgbDirect)
      {
        sPixSrc.nDirectR = nTmpY;
        sPixSrc.nDirectG = nTmpCb;
        sPixSrc.nDirectB = nTmpCr;

        const double fR = nTmpY;
        const double fG = nTmpCb;
        const double fB = nTmpCr;

        nTmpY = static_cast<int32_t>(std::lround(0.299 * fR + 0.587 * fG + 0.114 * fB));
        nTmpCb = static_cast<int32_t>(std::lround(-0.168736 * fR - 0.331264 * fG + 0.5 * fB));
        nTmpCr = static_cast<int32_t>(std::lround(0.5 * fR - 0.418688 * fG - 0.081312 * fB));
      }

      // Load the YCC value
      sPixSrc.nPrerangeY = nTmpY;
      sPixSrc.nPrerangeCb = nTmpCb;
      sPixSrc.nPrerangeCr = nTmpCr;

      // The decoded area is padded to whole MCUs: the padding isn't part of
      // the image, so it is left out of the statistics and clipping counts
      sPixSrc.bInImage = (nPixX < m_nDimX) && (nPixY < m_nDimY);

      // Update brightest pixel search here
      int32_t nBrightness = nTmpY;

      if(sPixSrc.bInImage && (nBrightness > m_nBrightY))
      {
        m_nBrightY = nTmpY;
        m_nBrightCb = nTmpCb;
        m_nBrightCr = nTmpCr;
        m_ptBrightMcu.setX(nMcuX);
        m_ptBrightMcu.setY(nMcuY);
      }

      // FIXME
      // Could speed this up by adding boolean check to see if we are truly needing to do any shifting!
      if(nMcuInd >= nMcuShiftInd)
      {
        sPixSrc.nPrerangeY += m_nPreviewShiftY;
        sPixSrc.nPrerangeCb += m_nPreviewShiftCb;
        sPixSrc.nPrerangeCr += m_nPreviewShiftCr;
      }

      // Invoke the appropriate color conversion routine
      if(m_bHistEn || m_bStatClipEn)
      {
        ConvertYCCtoRGB(nMcuX, nMcuY, sPixSrc);
      }
      else
      {
        ConvertYCCtoRGBFastFloat(sPixSrc);
        //ConvertYCCtoRGBFastFixed(sPixSrc);
      }

      // Accumulate the luminance value for this pixel
      // after we have converted it to the range 0..255
      if(sPixSrc.bInImage)
      {
        nSumY += sPixSrc.nFinalY;
      }

      // If we want a detailed decode of RGB, print it out
      // now if we are on the correct MCU.
      // NOTE: The level shift (adjust) will affect this report!
      if(bRgbDump && (nMcuY == m_nDetailVlcY) && (nMcuX == m_nDetailVlcX))
      {
        if(!bRowStart)
        {
          bRowStart = true;
          strLine = QString("      [ ");
        }

        strTmp = QString("x%1%2%3 ")
            .arg(sPixSrc.nFinalR, 2, 16, QChar('0'))
            .arg(sPixSrc.nFinalG, 2, 16, QChar('0'))
            .arg(sPixSrc.nFinalB, 2, 16, QChar('0'));
        strLine.append(strTmp);
      }

      // Perform any channel filtering if enabled
      ChannelExtract(m_nPreviewMode, sPixSrc, sPixDst);

      // Assign the RGB pixel map
      m_pDibTemp->setPixelColor(nPixX, nPixY, QColor(sPixDst.nFinalR, sPixDst.nFinalG, sPixDst.nFinalB));
    }                           // x

    // End the MCU's row here. (It ended at the next MCU's first pixel, so an
    // MCU in the last column lost its last row.)
    if(bRowStart)
    {
      strLine.append(" ]");
      m_pLog->AddLine(strLine);
      bRowStart = false;
    }
  }                             // y

  SetStatusText("");
  // ---------------------------------------------------------

  if(bRgbDump)
  {
    m_pLog->AddLine("");
  }

  // Assume that brightest pixel search was successful
  // Now compute the RGB value for this pixel!
  m_bBrightValid = true;
  sPixSrc.nPrerangeY = m_nBrightY;
  sPixSrc.nPrerangeCb = m_nBrightCb;
  sPixSrc.nPrerangeCr = m_nBrightCr;
  sPixSrc.bRgbDirect = false;   // these are YCbCr (converted from RGB if need be)
  ConvertYCCtoRGBFastFloat(sPixSrc);
  m_nBrightR = sPixSrc.nFinalR;
  m_nBrightG = sPixSrc.nFinalG;
  m_nBrightB = sPixSrc.nFinalB;

  // Now perform average luminance calculation
  // NOTE: This will result in a value in the range 0..255
  Q_ASSERT(nNumPixels > 0);

  // Avoid divide by zero
  if(nNumPixels == 0)
  {
    nNumPixels = 1;
  }

  m_nAvgY = static_cast<int32_t>(nSumY / nNumPixels);
  m_bAvgYValid = true;
}

// Extract the specified channel
//
// INPUT:
// - nMode                                      = Channel(s) to extract from
// - sSrc                                       = Color representations (YCC & RGB) for pixel
// OUTPUT:
// - sDst                                       = Resulting RGB output after filtering
//
void CimgDecode::ChannelExtract(uint32_t nMode, PixelCc & sSrc, PixelCc & sDst)
{
  if(nMode == PREVIEW_RGB)
  {
    sDst.nFinalR = sSrc.nFinalR;
    sDst.nFinalG = sSrc.nFinalG;
    sDst.nFinalB = sSrc.nFinalB;
  }
  else if(nMode == PREVIEW_YCC)
  {
    sDst.nFinalR = sSrc.nFinalCr;
    sDst.nFinalG = sSrc.nFinalY;
    sDst.nFinalB = sSrc.nFinalCb;
  }
  else if(nMode == PREVIEW_R)
  {
    sDst.nFinalR = sSrc.nFinalR;
    sDst.nFinalG = sSrc.nFinalR;
    sDst.nFinalB = sSrc.nFinalR;
  }
  else if(nMode == PREVIEW_G)
  {
    sDst.nFinalR = sSrc.nFinalG;
    sDst.nFinalG = sSrc.nFinalG;
    sDst.nFinalB = sSrc.nFinalG;
  }
  else if(nMode == PREVIEW_B)
  {
    sDst.nFinalR = sSrc.nFinalB;
    sDst.nFinalG = sSrc.nFinalB;
    sDst.nFinalB = sSrc.nFinalB;
  }
  else if(nMode == PREVIEW_Y)
  {
    sDst.nFinalR = sSrc.nFinalY;
    sDst.nFinalG = sSrc.nFinalY;
    sDst.nFinalB = sSrc.nFinalY;
  }
  else if(nMode == PREVIEW_CB)
  {
    sDst.nFinalR = sSrc.nFinalCb;
    sDst.nFinalG = sSrc.nFinalCb;
    sDst.nFinalB = sSrc.nFinalCb;
  }
  else if(nMode == PREVIEW_CR)
  {
    sDst.nFinalR = sSrc.nFinalCr;
    sDst.nFinalG = sSrc.nFinalCr;
    sDst.nFinalB = sSrc.nFinalCr;
  }
  else
  {
    sDst.nFinalR = sSrc.nFinalR;
    sDst.nFinalG = sSrc.nFinalG;
    sDst.nFinalB = sSrc.nFinalB;
  }
}

// Fetch the detailed decode settings (VLC)
//
// OUTPUT:
// - bDetail                    = Enable for detailed scan VLC reporting
// - nX                                 = Start of detailed scan decode MCU X coordinate
// - nY                                 = Start of detailed scan decode MCU Y coordinate
// - nLen                               = Number of MCUs to parse in detailed scan decode
//
void CimgDecode::GetDetailVlc(bool & bDetail, uint32_t &nX, uint32_t &nY, uint32_t &nLen)
{
  bDetail = m_bDetailVlc;
  nX = m_nDetailVlcX;
  nY = m_nDetailVlcY;
  nLen = m_nDetailVlcLen;
}

// Is the detailed scan decode's first MCU in the image being decoded?
// (PRE: m_nMcuXMax and m_nMcuYMax, set by DecodeScanImg())
bool CimgDecode::DetailVlcInImage() const
{
  return (m_nMcuXMax > 0) && (m_nMcuYMax > 0) &&
         (m_nDetailVlcX < static_cast<uint32_t>(m_nMcuXMax)) && (m_nDetailVlcY < static_cast<uint32_t>(m_nMcuYMax));
}

// Set the detailed scan decode settings (VLC)
//
// INPUT:
// - bDetail                    = Enable for detailed scan VLC reporting
// - nX                                 = Start of detailed scan decode MCU X coordinate
// - nY                                 = Start of detailed scan decode MCU Y coordinate
// - nLen                               = Number of MCUs to parse in detailed scan decode
//
void CimgDecode::SetDetailVlc(bool bDetail, uint32_t nX, uint32_t nY, uint32_t nLen)
{
  m_bDetailVlc = bDetail;
  m_nDetailVlcX = nX;
  m_nDetailVlcY = nY;
  m_nDetailVlcLen = nLen;
}

// Fetch the pointers for the pixel map
//
// OUTPUT:
// - pMayY                              = Pointer to pixel map for Y component
// - pMapCb                             = Pointer to pixel map for Cb component
// - pMapCr                             = Pointer to pixel map for Cr component
//
void CimgDecode::GetPixMapPtrs(int16_t *&pMapY, int16_t *&pMapCb, int16_t *&pMapCr)
{
  Q_ASSERT(m_pPixValY);
  Q_ASSERT(m_pPixValCb);
  Q_ASSERT(m_pPixValCr);
  pMapY = m_pPixValY;
  pMapCb = m_pPixValCb;
  pMapCr = m_pPixValCr;
}

// Get image pixel dimensions rounded up to nearest MCU
//
// OUTPUT:
// - nX                                 = X dimension of preview image
// - nY                                 = Y dimension of preview image
//
void CimgDecode::GetImageSize(uint32_t &nX, uint32_t &nY)
{
  nX = m_nImgSizeX;
  nY = m_nImgSizeY;
}

// The decoded JPEG image's own size (from the SOF), without the padding to
// whole MCUs that the preview and pixel maps have
//
// RETURN:
// - False if no JPEG scan has been decoded
//
bool CimgDecode::GetFrameSize(uint32_t &nX, uint32_t &nY)
{
  if(!m_bImgDecoded || !m_bPreviewIsJpeg || (m_nDimX <= 0) || (m_nDimY <= 0))
  {
    nX = 0;
    nY = 0;
    return false;
  }

  nX = static_cast<uint32_t>(std::min(m_nDimX, m_nImgSizeX));
  nY = static_cast<uint32_t>(std::min(m_nDimY, m_nImgSizeY));
  return true;
}

// A decoded pixel's Y, Cb and Cr as the pixel maps hold them: (level - 128)
// * 8, nominally -1024..1023. A greyscale image has Cb = Cr = 0; for a JPEG
// whose components are R, G and B they are converted, as the preview's
// statistics do.
//
// RETURN:
// - False if no JPEG scan has been decoded, or the pixel is outside it
//
bool CimgDecode::GetPixelYcc(uint32_t nPixX, uint32_t nPixY, int32_t &nValY, int32_t &nValCb, int32_t &nValCr)
{
  if(!m_bImgDecoded || !m_bPreviewIsJpeg || !m_pPixValY || (m_nBlkXMax <= 0) || (m_nBlkYMax <= 0))
  {
    return false;
  }

  const uint32_t nPixMapW = static_cast<uint32_t>(m_nBlkXMax) * BLK_SZ_X;
  const uint32_t nPixMapH = static_cast<uint32_t>(m_nBlkYMax) * BLK_SZ_Y;

  if((nPixX >= nPixMapW) || (nPixY >= nPixMapH))
  {
    return false;
  }

  const size_t nInd = static_cast<size_t>(nPixY) * nPixMapW + nPixX;
  const bool bColour = (m_nNumSosComps == NUM_CHAN_YCC) && m_pPixValCb && m_pPixValCr;

  nValY = m_pPixValY[nInd];
  nValCb = bColour ? m_pPixValCb[nInd] : 0;
  nValCr = bColour ? m_pPixValCr[nInd] : 0;

  if(bColour && m_bRgbComps)
  {
    const double fR = nValY;
    const double fG = nValCb;
    const double fB = nValCr;

    nValY = static_cast<int32_t>(std::lround(0.299 * fR + 0.587 * fG + 0.114 * fB));
    nValCb = static_cast<int32_t>(std::lround(-0.168736 * fR - 0.331264 * fG + 0.5 * fB));
    nValCr = static_cast<int32_t>(std::lround(0.5 * fR - 0.418688 * fG - 0.081312 * fB));
  }

  return true;
}

// Get the bitmap pointer
//
// OUTPUT:
// - pBitmap                    = Bitmap (DIB) of preview
//
void CimgDecode::GetBitmapPtr(uint8_t *&pBitmap)
{
  uint8_t *pDibImgTmpBits = 0;

//@@  pDibImgTmpBits = (uint8_t *) (m_pDibTemp.GetDIBBitArray());

  // Ensure that the pointers are available!
  if(!pDibImgTmpBits)
  {
    pBitmap = 0;
  }
  else
  {
    pBitmap = pDibImgTmpBits;
  }
}

// Calculate RGB pixel map from selected channels of YCC pixel map
//
// PRE:
// - m_pPixValY
// - m_pPixValCb
// - m_pPixValCr
// POST:
// - m_pDibTemp
// NOTE:
// - Channels are selected in CalcChannelPreviewFull()
//
void CimgDecode::CalcChannelPreview()
{
  qDebug() << QString("CimgDecode::CalcChannelPreview");
  uint8_t *pDibImgTmpBits = 0;

//@@  pDibImgTmpBits = (uint8_t *) (m_pDibTemp.GetDIBBitArray());

  // Ensure that the pointers are available!
//  if(!pDibImgTmpBits)
//  {
//    return;
//  }

  qDebug() << QString("CimgDecode::CalcChannelPreview DibTempReady=%1").arg(m_bDibTempReady);

  // If we need to do a YCC shift, then do full recalc into tmp array
  CalcChannelPreviewFull(0, pDibImgTmpBits);

  // Since this was a complex mod, we don't mark this channel as
  // being "done", so we will need to recalculate any time we change
  // the channel display.

  // Force an update of the view to be sure
  //m_pDoc->UpdateAllViews(0);

  return;
}

// Determine the file position from a pixel coordinate (of the decoded
// image, which is padded to whole MCUs)
//
// INPUT:
// - p                                          = Pixel coordinate
// OUTPUT:
// - nByte                                      = File offset (byte)
// - nBit                                       = File offset (bit)
// RETURN:
// - False (and 0) if the pixel is outside the image or its MCU wasn't decoded
//
bool CimgDecode::LookupFilePosPix(QPoint p, uint32_t &nByte, uint32_t &nBit)
{
  // (It didn't check the map or the range: with no scan decoded, Tools >
  // Lookup MCU Offset read a null map)
  if((p.x() < 0) || (p.y() < 0) || (p.x() >= m_nImgSizeX) || (p.y() >= m_nImgSizeY))
  {
    nByte = 0;
    nBit = 0;
    return false;
  }

  return LookupFilePosMcu(PixelToMcu(p), nByte, nBit);
}

// Determine the file position from a MCU coordinate
//
// INPUT:
// - nMcuX                                      = MCU coordinate (x)
// - nMcuY                                      = MCU coordinate (y)
// OUTPUT:
// - nByte                                    = File offset (byte)
// - nBit                                       = File offset (bit)
//
bool CimgDecode::LookupFilePosMcu(QPoint p, uint32_t &nByte, uint32_t &nBit)
{
  nByte = 0;
  nBit = 0;

  // The map is freed on Reset() and the point can come from the mouse
  if((!m_pMcuFileMap) || (p.x() < 0) || (p.y() < 0) || (p.x() >= m_nMcuXMax) || (p.y() >= m_nMcuYMax))
  {
    return false;
  }

  const uint64_t nPacked = m_pMcuFileMap[p.x() + p.y() * m_nMcuXMax];
  if(nPacked == kMcuNotDecoded)
  {
    return false;
  }

  UnpackFileOffset(nPacked, nByte, nBit);
  return true;
}

// Determine the YCC DC value of a specified block
//
// INPUT:
// - nBlkX                                      = 8x8 block coordinate (x)
// - nBlkY                                      = 8x8 block coordinate (y)
// OUTPUT:
// - nY                                         = Y channel value
// - nCb                                        = Cb channel value
// - nCr                                        = Cr channel value
//
void CimgDecode::LookupBlkYCC(QPoint p, int32_t &nY, int32_t &nCb, int32_t &nCr)
{
  // The maps are freed on Reset() and the point can come from the mouse
  if((!m_pBlkDcValY) || (p.x() < 0) || (p.y() < 0) || (p.x() >= m_nBlkXMax) || (p.y() >= m_nBlkYMax))
  {
    nY = 0;
    nCb = 0;
    nCr = 0;
    return;
  }

  nY = m_pBlkDcValY[p.x() + p.y() * m_nBlkXMax];

  if((m_nNumSosComps == NUM_CHAN_YCC) && m_pBlkDcValCb && m_pBlkDcValCr)
  {
    nCb = m_pBlkDcValCb[p.x() + p.y() * m_nBlkXMax];
    nCr = m_pBlkDcValCr[p.x() + p.y() * m_nBlkXMax];
  }
  else
  {
    nCb = 0;                    // FIXME
    nCr = 0;                    // FIXME
  }
}

// Convert pixel coordinate to MCU coordinate
//
// INPUT:
// - ptPix                                      = Pixel coordinate
// RETURN:
// - MCU coordinate
//
QPoint CimgDecode::PixelToMcu(QPoint ptPix)
{
  QPoint ptMcu(0, 0);

  // MCU size is zero until a scan has been decoded
  if((m_nMcuWidth <= 0) || (m_nMcuHeight <= 0))
  {
    return ptMcu;
  }

  ptMcu.setX(ptPix.x() / m_nMcuWidth);
  ptMcu.setY(ptPix.y() / m_nMcuHeight);
  return ptMcu;
}

// Convert pixel coordinate to block coordinate
//
// INPUT:
// - ptPix                                      = Pixel coordinate
// RETURN:
// - 8x8 block coordinate
//
QPoint CimgDecode::PixelToBlk(QPoint ptPix)
{
  QPoint ptBlk;

  ptBlk.setX(ptPix.x() / BLK_SZ_X);
  ptBlk.setY(ptPix.y() / BLK_SZ_Y);
  return ptBlk;
}

// Return the linear MCU offset from an MCU X,Y coord
//
// INPUT:
// - ptMcu                                      = MCU coordinate
// PRE:
// - m_nMcuXMax
// RETURN:
// - Index of MCU from start of MCUs
//
uint32_t CimgDecode::McuXyToLinear(QPoint ptMcu)
{
  uint32_t nLinear;

  nLinear = ((ptMcu.y() * m_nMcuXMax) + ptMcu.x());
  return nLinear;
}

// Record where the MCU marked by m_nMcuPosPending starts in the file
// (PackFileOffset() notation), as its first code is read
//
// INPUT:
// - nPos                       = File position of the code's first bit (byte)
// - nAlign                     = File position of the code's first bit (bit)
//
void CimgDecode::RecordMcuPos(uint32_t nPos, uint32_t nAlign)
{
  // From the marker that ends the scan data (or the end of the file) on,
  // the decoder reads bytes that aren't this scan's: an MCU that starts
  // there has no data, so it isn't recorded. (After the scan stops in a row,
  // the decoder goes on to the first MCU of each row after it, and recorded
  // its position: past the end of the file, or at the EOI marker.)
  if(m_pMcuFileMap && (nPos < m_nScanDataEnd))
  {
    m_pMcuFileMap[m_nMcuPosPending] = PackFileOffset(nPos, nAlign);
  }

  m_nMcuPosPending = -1;
}

// Create a file offset notation that represents bytes and bits
// - Essentially a fixed-point notation
//
// INPUT:
// - nByte                            = File byte position
// - nBit                               = File bit position
// RETURN:
// - Fixed-point file offset (60b for bytes, 4b for bits)
//
uint64_t CimgDecode::PackFileOffset(uint32_t nByte, uint32_t nBit)
{
  // Note that we only really need 3 bits, but I'll keep 4
  // so that the file offset is human readable. (In 32 bits that left 28 for
  // the byte, so a JPEG over 256 MB into a file got the wrong offsets.)
  return (static_cast<uint64_t>(nByte) << 4) + nBit;
}

// Convert from file offset notation to bytes and bits
//
// INPUT:
// - nPacked                    = Fixed-point file offset (60b for bytes, 4b for bits)
// OUTPUT:
// - nByte                            = File byte position
// - nBit                               = File bit position
//
void CimgDecode::UnpackFileOffset(uint64_t nPacked, uint32_t &nByte, uint32_t &nBit)
{
  nBit = static_cast<uint32_t>(nPacked & 0x7);
  nByte = static_cast<uint32_t>(nPacked >> 4);
}

// Fetch the number of block markers assigned
//
// RETURN:
// - Number of marker blocks
//
uint32_t CimgDecode::GetMarkerCount()
{
  return m_nMarkersBlkNum;
}

// Fetch an indexed block marker
//
// INPUT:
// - nInd                                       = Index into marker block array
// RETURN:
// - Point (8x8 block) from marker array
//
QPoint CimgDecode::GetMarkerBlk(uint32_t nInd)
{
  QPoint myPt(0, 0);

  if(nInd < m_nMarkersBlkNum)
  {
    myPt = m_aptMarkersBlk[nInd];
  }
  else
  {
    Q_ASSERT(false);
  }
  return myPt;
}

// Add a block to the block marker list
// - Also report out the YCC DC value for the block
//
// INPUT:
// - nBlkX                                      = 8x8 block X coordinate
// - nBlkY                                      = 8x8 block Y coordinate
// POST:
// - m_nMarkersBlkNum
// - m_aptMarkersBlk[]
//
void CimgDecode::SetMarkerBlk(QPoint p)
{
  // Only mark blocks of a decoded image (p comes from a mouse click, and the
  // MCU size below is zero before a scan is decoded)
  if((!m_bImgDecoded) || (m_nMcuWidth < BLK_SZ_X) || (m_nMcuHeight < BLK_SZ_Y) ||
     (p.x() < 0) || (p.y() < 0) || (p.x() >= m_nBlkXMax) || (p.y() >= m_nBlkYMax))
  {
    return;
  }

  if(m_nMarkersBlkNum == MAX_BLOCK_MARKERS)
  {
    // Shift them down by 1. Last entry will be deleted next
    for(uint32_t ind = 1; ind < MAX_BLOCK_MARKERS; ind++)
    {
      m_aptMarkersBlk[ind - 1] = m_aptMarkersBlk[ind];
    }

    // Reflect new reduced count
    m_nMarkersBlkNum--;
  }

  QString strTmp;

  int32_t nY, nCb, nCr;
  int32_t nMcuX, nMcuY;
  int32_t nCssX, nCssY;

  // Determine the YCC of the block
  // Then calculate the MCU coordinate and the block coordinate within the MCU
  LookupBlkYCC(p, nY, nCb, nCr);
  nMcuX = p.x() / (m_nMcuWidth / BLK_SZ_X);
  nMcuY = p.y() / (m_nMcuHeight / BLK_SZ_Y);
  nCssX = p.x() % (m_nMcuWidth / BLK_SZ_X);
  nCssY = p.y() % (m_nMcuHeight / BLK_SZ_X);

  strTmp = QString("Position Marked @ MCU=[%1,%2](%3,%4) Block=[%5,%6] YCC=[%7,%8,%9]")
      .arg(nMcuX, 4)
      .arg(nMcuY, 4)
      .arg(nCssX)
      .arg(nCssY)
      .arg(p.x(), 4)
      .arg(p.y(), 4)
      .arg(nY, 5)
      .arg(nCb, 5)
      .arg(nCr, 5);
  m_pLog->AddLine(strTmp);
  m_pLog->AddLine("");

  m_aptMarkersBlk[m_nMarkersBlkNum] = p;
  m_nMarkersBlkNum++;
}

// Fetch the preview zoom mode
//
// RETURN:
// - The zoom mode enumeration
//
uint32_t CimgDecode::GetPreviewZoomMode()
{
  return m_nZoomMode;
}

// Fetch the preview mode
//
// RETURN:
// - The image preview mode enumeration
//
uint32_t CimgDecode::GetPreviewMode()
{
  return m_nPreviewMode;
}

// Fetch the preview zoom level
//
// RETURN:
// - The preview zoom level enumeration
//
double CimgDecode::GetPreviewZoom()
{
  return m_nZoom;
}

// Change the current zoom level
// - Supports either direct setting of the level
//   or an increment/decrement operation
//
// INPUT:
// - bInc                               = Flag to increment the zoom level
// - bDec                               = Flag to decrement the zoom level
// - bSet                               = Flag to set the zoom level
// - nVal                               = Zoom level for "set" operation
// POST:
// - m_nZoomMode
//
void CimgDecode::SetPreviewZoom(bool bInc, bool bDec, bool bSet, uint32_t nVal)
{
  if(bInc)
  {
    if(m_nZoomMode + 1 < PRV_ZOOMEND)
    {
      m_nZoomMode++;

    }
  }
  else if(bDec)
  {
    if(m_nZoomMode - 1 > PRV_ZOOMBEGIN)
    {
      m_nZoomMode--;
    }
  }
  else if(bSet)
  {
    m_nZoomMode = nVal;
  }
  
  switch (m_nZoomMode)
  {
    case PRV_ZOOM_12:
      m_nZoom = 0.125;
      break;

    case PRV_ZOOM_25:
      m_nZoom = 0.25;
      break;

    case PRV_ZOOM_50:
      m_nZoom = 0.5;
      break;

    case PRV_ZOOM_100:
      m_nZoom = 1.0;
      break;

    case PRV_ZOOM_150:
      m_nZoom = 1.5;
      break;

    case PRV_ZOOM_200:
      m_nZoom = 2.0;
      break;

    case PRV_ZOOM_300:
      m_nZoom = 3.0;
      break;

    case PRV_ZOOM_400:
      m_nZoom = 4.0;
      break;

    case PRV_ZOOM_800:
      m_nZoom = 8.0;
      break;

    default:
      m_nZoom = 1.0;
      break;
  }
}

// Main draw routine for the image
// - Draws the preview image with frame
// - Draws any histogram
// - Draws a title
// - Draws any MCU overlays / grid
//
// INPUT:
// - pDC                                        = The device context pointer
// - rectClient                         = From GetClientRect()
// - ptScrolledPos                      = From GetScrollPosition()
// - pFont                                      = Pointer to the font used for title/lables
// OUTPUT:
// - szNewScrollSize            = New dimension used for SetScrollSizes()
//
void CimgDecode::ViewOnDraw(QPainter *, QRect, QPoint, QFont *, QSize &)
//void CimgDecode::ViewOnDraw(QPainter * pDC, QRect rectClient, QPoint ptScrolledPos, QFont * pFont, QSize & szNewScrollSize)
{
//  m_nPageWidth = 600;
//  m_nPageHeight = 10;           // Start with some margin

//  QString strTmp;
//  QString strRender;

//  int32_t nHeight;

//  uint32_t nYPosImgTitle = 0;
//  uint32_t nYPosImg = 0;
//  uint32_t nYPosHistTitle = 0;
//  uint32_t nYPosHist = 0;
//  uint32_t nYPosHistYTitle = 0;
//  uint32_t nYPosHistY = 0;      // Y position of Img & Histogram

//  QRect rectTmp;

//  qDebug() << "CimgDecode::ViewOnDraw Start";

//  // If we have displayed an image, make sure to allow for
//  // the additional space!

//  bool bImgDrawn = false;

////  QBrush brGray(QColor(128, 128, 128));
////  QBrush brGrayLt1(QColor(210, 210, 210));
////  QBrush brGrayLt2(QColor(240, 240, 240));
////  QBrush brBlueLt(QColor(240, 240, 255));

//  QColor brGray = QColor(128, 128, 128);
//  QColor brGrayLt1 = QColor(210, 210, 210);
//  QColor brGrayLt2 = QColor(240, 240, 240);
//  QColor brBlueLt = QColor(240, 240, 255);

//  QPen penRed(Qt::red, 1, Qt::DotLine);

//  if(m_bDibTempReady)
//  {
//    qDebug() << "CimgDecode::ViewOnDraw CP1";

//    nYPosImgTitle = m_nPageHeight;
//    m_nPageHeight += nTitleHeight;      // Margin at top for title

//    m_rectImgReal = QRect(0, 0, static_cast<int32_t>(m_rectImgBase.right() * m_nZoom), static_cast<int32_t>(m_rectImgBase.bottom() * m_nZoom));

//    nYPosImg = m_nPageHeight;
//    m_nPageHeight += m_rectImgReal.height();
//    m_nPageHeight += nBorderBottom;     // Margin at bottom
//    bImgDrawn = true;

//    m_rectImgReal.adjust(nBorderLeft, nYPosImg, 0, 0);

//    // Now create the shadow of the main image
////!!    rectTmp = m_rectImgReal;
////!!    rectTmp.adjust(4, 4, 0, 0);
////!!    pDC->fillRect(rectTmp, QBrush(brGrayLt1));
////!!    rectTmp.adjust(-1, -1, 1, 1);
////!!    pDC->FrameRect(rectTmp, &brGrayLt2);
//  }

//  if(m_bHistEn)
//  {
//    if(m_bDibHistRgbReady)
//    {
//      nYPosHistTitle = m_nPageHeight;
//      m_nPageHeight += nTitleHeight;    // Margin at top for title

//      m_rectHistReal = m_rectHistBase;

//      nYPosHist = m_nPageHeight;
//      m_nPageHeight += m_rectHistReal.height();
//      m_nPageHeight += nBorderBottom;   // Margin at bottom
//      bImgDrawn = true;

//      m_rectHistReal.adjust(nBorderLeft, nYPosHist, 0, 0);

//      // Create the border
//      rectTmp = m_rectHistReal;
//      rectTmp.adjust(-1, -1, 1, 1);
//      pDC->setPen(QPen(brGray));
//      pDC->drawRect(rectTmp);
//    }

//    if(m_bDibHistYReady)
//    {
//      nYPosHistYTitle = m_nPageHeight;
//      m_nPageHeight += nTitleHeight;    // Margin at top for title

//      m_rectHistYReal = m_rectHistYBase;

//      nYPosHistY = m_nPageHeight;
//      m_nPageHeight += m_rectHistYReal.height();
//      m_nPageHeight += nBorderBottom;   // Margin at bottom
//      bImgDrawn = true;

//      m_rectHistYReal.adjust(nBorderLeft, nYPosHistY, 0, 0);

//      // Create the border
//      rectTmp = m_rectHistYReal;
//      rectTmp.adjust(-1, -1, 1, 1);
//      pDC->setPen(QPen(brGray));
//      pDC->drawRect(rectTmp);
//    }
//  }

//  // Find a starting line based on scrolling
//  // and current client size

//  QRect rectClientScrolled = rectClient;

//  rectClientScrolled.adjust(ptScrolledPos.x(), ptScrolledPos.y(), 0, 0);

//  // Change the font
//  QFont pOldFont;

//  pOldFont = pDC->font();

//  qDebug() << "Title 1";

//  // Draw the bitmap if ready
//  if(m_bDibTempReady)
//  {
//    // Print label
//    qDebug() << "Title 2 m_bDibTempReady";

//    if(!m_bPreviewIsJpeg)
//    {
//      // For all non-JPEG images, report with simple title
//      m_strTitle = "Image";
//    }
//    else
//    {
//      qDebug() << "Title 3 m_bPreviewIsJpeg";
//      m_strTitle = "Image (";

//      switch (m_nPreviewMode)
//      {
//        case PREVIEW_RGB:
//          m_strTitle += "RGB";
//          break;

//        case PREVIEW_YCC:
//          m_strTitle += "YCC";
//          break;

//        case PREVIEW_R:
//          m_strTitle += "R";
//          break;

//        case PREVIEW_G:
//          m_strTitle += "G";
//          break;

//        case PREVIEW_B:
//          m_strTitle += "B";
//          break;

//        case PREVIEW_Y:
//          m_strTitle += "Y";
//          break;

//        case PREVIEW_CB:
//          m_strTitle += "Cb";
//          break;

//        case PREVIEW_CR:
//          m_strTitle += "Cr";
//          break;

//        default:
//          m_strTitle += "???";
//          break;
//      }

//      if(m_bDecodeScanAc)
//      {
//        m_strTitle += ", DC+AC)";
//      }
//      else
//      {
//        m_strTitle += ", DC)";
//      }
//    }

//    switch (m_nZoomMode)
//    {
//      case PRV_ZOOM_12:
//        m_strTitle += " @ 12.5% (1/8)";
//        break;

//      case PRV_ZOOM_25:
//        m_strTitle += " @ 25% (1/4)";
//        break;

//      case PRV_ZOOM_50:
//        m_strTitle += " @ 50% (1/2)";
//        break;

//      case PRV_ZOOM_100:
//        m_strTitle += " @ 100% (1:1)";
//        break;

//      case PRV_ZOOM_150:
//        m_strTitle += " @ 150% (3:2)";
//        break;

//      case PRV_ZOOM_200:
//        m_strTitle += " @ 200% (2:1)";
//        break;

//      case PRV_ZOOM_300:
//        m_strTitle += " @ 300% (3:1)";
//        break;

//      case PRV_ZOOM_400:
//        m_strTitle += " @ 400% (4:1)";
//        break;

//      case PRV_ZOOM_800:
//        m_strTitle += " @ 800% (8:1)";
//        break;

//      default:
//        m_strTitle += "";
//        break;
//    }

//    // Calculate the title width
//    QRect rectCalc = QRect(0, 0, 0, 0);

////@@            nHeight = pDC->DrawText(m_strTitle,-1,&rectCalc,DT_CALQRect | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
//    int32_t nWidth = rectCalc.width() + 2 * nTitleIndent;

//    // Determine the title area (could be larger than the image
//    // if the image zoom is < 100% or sized small)

//    // Draw title background
//    rectTmp = QRect(m_rectImgReal.left(), nYPosImgTitle,
//                    qMax(m_rectImgReal.right(), m_rectImgReal.left() + nWidth), m_rectImgReal.top() - nTitleLowGap);
//    pDC->fillRect(rectTmp, brBlueLt);

//    // Draw the title
//    Qt::BGMode nBkMode = pDC->backgroundMode();

//    pDC->setBackgroundMode(Qt::TransparentMode);
//    rectTmp.adjust(nTitleIndent, 0, 0, 0);
////@@            nHeight = pDC->DrawText(m_strTitle,-1,&rectTmp,DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
//    pDC->setBackgroundMode(nBkMode);

//    // Draw image

//    // Assume that the temp image has already been generated!
//    // For speed purposes, we use m_pDibTemp only when we are
//    // in a mode other than RGB or YCC. In the RGB/YCC modes,
//    // we skip the CalcChannelPreview() step.

//    // TODO: Improve redraw time by only redrawing the currently visible
//    // region. Requires a different CopyDIB routine with a subset region.
//    // Needs to take into account zoom setting (eg. intersection between
//    // rectClient and m_rectImgReal).

//    // Use a common DIB instead of creating/swapping tmp / ycc and rgb.
//    // This way we can also have more flexibility in modifying RGB & YCC displays.
//    // Time calling CalcChannelPreview() seems small, so no real impact.

//    // Image member usage:
//    // m_pDibTemp:

////@@            m_pDibTemp.CopyDIB(pDC,m_rectImgReal.left,m_rectImgReal.top,m_nZoom);

//    // Now create overlays

//    // Only draw overlay (eg. actual image boundary overlay) if the values
//    // have been set properly. Note that PSD decode currently sets these
//    // values to zero.
//    if((m_nDimX != 0) && (m_nDimY != 0))
//    {
//      pDC->setPen(penRed);

//      // Draw boundary for end of valid data (inside partial MCU)
//      int32_t nXZoomed = static_cast<int32_t>(m_nDimX * m_nZoom);

//      int32_t nYZoomed = static_cast<int32_t>(m_nDimY * m_nZoom);

//      pDC->drawLine(m_rectImgReal.left() + nXZoomed, m_rectImgReal.top(), m_rectImgReal.left() + nXZoomed, m_rectImgReal.top() + nYZoomed);
//      pDC->drawLine(m_rectImgReal.left(), m_rectImgReal.top() + nYZoomed, m_rectImgReal.left() + nXZoomed, m_rectImgReal.top() + nYZoomed);
////!!      pDC->setPen(pPen);
//    }

//    // Before we frame the region, let's draw any remaining overlays

//    // Only draw the MCU overlay if zoom is > 100%, otherwise we will have
//    // replaced entire image with the boundary lines!
//    if(m_bViewOverlaysMcuGrid && (m_nZoomMode >= PRV_ZOOM_25))
//    {
//      ViewMcuOverlay(pDC);
//    }

//    // Always draw the markers
//    ViewMcuMarkedOverlay(pDC);

//    // Final frame border
//    rectTmp = m_rectImgReal;
//    rectTmp.adjust(-1, -1, 1, 1);
////@@    pDC->setPen(brGray);
//    pDC->drawRect(rectTmp);

//    // Preserve the image placement (for other functions, such as click detect)
//    m_nPreviewPosX = m_rectImgReal.left();
//    m_nPreviewPosY = m_rectImgReal.top();
//    m_nPreviewSizeX = m_rectImgReal.width();
//    m_nPreviewSizeY = m_rectImgReal.height();

//    // m_nPageWidth is already hardcoded above
//    Q_ASSERT(m_nPageWidth >= 0);
//    m_nPageWidth = qMax(static_cast<uint32_t>(m_nPageWidth), m_nPreviewPosX + m_nPreviewSizeX);

//    update();
//  }

//  if(m_bHistEn)
//  {
//    if(m_bDibHistRgbReady)
//    {
//      // Draw title background
//      rectTmp = QRect(m_rectHistReal.left(), nYPosHistTitle, m_rectHistReal.right(), m_rectHistReal.top() - nTitleLowGap);
//      pDC->fillRect(rectTmp, brBlueLt);

//      // Draw the title
//      QRect boundingRect;
//      Qt::BGMode nBkMode = pDC->backgroundMode();
//      pDC->setBackgroundMode(Qt::TransparentMode);
//      rectTmp.adjust(nTitleIndent, 0, 0, 0);
//      pDC->drawText(rectTmp, Qt::AlignLeft | Qt::AlignTop, "Histogram (RGB)", &boundingRect);
//      pDC->setBackgroundMode(nBkMode);

//      // Draw image
////@@                    m_pDibHistRgb.CopyDIB(pDC,m_rectHistReal.left,m_rectHistReal.top);
//    }

//    if(m_bDibHistYReady)
//    {
//      // Draw title background
//      rectTmp = QRect(m_rectHistYReal.left(), nYPosHistYTitle, m_rectHistYReal.right(), m_rectHistYReal.top() - nTitleLowGap);
//      pDC->fillRect(rectTmp, brBlueLt);

//      // Draw the title
//      Qt::BGMode nBkMode = pDC->backgroundMode();
//      pDC->setBackgroundMode(Qt::TransparentMode);
//      rectTmp.adjust(nTitleIndent, 0, 0, 0);
////@@                    nHeight = pDC->DrawText("Histogram (Y)",-1,&rectTmp,DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
//      pDC->setBackgroundMode(nBkMode);

//      // Draw image
////@@                    m_pDibHistY.CopyDIB(pDC,m_rectHistYReal.left,m_rectHistYReal.top);
//      //m_pDibHistY.CopyDIB(pDC,nBorderLeft,nYPosHistY);
//    }

//    update();
//  }

//  // If no image has been drawn, indicate to user why!
//  if(!bImgDrawn)
//  {
//    //ScrollRect.top = m_nPageHeight;       // FIXME:?

//    // Print label
//    //nHeight = pDC->DrawText("Image Decode disabled. Enable with [Options->Decode Scan Image]", -1,&ScrollRect,
//    //      DT_TOP | DT_NOPREFIX | DT_SINGLELINE);

//  }

//  // Restore the original font
//  pDC->setFont(pOldFont);

//  // Set scroll bars accordingly. We use the page dimensions here.
//  QSize sizeTotal(m_nPageWidth + nBorderLeft, m_nPageHeight);

//  szNewScrollSize = sizeTotal;
}

// (Windows' ViewMcuOverlay(), which drew the MCU grid here, is now
// PreviewCanvas::paintMcuGrid() in Viewer.cpp)

// Draw an overlay that highlights the marked MCUs
//
// INPUT:
// - pDC                        = The device context pointer
//
void CimgDecode::ViewMcuMarkedOverlay(QPainter * pDC)
{
  pDC;                          // Unreferenced param

  // Now draw a simple MCU Marker overlay
  QRect my_rect;

  QBrush my_brush(QColor(255, 0, 255));

  for(uint32_t nMcuY = 0; nMcuY < m_nMcuYMax; nMcuY++)
  {
    for(uint32_t nMcuX = 0; nMcuX < m_nMcuXMax; nMcuX++)
    {
      //uint32_t nXY = nMcuY*m_nMcuXMax + nMcuX;
      //uint32_t nXZoomed = (uint32_t)(nMcuX*m_nMcuWidth*m_nZoom);
      //uint32_t nYZoomed = (uint32_t)(nMcuY*m_nMcuHeight*m_nZoom);

      /*
         // TODO: Implement an overlay function
         if (m_bMarkedMcuMapEn && m_abMarkedMcuMap && m_abMarkedMcuMap[nXY]) {
         // Note that drawing is an overlay, so we are dealing with real
         // pixel coordinates, not preview image coords
         my_rect = QRect(nXZoomed,nYZoomed,nXZoomed+(uint32_t)(m_nMcuWidth*m_nZoom),
         nYZoomed+(uint32_t)(m_nMcuHeight*m_nZoom));
         my_rect.adjust(m_rectImgReal.left,m_rectImgReal.top);
         pDC->FrameRect(my_rect,&my_brush);
         }
       */

    }
  }

}

// Draw an overlay for the indexed block
//
// INPUT:
// - pDC                        = The device context pointer
// - nBlkX                      = 8x8 block X coordinate
// - nBlkY                      = 8x8 block Y coordinate
//
void CimgDecode::ViewMarkerOverlay(QPainter * pDC, uint32_t nBlkX, uint32_t nBlkY)
{
  QRect my_rect;

  // Note that drawing is an overlay, so we are dealing with real
  // pixel coordinates, not preview image coords
  my_rect = QRect(static_cast<int32_t>(m_nZoom * (nBlkX + 0)),
                  static_cast<int32_t>(m_nZoom * (nBlkY + 0)),
                  static_cast<int32_t>(m_nZoom * (nBlkX + 1)),
                  static_cast<int32_t>(m_nZoom * (nBlkY + 1)));
  my_rect.adjust(m_nPreviewPosX, m_nPreviewPosY, 0, 0);
  pDC->fillRect(my_rect, QColor(255, 0, 255));
}

// Get the preview MCU grid setting
//
// OUTPUT:
// - The MCU grid enabled flag
//
bool CimgDecode::GetPreviewOverlayMcuGrid()
{
  return m_bViewOverlaysMcuGrid;
}

// Set the preview MCU grid setting (Windows toggled it)
//
// POST:
// - m_bViewOverlaysMcuGrid
//
void CimgDecode::SetPreviewOverlayMcuGrid(bool bOn)
{
  m_bViewOverlaysMcuGrid = bOn;
}

// The MCU's size in image pixels, for the last scan decoded (1x1 before any)
QSize CimgDecode::GetMcuSize()
{
  return QSize(m_nMcuWidth, m_nMcuHeight);
}

// Report the DC levels
// - UNUSED
//
// INPUT:
// - nMcuX                              = MCU x coordinate
// - nMcuY                              = MCU y coordinate
// - nMcuLen                    = Number of MCUs to report
//
void CimgDecode::ReportDcRun(uint32_t nMcuX, uint32_t nMcuY, uint32_t nMcuLen)
{
  // FIXME: Should I be working on MCU level or block level?
  QString strTmp;

  m_pLog->AddLine("");
  m_pLog->AddLineHdr("*** Reporting DC Levels ***");
  strTmp = QString("  Starting MCU   = [%1,%2]")
      .arg(nMcuX)
      .arg(nMcuY);
  strTmp = QString("  Number of MCUs = %1").arg(nMcuLen);
  m_pLog->AddLine(strTmp);
  for(uint32_t ind = 0; ind < nMcuLen; ind++)
  {
    // TODO: Need some way of getting these values
    // For now, just rely on bDetailVlcEn & PrintDcCumVal() to report this...
  }
}

// Update the YCC status text
//
// INPUT:
// - strText                    = Status text to display
//
void CimgDecode::SetStatusYccText(QString strText)
{
  m_strStatusYcc = strText;
}

// Fetch the YCC status text
//
// RETURN:
// - Status text currently displayed
//
QString CimgDecode::GetStatusYccText()
{
  return m_strStatusYcc;
}

// Update the MCU status text
//
// INPUT:
// - strText                            = MCU indicator text
//
void CimgDecode::SetStatusMcuText(QString strText)
{
  m_strStatusMcu = strText;
}

// Fetch the MCU status text
//
// RETURN:
// - MCU indicator text
//
QString CimgDecode::GetStatusMcuText()
{
  return m_strStatusMcu;
}

// Update the file position text
//
// INPUT:
// - strText                            = File position text
//
void CimgDecode::SetStatusFilePosText(QString strText)
{
  m_strStatusFilePos = strText;
}

// Fetch the file position text
//
// RETURN:
// - File position text
//
QString CimgDecode::GetStatusFilePosText()
{
  return m_strStatusFilePos;
}
