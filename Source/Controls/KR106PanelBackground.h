#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kr106touch {

// A panel that fills the page (the sequencers) tells the zoom / pan listener which of its points are empty
// background; only those start a zoom or pan, so the sliders and buttons keep every touch.
struct PanelBackground
{
  virtual ~PanelBackground() = default;
  virtual bool isBackgroundAt(juce::Point<int> pointInPanel) const = 0;
};

} // namespace kr106touch
