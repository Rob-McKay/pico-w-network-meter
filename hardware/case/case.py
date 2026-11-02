#!/usr/bin/env python3
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0

"""Generate a 3D-printable case for the network meter, as STL files.

The case holds a Raspberry Pi Pico W plugged into a Pimoroni Pico Display
Pack 2.0, with a Pimoroni LiPo Amigo Pro charger and a 150 mAh LiPo in a
compartment below the display:

    front.stl     the front shell: bezel with the display window, button
                  and LED holes, side walls, USB openings and screw bosses.
                  Print face down.
    back.stl      the back cover: pockets for the charger and battery,
                  posts that hold the Pico into the Display Pack's sockets,
                  a window onto the charger's switch, a pin hole for the
                  Pico's BOOTSEL button. Print outside face down.
    buttons.stl   four button caps for A, B, X and Y: a flange inside the
                  bezel keeps each in. Print flange down.

Every measurement the case depends on is a constant below, in millimetres.
The Display Pack's come from Pimoroni's dimension drawing (turned to
landscape, buttons A and B on the left); the others from the parts'
published sizes. Those that neither gives are marked MEASURE: check them
against your boards with calipers before printing.

Coordinates: x runs along the case from its left end (as you look at the
display), y up from its bottom edge, z back from the front face.

    python3 case.py [--out DIR] [--preview]

Needs numpy and manifold3d (pip install numpy manifold3d); --preview also
needs matplotlib, and draws PNG views next to the STL files.
"""

import argparse
import os
import struct
import sys

import numpy as np
from manifold3d import CrossSection, Manifold

# --- Print and fit --------------------------------------------------------

## Walls and the back cover's plate.
WALL = 2.0
## The bezel in front of the display.
FRONT = 1.6
## Outside corner radius.
CORNER_R = 4.0
## Clearance around parts that drop into pockets or openings.
FIT = 0.4
## Circles' segments: smooth enough to print round.
SEGMENTS = 48

# --- Pimoroni Pico Display Pack 2.0 ---------------------------------------

## Board: 55.2 x 34.6 mm (drawing).
PACK_L = 55.2
PACK_W = 34.6
PACK_PCB = 1.6
## Front of the board to the front of the display glass. MEASURE.
PACK_DISPLAY_H = 3.0
## Display glass: between the 3.6 mm button strips at the ends (drawing:
## 47.8 mm long), and across the board (its edges, MEASURE).
PACK_GLASS_X = (3.6, 51.4)
PACK_GLASS_Y = (0.2, 34.4)
## The window: the active area (drawing: 40.8 x 30.6 mm, from 5.4 mm at the
## A/B end and 2.0 mm from the bottom edge), with a margin. The bezel rests
## on the glass around it.
ACTIVE_X = (5.4, 46.2)
ACTIVE_Y = (2.0, 32.6)
WINDOW_MARGIN = 0.6
## Height of the female header sockets on the back of the board. MEASURE.
PACK_SOCKET_H = 7.0
## Buttons A, B, X, Y: centres from the board's bottom-left corner,
## looking at the display (drawing).
BUTTONS = {
    "A": (1.8, 24.95),
    "B": (1.8, 9.65),
    "X": (53.4, 24.95),
    "Y": (53.4, 9.65),
}
## Height of a button's top above the front of the board. MEASURE.
BUTTON_TOP_H = 1.5
## The RGB LED, from the board's bottom-left corner (drawing).
LED = (53.4, 17.3)

# --- Raspberry Pi Pico W --------------------------------------------------

## Board: 51 x 21 mm (published), plugged into the Display Pack's back.
PICO_L = 51.0
PICO_W = 21.0
PICO_PCB = 1.0
## Its tallest parts (USB socket, Wi-Fi shield) above its top face.
PICO_PARTS_H = 3.3
## Its bottom-left corner from the Display Pack's, looking from the front.
## MEASURE.
PICO_X = 2.5
PICO_Y = 7.0
## Which end its USB socket is at, looking from the front: the A/B end
## (left) or the X/Y end (right). The Pico fits the Display Pack only one
## way round, as the Display Pack's markings show. MEASURE.
PICO_USB_LEFT = True
## Mounting holes, from its USB end and bottom edge (published).
PICO_HOLES = [(2.0, 4.8), (2.0, 16.2), (49.0, 4.8), (49.0, 16.2)]
PICO_HOLE_D = 2.1
## BOOTSEL button, from its USB end and bottom edge. MEASURE.
PICO_BOOTSEL = (12.0, 13.0)
## Opening for a micro-USB plug's moulding.
PICO_USB_OPENING = (13.0, 8.5)

