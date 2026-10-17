#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// Spectral stage for Clouds' "Spectral" mode: 2048-point STFT, Hann window, hop 512,
// overlap-add. Each frame: magnitudes are smoothed over time (Feedback; Freeze holds them),
// bins are shifted by the Pitch ratio, and phases are randomised by Texture.
// Latency = N samples (the dry path is delayed by the same amount in Clouds).
namespace kr106 {

class CloudSpectral
{
public:
  static constexpr int N = 2048, H = 512, HALF = N / 2;

  void Init()
  {
    tw.resize(N / 2);
    for (int k = 0; k < N / 2; k++)
    {
      const double a = -2.0 * 3.14159265358979 * k / N;
      tw[k] = std::complex<float>(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
    }
    rev.resize(N);
    for (int i = 0; i < N; i++)
    {
      int r = 0;
      for (int b = 0; b < 11; b++) if (i & (1 << b)) r |= 1 << (10 - b);
      rev[i] = r;
    }
    win.resize(N);
    for (int i = 0; i < N; i++) win[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / N));
    in.assign(N, 0.f);
    out.assign(N, 0.f);
    mag.assign(HALF + 1, 0.f);
    srcMag.assign(HALF + 1, 0.f);
    buf.assign(N, std::complex<float>(0.f, 0.f));
    pos = 0; count = 0; rng = 0x9E3779B9u;
  }

  // One sample in, one sample out (latency N). ratio = pitch ratio (1 = unchanged).
  float Process(float x, float ratio, float texture, float feedback, bool freeze)
  {
    in[pos] = x;
    if (++count >= H) { count = 0; Frame(ratio, texture, feedback, freeze); }
    const float y = out[pos];
    out[pos] = 0.f;
    pos = (pos + 1) % N;
    return y;
  }

private:
  void Frame(float ratio, float tex, float fb, bool freeze)
  {
    for (int i = 0; i < N; i++)
    {
      const int idx = (pos - (N - 1) + i + N) % N;
      buf[i] = std::complex<float>(in[idx] * win[i], 0.f);
    }
    Fft(buf.data(), false);

    std::vector<float>& ph = phase;
    ph.resize(HALF + 1);
    for (int k = 0; k <= HALF; k++)
    {
      const float m = std::abs(buf[k]);
      ph[k] = std::arg(buf[k]);
      if (!freeze) mag[k] = (1.f - fb) * m + fb * mag[k];
    }
    std::copy(mag.begin(), mag.end(), srcMag.begin());

    const float r = std::max(0.125f, ratio);
    for (int k = 0; k <= HALF; k++)
    {
      const int s = static_cast<int>(k / r + 0.5f);
      const float m = (s >= 0 && s <= HALF) ? srcMag[s] : 0.f;
      float p = ph[k];
      if (tex > 0.f)
      {
        rng = rng * 1664525u + 1013904223u;
        const float rnd = static_cast<float>(rng >> 8) * (1.f / 16777216.f) * 6.2831853f - 3.1415927f;
        p = p * (1.f - tex) + tex * rnd;
      }
      buf[k] = std::polar(m, p);
      if (k > 0 && k < HALF) buf[N - k] = std::conj(buf[k]);
    }
    Fft(buf.data(), true);
    for (int i = 0; i < N; i++)
      out[(pos + i) % N] += buf[i].real() * win[i] * (1.f / 1.5f);   // Hann^2 overlap at hop N/4 = 1.5
  }

  void Fft(std::complex<float>* a, bool inverse)
  {
    for (int i = 0; i < N; i++) if (i < rev[i]) std::swap(a[i], a[rev[i]]);
    for (int len = 2; len <= N; len <<= 1)
    {
      const int half = len >> 1, step = N / len;
      for (int i = 0; i < N; i += len)
        for (int j = 0; j < half; j++)
        {
          std::complex<float> w = tw[j * step];
          if (inverse) w = std::conj(w);
          const std::complex<float> u = a[i + j], v = a[i + j + half] * w;
          a[i + j] = u + v;
          a[i + j + half] = u - v;
        }
    }
    if (inverse) for (int i = 0; i < N; i++) a[i] /= static_cast<float>(N);
  }

  std::vector<std::complex<float>> tw, buf;
  std::vector<int> rev;
  std::vector<float> win, in, out, mag, srcMag, phase;
  int pos = 0, count = 0;
  uint32_t rng = 1u;
};

} // namespace kr106
