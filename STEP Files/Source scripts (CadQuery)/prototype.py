"""Mavrik Pocket prototype case for the parts that were ordered. All sizes in mm.

Fits: ESP32-S3 N16R8 dual USB-C dev board, 2.8" SPI ILI9341 module (MSP2807 style, 50 x 86 PCB),
3.7 V 1200 mAh 523450 battery, KY-040 / HW-040 encoder, 0.5 W 8 ohm speaker, USB-C charger/booster,
two 12 x 12 tactile switches (Action on the right side, Back on the left side).

Axes: X = width, Y = height (screen at +Y), Z = thickness. Back at Z = 0, front face at Z = T.
Part sizes marked MEASURE are typical values: check yours with a ruler and change them here.
"""
import math
import cadquery as cq

# ---------------------------------------------------------------- case
W, H, T = 64.0, 140.0, 26.0
R_PLAN = 8.0
WALL, FACE, BACK = 2.0, 2.0, 2.0
IN_W, IN_H = W - 2 * WALL, H - 2 * WALL      # 60 x 136
Z_IN0, Z_IN1 = BACK, T - FACE                # cavity from z 2 to z 24

# ---------------------------------------------------------------- screen module (MEASURE)
DISP_PCB_W, DISP_PCB_H, DISP_PCB_T = 50.0, 86.0, 1.6
DISP_GLASS_W, DISP_GLASS_H, DISP_GLASS_T = 50.0, 69.2, 3.4   # LCD + touch film
DISP_ACTIVE_W, DISP_ACTIVE_H = 43.2, 57.6
DISP_CY = 20.5                         # centre of the PCB
DISP_ACTIVE_DY = 0.0                   # move the window if your active area is off-centre
WINDOW_MARGIN = 1.4                    # extra opening around the active area, each side
DISP_RAIL_H = 6.0

# ---------------------------------------------------------------- wheel (KY-040, MEASURE)
WHEEL_CY = -46.0
BUSHING_D = 7.0                        # M7 threaded bushing
SHAFT_D, SHAFT_FLAT = 6.0, 4.5         # D shaft: 6 mm, 4.5 mm across the flat
KNOB_D, KNOB_H = 36.0, 9.0
ENC_PCB_W, ENC_PCB_H = 32.0, 19.0
ENC_BODY_H = 6.5                       # encoder body height above its PCB
BUSHING_L = 5.0
SHAFT_ABOVE = 5.0                      # trim the shaft to this length above the bushing

# ---------------------------------------------------------------- side buttons (12 x 12 x 8 tactile)
BTN_CY = 30.0
BTN_Z = 11.5
BTN_HOLE_D = 6.2
SW_SIZE, SW_HEIGHT = 12.0, 8.0         # height includes the actuator
HOLDER_T = 1.6
SW_GAP = 1.5                           # wall to switch top: plunger flange 1.0 + nub 0.4 + 0.1 play

# ---------------------------------------------------------------- dev board (MEASURE)
DEV_W, DEV_L = 28.0, 70.0              # board outline; most dual USB-C S3 boards are 25.5-28.5 x 63-70
DEV_PIN_L = 8.5                        # header pins below the board
DEV_TOP_Y = 56.0                       # board runs from here down; USB-C ports at the bottom end
DEV_RAIL_H = 10.0

# ---------------------------------------------------------------- battery, speaker, charger (MEASURE)
BAT_W, BAT_H, BAT_T = 50.0, 34.0, 5.2  # 523450, lies across the wheel area behind the encoder
SPK_D, SPK_T = 36.0, 5.0
SPK_X, SPK_Y = -10.5, -46.0
CHG_W, CHG_L = 17.0, 25.0
CHG_X = 16.3
USB_SLOT_W, USB_SLOT_H = 10.5, 4.6
MIC_X = -10.0

# ---------------------------------------------------------------- screws (M2 self-tapping, 6-8 mm)
BOSS_D, PILOT_D, SCREW_D, HEAD_D = 5.0, 1.7, 2.3, 4.2
BOSSES = [(sx * (IN_W / 2 - 2.5), sy * (IN_H / 2 - 2.5)) for sx in (-1, 1) for sy in (-1, 1)]


def plan(w, h, r, t, z0=0.0):
    return cq.Workplane("XY").workplane(offset=z0).rect(w, h).extrude(t).edges("|Z").fillet(r)


def box(cx, cy, cz, w, h, t):
    return cq.Workplane("XY").box(w, h, t).translate((cx, cy, cz))


def zcyl(x, y, z0, d, t):
    return cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(d / 2).extrude(t)


def outer():
    return plan(W, H, R_PLAN, T).faces(">Z or <Z").edges().fillet(1.5)


