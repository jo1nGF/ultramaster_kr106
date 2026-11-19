// Checks the DSP side: both sequencers, the scale quantizer with Hold, strum / ratchet without the arp, and a
// random stress run of everything together (run with the sanitizers on).
#include "Source/DSP/KR106_DSP.h"
#include <cstdio>
#include <map>
#include <random>
using namespace kr106;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
struct Ev { long t; int note; bool on; };

template <class S> static std::vector<Ev> run(S& s, double seconds, bool stop = true)
{
  s.SetSampleRate(44100.f); s.Reset(); s.mRun.store(true);
  std::vector<Ev> ev; long base = 0; const int B = 256; const long total = (long)(seconds * 44100);
  auto on = [&](int n, int o) { ev.push_back({ base + o, n, true }); };
  auto off = [&](int n, int o) { ev.push_back({ base + o, n, false }); };
  while (base < total) { s.Process(B, on, off); base += B; }
  if (stop) { s.mRun.store(false); s.Process(B, on, off); }
  return ev;
}
static size_t countOns(const std::vector<Ev>& e) { size_t n = 0; for (auto& x : e) if (x.on) n++; return n; }

int main()
{
  // ---- Varigate: timing, repeat, gate+delay, bank scale
  { VarigateSeq s; auto ev = run(s, 3.99); CHECK(countOns(ev) == 16);
    std::map<int, int> bal; for (auto& e : ev) bal[e.note] += e.on ? 1 : -1; for (auto& kv : bal) CHECK(kv.second == 0); }
  { VarigateSeq s; s.mPat.Init(); s.mPat.t[0].gate[0].set(127); s.mPat.t[0].dly[0].set(64); s.mGateDelay.store(true);
    auto ev = run(s, 0.12); CHECK(countOns(ev) == 2); }
  { VarigateSeq s; s.mPresets[12].Init(); s.Recall(0); s.SetBankKey(7); s.Recall(12); CHECK(s.mPat.key.get() == 0); s.Recall(3); CHECK(s.mPat.key.get() == 7); }
  { VarigateSeq s; s.mPat.Init(); s.RecordEvent(8, 67); CHECK(s.mPat.t[0].gate[0].get() == 127); }

  // ---- Metropolix: timing, orders, CTRL destinations, 4 tracks, v1 state
  { MetropolixSeq s; s.mPat.trk[1].mute.set(1);
    for (int t = 2; t < kMxTracks; t++) s.mPat.trk[t].mute.set(1);
    auto ev = run(s, 2.24); CHECK(countOns(ev) == 14); }
  { std::vector<uint8_t> v; mxBuildOrder(5, 8, 0, v); CHECK((v == std::vector<uint8_t>{ 0, 3, 6, 1, 4, 7, 2, 5 })); }
  { MetropolixSeq s; s.mPat.ctrl[0].dest.set(kDStagesLen); s.mPat.ctrl[0].mask.set(1); s.mCtrl[0].store(0.f);
    for (int t = 1; t < kMxTracks; t++) s.mPat.trk[t].mute.set(1);
    auto ev = run(s, 0.99); bool same = true; int first = -1; for (auto& e : ev) if (e.on) { if (first < 0) first = e.note; if (e.note != first) same = false; } CHECK(same); }
  if (kMxTracks >= 4)
  { MetropolixSeq s; for (int t = 0; t < kMxTracks; t++) s.mPat.trk[t].mute.set(t != 3);
    auto ev = run(s, 1.0); CHECK(countOns(ev) > 0); }                                     // the 4th track plays on its own
  { MetropolixSeq a, b; a.mBpm.store(133.f); std::vector<uint8_t> blob; a.Serialize(blob); CHECK(b.Deserialize(blob.data(), blob.size())); CHECK(b.mBpm.load() == 133.f); }

  // ---- DSP: the scale quantizer follows Hold, and both sequencers play together
  { KR106DSP<float> d(6); d.Reset(44100.0, 256);
    d.SetParam(156, valueFromScaleType(1)); d.SetParam(157, 0.0);                        // C major
    d.mHold = true; d.NoteOn(61, 100); d.NoteOff(61);
    auto has = [&](int pitch) { for (int i = 0; i < d.NVoices(); i++) if (d.mVoiceNote[i] == pitch) return true; return false; };
    CHECK(has(60) && !has(61));
    d.SetParam(157, valueFromScaleRoot(2));                                              // D major: C# is in it now
    CHECK(has(61) && !has(60));
    d.AllNotesOff(); }
  { KR106DSP<float> d(6); d.Reset(44100.0, 256); d.mSeq.mRun.store(true); d.mMetro.mRun.store(true);
    std::vector<float> a(256), b(256); float* o[2] = { a.data(), b.data() };
    for (int i = 0; i < 200; i++) d.ProcessBlock(nullptr, o, 2, 256);
    CHECK(d.mSeq.mPos[0].load() >= 0 && d.mMetro.mPos[0].load() >= 0); }                 // both run at once

  // ---- strum and ratchet without the arp: a chord is spread, notes released early never sound
  { KR106DSP<float> d(8); d.Reset(44100.0, 256); d.mFreeStrum = true; d.mArp.mStrum = 0.5f; d.mArp.mRatchet = 1;
    std::vector<float> a(256), b(256); float* o[2] = { a.data(), b.data() };
    d.NoteOn(60, 100); d.NoteOn(64, 100); d.NoteOn(67, 100);
    d.ProcessBlock(nullptr, o, 2, 256);
    auto sounding = [&]() { int n = 0; for (int i = 0; i < d.NVoices(); i++) if (d.mVoiceNote[i] >= 0) n++; return n; };
    CHECK(sounding() == 1);                                                              // only the first note so far
    for (int i = 0; i < 40; i++) d.ProcessBlock(nullptr, o, 2, 256);
    CHECK(sounding() == 3);                                                              // the chord has been strummed
    d.AllNotesOff(); }
  { KR106DSP<float> d(8); d.Reset(44100.0, 256); d.mFreeStrum = true; d.mArp.mStrum = 1.0f;
    std::vector<float> a(256), b(256); float* o[2] = { a.data(), b.data() };
    d.NoteOn(60, 100); d.NoteOn(64, 100); d.NoteOff(64);                                 // released before its time
    for (int i = 0; i < 60; i++) d.ProcessBlock(nullptr, o, 2, 256);
    int n = 0; for (int i = 0; i < d.NVoices(); i++) if (d.mVoiceNote[i] == 64) n++; CHECK(n == 0); d.AllNotesOff(); }
  { KR106DSP<float> d(8); d.Reset(44100.0, 256); d.mFreeStrum = true; d.mArp.mStrum = 0.f; d.mArp.mRatchet = 3; d.mArp.mRate = 600.f;
    std::vector<float> a(256), b(256); float* o[2] = { a.data(), b.data() };
    const uint32_t before = d.mModNoteOns; d.NoteOn(60, 100);
    for (int i = 0; i < 60; i++) d.ProcessBlock(nullptr, o, 2, 256);
    std::printf("ratchet outside the arp: %u hits (expect 3)\n", d.mModNoteOns - before); CHECK(d.mModNoteOns - before == 3); d.AllNotesOff(); }

  // ---- FX 2: every effect on at the same time, loud notes: stays finite and bounded
  { KR106DSP<float> d(8); d.Reset(44100.0, 256);
    const int base = 92 + 77;                                       // kFxChOn (kModBlockStart + 77)
    for (int i = 0; i < 20; i++) d.SetParam(base + i, (i == 0 || i == 4 || i == 9 || i == 14) ? 1.0 : 0.9);
    std::vector<float> a(256), b(256); float* o[2] = { a.data(), b.data() };
    float peak = 0; bool ok = true;
    for (int blk = 0; blk < 400; blk++)
    { if (blk % 20 == 0) { d.NoteOn(48 + (blk / 20) % 12, 127); d.NoteOn(60, 127); d.NoteOn(67, 127); } if (blk % 20 == 10) d.AllNotesOff();
      d.ProcessBlock(nullptr, o, 2, 256);
      for (int i = 0; i < 256; i++) { peak = std::max(peak, std::fabs(a[i])); if (!std::isfinite(a[i]) || !std::isfinite(b[i])) ok = false; } }
    std::printf("fx2 on: peak %.2f\n", peak); CHECK(ok && peak < 4.f); }
  // ---- Metropolix states saved with two tracks (v2) still load, and the new tracks start muted
  { MetropolixSeq s; std::vector<uint8_t> blob; blob.push_back(2);
    auto put = [&](MxPattern& q) { q.each([&](A8& x) { blob.push_back(x.get()); }, 2); };
    MxPattern p; p.Init(); p.pitch[1].set(55); put(p); for (int i = 0; i < kMxPresets; i++) { MxPattern q; q.Init(); put(q); }
    blob.push_back(1); blob.push_back(3); float bpm = 99.f; const uint8_t* bp = reinterpret_cast<const uint8_t*>(&bpm); for (int i = 0; i < 4; i++) blob.push_back(bp[i]); blob.push_back(0); blob.push_back(0);
    CHECK(s.Deserialize(blob.data(), blob.size())); CHECK(s.mPat.pitch[1].get() == 55 && s.mBpm.load() == 99.f && s.mPat.trk[3].mute.get() == 1); }

  // ---- everything at once, randomly
  { std::mt19937 rng(7); auto I = [&](int n) { return (int)(rng() % n); };
    KR106DSP<float> d(6); d.Reset(44100.0, 256); d.mFreeStrum = true; d.mSeq.mRun.store(true); d.mMetro.mRun.store(true);
    std::vector<float> a(256), b(256); float* o[2] = { a.data(), b.data() };
    for (int blk = 0; blk < 2500; blk++)
    {
      if (I(5) == 0) d.NoteOn(36 + I(60), 1 + I(127));
      if (I(5) == 0) d.NoteOff(36 + I(60));
      if (I(60) == 0) d.mHold = !d.mHold;
      if (I(150) == 0) d.AllNotesOff();
      d.mArp.mStrum = (float)(I(100) / 100.0); d.mArp.mRatchet = 1 + I(4); d.SetParam(156, I(100) / 99.0); d.SetParam(157, I(100) / 99.0);
      if (I(25) == 0) { auto& m = d.mMetro.mPat; m.pitch[I(8)].set(I(128)); m.trk[I(kMxTracks)].order.set(I(20)); m.trk[I(kMxTracks)].clkDiv.set(1 + I(4));
        m.ctrl[I(2)].dest.set(I(kMxNumDest)); m.aux[I(3)].dest.set(I(kMxNumDest)); d.mMetro.mCtrl[I(2)].store((float)(I(100) / 100.0)); }
      if (I(30) == 0) d.mMetro.SetLoop(I(kMxTracks), I(3) == 0 ? -1 : I(8), -1);
      if (I(40) == 0) d.mSeq.mGateDelay.store(I(2));
      d.ProcessBlock(nullptr, o, 2, 256);
    }
    d.AllNotesOff(); std::printf("stress ok\n"); }

  std::printf("fails=%d\n", fails);
  return fails;
}
