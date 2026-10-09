"""Forme d'une icône : rapport rayon / côté du coin continu d'Apple (plan 60).

Utilisation :
  python tools/measure_icon_shape.py alpha <image.png|icone.ico>
      la forme est le canal alpha de l'image (pour un ICO : sa plus grande image) ;
  python tools/measure_icon_shape.py capture <capture.png> <x0> <y0> <x1> <y1>
      une icône posée sur un fond, dans la boîte donnée (avec une dizaine de pixels de fond autour) ;
  python tools/measure_icon_shape.py --self-test
      formes de synthèse de rapport connu, rendues directement ou étirées ×1,625 : l'écart doit rester sous 0,002.

Méthode : contour à 50 % (interpolé le long du gradient), puis ajustement en distance perpendiculaire du modèle du coin
continu (le même que src/geom/smooth_rect.cpp) sur les deux coins hauts et les bords droits. Pour une capture, la
couverture est locale à chaque coin : fond et icône relevés de part et d'autre d'un contour provisoire.
Dépendances : numpy, scipy, Pillow. Les captures de macOS servent à mesurer ; elles ne sont jamais versionnées.
"""
import io
import struct
import sys

import numpy as np
from PIL import Image
from scipy.ndimage import zoom
from scipy.optimize import least_squares
from scipy.spatial import cKDTree

E = 1.52866483   # le coin continu commence à E·r du sommet
SEGS = [
    (True, (1.08849323, 0), (0.86840689, 0), (0.66993427, 0.06549600)),
    (False, None, None, (0.63149399, 0.07491100)),
    (True, (0.37282392, 0.16905899), (0.16905899, 0.37282392), (0.07491100, 0.63149399)),
    (False, None, None, (0.06549600, 0.66993427)),
    (True, (0, 0.86840689), (0, 1.08849323), (0, E)),
]


def corner_curve(steps=400):
    """Coin en unités de rayon : (X le long du bord horizontal, Y le long du bord vertical), de (E, 0) à (0, E)."""
    pts, cur = [(E, 0.0)], (E, 0.0)
    for curve, c1, c2, end in SEGS:
        if not curve:
            pts.append(end)
        else:
            for k in range(1, steps + 1):
                t = k / steps
                u = 1 - t
                a, b, c, d = u ** 3, 3 * u * u * t, 3 * u * t * t, t ** 3
                pts.append((a * cur[0] + b * c1[0] + c * c2[0] + d * end[0], a * cur[1] + b * c1[1] + c * c2[1] + d * end[1]))
        cur = end
    return np.array(pts)


CORNER = corner_curve()


def outline(x0, x1, y0, r, reach):
    """Moitié haute du modèle : bord haut, coins haut-gauche et haut-droit, bords verticaux jusqu'à y0 + reach."""
    n = 600
    tl = np.stack([x0 + CORNER[:, 1] * r, y0 + CORNER[:, 0] * r], 1)
    tr = np.stack([x1 - CORNER[:, 1] * r, y0 + CORNER[:, 0] * r], 1)
    top = np.stack([np.linspace(x0 + E * r, x1 - E * r, n), np.full(n, y0)], 1)
    left = np.stack([np.full(n, x0), np.linspace(y0 + E * r, y0 + reach, n)], 1)
    right = np.stack([np.full(n, x1), np.linspace(y0 + E * r, y0 + reach, n)], 1)
    return np.vstack([top, tl, tr, left, right])


def contour(a):
    """Points du contour à 50 % : interpolation linéaire entre centres de pixels, dans la direction du gradient."""
    a = np.nan_to_num(a)
    gy, gx = np.gradient(a)
    h, w = a.shape
    pts = []
    for y in range(h):
        for x in range(w - 1):
            v0, v1 = a[y, x], a[y, x + 1]
            if (v0 - 0.5) * (v1 - 0.5) < 0 and abs(gx[y, x] + gx[y, x + 1]) >= abs(gy[y, x] + gy[y, x + 1]):
                pts.append((x + 0.5 + (0.5 - v0) / (v1 - v0), y + 0.5))
    for x in range(w):
        for y in range(h - 1):
            v0, v1 = a[y, x], a[y + 1, x]
            if (v0 - 0.5) * (v1 - 0.5) < 0 and abs(gy[y, x] + gy[y + 1, x]) > abs(gx[y, x] + gx[y + 1, x]):
                pts.append((x + 0.5, y + 0.5 + (0.5 - v0) / (v1 - v0)))
    return np.array(pts)


def fit(pts, x0, x1, y0, r0, reach):
    """Ajuste (x0, x1, y0, r) aux points ; rend le rapport r / côté et l'écart quadratique moyen en pixels."""
    def residual(p):
        return cKDTree(outline(p[0], p[1], p[2], p[3], reach)).query(pts)[0]
    sol = least_squares(residual, [x0, x1, y0, r0], diff_step=1e-5)
    X0, X1, Y0, R = sol.x
    return R / (X1 - X0), float(np.sqrt(np.mean(sol.fun ** 2))), X1 - X0


def measure_coverage(cov, guess_ratio=0.25):
    """Couverture 0..1 (forme posée sur un fond) : rapport rayon / côté, écart (px), côté (px)."""
    ys, xs = np.where(cov > 0.5)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    side = x1 - x0
    pts = contour(cov)
    pts = pts[pts[:, 1] < y0 + 0.5 * (y1 - y0)]   # moitié haute : coins hauts et bords
    return fit(pts, float(x0), float(x1), float(y0), guess_ratio * side, 0.5 * (y1 - y0))