def front_shell():
    shell = outer().intersect(plan(W + 2, H + 2, R_PLAN, T, BACK))
    shell = shell.cut(plan(IN_W, IN_H, R_PLAN - WALL, Z_IN1 - Z_IN0, Z_IN0))

    # screen window and the rails that locate the screen PCB
    win_w = DISP_ACTIVE_W + 2 * WINDOW_MARGIN
    win_h = DISP_ACTIVE_H + 2 * WINDOW_MARGIN
    shell = shell.cut(plan(win_w, win_h, 1.5, FACE + 1, Z_IN1 - 0.5)
                      .translate((0, DISP_CY + DISP_ACTIVE_DY, 0)))
    shell = shell.cut(plan(win_w + 3.0, win_h + 3.0, 2.5, 0.8, T - 0.8)
                      .translate((0, DISP_CY + DISP_ACTIVE_DY, 0)))           # bevel step
    ro = plan(DISP_PCB_W + 0.8 + 2.4, DISP_PCB_H + 0.8 + 2.4, 1.5, DISP_RAIL_H, Z_IN1 - DISP_RAIL_H)
    ri = plan(DISP_PCB_W + 0.8, DISP_PCB_H + 0.8, 0.5, DISP_RAIL_H, Z_IN1 - DISP_RAIL_H)
    rails = ro.cut(ri).translate((0, DISP_CY, 0))
    # openings in the rails at both short ends for the header pins and wires
    rails = rails.cut(box(0, DISP_CY + DISP_PCB_H / 2, Z_IN1 - DISP_RAIL_H / 2, 38, 6, DISP_RAIL_H + 1))
    rails = rails.cut(box(0, DISP_CY - DISP_PCB_H / 2, Z_IN1 - DISP_RAIL_H / 2, 38, 6, DISP_RAIL_H + 1))
    shell = shell.union(rails)

    # wheel: bushing hole and a shallow ring so the knob sits clear of the face
    shell = shell.cut(zcyl(0, WHEEL_CY, Z_IN1 - 0.5, BUSHING_D + 0.4, FACE + 1))
    shell = shell.cut(cq.Workplane("XY").workplane(offset=T - 0.6).center(0, WHEEL_CY)
                      .circle(KNOB_D / 2 + 1.5).circle(KNOB_D / 2 - 2).extrude(0.61))

    # side buttons: holes and switch holders (switch base against the holder plate)
    for side in (1, -1):
        hole = (cq.Workplane("YZ").workplane(offset=side * (W / 2 + 1) if side < 0 else IN_W / 2 - 0.5)
                .center(BTN_CY, BTN_Z).circle(BTN_HOLE_D / 2).extrude(WALL + 1.5))
        shell = shell.cut(hole)
        plate_x = side * (IN_W / 2 - SW_GAP - SW_HEIGHT - HOLDER_T / 2)
        z0 = BTN_Z - SW_SIZE / 2 - 1.5
        zt = SW_SIZE + 3.0
        plate = box(plate_x, BTN_CY, z0 + zt / 2, HOLDER_T, SW_SIZE + 4.0, zt)
        rib_len = IN_W / 2 - abs(plate_x) + HOLDER_T / 2
        rib_cx = side * (IN_W / 2 - rib_len / 2 + 0.01)
        for dy in (SW_SIZE / 2 + 0.3 + 0.6, -(SW_SIZE / 2 + 0.3 + 0.6)):
            plate = plate.union(box(rib_cx, BTN_CY + dy, z0 + zt / 2, rib_len, 1.2, zt))
        shell = shell.union(plate)

    # bottom wall: charger USB-C; top wall: mic hole
    z_usb = BACK + 1.6 + 1.6
    usb = (cq.Workplane("XZ").workplane(offset=IN_H / 2 - 0.5).center(CHG_X, z_usb)
           .slot2D(USB_SLOT_W, USB_SLOT_H, 0).extrude(WALL + 1.5))
    shell = shell.cut(usb)
    mic = (cq.Workplane("XZ").workplane(offset=-(H / 2 + 1)).center(MIC_X, 13.0)
           .circle(0.8).extrude(WALL + 1.5))
    shell = shell.cut(mic)

    # screw bosses
    for x, y in BOSSES:
        shell = shell.union(zcyl(x, y, Z_IN0, BOSS_D, Z_IN1 - Z_IN0))
        shell = shell.cut(zcyl(x, y, Z_IN0 - 0.01, PILOT_D, Z_IN1 - Z_IN0 - 1.0))
    return shell


