// Repère local du Dock (calculs « comme en bas ») et fenêtre réelle, selon le bord de l'écran.
#include "minitest.h"
#include "../src/layout/edge_frame.h"

using md::DockPosition;
using md::EdgeFrame;

namespace {
void roundtrip(DockPosition edge) {
    EdgeFrame f{edge, 300, 1000};
    for (POINT p : {POINT{0, 0}, POINT{10, 20}, POINT{299, 999}, POINT{150, 640}}) {
        POINT l = f.toLocal(p);
        auto w = f.toWindow(double(l.x), double(l.y));
        CHECK_EQ(LONG(w.x), p.x);
        CHECK_EQ(LONG(w.y), p.y);
    }
}
} // namespace

TEST_CASE(edge_frame_roundtrip_bottom) { roundtrip(DockPosition::Bottom); }
TEST_CASE(edge_frame_roundtrip_left) { roundtrip(DockPosition::Left); }
TEST_CASE(edge_frame_roundtrip_right) { roundtrip(DockPosition::Right); }

TEST_CASE(edge_frame_left_edge_is_x0) {
    // Repère local : x le long du Dock, y croît vers le bord de l'écran (y = cross au bord), comme en bas.
    EdgeFrame left{DockPosition::Left, 300, 1000};
    CHECK_EQ(left.axis(), 1000.0);
    CHECK_EQ(left.cross(), 300.0);
    CHECK_EQ(left.toLocal(POINT{0, 400}).y, 300L);     // contre le bord gauche
    CHECK_EQ(left.toLocal(POINT{0, 400}).x, 400L);     // l'axe suit la hauteur
    EdgeFrame right{DockPosition::Right, 300, 1000};
    CHECK_EQ(right.toLocal(POINT{300, 400}).y, 300L);  // contre le bord droit
    CHECK_EQ(right.toLocal(POINT{0, 400}).y, 0L);
    EdgeFrame bottom{DockPosition::Bottom, 1000, 300};
    CHECK_EQ(bottom.axis(), 1000.0);
    CHECK_EQ(bottom.toLocal(POINT{400, 300}).y, 300L);
}
