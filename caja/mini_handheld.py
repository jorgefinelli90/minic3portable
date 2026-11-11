"""
Mini handheld C3 — caja imprimible (opción A, vertical)
--------------------------------------------------------
v2: pulsadores 6x6x5.1 encastrados directo en la caja (sin placa perforada),
    táctil a la derecha (v1 salía espejado), variantes de 4 y 2 botones.
v3: frente plano (el rebaje de la cruz quedaba en el aire al imprimir),
    tapa a presión sin tornillo (faldón + 5 trabas + ranura para abrir).
v3.1: bahía del cargador 26 x 17 (antes 22 x 24). Solo cambia la tapa.
v4: pulsadores 12 x 12 x 6 -> caja 44 x 83 (calculada), táctil parado en el hueco
    entre el botón de arriba y el de la derecha, teclas 10 x 10.
v5: táctil a 7 mm de las teclas (se activaba al rozarlo con el pulgar), trabas más
    gruesas (1.0) con más enganche (0.5) y el botón de abajo girado 90° para que su
    traba no quede pegada a la pared.

Genera:
  caja_frente_4botones.stl / caja_frente_2botones.stl  -> frente (frente en la cama)
  tapa.stl         -> tapa trasera (cara exterior en la cama)
  teclas_x4.stl    -> teclas 5x5 (nuevas: las de v1 no pasan por el bolsillo)

Ejes del modelo: X = ancho visto DE FRENTE de izquierda a derecha (0..46),
      Y = alto (0 = base con los puertos), Z = profundidad (0 = cara frontal).
      Al exportar se espeja en X para que la pieza impresa (frente sobre la cama)
      quede igual a la maqueta. Unidades: mm.

Requiere:  pip install manifold3d trimesh numpy
Uso:       python3 mini_handheld.py  [--check]  (--check mide interferencias)
"""
import sys, os
import numpy as np
import trimesh
from manifold3d import Manifold as M, CrossSection as CS, JoinType, OpType, set_circular_segments

set_circular_segments(72)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl")

# ============================== PARÁMETROS ==============================
# v4: pulsadores 12 x 12 x 6. La disposición se CALCULA a partir de las piezas para que
#     la caja sea lo más chica posible: cruz abajo a la izquierda, táctil parado en el
#     hueco entre el botón de arriba y el de la derecha, OLED arriba.
WALL = 1.6                          # pared lateral
FRONT = 1.6                         # espesor del frente
SHELL_D = 17.0                      # profundidad de la caja frontal
LID_T = 1.6                         # espesor de la tapa  (total = 18.6)
R = 4.5                             # radio de esquinas
R_IN = 2.0                          # radio interno
IN_X0 = IN_Y0 = WALL

# --- Botones: pulsador 12 x 12, alto total 6.0 (base a punta del vástago)
SW = 12.0                           # cuerpo del pulsador
SW_BODY_H = 3.8                     # (solo para el dibujo de chequeo; el bolsillo no depende de esto)
SW_H = 6.0                          # alto total medido
SW_PLUNGER_D = 6.8
SW_PIN = (12.5, 5.0)                # separación de patas (a lo ancho / a lo largo de cada lado)
SW_CLR = 0.15                       # holgura por lado del cuerpo en el bolsillo -> túnel 12.3
KEY = 10.0                          # cabeza de la tecla
KEY_CLR = 0.25                      # holgura por lado en el agujero (agujero 10.5)
KEY_FLANGE = 12.0                   # pestaña: pasa por el túnel (12.3) y no sale por el agujero (10.5)
KEY_FLANGE_T = 0.8
KEY_PROTRUDE = 1.2                  # cuánto sobresale del frente
PLUNGER_GAP = 0.15                  # luz entre vástago y tecla
PLUNGER_Z = FRONT + KEY_FLANGE_T + PLUNGER_GAP   # punta del vástago
SW_TOP_Z = PLUNGER_Z + (SW_H - SW_BODY_H)        # cara superior del cuerpo
SW_BACK_Z = PLUNGER_Z + SW_H                     # base del pulsador (de donde salen las patas)
TUNNEL = SW + 2 * SW_CLR
POCKET = TUNNEL + 1.8               # bloque exterior del bolsillo
FINGER_W, FINGER_T, FINGER_Z0 = 9.0, 1.0, 3.4    # trabas flexibles (v5: más gruesas y más largas)
LIP = 0.65                          # diente de la traba (v5: engancha 0.5 mm, antes 0.3)
LIP_RAMP_TOP = 1.3                  # la rampa de entrada termina 1.3 mm detrás de la base del pulsador
FINGER_GAP_MIN = 0.6                # luz mínima detrás de cada traba para que pueda abrirse
KEY_ROT = {"abajo": 90}             # el de abajo va girado 90°: sus trabas no miran a la pared
CROSS_DEPTH = 0.0                   # sin rebaje: el frente se imprime boca abajo y quedaría en el aire
PITCH = TUNNEL + 0.8                # centro de cruz -> centro de botón (0.8 de pared en las esquinas)
_H = POCKET / 2

