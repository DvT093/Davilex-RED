#include "engine/video.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <vector>

#include "engine/assets.h"

namespace davi {

struct Video::Impl {
  AVFormatContext* fmt = nullptr;
  AVCodecContext* vctx = nullptr;
  AVCodecContext* actx = nullptr;
  int vidx = -1, aidx = -1;
  SwsContext* sws = nullptr;
  SwrContext* swr = nullptr;
  AVFrame* frame = nullptr;
  AVPacket* pkt = nullptr;
  SDL_AudioStream* stream = nullptr;
  Audio* audio = nullptr;
  float gain = 1;
  Uint64 start = 0;
  double next_pts = 0;  // seconds of the frame waiting to be shown
  bool have_pending = false, eof = false;

  ~Impl() {
    if (stream) SDL_DestroyAudioStream(stream);
    if (sws) sws_freeContext(sws);
    if (swr) swr_free(&swr);
    if (frame) av_frame_free(&frame);
    if (pkt) av_packet_free(&pkt);
    if (vctx) avcodec_free_context(&vctx);
    if (actx) avcodec_free_context(&actx);
    if (fmt) avformat_close_input(&fmt);
  }

  AVCodecContext* OpenCodec(int idx) {
    const AVCodecParameters* par = fmt->streams[idx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) return nullptr;
    AVCodecContext* c = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(c, par);
    if (avcodec_open2(c, codec, nullptr) < 0) avcodec_free_context(&c);
    return c;
  }
};

Video::Video() = default;
Video::~Video() { Close(); }

bool Video::IsOpen() const { return d_ != nullptr; }

void Video::Close() {
  d_.reset();
  if (frame_) SDL_DestroySurface(frame_);
  frame_ = nullptr;
}

bool Video::Open(const std::string& path, Audio* audio, float gain) {
  Close();
  auto d = std::make_unique<Impl>();
  if (avformat_open_input(&d->fmt, path.c_str(), nullptr, nullptr) < 0) return false;
  if (avformat_find_stream_info(d->fmt, nullptr) < 0) return false;
  d->vidx = av_find_best_stream(d->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  d->aidx = av_find_best_stream(d->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (d->vidx < 0 || !(d->vctx = d->OpenCodec(d->vidx))) return false;
  if (d->aidx >= 0) d->actx = d->OpenCodec(d->aidx);
  d->frame = av_frame_alloc();
  d->pkt = av_packet_alloc();
  frame_ = SDL_CreateSurface(d->vctx->width, d->vctx->height, SDL_PIXELFORMAT_XRGB8888);
  SDL_FillSurfaceRect(frame_, nullptr, 0);
  d->sws = sws_getContext(d->vctx->width, d->vctx->height, d->vctx->pix_fmt, d->vctx->width, d->vctx->height,
                          AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (d->actx && audio) {
    AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&d->swr, &out, AV_SAMPLE_FMT_S16, d->actx->sample_rate, &d->actx->ch_layout,
                            d->actx->sample_fmt, d->actx->sample_rate, 0, nullptr) == 0 &&
        swr_init(d->swr) == 0) {
      SDL_AudioSpec spec{SDL_AUDIO_S16, 2, d->actx->sample_rate};
      d->stream = audio->OpenStream(spec, gain);
    }
  }
  d->audio = audio;
  d->gain = gain;
  d->start = SDL_GetTicksNS();
  d_ = std::move(d);
  return true;
}

void Video::Restart() {
  if (!d_) return;
  av_seek_frame(d_->fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
  avcodec_flush_buffers(d_->vctx);
  if (d_->actx) avcodec_flush_buffers(d_->actx);
  if (d_->stream) SDL_ClearAudioStream(d_->stream);
  d_->start = SDL_GetTicksNS();
  d_->have_pending = false;
  d_->eof = false;
}

bool Video::Update() {
  if (!d_) return false;
  Impl& d = *d_;
  double now = double(SDL_GetTicksNS() - d.start) / 1e9;
  for (int guard = 0; guard < 256; guard++) {
    if (d.have_pending) {
      if (d.next_pts > now) return true;
      // Show the decoded frame.
      if (SDL_LockSurface(frame_)) {
        uint8_t* dst[4] = {(uint8_t*)frame_->pixels};
        int pitch[4] = {frame_->pitch};
        sws_scale(d.sws, d.frame->data, d.frame->linesize, 0, d.vctx->height, dst, pitch);
        SDL_UnlockSurface(frame_);
      }
      d.have_pending = false;
      continue;
    }
    // Next video frame: drain the decoder, else read packets.
    int r = avcodec_receive_frame(d.vctx, d.frame);
    if (r == 0) {
      AVRational tb = d.fmt->streams[d.vidx]->time_base;
      int64_t ts = d.frame->best_effort_timestamp;
      d.next_pts = ts == AV_NOPTS_VALUE ? now : ts * av_q2d(tb);
      d.have_pending = true;
      continue;
    }
    if (d.eof) return false;
    if (av_read_frame(d.fmt, d.pkt) < 0) {
      d.eof = true;
      avcodec_send_packet(d.vctx, nullptr);
      continue;
    }
    if (d.pkt->stream_index == d.vidx) {
      avcodec_send_packet(d.vctx, d.pkt);
    } else if (d.pkt->stream_index == d.aidx && d.actx && d.stream) {
      if (avcodec_send_packet(d.actx, d.pkt) == 0) {
        AVFrame* af = av_frame_alloc();
        while (avcodec_receive_frame(d.actx, af) == 0) {
          int n = swr_get_out_samples(d.swr, af->nb_samples);
          std::vector<int16_t> buf(size_t(n) * 2);
          uint8_t* out[1] = {(uint8_t*)buf.data()};
          int got = swr_convert(d.swr, out, n, (const uint8_t**)af->extended_data, af->nb_samples);
          if (got > 0) SDL_PutAudioStreamData(d.stream, buf.data(), got * 4);
        }
        av_frame_free(&af);
      }
    }
    av_packet_unref(d.pkt);
  }
  return true;
}

}  // namespace davi