def load_alpha(path):
    data = open(path, "rb").read()
    if data[:4] == b"\x00\x00\x01\x00":   # ICO : la plus grande image
        n = struct.unpack_from("<H", data, 4)[0]
        best = None
        for i in range(n):
            w, _, _, _, _, _, size, off = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
            w = w or 256
            if best is None or w > best[0]:
                best = (w, size, off)
        im = Image.open(io.BytesIO(data[best[2]:best[2] + best[1]]))
    else:
        im = Image.open(path)
    return np.array(im.convert("RGBA"))[:, :, 3].astype(float) / 255.0


def measure_alpha(path, pad=20):
    a = np.pad(load_alpha(path), pad)   # forme plein cadre : marge transparente autour
    return measure_coverage(a)


def measure_capture(path, x0, y0, x1, y1, guess_ratio=0.256):
    """Icône dans une capture : couverture locale de chaque coin haut (fond et icône relevés près du contour)."""
    img = np.array(Image.open(path).convert("RGB")).astype(float)[y0:y1, x0:x1]
    h, w, _ = img.shape
    # Contour provisoire : bords au milieu de la boîte, par la couleur du bord de la boîte (fond) et du centre (icône).
    bg = np.median(np.concatenate([img[:3].reshape(-1, 3), img[:, :3].reshape(-1, 3), img[:, -3:].reshape(-1, 3)]), axis=0)
    fg = np.median(img[h // 2 - 3:h // 2 + 4, w // 2 - 3:w // 2 + 4].reshape(-1, 3), axis=0)
    d = fg - bg
    cov0 = np.clip(((img - bg) @ d) / float(d @ d), 0, 1)
    ys, xs = np.where(cov0 > 0.5)
    px0, px1, py0 = xs.min(), xs.max() + 1, ys.min()
    side = px1 - px0
    reach = 0.45 * side
    Yi, Xi = np.mgrid[0:h, 0:w].astype(float)
    model = outline(px0, px1, py0, guess_ratio * side, reach)
    tree = cKDTree(model)
    pts_all = []
    for left in (True, False):
        reg = ((Xi < px0 + reach) if left else (Xi > px1 - reach)) & (Yi < py0 + reach)
        dist = tree.query(np.stack([Xi[reg], Yi[reg]], 1))[0]
        # signe : à l'intérieur du modèle provisoire (approximation : couverture provisoire > 0,5)
        inside = cov0[reg] > 0.5
        sd = np.where(inside, -dist, dist)
        pix = img[reg]
        f = np.median(pix[(sd < -2.5) & (sd > -5)], axis=0)
        b = np.median(pix[(sd > 2.5) & (sd < 5)], axis=0)
        dv = f - b
        cov = np.full((h, w), np.nan)
        cov[reg] = np.clip(((pix - b) @ dv) / float(dv @ dv), 0, 1)
        c = contour(np.where(reg, cov, 0.0))
        keep = [p for p in c if 0 <= int(round(p[0])) < w and 0 <= int(round(p[1])) < h and reg[int(round(p[1])), int(round(p[0]))]]
        keep = np.array(keep)
        dd = tree.query(keep)[0]
        pts_all.append(keep[dd < 3])
    pts = np.vstack(pts_all)
    return fit(pts, float(px0), float(px1), float(py0), guess_ratio * side, reach)


def synthetic(size, ratio, margin, ss=16):
    """Forme de côté `size` à coins continus, posée à (margin, margin), couverture suréchantillonnée."""
    r = ratio * size
    c = CORNER
    n = size + 2 * margin
    g = (np.arange(int(np.ceil(n)) * ss) + 0.5) / ss - margin
    X, Y = np.meshgrid(g, g)
    box = (X >= 0) & (X <= size) & (Y >= 0) & (Y <= size)
    u = np.minimum(X, size - X) / r
    v = np.minimum(Y, size - Y) / r
    order = np.argsort(c[:, 1])
    f = np.interp(u, c[order, 1], c[order, 0], left=E, right=0.0)   # coin haut-gauche : v = f(u)
    ins = box & ((u >= E) | (v >= E) | (v >= f))
    m = int(np.ceil(n))
    return ins.reshape(m, ss, m, ss).mean(axis=(1, 3))


def self_test():
    worst = 0.0
    for ratio in (0.225, 0.259):
        a = synthetic(256, ratio, 20)
        q, rms, _ = measure_coverage(a)
        print(f"direct 256 px, vrai {ratio} : mesuré {q:.4f} ({q - ratio:+.4f})")
        worst = max(worst, abs(q - ratio))
        b = synthetic(52, ratio, 6)
        for order in (1, 3):
            s = np.clip(zoom(b, 1.625, order=order, grid_mode=True, mode="grid-constant"), 0, 1)
            q, rms, _ = measure_coverage(s)
            print(f"52 px étirée ×1,625 (ordre {order}), vrai {ratio} : mesuré {q:.4f} ({q - ratio:+.4f})")
            worst = max(worst, abs(q - ratio))
    print("auto-contrôle :", "bon" if worst < 0.002 else "ÉCHEC", f"(écart maximal {worst:.4f})")
    return 0 if worst < 0.002 else 1


def main(argv):
    if len(argv) >= 2 and argv[1] == "--self-test":
        return self_test()
    if len(argv) == 3 and argv[1] == "alpha":
        q, rms, side = measure_alpha(argv[2])
    elif len(argv) == 7 and argv[1] == "capture":
        q, rms, side = measure_capture(argv[2], *(int(v) for v in argv[3:7]))
    else:
        print(__doc__)
        return 2
    print(f"rayon / côté {q:.4f} (côté {side:.2f} px, écart {rms:.3f} px)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