# cruz pegada a la esquina inferior izquierda (el túnel queda a 0.9 mm de la pared)
DPAD = (IN_X0 + 0.9 + TUNNEL / 2 + PITCH, IN_Y0 + 0.9 + TUNNEL / 2 + PITCH)   # los lados que miran a la pared son paredes rígidas

# --- Táctil TTP223 (11 x 15, parado) arriba a la derecha de la cruz
TTP_SIZE = (11.0, 15.0)
TTP_CLR = 0.3
TTP_POCKET = 0.5                    # el frente queda en 1.1 mm sobre el sensor
TTP_KEEPOUT = 7.0                   # v5: distancia mínima borde de tecla -> borde del módulo táctil (antes 2.85)
_tw, _th = TTP_SIZE[0] + 2 * TTP_CLR, TTP_SIZE[1] + 2 * TTP_CLR
TTP = (max(DPAD[0] + KEY / 2 + TTP_KEEPOUT - TTP_CLR, DPAD[0] + _H + 0.8) + _tw / 2,      # lejos de la tecla de arriba
       max(DPAD[1] + KEY / 2 + TTP_KEEPOUT - TTP_CLR, DPAD[1] + _H + 0.8) + _th / 2)      # lejos de la tecla derecha
RING_C = (TTP[0], TTP[1] + 1.4)

# --- Ancho: lo que pida lo más a la derecha (botón derecho o táctil), redondeado a 0.5
BATT = (40.0, 30.0, 8.0)            # batería LiPo asumida (va detrás de la OLED)
_right = max(DPAD[0] + PITCH + TUNNEL / 2 + 0.9, TTP[0] + _tw / 2 + 0.6)
W = float(np.ceil(max(_right + WALL, BATT[0] + 2 * (WALL + 0.4)) * 2) / 2)

# --- OLED 1.3" SH1106 (PCB 35.4 x 33.5, agujeros 30 x 28, vidrio 34.5 x 23 x ~1.5)
OLED_PCB = (35.4, 33.5)
PCB_CLR = 0.35                      # holgura por lado (el PCB tiene ±0.3 de tolerancia)
OLED_PCB_Y0 = round(max(TTP[1] + _th / 2, DPAD[1] + PITCH + _H) + 0.8 + PCB_CLR, 2)
OLED_CY = OLED_PCB_Y0 + OLED_PCB[1] / 2
OLED_HOLES = (30.0, 28.0)
OLED_PIN_D = 1.6                    # pines de ubicación (se pueden cortar si no coinciden)
OLED_GLASS_T = 1.5
OLED_PCB_T = 1.2
WIN = (32.4, 19.8)                  # ventana (área visible 31.4 x 16.7 + margen)
# v6: la ventana baja 1.2 mm del lado de los pines (abajo): en la impresión de
# v5 se tapaban las últimas filas de la pantalla. El borde de arriba no se mueve.
WIN_CY = OLED_PCB_Y0 + 15.6 - 0.6
H = float(np.ceil((OLED_PCB_Y0 + OLED_PCB[1] + 1.2 + WALL) * 2) / 2)
CX, CY = W / 2, H / 2
IN_X1, IN_Y1 = W - WALL, H - WALL

UPDOWN = (DPAD[0], DPAD[1], PITCH)  # (variante 2 botones, no se exporta en v4)

# --- Placas en la tapa
ESP = dict(x0=2.2, y0=2.2, w=18.0, l=22.5)            # ESP32-C3 SuperMini
CHG = dict(x0=IN_X1 - 0.2 - 1.2 - 0.2 - 17.0, y0=2.2, w=17.0, l=26.0)   # TP4056 26 x 17, micro-USB centrado
BOARD_BACK_Z = SHELL_D                                 # las placas apoyan en la tapa

# --- Puertos (base)
USBC = dict(cx=ESP["x0"] + ESP["w"] / 2, w=9.4, z0=11.9, bay_w=12.6)
MUSB = dict(cx=CHG["x0"] + CHG["w"] / 2, w=13.0, z0=11.7, bay_w=14.4)
BAY_DEPTH, BAY_Z0 = 0.8, 10.8

# --- Interruptor deslizante SS12D00 (lateral derecho, entre el cargador y la OLED)
SWT = dict(y=round((CHG["y0"] + CHG["l"] + 1.2 + OLED_PCB_Y0) / 2, 1), z=11.9, body=(3.5, 8.6, 3.6), slot=(4.5, 2.0))

