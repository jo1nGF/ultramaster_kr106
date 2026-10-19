#pragma once

// Scale quantizer sheet (touch UI): root, scale grid and the 12 notes of the custom scale.
// Incoming notes are snapped to the selected scale (see DSP/KR106Scale.h).
//
// The 12 note buttons always show the notes that are allowed. Tapping one edits them as the
// Custom scale - starting from the scale that is selected, so any scale can be tweaked.

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "../PluginProcessor.h"
#include "../DSP/KR106Scale.h"

class KR106ScaleSheet : public juce::Component
{
public:
  explicit KR106ScaleSheet(KR106AudioProcessor* p) : mProc(p)
  {
    setInterceptsMouseClicks(true, true);
  }

  std::function<void()> onClose;

  void resized() override { layout(); }

  void paint(juce::Graphics& g) override
  {
    g.fillAll(juce::Colour(0xb0000000));                                   // dim everything behind
    g.setColour(juce::Colour(0xff1b1b1b));
    g.fillRoundedRectangle(mPanel.toFloat(), 6.f);
    g.setColour(juce::Colour(0xffc60a0a));
    g.fillRect(mPanel.getX() + 6, mPanel.getY(), mPanel.getWidth() - 12, 2);

    const int type = scaleType(), root = scaleRoot();
    const uint16_t shown = (type == kr106::kScaleOff) ? uint16_t(0) : currentMask();

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.f));
    g.drawText("Scale quantizer", mPanel.getX() + 14, mPanel.getY() + 6, 300, 22, juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(10.5f));
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawText("Notes you play are snapped to the nearest note of the scale (Off = no quantizing)",
               mPanel.getX() + 150, mPanel.getY() + 6, 430, 22, juce::Justification::centredLeft);

    auto label = [&](const juce::String& t, int y) {
      g.setColour(juce::Colours::white.withAlpha(0.6f));
      g.setFont(juce::FontOptions(10.f));
      g.drawText(t, mPanel.getX() + 14, y, 400, 14, juce::Justification::centredLeft);
    };
    label("ROOT" + juce::String(type == kr106::kScaleCustom ? "  (not used by Custom)" : ""), mPanel.getY() + 32);
    label("SCALE", mPanel.getY() + 84);
    label("NOTES  (tap to edit as Custom)", mPanel.getY() + 252);

    for (auto& h : mHits)
    {
      bool active = false, dim = false, black = false;
      juce::String text;
      switch (h.kind)
      {
        case Root:  active = (h.idx == root); dim = (type == kr106::kScaleCustom || type == kr106::kScaleOff);
                    text = kr106::kNoteNames[h.idx]; break;
        case Type:  active = (h.idx == type); text = kr106::kScaleDefs[h.idx].name; break;
        case Note:  active = (shown & (1u << h.idx)) != 0; text = kr106::kNoteNames[h.idx];
                    black = juce::String(kr106::kNoteNames[h.idx]).contains("#"); break;
        case Close: text = "Close"; break;
      }
      auto r = h.r.toFloat().reduced(1.f);
      juce::Colour fill = black ? juce::Colour(0xff0e0e0e) : juce::Colour(0xff2a2a2a);
      if (h.kind == Close)       fill = juce::Colour(0xff9a0e0e);
      else if (active)           fill = (h.kind == Note) ? juce::Colour(0xffd8c040) : juce::Colour(0xff9a0e0e);
      g.setColour(fill.withAlpha(dim && !active ? 0.55f : 1.f));
      g.fillRoundedRectangle(r, 4.f);
      g.setColour(juce::Colour(0xff8a8a8a).withAlpha(0.8f));
      g.drawRoundedRectangle(r, 4.f, 1.f);
      g.setColour((h.kind == Note && active) ? juce::Colours::black
                                             : juce::Colours::white.withAlpha(dim && !active ? 0.5f : 1.f));
      g.setFont(juce::FontOptions(h.kind == Type ? 11.5f : 12.5f));
      g.drawFittedText(text, h.r.reduced(2, 0), juce::Justification::centred, 1, 0.7f);
    }
  }

  void mouseUp(const juce::MouseEvent& e) override
  {
    const auto pos = e.getPosition();
    if (!mPanel.contains(pos)) { close(); return; }               // tap outside the panel closes it
    for (auto& h : mHits)
      if (h.r.contains(pos))
      {
        if (h.kind == Close) { close(); return; }                  // the owner may delete this sheet: touch nothing after
        act(h);
        repaint();
        return;
      }
  }

private:
  enum Kind { Root, Type, Note, Close };
  struct Hit { juce::Rectangle<int> r; Kind kind; int idx; };

  juce::RangedAudioParameter* prm(int idx) const { return mProc->getParam(idx); }
  int scaleType() const { return kr106::scaleTypeFromValue(prm(kScaleType)->getValue()); }
  int scaleRoot() const { return kr106::scaleRootFromValue(prm(kScaleRoot)->getValue()); }
  uint16_t customMask() const { return kr106::customMaskFromValue(prm(kScaleMask)->getValue()); }
  uint16_t currentMask() const { return kr106::scaleAbsMask(scaleType(), scaleRoot(), customMask()); }

  void setValue(int idx, float v)
  {
    auto* p = prm(idx);
    p->beginChangeGesture();
    p->setValueNotifyingHost(v);
    p->endChangeGesture();
  }

  void act(const Hit& h)
  {
    switch (h.kind)
    {
      case Root:  setValue(kScaleRoot, kr106::valueFromScaleRoot(h.idx)); break;
      case Type:  setValue(kScaleType, kr106::valueFromScaleType(h.idx)); break;
      case Note:
      {
        uint16_t m = currentMask();                              // start from the selected scale
        m = static_cast<uint16_t>((m ^ (1u << h.idx)) & 0x0FFF);
        setValue(kScaleMask, kr106::valueFromCustomMask(m));
        setValue(kScaleType, kr106::valueFromScaleType(kr106::kScaleCustom));
        break;
      }
      case Close: break;
    }
  }

  void close() { if (onClose) onClose(); }

  void layout()
  {
    mHits.clear();
    mPanel = juce::Rectangle<int>(0, 0, 700, 330).withCentre({ getWidth() / 2, getHeight() / 2 });
    const int x0 = mPanel.getX(), y0 = mPanel.getY();

    mHits.push_back({ { x0 + mPanel.getWidth() - 84, y0 + 4, 78, 24 }, Close, 0 });

    for (int i = 0; i < 12; i++)                                  // root row
      mHits.push_back({ { x0 + 14 + i * 56, y0 + 48, 54, 28 }, Root, i });

    const int cols = 5;                                           // scale grid: Off, 17 scales, Custom
    for (int i = 0; i < kr106::kNumScaleTypes; i++)
      mHits.push_back({ { x0 + 14 + (i % cols) * 136, y0 + 100 + (i / cols) * 34, 132, 30 }, Type, i });

    for (int i = 0; i < 12; i++)                                  // 12 notes
      mHits.push_back({ { x0 + 14 + i * 56, y0 + 270, 54, 40 }, Note, i });
  }

  KR106AudioProcessor* mProc;
  juce::Rectangle<int> mPanel;
  std::vector<Hit> mHits;
};
