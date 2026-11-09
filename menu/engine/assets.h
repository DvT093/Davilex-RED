// Files, bitmaps and sounds for the menu, looked up by name in a list of
// directories. The mod directory comes first, so a file there with the same
// name (any case) replaces the game's original.
#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/archive.h"

namespace davi {

std::vector<uint8_t> ReadFile(const std::string& path);
// Case-insensitive lookup of `rel` ("MenuData/menu.ini", '/' or '\\') below dir.
std::string FindNoCase(const std::string& dir, const std::string& rel);

// A BMP (file bytes) as an XRGB8888 surface, or nullptr.
SDL_Surface* LoadBmp(const std::vector<uint8_t>& bytes);
// A TGA (types 2/10, 24/32 bit) as an ARGB8888 surface with alpha, or nullptr.
SDL_Surface* LoadTga(const std::vector<uint8_t>& bytes);
// BMP or TGA by content.
SDL_Surface* LoadImage(const std::vector<uint8_t>& bytes);

struct Sound {
  SDL_AudioSpec spec{};
  std::vector<uint8_t> pcm;
  bool Ok() const { return !pcm.empty(); }
};
Sound LoadWav(const std::vector<uint8_t>& bytes);

class Assets {
 public:
  // Directories searched in order of priority (first = wins).
  void AddDir(const std::string& dir) { dirs_.push_back(dir); }
  // RCS archives are searched after all directories (a loose file in the
  // mod directory replaces an archive item). False if it cannot be opened.
  bool AddArchive(const std::string& dir, const std::string& base);
  // Bytes of `name` from the directories, else the archives; empty if absent.
  std::vector<uint8_t> Load(const std::string& name) const;
  std::string Find(const std::string& name) const;  // "" if not found
  bool Has(const std::string& name) const;          // in a directory or an archive
  // "MAINSCRE" or "mainscre.bmp"; without an extension .bmp, then .tga is
  // tried. Cached, owned by Assets.
  SDL_Surface* Bitmap(const std::string& name);
  const Sound* Wav(const std::string& name);
  const std::vector<std::string>& Dirs() const { return dirs_; }

 private:
  std::vector<std::string> dirs_;
  std::vector<RcsArchive> archives_;
  std::map<std::string, SDL_Surface*> bmps_;
  std::map<std::string, std::unique_ptr<Sound>> wavs_;
};

// Simple mixer on SDL3 audio streams: fire-and-forget sounds, one voice
// channel (a new voice line stops the previous one) and looping music.
class Audio {
 public:
  bool Init();
  void Shutdown();
  void Play(const Sound* s, float gain = 1.0f);
  void PlayVoice(const Sound* s, float gain = 1.0f);
  bool VoicePlaying() const;
  void StopVoice();
  void PlayMusic(const Sound* s, float gain);
  void SetMusicGain(float gain);
  void StopMusic();
  void Pause(bool paused);  // while the race runs
  // A raw stream on the device (owned by the caller), e.g. for video sound.
  SDL_AudioStream* OpenStream(const SDL_AudioSpec& spec, float gain);
  void Update();            // refills the music loop, frees finished sounds

 private:
  SDL_AudioDeviceID dev_ = 0;
  std::vector<SDL_AudioStream*> sfx_;
  SDL_AudioStream* voice_ = nullptr;
  SDL_AudioStream* music_ = nullptr;
  const Sound* music_snd_ = nullptr;
};

}  // namespace davi
