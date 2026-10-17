#pragma once

// Copyright 2014 Emilie Gillet.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, subject to the MIT conditions: the
// above copyright notice and this permission notice shall be included in all copies or substantial
// portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
// Source: github.com/pichenettes/eurorack (clouds/dsp/wsola_sample_player.h, correlator.h/.cc, window.h)


#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// WSOLA pitch shifter / time stretch for Clouds' "Pitch shift" mode.
//
// Port of the Clouds firmware (MIT, Copyright 2014 Emilie Gillet): wsola_sample_player.h,
// correlator.h / correlator.cc and window.h (github.com/pichenettes/eurorack, clouds/dsp).
// Kept from the original: two overlapping triangular windows read the recording buffer at the
// pitch ratio; the splice point of each new window is chosen by a sign-correlation search
// (bit-packed, 32 samples per word); window width follows Size; pitch glides at most 12
// semitones per window.
// Changes: floating point instead of fixed point; the search runs when a window is scheduled
// (the firmware spreads it over several blocks); Window::Start marks the window active (the
// public window.h never clears its done flag, which would leave the output silent).
namespace kr106 {

class WsolaPlayer
{
public:
  static constexpr int32_t kMaxWindow = 4096;

  void Init(long long bufLen)
  {
    bufLen_ = bufLen;
    windowSize_ = kMaxWindow / 2;
    smoothedPitch_ = 0.f;
    nextRatio_ = 1.f;
    searchSource_ = 0; searchTarget_ = 0; pendingBest_ = 0;
    for (auto& w : win_) w = Win();
    srcBits_.assign(kMaxWindow / 32 + 4, 0u);
    dstBits_.assign(2 * kMaxWindow / 32 + 8, 0u);
  }

  // One sample of output. buf0/buf1 = the two recording channels, mask = bufLen - 1,
  // head = next write index, pitch in semitones, position 0..1, size 0..1.
  void Tick(const float* buf0, const float* buf1, long long mask, long long head,
            float pitch, float position, float size, float& outL, float& outR)
  {
    b0_ = buf0; b1_ = buf1; mask_ = mask; head_ = head;
    pitch_ = pitch; position_ = position; size_ = size;
    if (win_[0].done && win_[1].done)
    {
      win_[1].regen = true;
      Schedule(&win_[0]);
    }
    outL = 0.f; outR = 0.f;
    for (int i = 0; i < 2; i++) Overlap(win_[i], outL, outR);
    for (int i = 0; i < 2; i++)
    {
      if (win_[i].half && !win_[i].regen)
      {
        win_[i].regen = true;
        Schedule(&win_[1 - i]);
        Overlap(win_[1 - i], outL, outR);
      }
    }
  }

private:
  struct Win
  {
    bool done = true, half = false, regen = false;
    long long first = 0;
    int32_t phase = 0, inc = 0;
    float envInc = 1.f;
    void Start(long long bufLen, long long start, int32_t width, int32_t phaseInc)
    {
      first = ((start % bufLen) + bufLen) % bufLen;
      inc = phaseInc; phase = 0; regen = false;
      envInc = 2.f / static_cast<float>(width);
      done = false; half = false;                 // see the note above: the window is active now
    }
  };

  float Read(const float* buf, long long idx, int32_t frac16) const
  {
    const float t = static_cast<float>(frac16) * (1.f / 65536.f);
    const float a = buf[idx & mask_];
    const float b = buf[(idx + 1) & mask_];
    return a + (b - a) * t;
  }

  void Overlap(Win& w, float& outL, float& outR)
  {
    if (w.done) return;
    const int32_t pi = w.phase >> 16;
    const int32_t pf = w.phase & 0xffff;
    const long long idx = w.first + pi;
    const float envPhase = pi * w.envInc;
    w.done = envPhase >= 2.f;
    w.half = envPhase >= 1.f;
    const float gain = envPhase >= 1.f ? 2.f - envPhase : envPhase;
    outL += Read(b0_, idx, pf) * gain;
    outR += Read(b1_, idx, pf) * gain;
    w.phase += w.inc;
  }