# --- Pimoroni LiPo Amigo Pro ----------------------------------------------

## Board: 36 x 23 x 7 mm with its connectors (published), USB-C at one end.
AMIGO_L = 36.0
AMIGO_W = 23.0
AMIGO_PCB = 1.0
## Its USB-C socket's centre above its board.
AMIGO_USB_H = 1.6
## Opening for a USB-C plug's moulding.
AMIGO_USB_OPENING = (13.0, 7.5)
## Power switch and LEDs, from its USB end and bottom edge: the back
## cover's window. MEASURE.
AMIGO_SWITCH = (24.0, 11.5)
AMIGO_SWITCH_WINDOW = (12.0, 8.0)

# --- 150 mAh LiPo ---------------------------------------------------------

## 27 x 20.5 x 4.3 mm (published).
BATTERY_L = 27.0
BATTERY_W = 20.5
BATTERY_T = 4.3

# --- Fastenings -----------------------------------------------------------

## Four M2 x 8 mm self-tapping screws, one in each corner.
BOSS_D = 5.0
SCREW_PILOT_D = 1.6
SCREW_CLEAR_D = 2.4
SCREW_HEAD_D = 4.2
SCREW_HEAD_H = 1.4
## A lip on the back cover that fits inside the walls, to line it up.
LIP_H = 2.0
LIP_T = 1.2

# --- Button caps ----------------------------------------------------------

BUTTON_STEM_D = 3.0
## The tab that keeps a cap in. The buttons sit in a 3.6 mm strip between
## the board's end and the glass, so the tab runs along the strip: narrow
## across it (x), longer along it (y), clear of the LED between X and Y.
BUTTON_TAB_X = 3.0
BUTTON_TAB_Y = 7.0
BUTTON_TAB_H = 0.8
## How far a cap stands proud of the bezel.
BUTTON_PROUD = 1.2
LED_HOLE_D = 2.5

# --- Layout ---------------------------------------------------------------

## Gap between the charger and the battery, and around the compartment.
GAP = 2.0
## Clearance behind the Pico's tallest parts.
BACK_CLEAR = 1.0
## Keep the boards clear of the screw bosses.
BOSS_CLEAR = BOSS_D + 0.5

## Depths, from the front face.
Z_GLASS = FRONT
Z_PACK_FRONT = Z_GLASS + PACK_DISPLAY_H
Z_PACK_BACK = Z_PACK_FRONT + PACK_PCB
Z_PICO_BOTTOM = Z_PACK_BACK + PACK_SOCKET_H
Z_PICO_TOP = Z_PICO_BOTTOM + PICO_PCB
## Where the front shell meets the back cover.
Z_SPLIT = Z_PICO_TOP + PICO_PARTS_H + BACK_CLEAR
DEPTH = Z_SPLIT + WALL

## Inside: the charger and battery along the bottom, clear of the bottom
## screw bosses; the Display Pack above, clear of the top ones.
CHIN_Y = BOSS_CLEAR
CHIN_H = max(AMIGO_W, BATTERY_W) + FIT
INSIDE_L = max(PACK_L + 2 * BOSS_CLEAR,
               LIP_T + FIT + AMIGO_L + GAP + BATTERY_L + FIT + LIP_T + FIT)
PACK_X = (INSIDE_L - PACK_L) / 2
PACK_Y = CHIN_Y + CHIN_H + GAP
INSIDE_H = PACK_Y + PACK_W + FIT
LENGTH = INSIDE_L + 2 * WALL
HEIGHT = INSIDE_H + 2 * WALL

## Where things are on the case, from its outside bottom-left corner. The
## charger's USB end is just inside the back cover's lip.
AMIGO_X = WALL + FIT / 2 + LIP_T + FIT / 2
AMIGO_Y = WALL + CHIN_Y
BATTERY_X = AMIGO_X + AMIGO_L + GAP
BATTERY_Y = AMIGO_Y
BOARD_X = WALL + PACK_X
BOARD_Y = WALL + PACK_Y
BOSSES = [(WALL + BOSS_D / 2, WALL + BOSS_D / 2),
          (LENGTH - WALL - BOSS_D / 2, WALL + BOSS_D / 2),
          (WALL + BOSS_D / 2, HEIGHT - WALL - BOSS_D / 2),
          (LENGTH - WALL - BOSS_D / 2, HEIGHT - WALL - BOSS_D / 2)]