# --- Cierre a presión de la tapa
LID_CLR = 0.2                       # holgura faldón / pared
SKIRT_T = 1.0                       # espesor del faldón
SKIRT_Z0 = SHELL_D - 2.4            # punta del faldón
SKIRT_Y0 = 27.6                     # el faldón arranca arriba de las placas
SNAP_H = 0.5                        # cuánto sobresale cada diente
SNAP_Z = (15.3, 15.75, 15.85, 16.3) # perfil del diente: rampa, plano, rampa
GROOVE_DEPTH, GROOVE_Z = 0.6, (15.2, 16.4)
_sy1, _sy2 = OLED_PCB_Y0 + 6.0, H - 12.0
SNAPS = [("izq", _sy1, 8.0), ("izq", _sy2, 8.0), ("der", _sy1, 8.0), ("der", _sy2, 8.0), ("arr", CX, 10.0)]
PRY = ((USBC["cx"] + USBC["bay_w"] / 2 + MUSB["cx"] - MUSB["bay_w"] / 2) / 2 - 2.0,
       (USBC["cx"] + USBC["bay_w"] / 2 + MUSB["cx"] - MUSB["bay_w"] / 2) / 2 + 2.0)   # ranura para abrir con la uña


# ============================== HELPERS ==============================
def rr(w, h, r):
    r = max(0.01, min(r, w / 2 - 0.01, h / 2 - 0.01))
    return CS.square((w - 2 * r, h - 2 * r), center=True).offset(r, JoinType.Round)

def rprism(cx, cy, w, h, r, z0, z1):
    return rr(w, h, r).extrude(z1 - z0).translate((cx, cy, z0))

def slab(cx, cy, w, h, r, z):
    return rprism(cx, cy, w, h, r, z, z + 0.01)

def box(x0, y0, z0, x1, y1, z1):
    return M.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))

def cyl(x, y, z0, z1, d, d2=None):
    return M.cylinder(z1 - z0, d / 2, (d2 if d2 else d) / 2).translate((x, y, z0))

def hull(*ms):
    return M.batch_hull(list(ms))

def U(ms):
    return M.batch_boolean(list(ms), OpType.Add)

def key_centers(n=4):
    if n == 2:
        x, y, d = UPDOWN
        return {"arriba": (x, y + d), "abajo": (x, y - d)}
    x, y = DPAD
    return {"arriba": (x, y + PITCH), "abajo": (x, y - PITCH),
            "izq": (x - PITCH, y), "der": (x + PITCH, y)}

def mx(m):
    """Espeja en X (vista de frente -> pieza impresa con el frente en la cama)."""
    return m.mirror((1, 0, 0)).translate((W, 0, 0))


# ============================== BOLSILLO DEL PULSADOR ==============================
def _place(m, kx, ky, rot=0):
    return (m.rotate((0, 0, rot)) if rot else m).translate((kx, ky, 0))

def switch_pocket_solid(kx, ky, rot=0):
    """Material del bolsillo: base, paredes rígidas del lado de las patas y trabas flexibles
    del otro lado (en el sistema local: patas en ±x, trabas en ±y; rot gira todo)."""
    h, b = POCKET / 2, SW / 2 + SW_CLR
    top = SW_BACK_Z + LIP_RAMP_TOP
    parts = [
        box(-h, -h, FRONT - 0.01, h, h, FINGER_Z0),                 # base
        box(-h, -h, FRONT, -b, h, SW_BACK_Z - 0.8),                  # pared rígida (patas)
        box(b, -h, FRONT, h, h, SW_BACK_Z - 0.8),                    # pared rígida (patas)
    ]
    for sy in (-1, 1):
        y_in, y_out = sy * b, sy * (b + FINGER_T)
        parts.append(box(-FINGER_W / 2, min(y_in, y_out), FINGER_Z0 - 0.01, FINGER_W / 2, max(y_in, y_out), top))
    return _place(U(parts), kx, ky, rot)

def switch_pocket_lips(kx, ky, rot=0):
    """Dientes de las trabas (se agregan DESPUÉS de vaciar el túnel). Cara plana hacia el frente,
    rampa hacia atrás para que el pulsador entre a presión."""
    b = SW / 2 + SW_CLR
    top = SW_BACK_Z + LIP_RAMP_TOP
    lips = []
    for sy in (-1, 1):
        y_in, y_out = sy * b, sy * (b + FINGER_T)
        yl = sy * (b - LIP)
        lip_full = box(-FINGER_W / 2, min(yl, y_out), SW_BACK_Z + 0.05, FINGER_W / 2, max(yl, y_out), SW_BACK_Z + 0.4)
        lip_edge = box(-FINGER_W / 2, min(y_in - sy * 0.01, y_out), top - 0.01, FINGER_W / 2, max(y_in - sy * 0.01, y_out), top)
        lips.append(hull(lip_full, lip_edge))
    return _place(U(lips), kx, ky, rot)

