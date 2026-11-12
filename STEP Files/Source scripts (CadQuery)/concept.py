"""Mavrik Pocket, 8 mm concept (58 x 112 x 8 mm). All sizes in mm.

Axes: X = width (right +), Y = height (screen at +Y), Z = thickness.
Back of the device at Z = 0, front glass at Z = T.
"""
import cadquery as cq

# ---------------------------------------------------------------- body
W, H, T = 58.0, 112.0, 8.0
R_PLAN = 9.0          # corner radius seen from the front
EDGE = 1.2            # rounding of the front and back edges
WALL = 1.2            # side wall
FACE = 1.2            # front face thickness
BACK = 1.2            # back cover thickness

# ---------------------------------------------------------------- screen (2.8" 240x320 bare panel)
PANEL_W, PANEL_H, PANEL_T = 50.0, 69.2, 2.45
ACTIVE_W, ACTIVE_H = 43.2, 57.6
ACTIVE_SHIFT = 2.5    # active area sits closer to the top, away from the FPC
GLASS_W, GLASS_H, GLASS_T = 52.0, 70.0, 0.7
GLASS_R = 6.0
GLASS_TOP_MARGIN = 3.0
GLASS_CY = H / 2 - GLASS_TOP_MARGIN - GLASS_H / 2        # 18.0
PANEL_CY = GLASS_CY - 0.5
ACTIVE_CY = PANEL_CY + ACTIVE_SHIFT

# ---------------------------------------------------------------- touch wheel
WHEEL_CY = -37.0
WHEEL_OD, WHEEL_ID = 32.0, 13.0
WHEEL_T = 0.8           # inlay, sits in a recess in the front face
CENTRE_D = 12.0         # centre select button
CENTRE_GAP = 0.25

# ---------------------------------------------------------------- side action button (right side)
ACT_CY, ACT_LEN, ACT_H = 30.0, 14.0, 2.8

# ---------------------------------------------------------------- ports (bottom edge)
USBC_W, USBC_H = 9.2, 3.6
JACK_D = 4.2
JACK_X = -16.0
JACK_Z = 3.75          # a little below centre so the jack body clears the wheel flex
SPK_SLOTS_X = 13.0      # speaker grille centre on the bottom edge
MIC_X = 12.0            # mic hole on the top edge

# ---------------------------------------------------------------- back
RING_OD, RING_ID, RING_T = 54.0, 46.0, 0.6   # MagSafe-style magnet ring
RING_CY = 4.0
BADGE = 10.0

Z_MID = BACK + (T - BACK - FACE) / 2


def plan(w, h, r, t, z0=0.0):
    return (cq.Workplane("XY").workplane(offset=z0)
            .rect(w, h).extrude(t).edges("|Z").fillet(r))


def outer_body():
    b = plan(W, H, R_PLAN, T)
    return b.faces(">Z or <Z").edges().fillet(EDGE)


def front_shell():
    body = outer_body()
    # keep only the part above the back cover
    keep = plan(W + 2, H + 2, R_PLAN, T - BACK + 1, BACK)
    shell = body.intersect(keep)
    # hollow inside, leaving the front face
    cavity = plan(W - 2 * WALL, H - 2 * WALL, R_PLAN - WALL, T - BACK - FACE, BACK)
    shell = shell.cut(cavity)
    # glass recess and display window
    shell = shell.cut(plan(GLASS_W + 0.2, GLASS_H + 0.2, GLASS_R + 0.1, GLASS_T + 0.01, T - GLASS_T)
                      .translate((0, GLASS_CY, 0)))
    shell = shell.cut(plan(ACTIVE_W + 1.6, ACTIVE_H + 1.6, 1.5, FACE + 1, T - FACE - 0.5)
                      .translate((0, ACTIVE_CY, 0)))
    # touch-wheel recess and centre-button hole
    ring = (cq.Workplane("XY").workplane(offset=T - WHEEL_T)
            .center(0, WHEEL_CY).circle(WHEEL_OD / 2 + 0.1).circle(WHEEL_ID / 2 - 0.1)
            .extrude(WHEEL_T + 0.01))
    shell = shell.cut(ring)
    shell = shell.cut(cq.Workplane("XY").workplane(offset=T - FACE - 0.5)
                      .center(0, WHEEL_CY).circle(CENTRE_D / 2 + CENTRE_GAP).extrude(FACE + 1))
    # action button slot, right wall
    slot = (cq.Workplane("YZ").workplane(offset=W / 2 - WALL - 0.5)
            .center(ACT_CY, Z_MID).slot2D(ACT_LEN + 0.3, ACT_H + 0.3, 0).extrude(WALL + 1.5))
    shell = shell.cut(slot)
    # bottom edge: USB-C, headphone jack, speaker grille
    usbc = (cq.Workplane("XZ").workplane(offset=H / 2 - WALL - 0.5)
            .center(0, Z_MID).slot2D(USBC_W, USBC_H, 0).extrude(WALL + 1.5))
    shell = shell.cut(usbc)
    jack = (cq.Workplane("XZ").workplane(offset=H / 2 - WALL - 0.5)
            .center(JACK_X, JACK_Z).circle(JACK_D / 2).extrude(WALL + 1.5))
    shell = shell.cut(jack)
    for i in range(6):
        x = SPK_SLOTS_X - 5 + i * 2.0
        hole = (cq.Workplane("XZ").workplane(offset=H / 2 - WALL - 0.5)
                .center(x, Z_MID).circle(0.5).extrude(WALL + 1.5))
        shell = shell.cut(hole)
    # mic hole, top edge
    mic = (cq.Workplane("XZ").workplane(offset=-(H / 2 + 1))
           .center(MIC_X, Z_MID).circle(0.5).extrude(WALL + 1.5))
    shell = shell.cut(mic)
    return shell