## @brief Make a box between two corners.
#
# @param x0 Low x.
# @param y0 Low y.
# @param z0 Low z.
# @param x1 High x.
# @param y1 High y.
# @param z1 High z.
# @return The box.
#
# @sideeffects None.
def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


## @brief Make a cylinder along z.
#
# @param x Centre x.
# @param y Centre y.
# @param z0 Low z.
# @param z1 High z.
# @param d Diameter.
# @return The cylinder.
#
# @sideeffects None.
def cylinder(x, y, z0, z1, d):
    return Manifold.cylinder(z1 - z0, d / 2, d / 2, SEGMENTS).translate([x, y, z0])


## @brief Make a slab with rounded corners, the case's outline inset by a
#         margin.
#
# @param inset How far inside the outside edge.
# @param z0 Low z.
# @param z1 High z.
# @param radius Corner radius before the inset.
# @return The slab.
#
# @sideeffects None.
def rounded_slab(inset, z0, z1, radius=CORNER_R):
    r = max(radius - inset, 0.1)
    outline = CrossSection.square([LENGTH - 2 * inset - 2 * r, HEIGHT - 2 * inset - 2 * r])
    outline = outline.translate([inset + r, inset + r]).offset(r, circular_segments=SEGMENTS)
    return Manifold.extrude(outline, z1 - z0).translate([0, 0, z0])


## @brief Make an opening through an end wall, centred on a point.
#
# @param left The left wall if true, the right one if false.
# @param y Centre y.
# @param z Centre z.
# @param size Width (y) and height (z).
# @return The cutting solid: through the wall and a little inside it.
#
# @sideeffects None.
def end_wall_opening(left, y, z, size):
    w, h = size
    x0, x1 = (-1, WALL + 1) if left else (LENGTH - WALL - 1, LENGTH + 1)
    return box(x0, y - w / 2, z - h / 2, x1, y + w / 2, z + h / 2)


## @brief Find a point on the Pico, given from its USB end.
#
# @param along Distance from its USB end.
# @param across Distance from its bottom edge.
# @return (x, y) on the case.
#
# @sideeffects None.
def pico_point(along, across):
    x = along if PICO_USB_LEFT else PICO_L - along
    return BOARD_X + PICO_X + x, BOARD_Y + PICO_Y + across


## @brief Cut the two USB openings: the Pico's, in the wall at its USB end,
#         and the charger's, in the left wall.
#
# @return The cutting solids, applied to both the front shell and the back
#         cover's lip.
#
# @sideeffects None.
def usb_openings():
    pico = end_wall_opening(PICO_USB_LEFT, BOARD_Y + PICO_Y + PICO_W / 2,
                            Z_PICO_TOP + PICO_PARTS_H / 2, PICO_USB_OPENING)
    amigo_z = Z_SPLIT - AMIGO_PCB - AMIGO_USB_H
    amigo = end_wall_opening(True, AMIGO_Y + AMIGO_W / 2, amigo_z, AMIGO_USB_OPENING)
    return pico + amigo


