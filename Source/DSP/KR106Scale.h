#pragma once

#include <algorithm>
#include <cstdint>

// Scale quantizer.
//
// Incoming notes are snapped to the nearest note of a scale (ties go down).
// A scale is a 12-bit mask: bit i = the pitch class i semitones above the root.
// The "Custom" scale is different: its mask is absolute (bit 0 = C, bit 1 = C#, ...),
// so the player simply ticks the notes that are allowed, whatever the root is.
//
// Parameters (all stored as 0..1 slider values):
//   Scale type   index / (kNumScaleTypes - 1)      0 = Off
//   Scale root   index / 11                        0 = C ... 11 = B
//   Custom mask  mask / 4095                       bit i = pitch class i (C = 0)
namespace kr106 {

static constexpr int kScaleOff = 0;
static constexpr int kNumScaleTypes = 19;                 // Off + 17 scales + Custom
static constexpr int kScaleCustom = kNumScaleTypes - 1;
static constexpr uint16_t kScaleAllNotes = 0x0FFF;

struct ScaleDef { const char* name; uint16_t mask; };

// Intervals above the root, packed into a mask
#define KR106_SC(...) ([]{ uint16_t m = 0; for (int i : { __VA_ARGS__ }) m |= static_cast<uint16_t>(1u << i); return m; }())

static const ScaleDef kScaleDefs[kNumScaleTypes] = {
  { "Off",              kScaleAllNotes },
  { "Major",            KR106_SC(0, 2, 4, 5, 7, 9, 11) },
  { "Natural minor",    KR106_SC(0, 2, 3, 5, 7, 8, 10) },
  { "Harmonic minor",   KR106_SC(0, 2, 3, 5, 7, 8, 11) },
  { "Melodic minor",    KR106_SC(0, 2, 3, 5, 7, 9, 11) },
  { "Dorian",           KR106_SC(0, 2, 3, 5, 7, 9, 10) },
  { "Phrygian",         KR106_SC(0, 1, 3, 5, 7, 8, 10) },
  { "Lydian",           KR106_SC(0, 2, 4, 6, 7, 9, 11) },
  { "Mixolydian",       KR106_SC(0, 2, 4, 5, 7, 9, 10) },
  { "Locrian",          KR106_SC(0, 1, 3, 5, 6, 8, 10) },
  { "Major pentatonic", KR106_SC(0, 2, 4, 7, 9) },
  { "Minor pentatonic", KR106_SC(0, 3, 5, 7, 10) },
  { "Blues",            KR106_SC(0, 3, 5, 6, 7, 10) },
  { "Whole tone",       KR106_SC(0, 2, 4, 6, 8, 10) },
  { "Diminished",       KR106_SC(0, 2, 3, 5, 6, 8, 9, 11) },
  { "Phrygian dominant", KR106_SC(0, 1, 4, 5, 7, 8, 10) },
  { "Hungarian minor",  KR106_SC(0, 2, 3, 6, 7, 8, 11) },
  { "Japanese (In)",    KR106_SC(0, 1, 5, 7, 8) },
  { "Custom",           kScaleAllNotes },
};
#undef KR106_SC

static constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static inline int scaleTypeFromValue(float v)
{ return std::max(0, std::min(kNumScaleTypes - 1, static_cast<int>(v * static_cast<float>(kNumScaleTypes - 1) + 0.5f))); }
static inline float valueFromScaleType(int i) { return static_cast<float>(i) / static_cast<float>(kNumScaleTypes - 1); }
static inline int scaleRootFromValue(float v)
{ return std::max(0, std::min(11, static_cast<int>(v * 11.f + 0.5f))); }
static inline float valueFromScaleRoot(int i) { return static_cast<float>(i) / 11.f; }
static inline uint16_t customMaskFromValue(float v)
{ return static_cast<uint16_t>(std::max(0, std::min(4095, static_cast<int>(v * 4095.f + 0.5f)))); }
static inline float valueFromCustomMask(uint16_t m) { return static_cast<float>(m & 0x0FFF) / 4095.f; }

static inline uint16_t rotateMask(uint16_t relMask, int root)
{
  uint16_t out = 0;
  for (int i = 0; i < 12; i++)
    if (relMask & (1u << i)) out |= static_cast<uint16_t>(1u << ((i + root) % 12));
  return out;
}

// Absolute mask (bit = pitch class, C = 0) of the scale that is selected. All notes when Off.
static inline uint16_t scaleAbsMask(int type, int root, uint16_t custom)
{
  type = std::max(0, std::min(kNumScaleTypes - 1, type));
  if (type == kScaleOff) return kScaleAllNotes;
  if (type == kScaleCustom) return (custom & 0x0FFF) ? static_cast<uint16_t>(custom & 0x0FFF) : kScaleAllNotes;
  return rotateMask(kScaleDefs[type].mask, ((root % 12) + 12) % 12);
}

// Nearest allowed note (ties go down). A full or empty mask leaves the note alone.
static inline int quantizeNote(int note, uint16_t absMask)
{
  absMask &= 0x0FFF;
  if (absMask == 0 || absMask == kScaleAllNotes) return note;
  for (int d = 0; d <= 6; d++)
  {
    const int lo = note - d, hi = note + d;
    if (lo >= 0 && lo <= 127 && (absMask & (1u << (lo % 12)))) return lo;
    if (hi >= 0 && hi <= 127 && (absMask & (1u << (hi % 12)))) return hi;
  }
  return note;
}

} // namespace kr106
