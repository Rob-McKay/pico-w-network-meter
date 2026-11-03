#!/usr/bin/env python3
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0

"""Generate a 3D-printable case for the network meter, as STL files.

The case holds a Raspberry Pi Pico W plugged into a Pimoroni Pico Display
Pack 2.0, with a 350 mAh LiPo and a Pimoroni LiPo Amigo Pro charger stacked
behind the Pico, so the case is little bigger than the Display Pack. From
the front: the Display Pack, the Pico, a shelf, the battery, and the
charger, hanging top towards the back so its PWR button and LEDs face out.

    front.stl     the front shell: bezel with the display window, button
                  and LED holes, side walls, the charger's USB-C opening and
                  holes for the bolts. The display's active area is in the
                  middle of the front. Print face down.
    shelf.stl     a shelf between the Pico and the battery: posts on it hold
                  the Pico into the Display Pack's sockets, and the battery
                  and charger behind hold it in place. Print flat, posts up.
    back.stl      the back cover: posts the charger hangs on, and holes for
                  the PWR button's plunger and onto the charger's LEDs, and
                  blocks with nut traps for the four side bolts. Print
                  outside face down.
    buttons.stl   four button caps for A, B, X and Y, and a plunger for the
                  charger's PWR button: a tab inside keeps each in. Print
                  tab down.

The Pico's own USB socket has no opening, and the charger covers its BOOTSEL
button: open the case (four bolts) to flash new firmware.

Every measurement the case depends on is a constant below, in millimetres.
The Display Pack's come from Pimoroni's dimension drawing (turned to
landscape, buttons A and B on the left), the charger's are scaled from a
photograph, and the others are the parts' published sizes. Those that neither gives are marked MEASURE: check them
against your boards with calipers before printing.

Coordinates: x runs along the case from its left end (as you look at the
display), y up from its bottom edge, z back from the front face.

    python3 case.py [--out DIR] [--preview]

Needs numpy and manifold3d (pip install numpy manifold3d); --preview also
needs matplotlib, and draws PNG views next to the STL files.
"""

import argparse
import math
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
## Outside corner radius. Inside, the corners are square, for the boards'.
CORNER_R = 3.0
## Clearance around parts that fit into frames, holes or openings.
FIT = 0.4
## Circles' segments: smooth enough to print round.
SEGMENTS = 48

# --- Pimoroni Pico Display Pack 2.0 ---------------------------------------

## Board: 55.2 x 34.6 mm (drawing; measured as 55.0-55.2 long).
PACK_L = 55.2
PACK_W = 34.6
PACK_PCB = 1.6
## Front of the board to the front of the display glass. MEASURE.
PACK_DISPLAY_H = 3.0
## Display glass: in the middle of the board lengthwise, 4.0 mm from each
## end, and the board's full width (measured).
PACK_GLASS_X = (4.0, PACK_L - 4.0)
PACK_GLASS_Y = (0.0, PACK_W)
## The window: the active area (40.8 x 30.6 mm, the panel's size), in the
## middle of the glass, with a margin. The bezel rests on the glass around
## it. MEASURE: with the display lit, check the picture is in the middle.
ACTIVE_X = ((PACK_L - 40.8) / 2, (PACK_L + 40.8) / 2)
ACTIVE_Y = ((PACK_W - 30.6) / 2, (PACK_W + 30.6) / 2)
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
## way round, as the Display Pack's markings show; this places the shelf's
## posts and wire slots. MEASURE.
PICO_USB_LEFT = True
## Mounting holes, from its USB end and bottom edge (published).
PICO_HOLES = [(2.0, 4.8), (2.0, 16.2), (49.0, 4.8), (49.0, 16.2)]
PICO_HOLE_D = 2.1

# --- Pimoroni LiPo Amigo Pro ----------------------------------------------

