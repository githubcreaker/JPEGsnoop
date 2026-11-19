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

#import <AppKit/AppKit.h>

#include <QtGlobal>

#include "cmdline.h"

// A command-line run needs QApplication (the report is a text widget), which
// on macOS makes the app a foreground app: a Dock icon that bounces until it
// quits, an entry in the app switcher, and the menu bar. Qt makes it one
// unless this variable is set; and since the app is in a bundle, the system
// makes it one anyway when it first connects, unless the app has asked to be
// a background app before then. Both are done here, before QApplication.
void prepareCmdLineApp()
{
  qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
  [[NSApplication sharedApplication] setActivationPolicy:NSApplicationActivationPolicyProhibited];
}
