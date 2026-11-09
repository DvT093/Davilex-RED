#include "engine/assets.h"

#include <dirent.h>
#include <sys/stat.h>

#include <fstream>
#include <iterator>

#include "engine/dfm.h"  // Lower
#include "engine/hd.h"

namespace davi {

std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

std::string FindNoCase(const std::string& dir, const std::string& rel) {
  std::string cur = dir;
  size_t i = 0;
  while (i <= rel.size()) {
    size_t j = rel.find_first_of("/\\", i);
    std::string part = rel.substr(i, j == std::string::npos ? std::string::npos : j - i);
    i = j == std::string::npos ? rel.size() + 1 : j + 1;
    if (part.empty()) continue;
    std::string next = cur + "/" + part;
    struct stat st;
    if (stat(next.c_str(), &st) != 0) {
      next.clear();
      if (DIR* d = opendir(cur.c_str())) {
        std::string lw = Lower(part);
        while (dirent* e = readdir(d))
          if (Lower(e->d_name) == lw) {
            next = cur + "/" + e->d_name;
            break;
          }
        closedir(d);
      }
      if (next.empty()) return "";
    }
    cur = next;
  }
  struct stat st;
  return stat(cur.c_str(), &st) == 0 ? cur : "";
}

SDL_Surface* LoadBmp(const std::vector<uint8_t>& bytes) {
  if (bytes.empty()) return nullptr;
  SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
  SDL_Surface* s = SDL_LoadBMP_IO(io, true);
  if (!s) return nullptr;
  SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_XRGB8888);
  SDL_DestroySurface(s);
  return c;
}

Sound LoadWav(const std::vector<uint8_t>& bytes) {
  Sound snd;
  if (bytes.empty()) return snd;
  SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
  Uint8* buf = nullptr;
  Uint32 len = 0;
  if (SDL_LoadWAV_IO(io, true, &snd.spec, &buf, &len)) {
    snd.pcm.assign(buf, buf + len);
    SDL_free(buf);
  }
  return snd;
}

std::string Assets::Find(const std::string& name) const {
  for (auto& d : dirs_) {
    std::string p = FindNoCase(d, name);
    if (!p.empty()) return p;
  }
  return "";
}

SDL_Surface* LoadImage(const std::vector<uint8_t>& bytes) {
  if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') return LoadBmp(bytes);
  return LoadTga(bytes);
}

bool Assets::AddArchive(const std::string& dir, const std::string& base) {
  RcsArchive a;
  if (!a.Open(dir, base)) {
    SDL_Log("menu: archive %s/%s not found", dir.c_str(), base.c_str());
    return false;
  }
  archives_.push_back(std::move(a));
  return true;
}

std::vector<uint8_t> Assets::Load(const std::string& name) const {
  std::string p = Find(name);
  if (!p.empty()) return ReadFile(p);
  size_t slash = name.find_last_of("/\\");
  std::string leaf = slash == std::string::npos ? name : name.substr(slash + 1);
  for (auto& a : archives_)
    if (a.Has(leaf)) return a.Read(leaf);
  return {};
}

bool Assets::Has(const std::string& name) const {
  if (!Find(name).empty()) return true;
  std::string leaf = name.substr(name.find_last_of("/\\") + 1);
  for (auto& a : archives_)
    if (a.Has(leaf)) return true;
  return false;
}

SDL_Surface* Assets::Bitmap(const std::string& name) {
  std::string key = Lower(name);
  auto it = bmps_.find(key);
  if (it != bmps_.end()) return it->second;
  std::vector<std::string> tries{name};
  if (name.find('.') == std::string::npos) tries = {name + ".bmp", name + ".tga"};
  SDL_Surface* s = nullptr;
  for (auto& t : tries)
    if (!s) s = LoadImage(Load(t));
  if (!s) SDL_Log("menu: bitmap not found: %s", name.c_str());
  // HD pictures go by the file name without its extension ("mainscre").
  std::string base = name.substr(name.find_last_of("/\\") + 1);
  NamePicture(s, base.substr(0, base.rfind('.')));
  bmps_[key] = s;
  return s;
}

