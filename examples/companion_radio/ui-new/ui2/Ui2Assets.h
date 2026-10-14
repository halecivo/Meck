#pragma once
// =============================================================================
// UI v2 -- access to the design system's fonts and icons
//
// The font and icon data live in Ui2Assets.cpp only, so they are stored in
// flash once no matter how many screens use them.
// =============================================================================

#include <Adafruit_GFX.h>
#include "Ui2Icons.h"

// Text roles of the design system (Noto Sans for text, Montserrat for numerals)
enum Ui2Font : uint8_t {
  UI2_F_META,     // Noto 10         -- secondary info, timestamps, hints
  UI2_F_SMALL,    // Noto 11         -- dense values
  UI2_F_BODY,     // Noto 12         -- body text, row subtitles
  UI2_F_KEY,      // Noto Bold 9     -- keycap letters
  UI2_F_CHIP,     // Noto Bold 10    -- chips, section headers
  UI2_F_NODE,     // Noto Bold 11    -- node name in the status header
  UI2_F_LABEL,    // Noto Bold 12    -- row titles, tile labels, buttons
  UI2_F_TITLE,    // Noto Bold 14    -- screen titles, dialog titles
  UI2_F_HEAD16,   // Noto Bold 16    -- warning boxes
  UI2_F_HEAD18,   // Noto Bold 18    -- call screen names
  UI2_F_NUM9,     // Montserrat 9    -- row badges
  UI2_F_NUM10,    // Montserrat 10   -- tile badges
  UI2_F_NUM11,    // Montserrat 11   -- clock
  UI2_F_NUM16,    // Montserrat 16   -- glance values on tiles
  UI2_F_NUM22,    // Montserrat 22   -- big numbers (digits only)
  UI2_F_NUM40,    // Montserrat 40   -- huge numbers (digits only)
  UI2_F_COUNT
};

const GFXfont* ui2Font(Ui2Font f);
// Icon bitmap at 12, 16, 24 or 32 px; nullptr for other sizes
const uint8_t* ui2Icon(Ui2Icon id, uint8_t size);