  void Schedule(Win* w)
  {
    const long long nextPos = pendingBest_;
    w->Start(bufLen_, nextPos - windowSize_ / 2, windowSize_, static_cast<int32_t>(nextRatio_ * 65536.f));

    float err = pitch_ - smoothedPitch_;
    const float sign = err < 0.f ? -1.f : 1.f;
    err *= sign;
    if (err >= 12.f) err = 12.f;
    smoothedPitch_ += err * sign;
    const float ratio = std::pow(2.f, smoothedPitch_ / 12.f);
    const float invRatio = std::pow(2.f, -smoothedPitch_ / 12.f);
    nextRatio_ = ratio;

    const float sizeFactor = std::pow(2.f, ((size_ - 1.f) * 60.f) / 12.f);
    int32_t newSize = static_cast<int32_t>(sizeFactor * kMaxWindow);
    if (std::abs(newSize - windowSize_) > 64)
    {
      const int32_t e = (newSize - windowSize_) >> 5;
      newSize = windowSize_ + e;
      windowSize_ = newSize - (newSize % 4);
    }

    long long limit = bufLen_ - static_cast<long long>(2.f * windowSize_ * invRatio) - 2LL * windowSize_;
    if (limit < 0) limit = 0;
    long long target = head_ - static_cast<long long>(limit * position_) - windowSize_;
    searchSource_ = nextPos;
    searchTarget_ = target;
    Search();
  }

  // Sign-correlation search: the best splice point for the next window (LoadCorrelator + search)
  void Search()
  {
    float stride = windowSize_ / 2048.f;
    stride = std::min(2.f, std::max(1.f, stride)) * 65536.f;
    const float r = nextRatio_ < 1.25f ? 1.25f : nextRatio_;
    const int32_t inc = static_cast<int32_t>(stride * r);
    const int32_t srcN = ReadSignBits(searchSource_, inc, windowSize_, srcBits_);
    ReadSignBits(searchTarget_ - windowSize_, inc, 2 * windowSize_, dstBits_);

    const int32_t words = srcN >> 5;
    int32_t best = 0;
    uint32_t bestScore = 0;
    for (int32_t c = 0; c < srcN; c++)
    {
      const int32_t w0 = c >> 5, off = c & 31;
      uint32_t score = 0;
      for (int32_t i = 0; i < words; i++)
      {
        const uint32_t d = off ? ((dstBits_[w0 + i] >> off) | (dstBits_[w0 + i + 1] << (32 - off)))
                               : dstBits_[w0 + i];
        score += static_cast<uint32_t>(__builtin_popcount(~(srcBits_[i] ^ d)));
      }
      if (score > bestScore) { bestScore = score; best = c; }
    }
    const int32_t offset = static_cast<int32_t>(searchTarget_ - windowSize_ + windowSize_ / 2);
    pendingBest_ = offset + ((static_cast<int64_t>(best) * (inc >> 4)) >> 12);
  }

  // Packs the sign of the buffer (sum of channels) sampled with 16.16 increments: bit i = sample i > 0
  int32_t ReadSignBits(long long start, int32_t inc, int32_t size, std::vector<uint32_t>& bits)
  {
    std::fill(bits.begin(), bits.begin() + (size >> 5) + 2, 0u);
    int32_t phase = 0;
    for (int32_t i = 0; i < size; i++)
    {
      const long long integral = start + (phase >> 16);
      const int32_t frac = phase & 0xffff;
      const float s = Read(b0_, integral, frac) + Read(b1_, integral, frac);
      if (s > 0.f) bits[i >> 5] |= 1u << (i & 31);
      phase += inc;
    }
    return size;
  }

  long long bufLen_ = 1, mask_ = 0, head_ = 0;
  const float* b0_ = nullptr;
  const float* b1_ = nullptr;
  Win win_[2];
  int32_t windowSize_ = kMaxWindow / 2;
  float pitch_ = 0.f, smoothedPitch_ = 0.f, position_ = 0.f, size_ = 0.5f, nextRatio_ = 1.f;
  long long searchSource_ = 0, searchTarget_ = 0, pendingBest_ = 0;
  std::vector<uint32_t> srcBits_, dstBits_;
};

} // namespace kr106