## Board: 36 x 23 x 7 mm with its connectors (published): USB-C at one
## end, the BAT and DEVICE sockets facing out of the other, everything on
## its top. It hangs top towards the back cover, on posts in its mounting
## holes, so its PWR button and LEDs face the back.
##
## Positions on it are from its USB-C end ("along") and from its bottom
## edge as you look at its top with the USB-C on the right ("across"),
## scaled from a photograph. MEASURE.
AMIGO_L = 36.0
AMIGO_W = 23.0
AMIGO_PCB = 1.0
## Its tallest parts (the JST sockets) above its top.
AMIGO_PARTS_H = 6.0
## Mounting holes: two at the USB-C end, one at the far corner (the DEVICE
## socket takes the fourth corner).
AMIGO_HOLES = [(3.1, 2.4), (3.1, 20.8), (34.1, 20.6)]
## The holes' size; the pins into them are 0.3 mm thinner. 2.6 mm (from the
## photo) gave pins about 0.5 mm too wide on a print.
AMIGO_HOLE_D = 2.1
## USB-C socket: its centre across the board, and above it.
AMIGO_USB_ACROSS = 11.0
AMIGO_USB_H = 1.6
## Opening for a USB-C plug's moulding.
AMIGO_USB_OPENING = (13.0, 7.5)
## The PWR button (press to make), and the height of its top above the board.
AMIGO_PWR = (8.6, 2.0)
AMIGO_PWR_H = 1.5
## The CHG and ON LEDs beside it, seen through holes in the back cover.
AMIGO_LEDS = [(14.6, 2.0), (12.3, 2.0)]
## Room past its far end for the BAT and DEVICE plugs and their leads.
AMIGO_PLUG_CLEAR = 9.0

## Its height with its connectors (published).
AMIGO_H = AMIGO_PCB + AMIGO_PARTS_H

# --- 350 mAh LiPo ---------------------------------------------------------

## 32.0 x 20.5 x 7.3 mm. It lies along the case, in front of the charger.
## (Pimoroni's 150 mAh cell, 27 x 20.5 x 4.3 mm, fits too.)
BATTERY_L = 32.0
BATTERY_W = 20.5
BATTERY_T = 7.3

# --- Fastenings -----------------------------------------------------------

## Four M2 x 6 mm bolts through the long walls, two in each, into M2 hex
## nuts held in blocks on the back cover's rim beside the charger: the walls
## hug the Display Pack, leaving no room for screw posts inside.
SCREW_CLEAR_D = 2.4
## The bolts' positions along the case, from each end.
SCREW_FROM_END = 10.0
## The blocks: their width (x), how far in from the wall (y), and depth (z).
SCREW_BLOCK_W = 8.0
SCREW_BLOCK_T = 4.5
SCREW_BLOCK_H = 6.0
## An M2 hex nut: across its flats, and thick. Its slot, across the bolt,
## is open at the block's end (towards the inside of the case), so the nut
## drops in; the slot's sides hold its flats, so it can't turn.
NUT_AF = 4.0
NUT_T = 1.6
NUT_FIT = 0.3
## The nut's middle, in from the block's wall side: the bolt passes right
## through it and ends inside the block.
NUT_IN = 2.5
## The bolts' length under the head.
BOLT_L = 6.0
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

## Clearance behind the Pico's tallest parts, and behind the battery layer.
BACK_CLEAR = 0.5
## The shelf between the Pico and the battery layer.
SHELF_T = 1.5
## Its posts onto the Pico's mounting holes.
SHELF_POST_D = 3.2
## Slots in the shelf for the lead from the charger's DEVICE socket: in
## the gap past the charger, beside the Pico on either side (whichever its
## pins 38 and 39 are on), so the lead runs along the Pico's side to them.
SHELF_SLOT = (6.0, 4.0)
## Keep the charger clear of the screw blocks.
BLOCK_CLEAR = SCREW_BLOCK_T + 1.0

