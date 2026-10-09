#include "ui_layout.h"

#include <algorithm>
#include <cmath>

namespace md::ui {

PaneLayout layoutPane(const std::vector<GroupShape>& groups) {
    PaneLayout out;
    float y = metrics::contentTop;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const GroupShape& g = groups[i];
        GroupBox box;
        if (i > 0) y += metrics::groupGap;
        if (g.title) {
            box.titleTop = y;
            y += metrics::groupTitle;
        }
        box.top = y;
        for (float h : g.rows) {
            box.rows.push_back({y, h});
            y += h;
        }
        box.height = y - box.top;
        if (g.footer) {
            box.footerTop = y + metrics::footerGap;
            y = box.footerTop + metrics::footerHeight;
        }
        out.groups.push_back(std::move(box));
    }
    out.height = y + metrics::bottomPadding;
    return out;
}

float sliderKnobX(double value, double min, double max, float left, float right) {
    if (max <= min) return left;
    const double t = std::clamp((value - min) / (max - min), 0.0, 1.0);
    return float(left + t * (right - left));
}

double sliderValueAt(float x, double min, double max, double step, float left, float right) {
    if (right <= left || max <= min) return min;
    const double t = std::clamp(double(x - left) / double(right - left), 0.0, 1.0);
    double v = min + t * (max - min);
    if (step > 0) v = min + std::round((v - min) / step) * step;
    return std::clamp(v, min, max);
}

int segmentAt(float x, float left, float right, int count) {
    if (count <= 0 || x < left || x > right || right <= left) return -1;
    return std::min(count - 1, int((x - left) / ((right - left) / float(count))));
}

int menuReleaseChoice(int item, float movedPt, double heldSeconds) {
    if (item < 0) return -1;
    return movedPt >= 4.0f || heldSeconds >= 0.3 ? item : -1;
}

int menuItemAt(float y, float top, float itemHeight, int count) {
    if (itemHeight <= 0 || y < top) return -1;
    const int i = int((y - top) / itemHeight);
    return i < count ? i : -1;
}

int nextFocus(int current, const std::vector<bool>& focusable, bool backwards) {
    const int n = int(focusable.size());
    if (n == 0) return -1;
    int i = current;
    for (int step = 0; step < n; ++step) {
        if (backwards) i = i <= 0 ? n - 1 : i - 1;
        else i = i < 0 || i >= n - 1 ? 0 : i + 1;
        if (focusable[std::size_t(i)]) return i;
    }
    return -1;
}

Panel sidebarPanel(float windowHeight) {
    const float in = metrics::sidebarFloatInset;
    return {in, in, metrics::sidebarWidth - in, windowHeight - in, metrics::sidebarFloatRadius};
}

float sidebarVisibleBottom(float windowHeight) {
    const Panel p = sidebarPanel(windowHeight);
    return p.bottom - p.radius / 2;
}

bool insidePanel(const Panel& p, float x, float y) {
    if (x < p.left || x > p.right || y < p.top || y > p.bottom) return false;
    // Dans un coin : à moins d'un rayon du centre de son arrondi.
    const float cx = x < p.left + p.radius ? p.left + p.radius : x > p.right - p.radius ? p.right - p.radius : x;
    const float cy = y < p.top + p.radius ? p.top + p.radius : y > p.bottom - p.radius ? p.bottom - p.radius : y;
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= p.radius * p.radius;
}

std::vector<float> sidebarRowTops(const std::vector<int>& sizes) {
    std::vector<float> tops;
    float y = metrics::sidebarTop;
    for (std::size_t g = 0; g < sizes.size(); ++g) {
        if (g > 0) y += metrics::sidebarGroupGap;
        for (int i = 0; i < sizes[g]; ++i) {
            tops.push_back(y);
            y += metrics::sidebarRow;
        }
    }
    return tops;
}

int sidebarRowAt(float y, const std::vector<float>& tops) {
    for (std::size_t i = 0; i < tops.size(); ++i)
        if (y >= tops[i] && y < tops[i] + metrics::sidebarRow) return int(i);
    return -1;
}

}  // namespace md::ui
