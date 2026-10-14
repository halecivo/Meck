#ifdef MECK_UI_V2

#define UI2_ICONS_IMPL
#include "Ui2Assets.h"

#include "fonts/Ui2NotoSans10.h"
#include "fonts/Ui2NotoSans11.h"
#include "fonts/Ui2NotoSans12.h"
#include "fonts/Ui2NotoSansBold9.h"
#include "fonts/Ui2NotoSansBold10.h"
#include "fonts/Ui2NotoSansBold11.h"
#include "fonts/Ui2NotoSansBold12.h"
#include "fonts/Ui2NotoSansBold14.h"
#include "fonts/Ui2NotoSansBold16.h"
#include "fonts/Ui2NotoSansBold18.h"
#include "fonts/Ui2Montserrat9.h"
#include "fonts/Ui2Montserrat10.h"
#include "fonts/Ui2Montserrat11.h"
#include "fonts/Ui2Montserrat16.h"
#include "fonts/Ui2Montserrat22.h"
#include "fonts/Ui2Montserrat40.h"

static const GFXfont* const UI2_FONTS[UI2_F_COUNT] = {
  &Ui2NotoSans10, &Ui2NotoSans11, &Ui2NotoSans12,
  &Ui2NotoSansBold9, &Ui2NotoSansBold10, &Ui2NotoSansBold11, &Ui2NotoSansBold12,
  &Ui2NotoSansBold14, &Ui2NotoSansBold16, &Ui2NotoSansBold18,
  &Ui2Montserrat9, &Ui2Montserrat10, &Ui2Montserrat11, &Ui2Montserrat16,
  &Ui2Montserrat22, &Ui2Montserrat40,
};

const GFXfont* ui2Font(Ui2Font f) {
  return (f < UI2_F_COUNT) ? UI2_FONTS[f] : UI2_FONTS[UI2_F_BODY];
}

const uint8_t* ui2Icon(Ui2Icon id, uint8_t size) {
  return ui2IconBits(id, size);
}

#endif  // MECK_UI_V2