## Depths, from the front face.
Z_GLASS = FRONT
Z_PACK_FRONT = Z_GLASS + PACK_DISPLAY_H
Z_PACK_BACK = Z_PACK_FRONT + PACK_PCB
Z_PICO_BOTTOM = Z_PACK_BACK + PACK_SOCKET_H
Z_PICO_TOP = Z_PICO_BOTTOM + PICO_PCB
Z_SHELF = Z_PICO_TOP + PICO_PARTS_H + BACK_CLEAR
Z_LAYER = Z_SHELF + SHELF_T
## Behind the shelf, stacked: the battery, then the charger, which hangs on
## posts from the back cover, top towards it, so its PWR button and LEDs
## face the back. The battery is held between the shelf and the charger.
Z_BATTERY = Z_LAYER
Z_AMIGO_BOTTOM = Z_BATTERY + BATTERY_T + BACK_CLEAR
Z_AMIGO_TOP = Z_AMIGO_BOTTOM + AMIGO_PCB
## The charger's posts: its tallest parts clear the cover.
AMIGO_STANDOFF = AMIGO_PARTS_H + 0.3
## Where the front shell meets the back cover.
Z_SPLIT = Z_AMIGO_TOP + AMIGO_STANDOFF
DEPTH = Z_SPLIT + WALL
## Inside: the walls hug the Display Pack. If the display's active area
## were towards the X/Y end, the case would reach further at the A/B end to
## keep the active area in the middle of the front. Behind it the
## charger, against the left wall for its USB-C, with room past its far end
## for its plugs, and the battery in front of it; the charger clear of the
## screw blocks along the long walls.
_ACTIVE_MID = (ACTIVE_X[0] + ACTIVE_X[1]) / 2
BOARD_X = max(WALL + FIT / 2, PACK_L + FIT / 2 + WALL - 2 * _ACTIVE_MID)
LENGTH = 2 * (BOARD_X + _ACTIVE_MID)
INSIDE_L = LENGTH - 2 * WALL
INSIDE_H = max(PACK_W + FIT, AMIGO_W + FIT + 2 * BLOCK_CLEAR, BATTERY_W + FIT)
HEIGHT = INSIDE_H + 2 * WALL
BOARD_Y = WALL + (INSIDE_H - PACK_W) / 2

## Where things are on the case, from its outside bottom-left corner. The
## charger's USB end, and the battery's end, are against the left wall.
AMIGO_X = WALL + FIT / 2
AMIGO_Y = (HEIGHT - AMIGO_W) / 2
BATTERY_X = WALL + FIT / 2
BATTERY_Y = (HEIGHT - BATTERY_W) / 2
## The screws: x along the case, and z, through the long walls.
SCREWS_X = [SCREW_FROM_END, LENGTH - SCREW_FROM_END]
SCREW_Z = Z_SPLIT - SCREW_BLOCK_H / 2


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


## @brief Find a point on the charger, given along from its USB-C end and
#         across from its bottom edge (looking at its top, USB-C on the
#         right).
#
# It hangs top towards the back cover, USB-C at the left wall. Checked
# against a printed cover: its bottom edge (the PWR button and LEDs) is at
# the top of the case, so "across" runs down the case.
#
# @param along Distance from its USB-C end.
# @param across Distance from its bottom edge.
# @return (x, y) on the case.
#
# @sideeffects None.
def amigo_point(along, across):
    return AMIGO_X + along, AMIGO_Y + AMIGO_W - across


## @brief Cut the charger's USB-C opening, in the left wall.
#
# The Pico's own USB socket gets no opening: the case comes open to flash
# the Pico (four bolts), which is also the way to its BOOTSEL button.
#
# @return The cutting solid, applied to the front shell, the shelf and the
#         back cover's lip.
#
# @sideeffects None.
def usb_openings():
    _, y = amigo_point(0.0, AMIGO_USB_ACROSS)
    return end_wall_opening(True, y, Z_AMIGO_TOP + AMIGO_USB_H, AMIGO_USB_OPENING)