const Sound* Assets::Wav(const std::string& name) {
  std::string n = name;
  if (n.find('.') == std::string::npos) n += ".wav";
  std::string key = Lower(n);
  auto it = wavs_.find(key);
  if (it != wavs_.end()) return it->second.get();
  auto s = std::make_unique<Sound>();
  *s = LoadWav(Load(n));
  if (!s->Ok()) {
    SDL_Log("menu: sound not found: %s", n.c_str());
    s.reset();
  }
  return (wavs_[key] = std::move(s)).get();
}

bool Audio::Init() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
  dev_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
  return dev_ != 0;
}

void Audio::Shutdown() {
  for (auto* s : sfx_) SDL_DestroyAudioStream(s);
  sfx_.clear();
  StopVoice();
  StopMusic();
  if (dev_) SDL_CloseAudioDevice(dev_);
  dev_ = 0;
}

static SDL_AudioStream* Start(SDL_AudioDeviceID dev, const Sound* s, float gain) {
  if (!dev || !s || !s->Ok()) return nullptr;
  SDL_AudioStream* st = SDL_CreateAudioStream(&s->spec, nullptr);
  if (!st) return nullptr;
  SDL_SetAudioStreamGain(st, gain);
  SDL_PutAudioStreamData(st, s->pcm.data(), int(s->pcm.size()));
  SDL_FlushAudioStream(st);
  SDL_BindAudioStream(dev, st);
  return st;
}

void Audio::Play(const Sound* s, float gain) {
  if (SDL_AudioStream* st = Start(dev_, s, gain)) sfx_.push_back(st);
}

void Audio::PlayVoice(const Sound* s, float gain) {
  StopVoice();
  voice_ = Start(dev_, s, gain);
}

bool Audio::VoicePlaying() const { return voice_ && SDL_GetAudioStreamQueued(voice_) > 0; }

void Audio::StopVoice() {
  if (voice_) SDL_DestroyAudioStream(voice_);
  voice_ = nullptr;
}

void Audio::PlayMusic(const Sound* s, float gain) {
  if (music_snd_ == s && music_) {
    SetMusicGain(gain);
    return;
  }
  StopMusic();
  music_snd_ = s;
  music_ = Start(dev_, s, gain);
}

void Audio::SetMusicGain(float gain) {
  if (music_) SDL_SetAudioStreamGain(music_, gain);
}

void Audio::StopMusic() {
  if (music_) SDL_DestroyAudioStream(music_);
  music_ = nullptr;
  music_snd_ = nullptr;
}

SDL_AudioStream* Audio::OpenStream(const SDL_AudioSpec& spec, float gain) {
  if (!dev_) return nullptr;
  SDL_AudioStream* st = SDL_CreateAudioStream(&spec, nullptr);
  if (!st) return nullptr;
  SDL_SetAudioStreamGain(st, gain);
  SDL_BindAudioStream(dev_, st);
  return st;
}

void Audio::Pause(bool paused) {
  if (!dev_) return;
  if (paused) SDL_PauseAudioDevice(dev_);
  else SDL_ResumeAudioDevice(dev_);
}

void Audio::Update() {
  if (music_ && music_snd_ && SDL_GetAudioStreamQueued(music_) < int(music_snd_->pcm.size() / 4)) {
    SDL_PutAudioStreamData(music_, music_snd_->pcm.data(), int(music_snd_->pcm.size()));
    SDL_FlushAudioStream(music_);
  }
  for (size_t i = 0; i < sfx_.size();) {
    if (SDL_GetAudioStreamQueued(sfx_[i]) == 0) {
      SDL_DestroyAudioStream(sfx_[i]);
      sfx_.erase(sfx_.begin() + i);
    } else {
      i++;
    }
  }
}

}  // namespace davi
