#pragma once
// =============================================================================
// UI v2 -- layout constants of the e-ink design system (native 240x320 px)
//
// Source: "Meck UI e-ink design system" (Claude Design export). 8 px grid:
//   status header  y   0-23
//   title bar      y  24-47   (screens with a title)
//   content        y  48-295  (248 px: 6 rows of 40 px + 8 px slack)
//   footer         y 296-319  (keycap hints)
// =============================================================================

#include <stdint.h>

namespace ui2 {

static const int16_t SCREEN_W = 240;
static const int16_t SCREEN_H = 320;
static const int16_t PAD      = 8;     // standard side padding

static const int16_t HDR_H    = 24;    // status header
static const int16_t TITLE_Y  = 24;
static const int16_t TITLE_H  = 24;
static const int16_t FOOTER_H = 24;
static const int16_t FOOTER_Y = SCREEN_H - FOOTER_H;   // 296
static const int16_t BODY_Y   = TITLE_Y + TITLE_H;     // 48, below a title bar
static const int16_t BODY_H   = FOOTER_Y - BODY_Y;     // 248

static const int16_t ROW_H    = 40;    // list row (touch minimum)
static const int16_t ROWS_PER_PAGE = 6;
static const int16_t SECTION_H = 16;   // list section header

// Home tiles
static const int16_t TILE_W   = 108;
static const int16_t TILE_H   = 56;
static const int16_t TILE_X0  = 8;
static const int16_t TILE_Y0  = 32;
static const int16_t TILE_GAP_X = 8;
static const int16_t TILE_GAP_Y = 6;
static const int16_t TILE_COLS = 2;
static const int16_t TILE_ROWS = 4;
static const int16_t TILE_RADIUS = 3;

// Header layout
static const int16_t HDR_NODE_MAX_W = 90;
static const int16_t HDR_CLOCK_X    = 104;
static const int16_t HDR_CLOCK_W    = 32;
static const int16_t HDR_RIGHT_X    = 140;

// Dialog
static const int16_t DLG_X = 14;
static const int16_t DLG_W = 208;

// Toast
static const int16_t TOAST_W = 224;

}  // namespace ui2
