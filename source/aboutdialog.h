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
// - Help > About JPEGsnoop (on macOS, JPEGsnoop > About JPEGsnoop): the
//   icon, version and copyright, and links to the documentation
//
// ==========================================================================

#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>
#include <QPixmap>

class AboutDialog : public QDialog
{
  Q_OBJECT

public:
  explicit AboutDialog(QWidget *pParent = nullptr);

  // The documentation page (with the version, as on Windows) and the site
  static QString docUrl();
  static QString siteUrl();

  // The app icon's best 48-pixel frame, enlarged by a whole number for the
  // screen's pixel ratio so the pixel art stays sharp
  static QPixmap iconPixmap(qreal dDevicePixelRatio);
};

#endif // ABOUTDIALOG_H