def finger_clearance(kx, ky, rot=0):
    """Volumen que tiene que quedar libre detrás de cada traba para que flexione."""
    b = SW / 2 + SW_CLR
    zz = (FINGER_Z0 + 0.6, SW_BACK_Z + LIP_RAMP_TOP)
    v = [box(-FINGER_W / 2, min(sy * (b + FINGER_T + 0.02), sy * (b + FINGER_T + FINGER_GAP_MIN)), zz[0],
             FINGER_W / 2, max(sy * (b + FINGER_T + 0.02), sy * (b + FINGER_T + FINGER_GAP_MIN)), zz[1]) for sy in (-1, 1)]
    return _place(U(v), kx, ky, rot)

def switch_pocket_cut(kx, ky):
    """Huecos: agujero de la tecla en el frente + túnel 6.3 x 6.3 por donde entran tecla y pulsador."""
    b = SW / 2 + SW_CLR
    body = box(kx - b, ky - b, FRONT - 0.01, kx + b, ky + b, SHELL_D)
    hole = rprism(kx, ky, KEY + 2 * KEY_CLR, KEY + 2 * KEY_CLR, 1.0, -0.1, FRONT + 0.1)
    return U([body, hole])

OLED_PINS = [(CX + sx * OLED_HOLES[0] / 2, OLED_CY + sy * OLED_HOLES[1] / 2)
             for sx in (-1, 1) for sy in (-1, 1)]


# ============================== CIERRE A PRESIÓN ==============================
def _side_frame(side, pos, ln):
    """Devuelve (eje, coord_pared_interna, signo_hacia_afuera, rango a lo largo)."""
    if side == "izq":
        return "x", IN_X0, -1, (pos - ln / 2, pos + ln / 2)
    if side == "der":
        return "x", IN_X1, +1, (pos - ln / 2, pos + ln / 2)
    return "y", IN_Y1, +1, (pos - ln / 2, pos + ln / 2)

def _oriented_box(axis, a0, a1, l0, l1, z0, z1):
    """Caja con 'a' = coordinada perpendicular a la pared y 'l' = a lo largo de la pared."""
    lo, hi = min(a0, a1), max(a0, a1)
    if axis == "x":
        return box(lo, l0, z0, hi, l1, z1)
    return box(l0, lo, z0, l1, hi, z1)

def snap_groove(side, pos, ln):
    """Ranura en la pared con techo a 45° (sin voladizo al imprimir y la tapa sale haciendo palanca)."""
    axis, wall, s, (l0, l1) = _side_frame(side, pos, ln + 1.0)
    z0, z1 = GROOVE_Z
    deep = _oriented_box(axis, wall - s * 0.05, wall + s * GROOVE_DEPTH, l0, l1, z0, z1 - GROOVE_DEPTH)
    edge = _oriented_box(axis, wall - s * 0.05, wall, l0, l1, z0, z1)
    return hull(deep, edge)

def snap_bump(side, pos, ln):
    axis, wall, s, (l0, l1) = _side_frame(side, pos, ln)
    face = wall - s * LID_CLR                       # cara exterior del faldón
    tip = face + s * SNAP_H
    z0, z1, z2, z3 = SNAP_Z
    base = _oriented_box(axis, face - s * 0.3, face, l0, l1, z0, z3)   # pegado al faldón
    peak = _oriented_box(axis, face, tip, l0, l1, z1, z2)
    return hull(base, peak)