## @brief Model the front shell.
#
# @return The shell: bezel, walls, a frame locating the Display Pack, and
#         the screw bosses.
#
# @sideeffects None.
def front_shell():
    shell = rounded_slab(0, 0, Z_SPLIT) - rounded_slab(WALL, FRONT, Z_SPLIT + 1)

    # A frame that holds the Display Pack's edges, from the bezel to the
    # back of its board.
    frame_out = box(BOARD_X - FIT / 2 - 1.2, BOARD_Y - FIT / 2 - 1.2, FRONT,
                    BOARD_X + PACK_L + FIT / 2 + 1.2, BOARD_Y + PACK_W + FIT / 2 + 1.2,
                    Z_PACK_BACK)
    frame_in = box(BOARD_X - FIT / 2, BOARD_Y - FIT / 2, FRONT - 1,
                   BOARD_X + PACK_L + FIT / 2, BOARD_Y + PACK_W + FIT / 2, Z_PACK_BACK + 1)
    shell += frame_out - frame_in

    # Two ribs that rest just in front of the battery, to hold it down.
    rib_z = Z_SPLIT - BATTERY_T - 0.3
    for fx in (0.25, 0.75):
        x = BATTERY_X + fx * BATTERY_L
        shell += box(x - 0.6, BATTERY_Y, FRONT, x + 0.6, BATTERY_Y + BATTERY_W, rib_z)

    for x, y in BOSSES:
        boss = cylinder(x, y, FRONT, Z_SPLIT, BOSS_D)
        shell += boss - cylinder(x, y, FRONT + 1.0, Z_SPLIT + 1, SCREW_PILOT_D)

    m = WINDOW_MARGIN
    shell -= box(BOARD_X + ACTIVE_X[0] - m, BOARD_Y + ACTIVE_Y[0] - m, -1,
                 BOARD_X + ACTIVE_X[1] + m, BOARD_Y + ACTIVE_Y[1] + m, FRONT + 1)
    for bx, by in BUTTONS.values():
        shell -= cylinder(BOARD_X + bx, BOARD_Y + by, -1, FRONT + 1, BUTTON_STEM_D + FIT)
    shell -= cylinder(BOARD_X + LED[0], BOARD_Y + LED[1], -1, FRONT + 1, LED_HOLE_D)

    return shell - usb_openings()


## @brief Model the back cover's lip: strips along each wall, inside it,
#         that line the cover up with the shell.
#
# @return The lip, clear of the corner bosses.
#
# @sideeffects None.
def back_lip():
    z0, z1 = Z_SPLIT - LIP_H, Z_SPLIT
    i0 = WALL + FIT / 2
    i1 = i0 + LIP_T
    end = WALL + BOSS_D + 1.0
    return (box(end, i0, z0, LENGTH - end, i1, z1)
            + box(end, HEIGHT - i1, z0, LENGTH - end, HEIGHT - i0, z1)
            + box(i0, end, z0, i1, HEIGHT - end, z1)
            + box(LENGTH - i1, end, z0, LENGTH - i0, HEIGHT - end, z1))


## @brief Model a low pocket wall around a part lying on the back cover.
#
# @param x Part's left edge.
# @param y Part's bottom edge.
# @param length Part's length (x).
# @param width Part's width (y).
# @param height Wall height.
# @param open_left Leave the left end open (for a connector).
# @return The walls.
#
# @sideeffects None.
def pocket(x, y, length, width, height, open_left=False):
    z0 = Z_SPLIT - height
    t = 1.0
    x0, y0 = x - FIT / 2, y - FIT / 2
    x1, y1 = x + length + FIT / 2, y + width + FIT / 2
    walls = (box(x0 - t, y0 - t, z0, x1 + t, y0, Z_SPLIT)
             + box(x0 - t, y1, z0, x1 + t, y1 + t, Z_SPLIT)
             + box(x1, y0, z0, x1 + t, y1, Z_SPLIT))
    if not open_left:
        walls += box(x0 - t, y0, z0, x0, y1, Z_SPLIT)
    return walls


## @brief Model the back cover.
#
# @return The cover: plate, lip, pockets for the charger and battery,
#         posts pressing the Pico into its sockets, screw holes, the
#         charger's switch window and a BOOTSEL pin hole.
#
# @sideeffects None.
def back_cover():
    cover = rounded_slab(0, Z_SPLIT, DEPTH) + back_lip()
    cover += pocket(AMIGO_X, AMIGO_Y, AMIGO_L, AMIGO_W, 2.0, open_left=True)
    cover += pocket(BATTERY_X, BATTERY_Y, BATTERY_L, BATTERY_W, 2.0)

    # Posts at the Pico's mounting holes, with pins into them.
    pico_top = Z_PICO_TOP
    for hx, hy in PICO_HOLES:
        x, y = pico_point(hx, hy)
        cover += cylinder(x, y, pico_top, Z_SPLIT, 3.2)
        cover += cylinder(x, y, pico_top - 1.0, pico_top, PICO_HOLE_D - 0.3)

    for x, y in BOSSES:
        cover -= cylinder(x, y, Z_SPLIT - 1, DEPTH + 1, SCREW_CLEAR_D)
        cover -= cylinder(x, y, DEPTH - SCREW_HEAD_H, DEPTH + 1, SCREW_HEAD_D)

    sx = AMIGO_X + AMIGO_SWITCH[0]
    sy = AMIGO_Y + AMIGO_SWITCH[1]
    w, h = AMIGO_SWITCH_WINDOW
    cover -= box(sx - w / 2, sy - h / 2, Z_SPLIT - 3, sx + w / 2, sy + h / 2, DEPTH + 1)
    bootsel_x, bootsel_y = pico_point(*PICO_BOOTSEL)
    cover -= cylinder(bootsel_x, bootsel_y, Z_SPLIT - 1, DEPTH + 1, 2.0)

    return cover - usb_openings()