def back_cover():
    plate = outer().intersect(plan(W + 2, H + 2, R_PLAN, BACK, 0))
    # locating lip
    lo = plan(IN_W - 0.4, IN_H - 0.4, R_PLAN - WALL - 0.2, 1.5, BACK)
    li = plan(IN_W - 2.8, IN_H - 2.8, R_PLAN - WALL - 1.4, 1.5, BACK)
    lip = lo.cut(li)
    for x, y in BOSSES:
        lip = lip.cut(zcyl(x, y, BACK - 0.1, BOSS_D + 4.0, 2.0))
    lip = lip.cut(box(CHG_X, -IN_H / 2 + 2, BACK + 1, CHG_W + 2.0, 6, 3))   # gap for the charger
    plate = plate.union(lip)
    # screw holes with counterbores
    for x, y in BOSSES:
        plate = plate.cut(zcyl(x, y, -0.1, SCREW_D, BACK + 1))
        plate = plate.cut(zcyl(x, y, -0.1, HEAD_D, 1.1))
    # dev board side rails (board rests on its header pins)
    dev_cy = DEV_TOP_Y - DEV_L / 2
    for sx in (-1, 1):
        x = sx * (DEV_W / 2 + 0.3 + 0.6)
        plate = plate.union(box(x, dev_cy, BACK + DEV_RAIL_H / 2, 1.2, DEV_L - 10, DEV_RAIL_H))
    # speaker grille (glue the speaker over it)
    for r, n in ((0, 1), (4, 6), (8, 12), (12, 18)):
        for i in range(n):
            a = 2 * math.pi * i / n
            plate = plate.cut(zcyl(SPK_X + r * math.cos(a), SPK_Y + r * math.sin(a), -0.1, 1.6, BACK + 0.2))
    return plate


def wheel_knob():
    k = zcyl(0, 0, 0, KNOB_D, KNOB_H).faces(">Z").edges().fillet(2.0)
    # dish in the top so the thumb finds the centre
    k = k.cut(cq.Workplane("XY").sphere(60).translate((0, 0, KNOB_H + 60 - 0.8)))
    # recess underneath for the bushing nut and washer
    k = k.cut(zcyl(0, 0, -0.01, 15.0, 3.5))
    # D-shaped bore for the shaft
    bore = zcyl(0, 0, 3.0, SHAFT_D + 0.15, KNOB_H - 3.0 - 1.5)
    flat_cut = box(SHAFT_FLAT + 0.1 - (SHAFT_D + 0.15) / 2 + 5, 0, 3.0 + (KNOB_H - 4.5) / 2, 10, 10, KNOB_H - 4.5)
    bore = bore.cut(flat_cut)
    k = k.cut(bore)
    # light grip ridges around the edge
    for i in range(36):
        a = 2 * math.pi * i / 36
        k = k.cut(zcyl((KNOB_D / 2 + 0.2) * math.cos(a), (KNOB_D / 2 + 0.2) * math.sin(a), 1.0, 1.0, KNOB_H - 3.5))
    return k


def button_plunger():
    head_len = WALL + 1.2
    head = cq.Workplane("YZ").circle((BTN_HOLE_D - 0.4) / 2).extrude(head_len)
    head = head.faces(">X").edges().fillet(0.8)
    flange = cq.Workplane("YZ").workplane(offset=-1.0).circle(4.5).extrude(1.0)
    nub = cq.Workplane("YZ").workplane(offset=-1.4).circle(1.6).extrude(0.4)
    return head.union(flange).union(nub)


# ---------------------------------------------------------------- reference models of the bought parts
def ref_display():
    pcb = box(0, DISP_CY, Z_IN1 - DISP_GLASS_T - DISP_PCB_T / 2, DISP_PCB_W, DISP_PCB_H, DISP_PCB_T)
    glass = box(0, DISP_CY, Z_IN1 - DISP_GLASS_T / 2, DISP_GLASS_W, DISP_GLASS_H, DISP_GLASS_T)
    zp = Z_IN1 - DISP_GLASS_T - DISP_PCB_T
    pins = box(0, DISP_CY + DISP_PCB_H / 2 - 3, zp - 4.25, 35.6, 2.5, 8.5)
    sd = box(-8, DISP_CY - DISP_PCB_H / 2 + 3, zp - 4.25, 10.2, 2.5, 8.5)
    return pcb.union(glass).union(pins).union(sd)


def ref_devboard():
    cy = DEV_TOP_Y - DEV_L / 2
    zb = BACK + DEV_PIN_L
    pcb = box(0, cy, zb + 0.8, DEV_W, DEV_L, 1.6)
    module = box(0, DEV_TOP_Y - 13, zb + 1.6 + 1.6, 18, 25.5, 3.2)
    pins = box(0, cy, BACK + DEV_PIN_L / 2, DEV_W - 2, DEV_L - 6, DEV_PIN_L)
    usb1 = box(-6.5, DEV_TOP_Y - DEV_L + 3.5, zb + 1.6 + 1.6, 9, 7.5, 3.2)
    usb2 = box(6.5, DEV_TOP_Y - DEV_L + 3.5, zb + 1.6 + 1.6, 9, 7.5, 3.2)
    return pcb.union(module).union(pins).union(usb1).union(usb2)