# ============================== CAJA FRONTAL ==============================
def shell(n=4):
    outer = hull(slab(CX, CY, W - 1.6, H - 1.6, R - 0.8, 0),
                 rprism(CX, CY, W, H, R, 0.8, SHELL_D))
    # esquinas internas casi rectas para que entren placas y batería
    cavity = rprism(CX, CY, W - 2 * WALL, H - 2 * WALL, R_IN, FRONT, SHELL_D + 1)
    s = outer - cavity

    add = []
    # marco de la OLED (encastre del PCB)
    add.append(box(CX - OLED_PCB[0] / 2 - PCB_CLR - 1.0, OLED_PCB_Y0 - PCB_CLR - 1.0, FRONT,
                   CX + OLED_PCB[0] / 2 + PCB_CLR + 1.0, IN_Y1 + 0.1,
                   FRONT + OLED_GLASS_T + OLED_PCB_T + 0.3))
    # marco del táctil
    tx, ty = TTP
    tw, th = TTP_SIZE[0] + 2 * TTP_CLR, TTP_SIZE[1] + 2 * TTP_CLR
    add.append(box(tx - tw / 2 - 0.8, ty - th / 2 - 0.8, FRONT, tx + tw / 2 + 0.8, ty + th / 2 + 0.8, FRONT + 1.0))
    # bolsillos de los pulsadores
    for k, (kx, ky) in key_centers(n).items():
        add.append(switch_pocket_solid(kx, ky, KEY_ROT.get(k, 0)))
    # soporte del interruptor (piso + topes)
    bx, by, bz = SWT["body"]
    y0, y1 = SWT["y"] - by / 2 - 0.2, SWT["y"] + by / 2 + 0.2
    zf = SWT["z"] - bz / 2 - 0.1
    zb = zf - 3.0
    add.append(hull(box(IN_X1 - 3.1, y0 - 1.2, zb, IN_X1 + 0.1, y1 + 1.2, zf),          # repisa
                    box(IN_X1 - 0.05, y0 - 1.2, zb - 3.2, IN_X1 + 0.1, y1 + 1.2, zf)))   # apoyo a 45° contra la pared
    add.append(box(IN_X1 - 3.1, y0 - 1.2, zf - 0.01, IN_X1 + 0.1, y0, zf + bz + 0.8))   # topes
    add.append(box(IN_X1 - 3.1, y1, zf - 0.01, IN_X1 + 0.1, y1 + 1.2, zf + bz + 0.8))
    s = U([s] + add)

    cut = []
    # ventana con chaflán exterior
    cut.append(hull(rprism(CX, WIN_CY, WIN[0], WIN[1], 1.0, 0.8, FRONT + 0.1),
                    slab(CX, WIN_CY, WIN[0] + 1.6, WIN[1] + 1.6, 1.8, -0.01)))
    # bolsillos de pulsadores (frente plano, sin rebaje)
    for (kx, ky) in key_centers(n).values():
        cut.append(switch_pocket_cut(kx, ky))
    # anillo del botón A: canal en V (45°), se imprime sin puentes
    v = CS([[(5.0, -0.1), (5.9, -0.1), (5.45, 0.4)]]).revolve(96)
    cut.append(v.translate((RING_C[0], RING_C[1], 0)))
    # bolsillo del táctil
    cut.append(box(tx - tw / 2, ty - th / 2, FRONT - TTP_POCKET, tx + tw / 2, ty + th / 2, FRONT + 2))
    # bolsillo PCB OLED (+ lugar para la cinta flex arriba)
    cut.append(box(CX - OLED_PCB[0] / 2 - PCB_CLR, OLED_PCB_Y0 - PCB_CLR, FRONT,
                   CX + OLED_PCB[0] / 2 + PCB_CLR, IN_Y1, FRONT + OLED_GLASS_T + OLED_PCB_T + 0.35))
    cut.append(box(CX - 6, OLED_PCB_Y0 - PCB_CLR - 1.1, FRONT, CX + 6, OLED_PCB_Y0, FRONT + 3.2))  # salida cables
    # interruptor: cuerpo + ranura de la palanca
    cut.append(box(IN_X1 - bx - 0.2, y0, zf, IN_X1 + 0.01, y1, zf + bz + 0.2))
    sl, sh = SWT["slot"]
    cut.append(box(IN_X1 - 0.1, SWT["y"] - sl / 2, SWT["z"] - sh / 2, W + 0.1, SWT["y"] + sl / 2, SWT["z"] + sh / 2))
    # puertos: bahía exterior + ranura abierta hacia la tapa
    for p in (USBC, MUSB):
        cut.append(box(p["cx"] - p["bay_w"] / 2, -0.1, BAY_Z0, p["cx"] + p["bay_w"] / 2, BAY_DEPTH, SHELL_D + 0.1))
        cut.append(box(p["cx"] - p["w"] / 2, -0.1, p["z0"], p["cx"] + p["w"] / 2, WALL + 0.1, SHELL_D + 0.1))
    # ranuras de las trabas de la tapa
    for side, pos, ln in SNAPS:
        cut.append(snap_groove(side, pos, ln))
    # ranura para abrir la tapa con la uña (base, entre los dos puertos)
    cut.append(box(PRY[0], -0.1, SHELL_D - 0.9, PRY[1], WALL - 0.5, SHELL_D + 0.1))
    s = s - U(cut)

    # pines de ubicación de la OLED (después de vaciar el bolsillo)
    pins = [cyl(px, py, FRONT, FRONT + OLED_GLASS_T + OLED_PCB_T + 0.7, OLED_PIN_D) for px, py in OLED_PINS]
    lips = [switch_pocket_lips(kx, ky, KEY_ROT.get(k, 0)) for k, (kx, ky) in key_centers(n).items()]
    return U([s] + pins + lips)


