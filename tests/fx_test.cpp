// The FX 2 effects on their own: silent when off, stable, and doing what they should when on.
#include "Source/DSP/KR106Fx.h"
#include <cstdio>
#include <random>
using namespace kr106;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
template <class F> static float rms(F&& f, int n, float freq, float amp, std::vector<float>* outL = nullptr)
{
  double acc = 0; for (int i = 0; i < n; i++) { float l = amp * std::sin(6.2831853f * freq * i / 44100.f), r = l; f(l, r); acc += l * l; if (outL) outL->push_back(l); if (!std::isfinite(l)) return -1.f; }
  return static_cast<float>(std::sqrt(acc / n));
}
int main()
{
  const float sr = 44100.f;
  // off = bit-exact
  { FxChorus a; FxPhaser b; FxSpring c; FxCompressor d; a.Init(sr); b.Init(sr); c.Init(sr); d.Init(sr);
    bool same = true; for (int i = 0; i < 2000; i++) { float l = std::sin(i * 0.05f), r = l, l0 = l; a.Process(l, r); b.Process(l, r); c.Process(l, r); d.Process(l, r); if (l != l0) same = false; } CHECK(same); }
  // chorus changes the sound and stays bounded
  { FxChorus a; a.Init(sr); a.Set(true, 0.5f, 0.7f, 1.f); std::vector<float> o; float e = rms([&](float& l, float& r) { a.Process(l, r); }, 20000, 440.f, 0.5f, &o);
    float peak = 0; for (float v : o) peak = std::max(peak, std::fabs(v)); std::printf("chorus rms %.3f peak %.3f\n", e, peak); CHECK(e > 0.2f && peak < 1.2f); }
  // phaser with feedback is stable on noise
  { FxPhaser p; p.Init(sr); p.Set(true, 0.4f, 1.f, 1.f, 1.f); std::mt19937 g(1); float peak = 0; bool ok = true;
    for (int i = 0; i < 200000; i++) { float l = ((int) (g() % 2000) - 1000) / 1000.f, r = l; p.Process(l, r); peak = std::max(peak, std::fabs(l)); if (!std::isfinite(l)) ok = false; }
    std::printf("phaser peak %.2f\n", peak); CHECK(ok && peak < 8.f); }
  // spring: an impulse leaves a decaying tail that is finite; more decay = longer tail
  { auto tail = [&](float decay) { FxSpring s; s.Init(sr); s.Set(true, decay, 0.6f, 0.2f, 1.f); double e = 0; bool ok = true;
      for (int i = 0; i < 60000; i++) { float l = (i == 0) ? 1.f : 0.f, r = l; s.Process(l, r); if (i > 22050) e += l * l; if (!std::isfinite(l)) ok = false; } CHECK(ok); return e; };
    const double shortT = tail(0.1f), longT = tail(0.95f); std::printf("spring tail energy short %.6f long %.6f\n", shortT, longT); CHECK(longT > shortT * 2.0 && longT > 0.0); }
  { FxSpring s; s.Init(sr); s.Set(true, 1.f, 1.f, 1.f, 1.f); std::mt19937 g(3); bool ok = true; for (int i = 0; i < 300000; i++) { float l = ((int) (g() % 2000) - 1000) / 500.f, r = -l; s.Process(l, r); if (!std::isfinite(l) || std::fabs(l) > 100.f) ok = false; } CHECK(ok); }
  // compressor: loud input comes down, quiet input does not, the meter moves
  { FxCompressor c; c.Init(sr); c.Set(true, 0.5f, 0.6f, 0.1f, 0.3f, 0.f);                  // threshold -20 dB
    const float loud = rms([&](float& l, float& r) { c.Process(l, r); }, 44100, 440.f, 1.0f), gr = c.GetGainReductionDb();
    FxCompressor q; q.Init(sr); q.Set(true, 0.5f, 0.6f, 0.1f, 0.3f, 0.f);
    const float quiet = rms([&](float& l, float& r) { q.Process(l, r); }, 44100, 440.f, 0.02f);
    std::printf("compressor: loud %.3f (in 0.707) gr %.1f dB, quiet %.4f (in 0.0141)\n", loud, gr, quiet);
    CHECK(loud < 0.5f && gr < -3.f); CHECK(std::fabs(quiet - 0.02f / std::sqrt(2.f)) < 0.002f); }
  { FxCompressor c; c.Init(sr); c.Set(true, 0.5f, 0.6f, 0.1f, 0.3f, 1.f);                    // make-up +24 dB adds gain
    const float v = rms([&](float& l, float& r) { c.Process(l, r); }, 20000, 440.f, 0.01f); CHECK(v > 0.05f); }
  std::printf("fails=%d\n", fails); return fails;
}