## @brief Make a screw hole through a long wall, along y.
#
# @param x The screw's position along the case.
# @param y0 Where the hole starts.
# @param y1 Where it ends.
# @param d Its diameter.
# @return The hole, at SCREW_Z.
#
# @sideeffects None.
def screw_hole(x, y0, y1, d):
    hole = Manifold.cylinder(y1 - y0, d / 2, d / 2, SEGMENTS).rotate([-90, 0, 0])
    return hole.translate([x, y0, SCREW_Z])


## @brief Make the slot that holds a bolt's hex nut, in a screw block.
#
# Across the bolt, as wide as the nut's flats (so it can't turn) and a
# little thicker than the nut, open at the block's end so the nut drops in,
# and deep enough that the nut, point down, sits centred on the bolt.
#
# @param x The bolt's position along the case.
# @param y The nut's middle, across the case.
# @return The slot.
#
# @sideeffects None.
def nut_slot(x, y):
    w = NUT_AF + NUT_FIT
    t = NUT_T + NUT_FIT
    corners = (NUT_AF / math.cos(math.radians(30)) + NUT_FIT) / 2
    return box(x - w / 2, y - t / 2, Z_SPLIT - SCREW_BLOCK_H - 1, x + w / 2, y + t / 2,
               SCREW_Z + corners)


## @brief Model the front shell.
#
# @return The shell: bezel, walls (square inside, hugging the Display Pack),
#         a frame locating the Display Pack at its A/B end, and the screws'
#         clearance holes through the long walls.
#
# @sideeffects None.
def front_shell():
    shell = rounded_slab(0, 0, Z_SPLIT) - box(WALL, WALL, FRONT, LENGTH - WALL, HEIGHT - WALL,
                                              Z_SPLIT + 1)

    # A frame that holds the Display Pack's edges, from the bezel to the
    # back of its board.
    frame_out = box(BOARD_X - FIT / 2 - 1.2, BOARD_Y - FIT / 2 - 1.2, FRONT,
                    BOARD_X + PACK_L + FIT / 2 + 1.2, BOARD_Y + PACK_W + FIT / 2 + 1.2,
                    Z_PACK_BACK)
    frame_in = box(BOARD_X - FIT / 2, BOARD_Y - FIT / 2, FRONT - 1,
                   BOARD_X + PACK_L + FIT / 2, BOARD_Y + PACK_W + FIT / 2, Z_PACK_BACK + 1)
    shell += frame_out - frame_in

    for x in SCREWS_X:
        shell -= screw_hole(x, -1, WALL + 1, SCREW_CLEAR_D)
        shell -= screw_hole(x, HEIGHT - WALL - 1, HEIGHT + 1, SCREW_CLEAR_D)

    m = WINDOW_MARGIN
    shell -= box(BOARD_X + ACTIVE_X[0] - m, BOARD_Y + ACTIVE_Y[0] - m, -1,
                 BOARD_X + ACTIVE_X[1] + m, BOARD_Y + ACTIVE_Y[1] + m, FRONT + 1)
    for bx, by in BUTTONS.values():
        shell -= cylinder(BOARD_X + bx, BOARD_Y + by, -1, FRONT + 1, BUTTON_STEM_D + FIT)
    shell -= cylinder(BOARD_X + LED[0], BOARD_Y + LED[1], -1, FRONT + 1, LED_HOLE_D)

    return shell - usb_openings()


## @brief Model the back cover's lip: strips along the walls, inside them,
#         that line the cover up with the shell.
#
# None along the left wall: the charger's end goes right against it, so
# that its USB-C socket is close to the outside. The other
# three line the cover up.
#
# @return The lip.
#
# @sideeffects None.
def back_lip():
    z0, z1 = Z_SPLIT - LIP_H, Z_SPLIT
    i0 = WALL + FIT / 2
    i1 = i0 + LIP_T
    end = WALL + 1.0
    return (box(end, i0, z0, LENGTH - end, i1, z1)
            + box(end, HEIGHT - i1, z0, LENGTH - end, HEIGHT - i0, z1)
            + box(LENGTH - i1, end, z0, LENGTH - i0, HEIGHT - end, z1))