# ============================== TAPA ==============================
def lid():
    z0, z1 = SHELL_D, SHELL_D + LID_T
    l = hull(rprism(CX, CY, W, H, R, z0, z1 - 0.6), slab(CX, CY, W - 1.2, H - 1.2, R - 0.6, z1 - 0.01))
    rim_z = z0 - 1.2
    add = []
    # bolsillos para las placas (ESP32 y cargador)
    e, c = ESP, CHG
    # ESP32: costilla a la derecha + tope arriba
    add.append(box(e["x0"] + e["w"] + 0.2, IN_Y0 + 0.2, rim_z, e["x0"] + e["w"] + 1.2, e["y0"] + e["l"] + 1.0, z0))
    add.append(box(IN_X0 + 0.2, e["y0"] + e["l"] + 0.2, rim_z, e["x0"] + e["w"] + 1.2, e["y0"] + e["l"] + 1.0, z0))
    # cargador: costillas a los dos lados + tope arriba (bahía 17.4 x 26.2)
    add.append(box(c["x0"] - 1.2, IN_Y0 + 0.2, rim_z, c["x0"] - 0.2, c["y0"] + c["l"] + 1.0, z0))
    add.append(box(c["x0"] + c["w"] + 0.2, IN_Y0 + 0.2, rim_z, c["x0"] + c["w"] + 1.2, c["y0"] + c["l"] + 1.0, z0))
    add.append(box(c["x0"] - 1.2, c["y0"] + c["l"] + 0.2, rim_z, c["x0"] + c["w"] + 1.2, c["y0"] + c["l"] + 1.0, z0))
    inside = rprism(CX, CY, W - 2 * WALL - 2 * LID_CLR, H - 2 * WALL - 2 * LID_CLR, R_IN - LID_CLR, rim_z - 1, z0 + 0.01)
    add = [U(add) ^ inside]
    # lengüetas que cierran la parte de arriba de las ranuras de los puertos
    for p in (USBC, MUSB):
        add.append(box(p["cx"] - p["w"] / 2 + 0.15, BAY_DEPTH + 0.05, 16.4, p["cx"] + p["w"] / 2 - 0.15, WALL, z0))
    # faldón perimetral (de las placas para arriba) + dientes a presión
    sw, sh = W - 2 * WALL - 2 * LID_CLR, H - 2 * WALL - 2 * LID_CLR
    skirt = rprism(CX, CY, sw, sh, R_IN - LID_CLR, SKIRT_Z0, z0 + 0.01) - \
            rprism(CX, CY, sw - 2 * SKIRT_T, sh - 2 * SKIRT_T, max(0.3, R_IN - LID_CLR - SKIRT_T), SKIRT_Z0 - 1, z0 + 1)
    skirt = skirt ^ box(0, SKIRT_Y0, SKIRT_Z0 - 1, W, H, z0 + 1)
    add.append(skirt)
    for side, pos, ln in SNAPS:
        add.append(snap_bump(side, pos, ln))
    l = U([l] + add)
    cut = []
    for p in (USBC, MUSB):
        cut.append(box(p["cx"] - p["bay_w"] / 2, -0.1, BAY_Z0, p["cx"] + p["bay_w"] / 2, BAY_DEPTH, z1 + 0.1))
    return l - U(cut)


# ============================== TECLAS ==============================
def key_cap():
    f = rprism(0, 0, KEY_FLANGE, KEY_FLANGE, 0.8, 0, KEY_FLANGE_T)
    top = KEY_FLANGE_T + FRONT - CROSS_DEPTH + KEY_PROTRUDE + CROSS_DEPTH   # sobresale KEY_PROTRUDE del frente
    head = hull(rprism(0, 0, KEY, KEY, 0.8, KEY_FLANGE_T - 0.01, top - 0.4), slab(0, 0, KEY - 0.8, KEY - 0.8, 0.5, top - 0.01))
    arrow = CS([[(0, 2.4), (-2.2, -1.4), (2.2, -1.4)]]).extrude(1.0).translate((0, 0, top - 0.45))
    return U([f, head]) - arrow

def keys_plate():
    k = key_cap()
    return U([k.translate((dx, dy, 0)) for dx in (0, 15) for dy in (0, 15)])


# ============================== EXPORT ==============================
def to_trimesh(m):
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3],
                           faces=np.asarray(mesh.tri_verts), process=True)

