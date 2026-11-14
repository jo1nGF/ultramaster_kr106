// Draws both sequencer panels and throws thousands of touches and drags at them (with sanitizers on).
#include <cstdio>
#include <functional>
#include "Source/Controls/KR106SeqPage.h"
int main()
{
  KR106AudioProcessor p;
  KR106SeqPage page(p); page.resized();
  juce::Graphics g; long n = 0;
  for (int mode = 0; mode < 2; mode++)
  {
    p.setSeqMode(mode);
    KR106VarigatePanel vp(p); KR106MetropolixPanel mp(p); vp.resized(); mp.resized();
    for (int pass = 0; pass < 4; pass++)
      for (int y = 0; y < 356; y += 5) for (int x = 0; x < 870; x += 5)
      {
        juce::MouseEvent e; e.p = { x, y };
        auto drive = [&](auto& panel) {
          panel.mouseDown(e); e.p = { x + (pass % 3) * 9, y + 6 + pass * 3 }; panel.mouseDrag(e); e.p = { x + 3, y - 8 }; panel.mouseDrag(e); e.p = { x, y }; panel.mouseUp(e);
          panel.paint(g); (void) panel.isBackgroundAt({ x, y }); };
        if (mode == 0) drive(vp); else drive(mp);
        n++;
      }
  }
  std::printf("%ld events ok\n", n);
  return 0;
}