## @brief Model the back cover.
#
# @return The cover: plate, lip, blocks with nut traps for the side bolts, posts the
#         charger hangs on (pins in its mounting holes), and holes for the
#         charger's PWR button plunger and onto its LEDs.
#
# @sideeffects None.
def back_cover():
    cover = rounded_slab(0, Z_SPLIT, DEPTH) + back_lip()
    for hx, hy in AMIGO_HOLES:
        x, y = amigo_point(hx, hy)
        cover += cylinder(x, y, Z_AMIGO_TOP, Z_SPLIT, 4.5)
        cover += cylinder(x, y, Z_AMIGO_TOP - AMIGO_PCB, Z_AMIGO_TOP, AMIGO_HOLE_D - 0.3)

    i0 = WALL + FIT / 2
    for x in SCREWS_X:
        for wall_y, inward in ((i0, 1), (HEIGHT - i0, -1)):
            y0, y1 = sorted([wall_y, wall_y + inward * SCREW_BLOCK_T])
            cover += box(x - SCREW_BLOCK_W / 2, y0, Z_SPLIT - SCREW_BLOCK_H,
                         x + SCREW_BLOCK_W / 2, y1, Z_SPLIT)
            cover -= screw_hole(x, y0 - 1, y1 + 1, SCREW_CLEAR_D)
            cover -= nut_slot(x, wall_y + inward * NUT_IN)
    x, y = amigo_point(*AMIGO_PWR)
    cover -= cylinder(x, y, Z_SPLIT - 1, DEPTH + 1, BUTTON_STEM_D + FIT)
    for led in AMIGO_LEDS:
        x, y = amigo_point(*led)
        cover -= cylinder(x, y, Z_SPLIT - 1, DEPTH + 1, 1.8)

    return cover - usb_openings()


## @brief Model the shelf between the Pico and the battery layer.
#
# A plate filling the inside, with posts whose
# pins go into the Pico's mounting holes. Two slots, in the gap past the
# charger and just beside the Pico on either side, let the lead from the
# charger's DEVICE socket through, to run along the Pico's side to its pins
# 38 and 39. The charger and battery behind keep the shelf pressed onto the
# Pico.
#
# @return The shelf.
#
# @sideeffects None.
def shelf():
    i0 = WALL + FIT / 2
    plate = box(i0, i0, Z_SHELF, LENGTH - i0, HEIGHT - i0, Z_LAYER)
    for hx, hy in PICO_HOLES:
        x, y = pico_point(hx, hy)
        plate += cylinder(x, y, Z_PICO_TOP, Z_SHELF, SHELF_POST_D)
        plate += cylinder(x, y, Z_PICO_TOP - 1.0, Z_PICO_TOP, PICO_HOLE_D - 0.3)
    w, h = SHELF_SLOT
    x0 = AMIGO_X + AMIGO_L + (AMIGO_PLUG_CLEAR - w) / 2
    pico_y0 = BOARD_Y + PICO_Y
    for y in (pico_y0 - h / 2 - 1.0, pico_y0 + PICO_W + h / 2 + 1.0):
        plate -= box(x0, y - h / 2, Z_SHELF - 1, x0 + w, y + h / 2, Z_LAYER + 1)
    return plate - usb_openings()


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


## @brief Model the plunger for the charger's PWR button, standing on its
#         tab, its stem's centre at the origin.
#
# The stem reaches from the button's top, through the back cover, to
# BUTTON_PROUD behind it; the tab rests on the button and stops the plunger
# falling out.
#
# @return The plunger, tab on z = 0.
#
# @sideeffects None.
def pwr_plunger():
    length = (DEPTH - (Z_AMIGO_TOP + AMIGO_PWR_H)) + BUTTON_PROUD
    stem = Manifold.cylinder(length, BUTTON_STEM_D / 2, BUTTON_STEM_D / 2, SEGMENTS)
    return stem + box(-1.5, -2.5, 0, 1.5, 2.5, BUTTON_TAB_H)


