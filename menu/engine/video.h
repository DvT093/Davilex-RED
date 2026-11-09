// Video playback for the menu (Delphi TMediaPlayer / MCI): MPEG-1 intro,
// Cinepak AVIs. Decodes with ffmpeg; frames as an SDL surface, sound
// through the App's audio device, timed by the audio clock's wall time.
#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

namespace davi {

class Audio;

class Video {
 public:
  Video();
  ~Video();
  bool Open(const std::string& path, Audio* audio, float gain);
  void Close();
  // Decodes up to the current time; false once the video has ended.
  bool Update();
  void Restart();  // for looping
  SDL_Surface* Frame() const { return frame_; }
  bool IsOpen() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
  SDL_Surface* frame_ = nullptr;
};

}  // namespace davi
