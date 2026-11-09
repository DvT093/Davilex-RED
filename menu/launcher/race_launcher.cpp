#include "launcher/race_launcher.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <map>

#include "engine/app.h"
#include "engine/assets.h"

namespace red {

namespace {

void MakeDirs(const std::string& path) {
  for (size_t i = 1; i <= path.size(); i++) {
    if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
  }
}

}  // namespace

std::string Install::DefaultUserDir(const std::string& id) {
  // Same rule as SwARc's DataDir(): $XDG_DATA_HOME/<id>, else Application Support.
  if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return std::string(x) + "/" + id;
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/Library/Application Support/" + id;
}

std::string Install::ReadPath(const std::string& rel) const {
  std::string p = davi::FindNoCase(user_dir + "/install", rel);
  if (!p.empty()) return p;
  p = davi::FindNoCase(game_dir, rel);
  return p.empty() ? game_dir + "/" + rel : p;
}

std::string Install::WritePath(const std::string& rel) const {
  std::string p = davi::FindNoCase(user_dir + "/install", rel);
  if (!p.empty()) return p;
  // Keep the install's spelling of the name (SwARc resolves case-insensitively).
  std::string inst = davi::FindNoCase(game_dir, rel);
  std::string name = inst.empty() ? rel : inst.substr(game_dir.size() + 1);
  std::string out = user_dir + "/install/" + name;
  MakeDirs(out.substr(0, out.find_last_of('/')));
  return out;
}

davi::IniFile RaceLauncher::LoadGameIni() const {
  davi::IniFile ini;
  ini.Load(install_.ReadPath("game.ini"));
  return ini;
}

void RaceLauncher::Set(const std::string& sec, const std::string& key, const std::string& value) {
  SetIn("game.ini", sec, key, value);
}

void RaceLauncher::SetIn(const std::string& file, const std::string& sec, const std::string& key,
                         const std::string& value) {
  for (auto& p : pending_)
    if (davi::Lower(p.file) == davi::Lower(file) && davi::Lower(p.sec) == davi::Lower(sec) &&
        davi::Lower(p.key) == davi::Lower(key)) {
      p.value = value;
      return;
    }
  pending_.push_back({sec, key, value, file});
}

void RaceLauncher::SetInt(const std::string& sec, const std::string& key, int value) {
  Set(sec, key, std::to_string(value));
}

RaceResult RaceLauncher::ReadResult() const {
  davi::IniFile ini = LoadGameIni();
  RaceResult r;
  r.finished = ini.GetInt("PLAYER", "finished") != 0;
  r.position = ini.GetInt("PLAYER", "position");
  r.tracktime = ini.GetInt("PLAYER", "tracktime");
  r.lapcount = ini.GetInt("PLAYER", "lapcount");
  r.damage = ini.GetInt("PLAYER", "damage");
  r.turbo_amount = ini.GetInt("PLAYER", "TURBO_AMOUNT");
  r.checkpoint_money = ini.GetInt("PLAYER", "checkpoint_money");
  r.radarbonus = ini.GetInt("PLAYER", "radarbonus");
  int n = ini.GetInt("RACERS", "count");
  for (int i = 0; i < n; i++) {
    auto v = davi::Split(ini.Get("RACERS", std::to_string(i)), ',');
    if (v.size() < 3) continue;
    r.racers.push_back({std::atoi(v[0].c_str()), std::atoi(v[1].c_str()), std::atoi(v[2].c_str())});
  }
  return r;
}

RaceResult RaceLauncher::Run(davi::App& app) {
  davi::IniFile ini = LoadGameIni();
  std::map<std::string, davi::IniFile> others;
  for (auto& p : pending_) {
    if (davi::Lower(p.file) == "game.ini") {
      ini.Set(p.sec, p.key, p.value);
      continue;
    }
    auto it = others.find(davi::Lower(p.file));
    if (it == others.end()) {
      it = others.emplace(davi::Lower(p.file), davi::IniFile()).first;
      it->second.Load(install_.ReadPath(p.file));
    }
    it->second.Set(p.sec, p.key, p.value);
  }
  pending_.clear();
  for (auto& f : before_race) f(ini);
  std::string out = install_.WritePath("game.ini");
  int st;
  for (;;) {
    if (!ini.Save(out)) SDL_Log("menu: cannot write %s", out.c_str());
    for (auto& [file, other] : others) other.Save(install_.WritePath(file));
    std::vector<std::string> messages;
    st = app.RunChild(argv_, &messages);
    // "Opnieuw starten" in the race's pause menu: the same race again,
    // from the files as they were before it (the race wrote its results).
    if (std::find(messages.begin(), messages.end(), "restart") == messages.end()) break;
    SDL_Log("menu: race restarted");
  }
  RaceResult r = ReadResult();
  r.started = st >= 0;
  r.exit_status = st;
  return r;
}

}  // namespace red
