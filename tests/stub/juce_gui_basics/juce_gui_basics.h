#pragma once
// A tiny stand-in for the JUCE calls the sequencer panels use, only to compile and exercise them without JUCE.
#include <string>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <functional>
namespace juce {
using uint32 = std::uint32_t;
template <class T> T jlimit(T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }
template <class T> T jmax(T a, T b) { return a > b ? a : b; }
inline int roundToInt(float v) { return (int) std::lround(v); }
template <class T> struct MathConstants { static constexpr T pi = (T) 3.14159265358979323846; };
class String {
public:
  String() = default; String(const char* s) : s_(s) {} String(int i) : s_(std::to_string(i)) {} String(unsigned u) : s_(std::to_string(u)) {}
  String(double d, int) : s_(std::to_string(d)) {}
  String operator+(const String& o) const { String r; r.s_ = s_ + o.s_; return r; }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  bool isEmpty() const { return s_.empty(); } bool isNotEmpty() const { return !s_.empty(); }
  String substring(int a, int b) const { return String(s_.substr(a, b - a).c_str()); }
  String substring(int a) const { return String(s_.substr(a).c_str()); }
  String toUpperCase() const { return *this; }
  bool contains(const char*) const { return false; }
  std::string s_;
};
inline String operator+(const char* a, const String& b) { return String(a) + b; }
template <class T> struct Point { T x{}, y{}; Point() = default; Point(T a, T b) : x(a), y(b) {} Point<float> toFloat() const { return { (float) x, (float) y }; }
  float getDistanceFrom(Point<float> o) const { return std::hypot(x - o.x, y - o.y); } };
template <class T> struct Rectangle {
  T x{}, y{}, w{}, h{};
  Rectangle() = default; Rectangle(T W, T H) : w(W), h(H) {} Rectangle(T X, T Y, T W, T H) : x(X), y(Y), w(W), h(H) {}
  T getX() const { return x; } T getY() const { return y; } T getWidth() const { return w; } T getHeight() const { return h; }
  T getRight() const { return x + w; } T getBottom() const { return y + h; } T getCentreX() const { return x + w / 2; } T getCentreY() const { return y + h / 2; }
  Point<T> getCentre() const { return { getCentreX(), getCentreY() }; }
  Rectangle withCentre(Point<T> c) const { return { c.x - w / 2, c.y - h / 2, w, h }; }
  Rectangle reduced(T a) const { return { x + a, y + a, w - 2 * a, h - 2 * a }; }
  Rectangle reduced(T a, T b) const { return { x + a, y + b, w - 2 * a, h - 2 * b }; }
  Rectangle expanded(T a, T b) const { return { x - a, y - b, w + 2 * a, h + 2 * b }; }
  Rectangle expanded(T a) const { return { x - a, y - a, w + 2 * a, h + 2 * a }; }
  bool contains(Point<T> p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
  Rectangle<float> toFloat() const { return { (float) x, (float) y, (float) w, (float) h }; }
};
struct Justification { enum Flags { centred, centredLeft, centredRight }; Justification(Flags) {} };
struct Colour { Colour() = default; Colour(uint32) {} Colour withAlpha(float) const { return *this; } Colour interpolatedWith(Colour, float) const { return *this; } };
namespace Colours { static const Colour white; }
struct Font { enum FontStyleFlags { plain, bold }; };
struct FontOptions { FontOptions(float) {} FontOptions(float, Font::FontStyleFlags) {} };
struct Path { void addTriangle(float, float, float, float, float, float) {} };
struct Graphics {
  void fillAll(Colour) {} void setColour(Colour) {} void setFont(FontOptions) {}
  void drawRect(Rectangle<int>, int) {} void drawRect(Rectangle<float>, float) {} void drawRect(int, int, int, int, int) {}
  void fillRect(int, int, int, int) {} void fillRect(float, float, float, float) {} void fillRect(Rectangle<int>) {} void fillRect(Rectangle<float>) {}
  void fillRoundedRectangle(float, float, float, float, float) {} void fillRoundedRectangle(Rectangle<float>, float) {}
  void drawRoundedRectangle(Rectangle<float>, float, float) {}
  void fillEllipse(float, float, float, float) {} void drawEllipse(float, float, float, float, float) {}
  void drawLine(float, float, float, float, float) {} void fillPath(const Path&) {}
  void drawText(const String&, int, int, int, int, Justification) {}
  void drawFittedText(const String&, Rectangle<int>, Justification, int, float = 1.f) {}
};
struct MouseEvent { Point<int> p; Point<int> getPosition() const { return p; } };
struct Time { static uint32 getMillisecondCounter() { return 0; } };
class Timer { public: virtual ~Timer() = default; void startTimerHz(int) {} virtual void timerCallback() = 0; };
class Component { public: virtual ~Component() = default; virtual void paint(Graphics&) {} virtual void resized() {} virtual void mouseDown(const MouseEvent&) {} virtual void mouseDrag(const MouseEvent&) {} virtual void mouseUp(const MouseEvent&) {}
  Rectangle<int> getLocalBounds() const { return Rectangle<int>(0, 0, 870, 356); } int getWidth() const { return 118; } int getHeight() const { return 20; }
  void addAndMakeVisible(Component&) {} void setBounds(Rectangle<int>) {} void setBounds(int, int, int, int) {} void setVisible(bool) {}
  bool isShowing() const { return true; } void repaint() {} };
}