## @brief Model one button cap, standing on its tab, its stem's centre at
#         the origin.
#
# The stem reaches from the button's top, through the bezel, to
# BUTTON_PROUD in front of it. The tab, at its back, rests on the button
# and stops the cap falling out through the bezel.
#
# @return The cap, tab on z = 0.
#
# @sideeffects None.
def button_cap():
    length = (Z_PACK_FRONT - BUTTON_TOP_H) + BUTTON_PROUD
    stem = Manifold.cylinder(length, BUTTON_STEM_D / 2, BUTTON_STEM_D / 2, SEGMENTS)
    tab = box(-BUTTON_TAB_X / 2, -BUTTON_TAB_Y / 2, 0, BUTTON_TAB_X / 2, BUTTON_TAB_Y / 2,
              BUTTON_TAB_H)
    return stem + tab


## @brief Lay out the four button caps, a print's worth.
#
# @return The caps in a row.
#
# @sideeffects None.
def button_caps():
    cap = button_cap()
    caps = cap
    for i in range(1, len(BUTTONS)):
        caps += cap.translate([i * (BUTTON_TAB_X + 4), 0, 0])
    return caps


## @brief Place the button caps where they sit in the case, for checking the
#         fit.
#
# @return The caps, stems through the bezel, tabs on the buttons.
#
# @sideeffects None.
def placed_button_caps():
    caps = None
    for bx, by in BUTTONS.values():
        # Turned to point out of the front: the tab's face (z = 0) on the button.
        cap = button_cap().rotate([180, 0, 0])
        cap = cap.translate([BOARD_X + bx, BOARD_Y + by, Z_PACK_FRONT - BUTTON_TOP_H])
        caps = cap if caps is None else caps + cap
    return caps


## @brief Model the Pico's board, with its mounting holes.
#
# @param px Its left (USB) end.
# @param py Its bottom edge.
# @return The board.
#
# @sideeffects None.
def pico_board(px, py):
    board = box(px, py, Z_PICO_BOTTOM, px + PICO_L, py + PICO_W, Z_PICO_TOP)
    for hx, hy in PICO_HOLES:
        x, y = pico_point(hx, hy)
        board -= cylinder(x, y, Z_PICO_BOTTOM - 1, Z_PICO_TOP + 1, PICO_HOLE_D)
    return board


## @brief Model the Pico's USB socket, at its USB end.
#
# @return The socket: 8 mm wide, 3 mm tall, standing 1.3 mm past the board's
#         end.
#
# @sideeffects None.
def pico_usb_socket():
    x0, y = pico_point(-1.3, PICO_W / 2)
    x1, _ = pico_point(5.0, PICO_W / 2)
    x0, x1 = sorted([x0, x1])
    return box(x0, y - 4, Z_PICO_TOP, x1, y + 4, Z_PICO_TOP + 3.0)


## @brief Model the parts inside the case as blocks, where they sit.
#
# Rough shapes, to check the case clears them: the Display Pack's board,
# glass and header sockets, the Pico's board and USB socket, the charger
# and the battery.
#
# @return A dict of name to block.
#
# @sideeffects None.
def components():
    bx, by = BOARD_X, BOARD_Y
    px, py = bx + PICO_X, by + PICO_Y
    socket_w = 2.6
    return {
        "Display Pack board": box(bx, by, Z_PACK_FRONT, bx + PACK_L, by + PACK_W, Z_PACK_BACK),
        "display glass": box(bx + PACK_GLASS_X[0], by + PACK_GLASS_Y[0], Z_GLASS,
                             bx + PACK_GLASS_X[1], by + PACK_GLASS_Y[1], Z_PACK_FRONT),
        "header sockets": box(px, py, Z_PACK_BACK, px + PICO_L, py + socket_w, Z_PICO_BOTTOM)
        + box(px, py + PICO_W - socket_w, Z_PACK_BACK, px + PICO_L, py + PICO_W,
              Z_PICO_BOTTOM),
        "Pico board": pico_board(px, py),
        "Pico USB socket": pico_usb_socket(),
        "charger": box(AMIGO_X, AMIGO_Y, Z_SPLIT - 7.0, AMIGO_X + AMIGO_L, AMIGO_Y + AMIGO_W,
                       Z_SPLIT),
        "battery": box(BATTERY_X, BATTERY_Y, Z_SPLIT - BATTERY_T, BATTERY_X + BATTERY_L,
                       BATTERY_Y + BATTERY_W, Z_SPLIT),
    }