def ref_battery():
    z1 = Z_IN1 - ENC_BODY_H - 1.6 - 0.5
    return box(0, WHEEL_CY + 1, z1 - BAT_T / 2, BAT_W, BAT_H, BAT_T)


def ref_encoder():
    zf = Z_IN1
    body = box(0, WHEEL_CY, zf - ENC_BODY_H / 2, 12.5, 13.5, ENC_BODY_H)
    pcb = box(0, WHEEL_CY - 0.0, zf - ENC_BODY_H - 0.8, ENC_PCB_W, ENC_PCB_H, 1.6)
    bush = zcyl(0, WHEEL_CY, zf, BUSHING_D, BUSHING_L)
    shaft = zcyl(0, WHEEL_CY, zf + BUSHING_L, SHAFT_D, SHAFT_ABOVE)
    shaft = shaft.cut(box(SHAFT_FLAT - SHAFT_D / 2 + 5, WHEEL_CY, zf + BUSHING_L + SHAFT_ABOVE / 2,
                          10, 10, SHAFT_ABOVE + 0.2))
    return body.union(pcb).union(bush).union(shaft)


def ref_speaker():
    return zcyl(SPK_X, SPK_Y, BACK, SPK_D, SPK_T)


def ref_charger():
    cy = -IN_H / 2 + CHG_L / 2 + 0.3
    pcb = box(CHG_X, cy, BACK + 0.8, CHG_W, CHG_L, 1.6)
    usb = box(CHG_X, -IN_H / 2 + 3.8, BACK + 1.6 + 1.6, 9, 7.5, 3.2)
    return pcb.union(usb)


def ref_switches():
    out = None
    for side in (1, -1):
        x = side * (IN_W / 2 - SW_GAP - SW_HEIGHT / 2)
        s = box(x, BTN_CY, BTN_Z, SW_HEIGHT, SW_SIZE, SW_SIZE)
        out = s if out is None else out.union(s)
    return out


def ref_mic():
    # INMP441 board stood against the top wall, its sound hole facing the case hole
    return box(MIC_X, IN_H / 2 - 1.3, 13.0, 14.0, 2.6, 14.0)


def ref_amp():
    # MAX98357A, wires soldered straight on (no screw terminal), lying over the speaker
    return box(-18.0, -44.0, 8.6, 17.8, 19.3, 2.6)


def ref_vibration_motor():
    return zcyl(22.0, 4.0, BACK, 10.0, 2.7)


def plunger_placed(side):
    p = button_plunger()
    if side > 0:
        return p.translate((IN_W / 2, BTN_CY, BTN_Z))
    return p.mirror("YZ").translate((-IN_W / 2, BTN_CY, BTN_Z))


def knob_placed():
    return wheel_knob().translate((0, WHEEL_CY, T + 0.8))


PARTS = {
    "front_shell": (front_shell, (0.11, 0.11, 0.13)),
    "back_cover": (back_cover, (0.11, 0.11, 0.13)),
    "wheel_knob": (wheel_knob, (1.0, 0.42, 0.10)),
    "button_plunger": (button_plunger, (1.0, 0.42, 0.10)),
}

ASSEMBLY = [
    ("front_shell", front_shell, (0.11, 0.11, 0.13, 0.35)),
    ("back_cover", back_cover, (0.11, 0.11, 0.13, 1.0)),
    ("wheel_knob", knob_placed, (1.0, 0.42, 0.10, 1.0)),
    ("action_plunger", lambda: plunger_placed(1), (1.0, 0.42, 0.10, 1.0)),
    ("back_plunger", lambda: plunger_placed(-1), (0.3, 0.3, 0.32, 1.0)),
    ("ref_screen_module", ref_display, (0.6, 0.1, 0.1, 1.0)),
    ("ref_esp32_s3_devboard", ref_devboard, (0.1, 0.1, 0.1, 1.0)),
    ("ref_battery_523450", ref_battery, (0.8, 0.8, 0.82, 1.0)),
    ("ref_encoder_ky040", ref_encoder, (0.2, 0.3, 0.6, 1.0)),
    ("ref_speaker", ref_speaker, (0.25, 0.25, 0.25, 1.0)),
    ("ref_charger_module", ref_charger, (0.1, 0.4, 0.7, 1.0)),
    ("ref_tactile_switches", ref_switches, (0.2, 0.2, 0.2, 1.0)),
    ("ref_mic_inmp441", ref_mic, (0.1, 0.3, 0.6, 1.0)),
    ("ref_amp_max98357", ref_amp, (0.1, 0.3, 0.6, 1.0)),
    ("ref_vibration_motor", ref_vibration_motor, (0.7, 0.7, 0.72, 1.0)),
]