def export_all():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    sh4, ld = shell(4), lid()
    parts = {"caja_frente_4botones": sh4, "tapa": ld, "teclas_x4": keys_plate()}
    top_tests = max(TTP[1] + _th / 2 + 1.0, DPAD[1] + PITCH + _H + 0.2)
    printable = {
        "caja_frente_4botones": mx(sh4),                                          # frente en la cama
        "tapa": mx(ld).mirror((0, 0, 1)).translate((0, 0, SHELL_D + LID_T)),     # cara exterior en la cama
        "teclas_x4": parts["teclas_x4"],                                          # pestaña en la cama
        # pruebas rápidas
        "prueba_botones_y_tactil": mx(sh4 ^ box(-1, -1, -1, W + 1, top_tests, SW_BACK_Z + 0.9)),
        "prueba_oled": mx(sh4 ^ box(-1, OLED_PCB_Y0 - 3.0, -1, W + 1, H + 1, FRONT + OLED_GLASS_T + OLED_PCB_T + 1.0)),
        "prueba_cierre": mx(sh4 ^ box(-1, -1, SHELL_D - 5.0, W + 1, H + 1, SHELL_D + 1)).translate((0, 0, -(SHELL_D - 5.0))),
    }
    for name, m in printable.items():
        t = to_trimesh(m)
        t.export(os.path.join(OUT, f"{name}.stl"))
        print(f"{name:26s} watertight={t.is_watertight}  vol={t.volume/1000:.2f} cm3  "
              f"bbox={np.round(t.extents, 2)}")
    return parts


# ============================== CHEQUEO DE INTERFERENCIAS ==============================
def dummies(n=4):
    """Volúmenes aproximados de los componentes reales, en posición de armado."""
    d = {}
    gy0 = OLED_PCB_Y0 + 5.9
    d["oled_vidrio"] = box(CX - 17.25, gy0, FRONT + 0.01, CX + 17.25, gy0 + 23, FRONT + OLED_GLASS_T)
    pcb = box(CX - OLED_PCB[0] / 2, OLED_PCB_Y0, FRONT + OLED_GLASS_T, CX + OLED_PCB[0] / 2,
              OLED_PCB_Y0 + OLED_PCB[1], FRONT + OLED_GLASS_T + OLED_PCB_T)
    d["oled_pcb"] = pcb - U([cyl(x, y, 0, 10, 2.3) for x, y in OLED_PINS])
    d["oled_flex"] = box(CX - 11, OLED_PCB_Y0 + OLED_PCB[1], FRONT + 0.3, CX + 11, OLED_PCB_Y0 + OLED_PCB[1] + 0.4, FRONT + 3.0)
    d["oled_header"] = U([box(CX - 5.1, OLED_PCB_Y0 + 0.6, FRONT + OLED_GLASS_T + OLED_PCB_T, CX + 5.1, OLED_PCB_Y0 + 3.2, FRONT + OLED_GLASS_T + OLED_PCB_T + 2.5),
                          box(CX - 4.2, OLED_PCB_Y0 + 1.6, FRONT + 5.2, CX + 4.2, OLED_PCB_Y0 + 2.2, FRONT + 11.2)])  # pines rectos sin conector
    d["oled_dorso"] = box(CX - 13, OLED_PCB_Y0 + 3, FRONT + 2.7, CX + 13, OLED_PCB_Y0 + 30, FRONT + 3.8)
    for k, (kx, ky) in key_centers(n).items():
        parts = [box(0 - SW / 2, 0 - SW / 2, SW_TOP_Z, 0 + SW / 2, 0 + SW / 2, SW_BACK_Z),
                 cyl(0, 0, PLUNGER_Z, SW_TOP_Z + 0.01, SW_PLUNGER_D)]
        for sx in (-1, 1):
            for sy in (-1, 1):
                px, py = 0 + sx * SW_PIN[0] / 2, 0 + sy * SW_PIN[1] / 2
                po = SW / 2 + 0.6                                                                  # hasta dónde sale la pata
                parts.append(box(min(0 + sx * (SW / 2 - 0.3), 0 + sx * po), py - 0.4, SW_BACK_Z - 0.5,
                                 max(0 + sx * (SW / 2 - 0.3), 0 + sx * po), py + 0.4, SW_BACK_Z))   # hombro de la pata
                parts.append(box(min(px - sx * 0.15, 0 + sx * po), py - 0.4, SW_BACK_Z - 0.01,
                                 max(px - sx * 0.15, 0 + sx * po), py + 0.4, SW_BACK_Z + 3.5))       # pata sin cortar
                parts.append(box(px - 0.7, py - 0.6, SW_BACK_Z + 2.5, px + 0.7, py + 0.6, SW_BACK_Z + 3.7))  # soldadura + cable
        d[f"pulsador_{k}"] = _place(U(parts), kx, ky, KEY_ROT.get(k, 0))
        d[f"tecla_{k}"] = key_cap().mirror((0, 0, 1)).translate((kx, ky, FRONT + KEY_FLANGE_T))
    tx, ty = TTP
    d["ttp223"] = box(tx - 5.5, ty - 7.5, FRONT - TTP_POCKET + 0.01, tx + 5.5, ty + 7.5, FRONT - TTP_POCKET + 1.6)
    d["ttp223_comp"] = box(tx - 4, ty - 5, FRONT - TTP_POCKET + 1.6, tx + 4, ty + 5, FRONT - TTP_POCKET + 2.8)
    e = ESP
    d["esp32_pcb"] = box(e["x0"], e["y0"], BOARD_BACK_Z - 1.0, e["x0"] + e["w"], e["y0"] + e["l"], BOARD_BACK_Z - 0.01)
    d["esp32_usbc"] = box(USBC["cx"] - 4.47, e["y0"] - 0.6, BOARD_BACK_Z - 1.0 - 3.2, USBC["cx"] + 4.47, e["y0"] + 7.3, BOARD_BACK_Z - 1.0)
    d["esp32_comp"] = box(e["x0"] + 1, e["y0"] + 8, BOARD_BACK_Z - 2.2, e["x0"] + e["w"] - 1, e["y0"] + e["l"] - 1, BOARD_BACK_Z - 1.0)
    c = CHG
    d["carg_pcb"] = box(c["x0"], c["y0"], BOARD_BACK_Z - 1.2, c["x0"] + c["w"], c["y0"] + c["l"], BOARD_BACK_Z - 0.01)
    d["carg_microusb"] = box(MUSB["cx"] - 3.9, c["y0"] - 0.5, BOARD_BACK_Z - 1.2 - 2.8, MUSB["cx"] + 3.9, c["y0"] + 5, BOARD_BACK_Z - 1.2)
    d["carg_comp"] = box(c["x0"] + 1, c["y0"] + 6, BOARD_BACK_Z - 1.2 - 3.0, c["x0"] + c["w"] - 1, c["y0"] + c["l"] - 1, BOARD_BACK_Z - 1.2)
    d["bateria_40x30x8"] = box(CX - BATT[0] / 2, OLED_PCB_Y0 + 3.6, FRONT + 4.1, CX + BATT[0] / 2, OLED_PCB_Y0 + 3.6 + BATT[1], FRONT + 4.1 + BATT[2])
    bx, by, bz = SWT["body"]
    d["interruptor"] = box(IN_X1 - bx, SWT["y"] - by / 2, SWT["z"] - bz / 2, IN_X1, SWT["y"] + by / 2, SWT["z"] + bz / 2)
    d["interruptor_palanca"] = box(IN_X1, SWT["y"] - 0.75, SWT["z"] - 0.75, IN_X1 + 3.0, SWT["y"] + 0.75, SWT["z"] + 0.75)
    d["interruptor_pines"] = box(IN_X1 - bx - 3.5, SWT["y"] - 2.5, SWT["z"] - 0.6, IN_X1 - bx, SWT["y"] + 2.5, SWT["z"] + 0.6)
    return d