## @brief Check the case clears the parts inside it.
#
# @return A list of clashes, "<part> hits <case part> (<volume> mm3)";
#         empty if everything fits.
#
# @sideeffects None.
def check_fit():
    case = {"front shell": front_shell(), "back cover": back_cover(),
            "button caps": placed_button_caps()}
    clashes = []
    for name, block in components().items():
        for case_name, part in case.items():
            volume = (block ^ part).volume()
            if volume > 0.01:
                clashes.append(f"{name} hits {case_name} ({volume:.2f} mm3)")
    caps = case["button caps"]
    for case_name in ("front shell", "back cover"):
        volume = (caps ^ case[case_name]).volume()
        if volume > 0.01:
            clashes.append(f"button caps hit {case_name} ({volume:.2f} mm3)")
    return clashes


## @brief Turn a part over, so its back (its highest z) prints on the bed.
#
# A half turn about the x axis, not a mirror, which would print the part
# back to front.
#
# @param part The part.
# @param z_top The part's highest z, which goes onto the bed.
# @return The part, turned over, on z = 0.
#
# @sideeffects None.
def turn_over(part, z_top):
    return part.rotate([180, 0, 0]).translate([0, HEIGHT, z_top])


## @brief Write a part as a binary STL file.
#
# @param part The part.
# @param path Where to write it.
# @return The number of triangles.
#
# @sideeffects Writes the file.
def write_stl(part, path):
    mesh = part.to_mesh()
    verts = np.asarray(mesh.vert_properties)[:, :3]
    tris = np.asarray(mesh.tri_verts)
    with open(path, "wb") as f:
        f.write(b"pico-w-network-meter case".ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for a, b, c in tris:
            p0, p1, p2 = verts[a], verts[b], verts[c]
            n = np.cross(p1 - p0, p2 - p0)
            norm = np.linalg.norm(n)
            n = n / norm if norm > 0 else n
            f.write(struct.pack("<12fH", *n, *p0, *p1, *p2, 0))
    return len(tris)


## @brief Draw a part from a few directions into a PNG file.
#
# @param part The part.
# @param path Where to write the image.
# @param title A title for it.
#
# @sideeffects Writes the file. Exits with an error if matplotlib is
#              missing.
def preview(part, path, title):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    except ImportError:
        sys.exit("--preview needs matplotlib: pip install matplotlib")

    mesh = part.to_mesh()
    verts = np.asarray(mesh.vert_properties)[:, :3]
    tris = verts[np.asarray(mesh.tri_verts)]
    fig = plt.figure(figsize=(12, 4))
    for i, (elev, azim) in enumerate([(90, -90), (-90, -90), (25, -60)]):
        ax = fig.add_subplot(1, 3, i + 1, projection="3d")
        ax.add_collection3d(Poly3DCollection(tris, facecolor="#8ab", edgecolor="#345",
                                             linewidths=0.1))
        lo, hi = verts.min(axis=0), verts.max(axis=0)
        span = (hi - lo).max() / 2
        mid = (hi + lo) / 2
        ax.set_xlim(mid[0] - span, mid[0] + span)
        ax.set_ylim(mid[1] - span, mid[1] + span)
        ax.set_zlim(mid[2] - span, mid[2] + span)
        ax.view_init(elev=elev, azim=azim)
        ax.set_axis_off()
    fig.suptitle(title)
    fig.savefig(path, dpi=110, bbox_inches="tight")
    plt.close(fig)


## @brief Cut a part along a plane, giving the outline of the cut.
#
# @param part The part.
# @param axis "x", "y" or "z": the plane is at right angles to it.
# @param at Where along that axis.
# @return Polygons (lists of 2D points) in the plane: (y, z) for an x cut,
#         (x, z) for a y cut, (x, y) for a z cut.
#
# @sideeffects None.
def section(part, axis, at):
    if axis == "x":
        # (x, y, z) -> (y, z, x): the cut at x becomes a cut at z.
        turned = part.transform([[0, 1, 0, 0], [0, 0, 1, 0], [1, 0, 0, 0]])
    elif axis == "y":
        turned = part.transform([[1, 0, 0, 0], [0, 0, 1, 0], [0, 1, 0, 0]])
    else:
        turned = part
    return [np.asarray(poly) for poly in turned.slice(at).to_polygons()]


## @brief Draw sections through the assembled case into a PNG file: the
#         case's parts in grey, the parts inside in colour.
#
# @param path Where to write the image.
#
# @sideeffects Writes the file. Exits with an error if matplotlib is
#              missing.
def preview_sections(path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("--preview needs matplotlib: pip install matplotlib")

    case = [front_shell(), back_cover(), placed_button_caps()]
    colours = ["#1f77b4", "#aec7e8", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b"]
    inside = list(components().items())
    pico_mid_y = BOARD_Y + PICO_Y + PICO_W / 2
    cuts = [
        ("y", pico_mid_y, "Through the Pico's USB socket (from below)", ("x", "z")),
        ("y", AMIGO_Y + AMIGO_W / 2, "Through the charger's USB-C socket", ("x", "z")),
        ("x", BATTERY_X + BATTERY_L / 2, "Through the battery and Pico (from the side)",
         ("y", "z")),
        ("z", Z_SPLIT - 1.0, "Inside, from the back, at the back cover", ("x", "y")),
    ]
    fig, axes = plt.subplots(2, 2, figsize=(13, 10))
    for ax, (axis, at, title, labels) in zip(axes.flat, cuts):
        for part in case:
            for poly in section(part, axis, at):
                ax.fill(poly[:, 0], poly[:, 1], color="#bbbbbb", edgecolor="#555555",
                        linewidth=0.4)
        for (name, block), colour in zip(inside, colours):
            for poly in section(block, axis, at):
                ax.fill(poly[:, 0], poly[:, 1], color=colour, alpha=0.8, label=name)
        ax.set_title(title)
        ax.set_xlabel(f"{labels[0]} (mm)")
        ax.set_ylabel(f"{labels[1]} (mm)")
        ax.set_aspect("equal")
        if axis != "z":
            ax.invert_yaxis()  # the front (z = 0) at the top
    handles = {}
    for ax in axes.flat:
        for h, label in zip(*ax.get_legend_handles_labels()):
            handles.setdefault(label, h)
    fig.legend(handles.values(), handles.keys(), loc="lower center", ncol=4)
    fig.suptitle(f"Case {LENGTH:.1f} x {HEIGHT:.1f} x {DEPTH:.1f} mm: sections")
    fig.savefig(path, dpi=110, bbox_inches="tight")
    plt.close(fig)


## @brief Generate the parts and, with --preview, their pictures.
#
# @return 0.
#
# @sideeffects Writes front.stl, back.stl and buttons.stl (and PNGs of the
#              parts and of sections through the assembly with --preview)
#              into the output directory, creating it; prints the case's
#              size, any clash between it and the parts inside, and each
#              file written.
def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "stl"),
                        help="output directory (default: stl/ beside this script)")
    parser.add_argument("--preview", action="store_true", help="also draw PNG views")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    print(f"Case: {LENGTH:.1f} x {HEIGHT:.1f} x {DEPTH:.1f} mm")
    clashes = check_fit()
    for clash in clashes:
        print(f"  CLASH: {clash}")
    if not clashes:
        print("  The parts inside fit.")
    parts = {
        "front": (front_shell(), "Front shell (print face down)"),
        "back": (turn_over(back_cover(), DEPTH), "Back cover (print outside face down)"),
        "buttons": (button_caps(), "Button caps (print flange down)"),
    }
    for name, (part, title) in parts.items():
        if part.is_empty():
            sys.exit(f"{name}: the model is empty")
        path = os.path.join(args.out, f"{name}.stl")
        count = write_stl(part, path)
        print(f"  {path}: {count} triangles, {part.volume() / 1000:.1f} cm3")
        if args.preview:
            preview(part, os.path.join(args.out, f"{name}.png"), title)
    if args.preview:
        preview_sections(os.path.join(args.out, "sections.png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