## @brief Lay out the four button caps and the PWR plunger, a print's worth.
#
# @return The caps in a row, then the plunger.
#
# @sideeffects None.
def button_caps():
    cap = button_cap()
    caps = cap
    for i in range(1, len(BUTTONS)):
        caps += cap.translate([i * (BUTTON_TAB_X + 4), 0, 0])
    return caps + pwr_plunger().translate([len(BUTTONS) * (BUTTON_TAB_X + 4), 0, 0])


## @brief Place the button caps and the PWR plunger where they sit in the
#         case, for checking the fit.
#
# @return The caps, stems through the bezel, and the plunger, stem through
#         the back cover, tabs on their buttons.
#
# @sideeffects None.
def placed_button_caps():
    caps = None
    for bx, by in BUTTONS.values():
        # Turned to point out of the front: the tab's face (z = 0) on the button.
        cap = button_cap().rotate([180, 0, 0])
        cap = cap.translate([BOARD_X + bx, BOARD_Y + by, Z_PACK_FRONT - BUTTON_TOP_H])
        caps = cap if caps is None else caps + cap
    x, y = amigo_point(*AMIGO_PWR)
    return caps + pwr_plunger().translate([x, y, Z_AMIGO_TOP + AMIGO_PWR_H])


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


## @brief Model the four bolts and their nuts, where they sit.
#
# Each bolt is a 2 mm rod BOLT_L long from the outside of its wall; each nut
# a hex prism, point down, in its slot.
#
# @return The bolts and nuts.
#
# @sideeffects None.
def bolts_and_nuts():
    parts = None
    i0 = WALL + FIT / 2
    hexagon = CrossSection.circle(NUT_AF / math.sqrt(3), 6).rotate(90)
    for x in SCREWS_X:
        for outside, inward in ((0.0, 1), (HEIGHT, -1)):
            y0, y1 = sorted([outside, outside + inward * BOLT_L])
            bolt = screw_hole(x, y0, y1, 2.0)
            ny = (i0 if inward > 0 else HEIGHT - i0) + inward * NUT_IN
            nut = Manifold.extrude(hexagon, NUT_T).translate([0, 0, -NUT_T / 2])
            nut = nut.rotate([90, 0, 0]).translate([x, ny, SCREW_Z])
            nut -= screw_hole(x, ny - NUT_T, ny + NUT_T, 2.0)
            piece = bolt + nut
            parts = piece if parts is None else parts + piece
    return parts


## @brief Model the charger as it hangs, top towards the back cover.
#
# Its board, with its mounting holes, and a block for its parts on its top,
# clear of the holes and of the space over its PWR button, where the
# plunger goes.
#
# @return The charger.
#
# @sideeffects None.
def charger():
    x0, y0 = AMIGO_X, AMIGO_Y
    z_board = Z_AMIGO_TOP - AMIGO_PCB
    board = box(x0, y0, z_board, x0 + AMIGO_L, y0 + AMIGO_W, Z_AMIGO_TOP)
    parts = box(x0, y0, Z_AMIGO_TOP, x0 + AMIGO_L, y0 + AMIGO_W, Z_AMIGO_TOP + AMIGO_PARTS_H)
    for hx, hy in AMIGO_HOLES:
        x, y = amigo_point(hx, hy)
        board -= cylinder(x, y, z_board - 1, Z_AMIGO_TOP + 1, AMIGO_HOLE_D)
        parts -= cylinder(x, y, Z_AMIGO_TOP - 1, Z_SPLIT + 1, 5.0)
    x, y = amigo_point(*AMIGO_PWR)
    parts -= box(x - 3, y - 3, Z_AMIGO_TOP + AMIGO_PWR_H, x + 3, y + 3, Z_SPLIT + 1)
    return board + parts


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
        "charger": charger(),
        "bolts and nuts": bolts_and_nuts(),
        "battery": box(BATTERY_X, BATTERY_Y, Z_BATTERY, BATTERY_X + BATTERY_L,
                       BATTERY_Y + BATTERY_W, Z_BATTERY + BATTERY_T),
    }


