#pragma once

// =============================================================================
// GamesMenuScreen -- Game launcher menu for Meck
//
// Lists available games. W/S to navigate, Enter to launch, Shift+Del to exit.
// Uses wantsExit() and wantsLaunch() flags for navigation -- same pattern
// as ChannelPickerScreen.
// =============================================================================

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
#endif

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;   // for NodePrefs font helpers (same route as ChannelPickerScreen)

// Game identifiers -- add new entries here as games are added
enum GameID {
  GAME_NONE = 0,
  GAME_SNAKE,
  GAME_MINESWEEPER,
  GAME_GBC,          // Game Boy / Game Boy Color emulator (T-Deck Pro and Max)
  // GAME_2048,
  GAME_COUNT   // Must be last -- used for array sizing
};

class GamesMenuScreen : public UIScreen {
private:
  UITask* _task;
  bool _wantsExit;
  bool _wantsLaunch;
  int _cursor;
  GameID _selectedGame;

  // Game registry -- add new games here
  struct GameEntry {
    GameID id;
    const char* name;
    const char* description;
    const char* name_fr;  // French name (Experimental Features > Language)
  };

// The Game Boy emulator entry is present on the T-Deck Pro and Max, except on
// the 40 MHz battery-saver builds where it needs a clock they do not run at.
#if defined(LilyGo_TDeck_Pro) && !defined(MECK_40MHZ_TEST)
  static constexpr int NUM_GAMES = 3;
#else
  static constexpr int NUM_GAMES = 2;
#endif

  static const GameEntry* getGames() {
    static const GameEntry games[NUM_GAMES] = {
      { GAME_SNAKE,       "Snake",       "Classic Nokia-style", "Snake" },
      { GAME_MINESWEEPER, "Minesweeper", "Find the mines", "D\xC3\xA9mineur" },
#if defined(LilyGo_TDeck_Pro) && !defined(MECK_40MHZ_TEST)
      { GAME_GBC,         "Game Boy",    "GB / GBC emulator", "Game Boy" },
#endif
      // { GAME_2048,        "2048",        "Slide and merge" },
    };
    return games;
  }

public:
  GamesMenuScreen(UITask* task)
    : _task(task), _wantsExit(false), _wantsLaunch(false),
      _cursor(0), _selectedGame(GAME_NONE) {}

  bool wantsExit() const { return _wantsExit; }
  bool wantsLaunch() const { return _wantsLaunch; }
  GameID selectedGame() const { return _selectedGame; }

  void enter() {
    _wantsExit = false;
    _wantsLaunch = false;
    _selectedGame = GAME_NONE;
    // Preserve cursor position so returning from a game stays on the same entry
  }

  void clearFlags() {
    _wantsExit = false;
    _wantsLaunch = false;
    _selectedGame = GAME_NONE;
  }

  // ------- Input -------
  bool handleInput(char c) override {
    switch (c) {
      case 'w': case 'W':
        if (_cursor > 0) _cursor--;
        return true;
      case 's': case 'S':
        if (_cursor < NUM_GAMES - 1) _cursor++;
        return true;
      case '\r':
        _selectedGame = getGames()[_cursor].id;
        _wantsLaunch = true;
        return true;
      case KEY_CANCEL:
        _wantsExit = true;
        return true;
      default:
        return false;
    }
  }

#ifdef MECK_UI_V2
  // UI v2: one row per game with its short description
  int renderV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    Ui2Chrome::titleBar(c, MECK_TR("Games", "Jeux"));
    for (int i = 0; i < NUM_GAMES; i++) {
      Ui2Row r;
      r.icon = UI2_IC_GAMES;
      r.title = MECK_TR(getGames()[i].name, getGames()[i].name_fr);
      r.sub = getGames()[i].description;
      r.chevron = true;
      r.selected = (i == _cursor);
      Ui2List::drawRow(c, BODY_Y + i * ROW_H, r);
    }
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Play", "Jouer")),
      ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
      ui2Key("Q", MECK_TR("Back", "Retour")),
    };
    Ui2Chrome::footer(c, keys, 3);
    return 5000;
  }

  // Touch hit test in physical pixels: 0 = miss, 1 = moved, 2 = launch
  int selectRowAtPx(int py) {
    int row = Ui2List::rowAt(py, ui2::BODY_Y, ui2::ROW_H, ui2::ROWS_PER_PAGE);
    if (row < 0 || row >= NUM_GAMES) return 0;
    if (row == _cursor) {
      _selectedGame = getGames()[_cursor].id;
      _wantsLaunch = true;
      return 2;
    }
    _cursor = row;
    return 1;
  }
#endif

  // ------- Render -------
  int render(DisplayDriver& display) override {
    display.startFrame();
#ifdef MECK_UI_V2
    return renderV2(display);
#endif
    display.setTextSize(1);

    // --- Header ---
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(2, 2);
    display.print(MECK_TR("Games", "Jeux"));
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 12, display.width(), 1);

    // --- Game list ---
    // T-Deck Pro / MAX: rows follow the user's font size and style, the same
    // way the channel picker does (NodePrefs font helpers).
    NodePrefs* prefs = the_mesh.getNodePrefs();
    const int lineH = prefs->smallLineH();
    const int hlOff = prefs->smallHighlightOff();
    int y = 14;
    display.setTextSize(prefs->smallTextSize());

    for (int i = 0; i < NUM_GAMES; i++) {
      bool selected = (i == _cursor);

      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + hlOff, display.width(), lineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      display.drawTextEllipsized(6, y, display.width() - 12, MECK_TR(getGames()[i].name, getGames()[i].name_fr));

      y += lineH;
    }

    // --- Footer ---
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    int fy = display.height() - 12;
    display.drawRect(0, fy - 2, display.width(), 1);
    display.setCursor(2, fy);
    display.print(MECK_TR("Enter:Play  Q:Back", "Ent:Jouer  Q:Retour"));

    return 5000;  // Static menu -- slow refresh
  }

  // --- Touch: tap to select game entry (T-Deck Pro / MAX) ---
  int selectRowAtVY(int vy) {
    // Must match the render geometry above (NodePrefs font helpers).
    NodePrefs* prefs = the_mesh.getNodePrefs();
    int lineH = prefs->smallLineH();
    int y = 14 + prefs->smallHighlightOff();
    if (vy < y) return 0;  // Above list
    int row = (vy - y) / lineH;
    if (row >= NUM_GAMES) return 0;  // Below list

    if (row == _cursor) {
      // Tapped current selection -- launch
      _selectedGame = getGames()[_cursor].id;
      _wantsLaunch = true;
      return 2;
    }
    _cursor = row;
    return 1;  // Moved cursor
  }
};