def back_cover():
    body = outer_body()
    keep = plan(W + 2, H + 2, R_PLAN, BACK, 0)
    plate = body.intersect(keep)
    # locating lip that sits inside the shell
    lip_o = plan(W - 2 * WALL - 0.2, H - 2 * WALL - 0.2, R_PLAN - WALL, 0.8, BACK)
    lip_i = plan(W - 2 * WALL - 1.8, H - 2 * WALL - 1.8, R_PLAN - WALL - 0.8, 0.8, BACK)
    lip = lip_o.cut(lip_i)
    lip = lip.cut(cq.Workplane("XY").box(9, 6, 2).translate((JACK_X, -H / 2 + WALL + 1, BACK + 0.5)))
    plate = plate.union(lip)
    # magnet-ring recess from the inside, leaving a 0.5 mm skin
    ring = (cq.Workplane("XY").workplane(offset=BACK - 0.7)
            .center(0, RING_CY).circle(RING_OD / 2 + 0.15).circle(RING_ID / 2 - 0.15).extrude(0.71))
    plate = plate.cut(ring)
    # logo badge recess on the outside
    badge = plan(BADGE, BADGE, 2.5, 0.31, -0.01).translate((0, 30, 0))
    plate = plate.cut(badge)
    # lanyard pocket in the bottom-right corner, with a cross hole for a 1 mm pin
    pocket = plan(6.0, 3.0, 1.0, 0.91, -0.01).translate((W / 2 - 9, -H / 2 + 6.5, 0))
    plate = plate.cut(pocket)
    plate = plate.cut(lanyard_pin(0.45))
    return plate


def cover_glass():
    return plan(GLASS_W, GLASS_H, GLASS_R, GLASS_T, T - GLASS_T).translate((0, GLASS_CY, 0))


def wheel_ring():
    r = (cq.Workplane("XY").workplane(offset=T - WHEEL_T).center(0, WHEEL_CY)
         .circle(WHEEL_OD / 2).circle(WHEEL_ID / 2).extrude(WHEEL_T))
    return r.faces(">Z").edges().fillet(0.3)


PCB_TOP = T - FACE - 0.6      # 0.6 mm under the face: room for the select dome and button flange
DOME_T = 0.25


def centre_button():
    z0 = PCB_TOP + DOME_T
    cap = (cq.Workplane("XY").workplane(offset=z0).center(0, WHEEL_CY)
           .circle(CENTRE_D / 2).extrude(T - z0 + 0.15))
    cap = cap.faces(">Z").edges().fillet(0.5)
    flange = (cq.Workplane("XY").workplane(offset=z0).center(0, WHEEL_CY)
              .circle(CENTRE_D / 2 + 1.2).extrude(T - FACE - z0))
    return cap.union(flange)


def select_dome():
    return (cq.Workplane("XY").workplane(offset=PCB_TOP).center(0, WHEEL_CY)
            .circle(3.0).extrude(DOME_T))


def action_button():
    x0 = W / 2 - WALL
    key = (cq.Workplane("YZ").workplane(offset=x0).center(ACT_CY, Z_MID)
           .slot2D(ACT_LEN, ACT_H, 0).extrude(WALL + 0.6))
    key = key.faces(">X").edges().fillet(0.4)
    flange = (cq.Workplane("YZ").workplane(offset=x0 - 0.6).center(ACT_CY, Z_MID)
              .slot2D(ACT_LEN + 2.0, min(ACT_H + 1.2, T - BACK - FACE - 0.4), 0).extrude(0.6))
    return key.union(flange)


def magnet_ring():
    return (cq.Workplane("XY").workplane(offset=BACK - 0.7).center(0, RING_CY)
            .circle(RING_OD / 2).circle(RING_ID / 2).extrude(0.6))


