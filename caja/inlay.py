"""Dibujo de primera capa (cambio de color) para caja_frente_4botones.
Genera dos STL que se cargan JUNTOS como un objeto de dos partes:
  caja_frente_4botones_color.stl  -> la caja con el dibujo vaciado en la 1.a capa
  dibujo_capa1.stl                -> el dibujo (0.2 mm), otro filamento
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from matplotlib.textpath import TextPath
from matplotlib.font_manager import FontProperties
from manifold3d import CrossSection as CS, FillRule, JoinType
import mini_handheld as mh

INLAY_T = 0.2          # 1 capa = 1 solo cambio de filamento (0.4 = 2 capas, más opaco, 3 cambios)
N = 4

def rr2(cx, cy, w, h, r):
    return mh.rr(w, h, r).translate((cx, cy))

def text_cs(s, height, cx, cy, weight="bold", family="DejaVu Sans"):
    tp = TextPath((0, 0), s, size=10, prop=FontProperties(family=family, weight=weight))
    polys = [p for p in tp.to_polygons() if len(p) > 2]
    c = CS(polys, FillRule.EvenOdd)
    b = c.bounds()                                   # (xmin, ymin, xmax, ymax)
    k = height / (b[3] - b[1])
    c = c.scale((k, k))
    b = c.bounds()
    return c.translate((cx - (b[0] + b[2]) / 2, cy - (b[1] + b[3]) / 2))

def design():
    parts = []
    # 1) marco de la pantalla con franja inferior + texto calado (estilo consola)
    wx, wy = mh.WIN[0] + 1.6, mh.WIN[1] + 1.6          # borde de la ventana en la cara (con chaflán)
    gap, band = 0.8, 1.6
    top = mh.WIN_CY + wy / 2 + gap + band
    bot = mh.WIN_CY - wy / 2 - gap - 5.0               # franja inferior más ancha para el texto
    left, right = mh.CX - wx / 2 - gap - band, mh.CX + wx / 2 + gap + band
    outer = rr2((left + right) / 2, (top + bot) / 2, right - left, top - bot, 3.2)
    inner = rr2(mh.CX, mh.WIN_CY, wx + 2 * gap, wy + 2 * gap, 2.4)
    label = text_cs("MINI·C3", 3.0, mh.CX, (bot + (mh.WIN_CY - wy / 2 - gap)) / 2)
    parts.append(outer - inner - label)
    # 2) placa en cruz detrás de las teclas
    x, y = mh.DPAD
    arm = mh.KEY + 2 * mh.KEY_CLR + 2.0
    span = 2 * mh.PITCH + mh.KEY + 2 * mh.KEY_CLR + 2.0
    parts.append(rr2(x, y, arm, span, 1.6) + rr2(x, y, span, arm, 1.6))
    # 3) botón A: disco dentro del anillo en V, con "A" calada
    rx, ry = mh.RING_C
    disc = CS.circle(4.4, 96).translate((rx, ry))
    parts.append(disc - text_cs("A", 4.2, rx, ry))
    d2 = parts[0] + parts[1] + parts[2]
    # nada del dibujo dentro de los agujeros de teclas / ventana (se recortan igual en 3D)
    return d2

def build():
    d2 = design()
    sh = mh.shell(N)
    slab = d2.extrude(INLAY_T + 0.0)
    dib = sh ^ slab                                    # el dibujo solo donde hay caja (respeta agujeros y anillo)
    body = sh - slab
    return body, dib, d2

def min_feature(d2):
    """Ancho mínimo aproximado: qué se pierde al erosionar 0.25 y volver a crecer (línea de 0.5)."""
    opened = d2.offset(-0.25, JoinType.Round).offset(0.25, JoinType.Round)
    lost = d2.area() - opened.area()
    return lost / d2.area()

if __name__ == "__main__":
    body, dib, d2 = build()
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl")
    for name, m in (("caja_frente_4botones_color", body), ("dibujo_capa1", dib)):
        t = mh.to_trimesh(mh.mx(m))
        t.export(os.path.join(out, f"{name}.stl"))
        print(f"{name:28s} watertight={t.is_watertight} vol={t.volume:.1f} mm3 bbox={np.round(t.bounds, 2).tolist()}")
    print("área del dibujo: %.1f mm2 | fracción más fina que 0.5 mm: %.1f%%" % (d2.area(), 100 * min_feature(d2)))
    # el dibujo + cuerpo tiene que ser exactamente la caja
    sh = mh.shell(N)
    print("diferencia con la caja original:", round(abs((body + dib).volume() - sh.volume()), 4), "mm3")
    print("superposición cuerpo/dibujo:", round((body ^ dib).volume(), 5), "mm3")