def check(parts, n=4):
    print(f"\n========== variante {n} botones ==========")
    d = dummies(n)
    printed = {"caja": parts[f"caja_frente_{n}botones"], "tapa": parts["tapa"]}
    bad = 0
    print("\n-- interferencias pieza impresa vs componente (mm3) --")
    for pn, pm in printed.items():
        for dn, dm in d.items():
            v = (pm ^ dm).volume()
            if v > 0.01:
                bad += 1
                print(f"  CHOCA {pn} x {dn}: {v:.3f}")
    print("-- caja vs tapa --")
    v = (printed["caja"] ^ printed["tapa"]).volume()
    print(f"  {v:.4f}")
    print("-- componentes entre sí --")
    names = list(d)
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            a, b = names[i], names[j]
            if a.split("_")[0] == b.split("_")[0] and a.split("_")[0] in ("oled", "esp32", "carg", "interruptor", "ttp223"):
                continue
            v = (d[a] ^ d[b]).volume()
            if v > 0.01:
                bad += 1
                print(f"  CHOCA {a} x {b}: {v:.3f}")
    print("-- espacio libre detrás de cada traba (tiene que ser 0) --")
    sh = printed["caja"]
    for k, (kx, ky) in key_centers(n).items():
        v = (sh ^ finger_clearance(kx, ky, KEY_ROT.get(k, 0))).volume()
        print(f"  {k:7s} {v:.3f} mm3" + ("   <-- TRABA BLOQUEADA" if v > 0.01 else ""))
        bad += v > 0.01
    print(f"\nTotal de choques: {bad}")
    return d


if __name__ == "__main__":
    parts = export_all()
    print(f"\nCaja {W} x {H} x {SHELL_D + LID_T} mm | cruz {DPAD} paso {PITCH} | táctil {TTP} | OLED desde y={OLED_PCB_Y0}")
    if "--check" in sys.argv:
        check(parts, 4)
