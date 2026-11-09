// Starting a race from the menu: the RED-engine handshake through game.ini.
//
// The original menu wrote game.ini, quit with exit code 40, and the game's
// launcher ran spel.dat and then restarted the menu, which read the results
// back from game.ini. Natively the menu stays open: it writes the
// same keys, runs the native race binary as a child process and reads the
// same keys back.
//
// Files: the race runs on SwARc, which keeps the CD install read-only and
// sends writes to an overlay ($XDG_DATA_HOME/<id>/install/...). The menu
// reads and writes the same live copy: the overlay file if it exists, else
// the install's, and every write goes to the overlay.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "engine/ini.h"

namespace davi {
class App;
}

namespace red {

struct Install {
  std::string game_dir;  // the CD's data folder (spel.dat, game.ini, MenuData/)
  std::string user_dir;  // $XDG_DATA_HOME/<id> (SwARc's per-game data dir)
  std::string ReadPath(const std::string& rel) const;   // overlay copy or install
  std::string WritePath(const std::string& rel) const;  // always in the overlay
  static std::string DefaultUserDir(const std::string& id);
};

// One racer line of [RACERS] ("i=car,position,time").
struct RacerResult {
  int car = 0, position = 0, time_ms = 0;
};

struct RaceResult {
  bool started = false;  // the race binary ran
  int exit_status = -1;
  bool finished = false;
  int position = 0;  // zero based
  int tracktime = 0, lapcount = 0, damage = 0, turbo_amount = 0;
  int checkpoint_money = 0, radarbonus = 0;
  std::vector<RacerResult> racers;
};

class RaceLauncher {
 public:
  RaceLauncher(Install install, std::vector<std::string> race_argv)
      : install_(std::move(install)), argv_(std::move(race_argv)) {}

  // The live game.ini (reloaded on every call).
  davi::IniFile LoadGameIni() const;
  // Values to write before the next race: (section, key, value).
  void Set(const std::string& sec, const std::string& key, const std::string& value);
  void SetInt(const std::string& sec, const std::string& key, int value);
  // Values for another ini of the install (e.g. "raser.ini").
  void SetIn(const std::string& file, const std::string& sec, const std::string& key, const std::string& value);
  // Extra writers (mods): called after the flow's own values, before saving.
  std::vector<std::function<void(davi::IniFile&)>> before_race;

  // Writes game.ini, runs the race in the menu window's place (again, from
  // the same files, when the race asks for a restart) and reads the results.
  RaceResult Run(davi::App& app);
  // Only reads the result keys of the current game.ini.
  RaceResult ReadResult() const;
  const Install& install() const { return install_; }

 private:
  Install install_;
  std::vector<std::string> argv_;
  struct Pending { std::string sec, key, value, file = "game.ini"; };
  std::vector<Pending> pending_;
};

}  // namespace red