## @brief Check the case clears the parts inside it, the case's parts clear
#         each other, and the Display Pack can be put in.
#
# @return A list of clashes, "<part> hits <case part> (<volume> mm3)";
#         empty if everything fits.
#
# @sideeffects None.
def check_fit():
    case = {"front shell": front_shell(), "shelf": shelf(), "back cover": back_cover(),
            "button caps": placed_button_caps()}
    clashes = []
    for name, block in components().items():
        for case_name, part in case.items():
            volume = (block ^ part).volume()
            if volume > 0.01:
                clashes.append(f"{name} hits {case_name} ({volume:.2f} mm3)")
    caps = case["button caps"]
    for case_name in ("front shell", "shelf", "back cover"):
        volume = (caps ^ case[case_name]).volume()
        if volume > 0.01:
            clashes.append(f"button caps hit {case_name} ({volume:.2f} mm3)")
    # The Display Pack, with the Pico on it, goes in from the back: its
    # outline, swept from the open back to where it sits, must clear the shell.
    sweep = box(BOARD_X, BOARD_Y, Z_PACK_FRONT, BOARD_X + PACK_L, BOARD_Y + PACK_W, Z_SPLIT + 1)
    volume = (sweep ^ case["front shell"]).volume()
    if volume > 0.01:
        clashes.append(f"the Display Pack can't go in past the front shell ({volume:.2f} mm3)")
    for a, b in (("front shell", "shelf"), ("front shell", "back cover"), ("shelf", "back cover")):
        volume = (case[a] ^ case[b]).volume()
        if volume > 0.01:
            clashes.append(f"{a} hits {b} ({volume:.2f} mm3)")
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
        from matplotlib.patches import PathPatch
        from matplotlib.path import Path
    except ImportError:
        sys.exit("--preview needs matplotlib: pip install matplotlib")

    ## @brief Draw a cut through a part as one shape, holes and all.
    #
    # @param ax The axes.
    # @param polys The cut (section()).
    # @param style PathPatch's style (colours, label).
    #
    # @sideeffects Adds the shape to @p ax.
    def draw(ax, polys, **style):
        if not polys:
            return
        verts, codes = [], []
        for poly in polys:
            verts += list(poly) + [poly[0]]
            codes += [Path.MOVETO] + [Path.LINETO] * (len(poly) - 1) + [Path.CLOSEPOLY]
        ax.add_patch(PathPatch(Path(verts, codes), **style))

    case = [front_shell(), shelf(), back_cover(), placed_button_caps()]
    colours = ["#1f77b4", "#aec7e8", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b",
               "#e377c2", "#bcbd22", "#17becf", "#7f7f7f", "#ffbb78"]
    inside = list(components().items())
    if len(inside) > len(colours):
        raise ValueError(f"{len(inside)} parts inside but only {len(colours)} colours")
    pico_mid_y = BOARD_Y + PICO_Y + PICO_W / 2
    # (plane cut, where, title, axis names): None for the front view.
    panels = [
        (None, 0.0, "Front view", ("x", "y")),
        ("z", Z_BATTERY + BATTERY_T / 2, "Through the battery, seen from the front",
         ("x", "y")),
        ("y", pico_mid_y, "Through the Pico, seen from below", ("x", "z")),
        ("y", amigo_point(0.0, AMIGO_PWR[1])[1], "Through the charger's PWR button",
         ("x", "z")),
        ("x", BATTERY_X + BATTERY_L / 2, "Through the battery, charger and Pico, from the side",
         ("y", "z")),
        ("z", Z_AMIGO_TOP + 2.0, "Through the charger, seen from the front", ("x", "y")),
    ]
    # One scale for every panel: each shows the same span, centred on the case.
    span_h = LENGTH + 6
    span_v = max(DEPTH, HEIGHT) + 6
    mids = {"x": LENGTH / 2, "y": HEIGHT / 2, "z": DEPTH / 2}
    fig, axes = plt.subplots(3, 2, figsize=(13, 14), layout="constrained")
    for ax, (axis, at, title, labels) in zip(axes.flat, panels):
        if axis is None:
            draw_front(ax, draw, colours)
        else:
            for part in case:
                draw(ax, section(part, axis, at), facecolor="#bbbbbb", edgecolor="#555555",
                     linewidth=0.4)
            for (name, block), colour in zip(inside, colours):
                draw(ax, section(block, axis, at), facecolor=colour, alpha=0.8, label=name,
                     linewidth=0)
        h, v = labels
        ax.set_xlim(mids[h] - span_h / 2, mids[h] + span_h / 2)
        ax.set_ylim(mids[v] - span_v / 2, mids[v] + span_v / 2)
        if v == "z":
            ax.invert_yaxis()  # the front (z = 0) at the top
        ax.set_aspect("equal")
        ax.set_title(title)
        ax.set_xlabel(f"{h} (mm)")
        ax.set_ylabel(f"{v} (mm)")
    handles = {}
    for ax in axes.flat:
        for h, label in zip(*ax.get_legend_handles_labels()):
            handles.setdefault(label, h)
    fig.legend(handles.values(), handles.keys(), loc="outside lower center", ncol=4)
    fig.suptitle(f"Case {LENGTH:.1f} x {HEIGHT:.1f} x {DEPTH:.1f} mm, all at one scale")
    fig.savefig(path, dpi=110, bbox_inches="tight")
    plt.close(fig)


