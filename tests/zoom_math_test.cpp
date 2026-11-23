#include "../src/zoom_math.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using smooth_zoom::View;
using smooth_zoom::step;
using smooth_zoom::advance;
static bool close(double a, double b) { return std::abs(a-b) < 1e-8; }
int main() {
  View v;
  v = step(v, 2.0, .5, .5, 5.0);
  assert(close(v.zoom, 2.0));
  assert(close(v.x, .25) && close(v.y, .25));
  const double sourceAtCursor = v.x + .3/v.zoom;
  View next = step(v, 1.3, .3, .5, 5.0);
  assert(close(sourceAtCursor, next.x + .3/next.zoom));
  View reset = step(v, .1, .5, .5, 5.0);
  assert(close(reset.zoom, 1.0) && close(reset.x, 0.0) && close(reset.y, 0.0));
  assert(close(step(v, 100.0, .5, .5, 3.0).zoom, 3.0));
  assert(close(advance(v, next, .0, 150).zoom, v.zoom));
  assert(close(advance(v, next, .016, 0).zoom, next.zoom));
  const View midway = advance(v, next, .016, 150);
  assert(midway.zoom > v.zoom && midway.zoom < next.zoom);
  std::cout << "Zoom math tests passed\n";
}
