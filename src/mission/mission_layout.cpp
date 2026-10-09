#include "mission_layout.h"

#include <algorithm>
#include <numeric>

#include "../core/strings.h"

namespace md {

MissionRect missionArea(const MissionRect& work, double scale) {
    const double side = 48 * scale, top = 64 * scale, bottom = 48 * scale;
    return {work.x + side, work.y + top, std::max(0.0, work.w - 2 * side), std::max(0.0, work.h - top - bottom)};
}

std::vector<MissionRect> missionLayout(const std::vector<MissionRect>& windows, const MissionRect& area, double gap,
                                       double rowGap) {
    if (rowGap < 0) rowGap = gap;
    const std::size_t n = windows.size();
    std::vector<MissionRect> out(n);
    if (!n) return out;
    std::vector<MissionRect> win = windows;
    for (auto& w : win) {   // une taille nulle compte pour 1 × 1
        w.w = std::max(w.w, 1.0);
        w.h = std::max(w.h, 1.0);
    }
    std::vector<std::size_t> byY(n);
    std::iota(byY.begin(), byY.end(), 0);
    std::stable_sort(byY.begin(), byY.end(),
                     [&](std::size_t a, std::size_t b) { return win[a].y + win[a].h / 2 < win[b].y + win[b].h / 2; });

    std::vector<std::vector<std::size_t>> best;
    double bestScale = -1;
    for (std::size_t r = 1; r <= n; ++r) {
        std::vector<std::vector<std::size_t>> rows(r);
        for (std::size_t k = 0; k < n; ++k) rows[k * r / n].push_back(byY[k]);
        double heights = 0, widthScale = 1e300;
        for (auto& row : rows) {
            std::stable_sort(row.begin(), row.end(),
                             [&](std::size_t a, std::size_t b) { return win[a].x + win[a].w / 2 < win[b].x + win[b].w / 2; });
            double w = 0, h = 0;
            for (std::size_t i : row) {
                w += win[i].w;
                h = std::max(h, win[i].h);
            }
            const double room = area.w - gap * double(row.size() - 1);
            widthScale = std::min(widthScale, room > 0 ? room / w : 0.0);
            heights += h;
        }
        const double roomH = area.h - rowGap * double(r - 1);
        const double s = std::max(0.0, std::min({1.0, widthScale, roomH > 0 ? roomH / heights : 0.0}));
        if (s > bestScale + 1e-12) {
            bestScale = s;
            best = std::move(rows);
        }
    }

    const double s = bestScale;
    double total = rowGap * double(best.size() - 1);
    std::vector<double> rowH(best.size(), 0);
    for (std::size_t r = 0; r < best.size(); ++r) {
        for (std::size_t i : best[r]) rowH[r] = std::max(rowH[r], win[i].h * s);
        total += rowH[r];
    }
    double y = area.y + std::max(0.0, (area.h - total) / 2);
    for (std::size_t r = 0; r < best.size(); ++r) {
        double rowW = gap * double(best[r].size() - 1);
        for (std::size_t i : best[r]) rowW += win[i].w * s;
        double x = area.x + std::max(0.0, (area.w - rowW) / 2);
        for (std::size_t i : best[r]) {
            const double w = win[i].w * s, h = win[i].h * s;
            out[i] = {x, y + (rowH[r] - h) / 2, w, h};
            x += w + gap;
        }
        y += rowH[r] + rowGap;
    }
    return out;
}

int missionHit(const std::vector<MissionRect>& rects, double x, double y) {
    for (std::size_t i = rects.size(); i-- > 0;) {
        const MissionRect& r = rects[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return int(i);
    }
    return -1;
}

int missionNeighbor(const std::vector<MissionRect>& rects, int from, int dx, int dy) {
    if (rects.empty()) return -1;
    const auto cx = [&](int i) { return rects[std::size_t(i)].x + rects[std::size_t(i)].w / 2; };
    const auto cy = [&](int i) { return rects[std::size_t(i)].y + rects[std::size_t(i)].h / 2; };
    const int n = int(rects.size());
    if (from < 0 || from >= n) {   // la première : la plus haute, puis la plus à gauche
        int best = 0;
        for (int i = 1; i < n; ++i)
            if (cy(i) < cy(best) - 0.5 || (std::abs(cy(i) - cy(best)) <= 0.5 && cx(i) < cx(best))) best = i;
        return best;
    }
    int best = from;
    double bestScore = 0;
    for (int i = 0; i < n; ++i) {
        if (i == from) continue;
        const double ddx = cx(i) - cx(from), ddy = cy(i) - cy(from);
        const double along = ddx * dx + ddy * dy;              // chemin dans la direction voulue
        const double across = dx != 0 ? std::abs(ddy) : std::abs(ddx);   // décalage de côté, pénalisé
        if (along <= 1) continue;
        const double score = along + 2 * across;
        if (best == from || score < bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

MissionRect lerpRect(const MissionRect& a, const MissionRect& b, double t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.w + (b.w - a.w) * t, a.h + (b.h - a.h) * t};
}

double easeOut(double t) {
    const double u = 1 - std::clamp(t, 0.0, 1.0);
    return 1 - u * u * u;
}

MissionShelf missionShelf(const std::vector<MissionRect>& minimized, const MissionRect& area, double gap, double labelRoom) {
    MissionShelf out{{}, area, area.y + area.h};
    if (minimized.empty()) return out;
    const std::size_t n = minimized.size();
    if (area.w <= 0 || area.h <= 0) {   // une place (vide) par fenêtre : l'appelant les lit toutes
        out.rects.assign(n, MissionRect{area.x, area.y, 0, 0});
        return out;
    }
    const double band = area.h * 0.18;               // bas de l'écran, comme les fenêtres réduites de macOS
    const double rowH = std::max(1.0, band - gap);   // trait de séparation au-dessus de la rangée
    // Beaucoup de fenêtres : les écarts rétrécissent aussi (jamais de largeur nulle ou négative).
    const double g = n > 1 ? std::min(gap, 0.25 * area.w / double(n - 1)) : gap;
    std::vector<double> w(n), h(n);
    double widths = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double sw = std::max(1.0, minimized[i].w), sh = std::max(1.0, minimized[i].h);
        const double k = std::min(1.0, rowH / sh);   // jamais agrandie
        w[i] = sw * k;
        h[i] = sh * k;
        widths += w[i];
    }
    const double gaps = g * double(n - 1);
    const double fit = widths + gaps > area.w ? (area.w - gaps) / widths : 1.0;
    double x = area.x + (area.w - std::min(widths * fit + gaps, area.w)) / 2;
    const double bottom = area.y + area.h;
    for (std::size_t i = 0; i < n; ++i) {
        const double rw = w[i] * fit, rh = h[i] * fit;
        out.rects.push_back({x, bottom - rh, rw, rh});
        x += rw + g;
    }
    // Les fenêtres ouvertes au-dessus, avec la place de leur pastille de titre.
    out.above = {area.x, area.y, area.w, std::max(0.0, area.h - band - labelRoom)};
    out.lineY = area.y + area.h - band + gap / 2;
    return out;
}

std::optional<HotkeySpec> parseAppExposeHotkey(const std::wstring& text) { return parseHotkey(text); }

std::optional<HotkeySpec> parseMissionHotkey(const std::wstring& text) { return parseHotkey(text); }

} // namespace md