## @brief Draw the case from the front: the bezel, the display through its
#         window, the button caps and the LED.
#
# @param ax The axes.
# @param draw The shape-drawing function (preview_sections()'s).
# @param colours Its colours: the display's glass and board take the first
#        two.
#
# @sideeffects Adds shapes and labels to @p ax.
def draw_front(ax, draw, colours):
    glass = box(BOARD_X + PACK_GLASS_X[0], BOARD_Y + PACK_GLASS_Y[0], 0,
                BOARD_X + PACK_GLASS_X[1], BOARD_Y + PACK_GLASS_Y[1], 1)
    active = box(BOARD_X + ACTIVE_X[0], BOARD_Y + ACTIVE_Y[0], 0, BOARD_X + ACTIVE_X[1],
                 BOARD_Y + ACTIVE_Y[1], 1)
    draw(ax, section(glass, "z", 0.5), facecolor=colours[1], linewidth=0)
    draw(ax, section(active, "z", 0.5), facecolor="#16324f", linewidth=0,
         label="display (active area)")
    led = cylinder(BOARD_X + LED[0], BOARD_Y + LED[1], 0, 1, LED_HOLE_D + 1)
    draw(ax, section(led, "z", 0.5), facecolor="#d020d0", linewidth=0, label="RGB LED")
    draw(ax, section(front_shell(), "z", FRONT / 2), facecolor="#bbbbbb", edgecolor="#555555",
         linewidth=0.4)
    draw(ax, section(placed_button_caps(), "z", -BUTTON_PROUD / 2), facecolor="#777777",
         linewidth=0)
    for name, (bx, by) in BUTTONS.items():
        dx = -3.5 if bx < PACK_L / 2 else 3.5
        ax.text(BOARD_X + bx + dx, BOARD_Y + by, name, ha="center", va="center", fontsize=9)
    ax.plot([LENGTH / 2], [HEIGHT / 2], "+", color="#ffffff", markersize=10)


## @brief Generate the parts and, with --preview, their pictures.
#
# @return 0.
#
# @sideeffects Writes front.stl, shelf.stl, back.stl and buttons.stl (and PNGs of the
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
        "shelf": (turn_over(shelf(), Z_LAYER), "Shelf (print posts up)"),
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