def lanyard_pin(r=0.4):
    return (cq.Workplane("YZ").workplane(offset=W / 2 - 12.5).center(-H / 2 + 6.5, 0.5)
            .circle(r).extrude(7.0))


# -------------------------------------------------- internal placeholders (for checking space)
def display_panel():
    return (cq.Workplane("XY").workplane(offset=T - FACE - PANEL_T)
            .center(0, PANEL_CY).rect(PANEL_W, PANEL_H).extrude(PANEL_T))


def battery():
    # thin cell for the 8 mm build, e.g. 3.0 x 40 x 50 mm (about 600-700 mAh)
    return (cq.Workplane("XY").workplane(offset=BACK)
            .center(0, PANEL_CY + 2).rect(40, 50).extrude(3.0))


def _box(cx, cy, cz, w, h, t):
    return cq.Workplane("XY").box(w, h, t).translate((cx, cy, cz))


USBC_BODY = (8.94, 7.35, 3.26)
JACK_BODY = (6.0, 12.0, 4.5)
SPK_BOX = (14.0, 10.0, 3.0)        # sealed cavity speaker, 8 ohm


def main_pcb():
    # 0.8 mm main board under the wheel, with cut-outs for the edge parts
    pcb = (cq.Workplane("XY").workplane(offset=PCB_TOP - 0.8)
           .center(0, WHEEL_CY + 1).rect(50, 32).extrude(0.8))
    zc = PCB_TOP - 0.4
    pcb = pcb.cut(_box(0, -H / 2 + WALL + 4.0, zc, 10.4, 8.0, 2))           # USB-C, mid-mount
    pcb = pcb.cut(_box(JACK_X, -H / 2 + WALL + 6.6, zc, 7.0, 13.2, 2))      # headphone jack
    pcb = pcb.cut(_box(SPK_SLOTS_X, -H / 2 + WALL + 5.6, zc, 15.0, 11.2, 2))  # speaker box
    parts = (cq.Workplane("XY").workplane(offset=PCB_TOP - 0.8 - 1.6)
             .center(-12, -32).rect(18, 18).extrude(1.6))   # ESP32-S3, flash, audio and power parts
    return pcb.union(parts)


def touch_flex():
    # thin flex circuit carrying the touch-wheel pads, right under the front face
    return (cq.Workplane("XY").workplane(offset=PCB_TOP).center(0, WHEEL_CY)
            .circle(WHEEL_OD / 2).circle(CENTRE_D / 2 + 1.5).extrude(0.2))


def usbc_socket():
    w, d, t = USBC_BODY
    return _box(0, -H / 2 + WALL + 0.1 + d / 2, Z_MID, w, d, t)


def headphone_jack():
    w, d, t = JACK_BODY
    return _box(JACK_X, -H / 2 + WALL + 0.2 + d / 2, JACK_Z, w, d, t)


def speaker_box():
    w, d, t = SPK_BOX
    return _box(SPK_SLOTS_X, -H / 2 + WALL + 0.2 + d / 2, Z_MID, w, d, t)


def mic():
    return _box(MIC_X, H / 2 - WALL - 0.1 - 0.5, Z_MID, 3.5, 1.0, 2.65)


def haptic_motor():
    return (cq.Workplane("XY").workplane(offset=BACK + 0.1).center(15, -30)
            .circle(4.0).extrude(2.5))


PARTS = {
    "front_shell": (front_shell, (0.11, 0.11, 0.13)),
    "back_cover": (back_cover, (0.11, 0.11, 0.13)),
    "cover_glass": (cover_glass, (0.6, 0.75, 0.85)),
    "touch_wheel_ring": (wheel_ring, (1.0, 0.42, 0.10)),
    "centre_button": (centre_button, (0.15, 0.15, 0.17)),
    "action_button": (action_button, (1.0, 0.42, 0.10)),
    "magnet_ring": (magnet_ring, (0.55, 0.55, 0.58)),
    "lanyard_pin": (lanyard_pin, (0.7, 0.7, 0.72)),
    "ref_display_panel": (display_panel, (0.05, 0.05, 0.05)),
    "ref_battery_3x40x50": (battery, (0.8, 0.8, 0.82)),
    "ref_main_pcb": (main_pcb, (0.1, 0.45, 0.2)),
    "ref_select_dome": (select_dome, (0.75, 0.75, 0.78)),
    "ref_touch_wheel_flex": (touch_flex, (0.85, 0.55, 0.15)),
    "ref_usb_c_socket": (usbc_socket, (0.75, 0.75, 0.78)),
    "ref_headphone_jack": (headphone_jack, (0.15, 0.15, 0.15)),
    "ref_speaker_box": (speaker_box, (0.25, 0.25, 0.27)),
    "ref_mic": (mic, (0.75, 0.75, 0.78)),
    "ref_haptic_motor": (haptic_motor, (0.7, 0.7, 0.72)),
}
