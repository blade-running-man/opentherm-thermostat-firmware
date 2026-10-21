// Parametric enclosure for the LOLIN C3 mini + DIYLess OpenTherm Master Shield stack.
//
// Geometry only -- all numbers live in params.scad. Rationale is in docs/device/enclosure.md;
// the load-bearing facts a reader needs while editing this file (all confirmed by the owner's
// photos of the real assembly):
//
//  * The C3 is the TOP board, the shield the BOTTOM board. So the C3's USB-C, RST, BOOT and RGB
//    LED face UP toward the lid -- reached from the top. That is why the LED window and the
//    button poke-holes are in the LID, not the walls.
//  * The tall parts (KF128 terminal + optocouplers) sit BETWEEN the boards, inside board_gap --
//    their height is NOT stacked on top. This is why the box is only ~24 mm tall.
//  * USB-C (C3 top edge) and the boiler KF128 (gap level) exit the SAME front (+Y) short edge,
//    through one tall front window.
//  * Coordinates: origin at the footprint centre, Z=0 at the outer bottom. +Y = front (the
//    USB/KF128 edge). The shield sits on four floor posts; the C3 rides above it on the headers.
//  * Base and lid print open-face-up with no supports.

include <params.scad>

// ---- Derived geometry -----------------------------------------------------
cav_x = board_w + 2*fit;          // cavity footprint, short axis (X)
cav_y = board_l + 2*fit;          // cavity footprint, long axis  (Y)
ext_x = cav_x + 2*wall;
ext_y = cav_y + 2*wall;
in_r  = board_r + fit;
out_r = in_r + wall;

cavity_h = below_h + shield_thick + board_gap + c3_thick + top_clear;
base_h   = floor_t + cavity_h;    // Z of the base top rim

// Z landmarks from the outer bottom (0), shield BELOW, C3 ABOVE
z_shield_bot = floor_t + below_h;
z_shield_top = z_shield_bot + shield_thick;
z_c3_bot     = z_shield_top + board_gap;
z_c3_top     = z_c3_bot + c3_thick;

eps = 0.01;
big = max(ext_x, ext_y) + 20;

// ---- Helper: rounded rectangular prism, centred in XY, from z=0 up --------
// offset(-r) then offset(+r) rounds the corners to radius r while preserving the sx*sy box
// (needs r < min(sx,sy)/2 -- callers guard the small insets).
module rrect(sx, sy, h, r) {
  linear_extrude(height = h)
    offset(r = r) offset(r = -r)
      square([sx, sy], center = true);
}

// ---- Base -----------------------------------------------------------------
module base() {
  difference() {
    rrect(ext_x, ext_y, base_h, out_r);                 // solid outer block
    translate([0, 0, floor_t])                          // hollow, open at the top
      rrect(cav_x, cav_y, cavity_h + big, in_r);
    front_window();
  }
  corner_posts();                                        // the shield rests on these
}

// Four posts the BOTTOM board (shield) sits on, clearing its trimmed down-pins. Lateral hold
// comes from the cavity walls (fit clearance), not the posts.
module corner_posts() {
  p = 3.5;
  for (sx = [-1, 1], sy = [-1, 1])
    translate([ sx*(cav_x/2 - p/2), sy*(cav_y/2 - p/2), floor_t + below_h/2 ])
      cube([p, p, below_h], center = true);
}

// One tall opening on the front (+Y) wall: the KF128 in the gap (lower) and the USB-C on the C3
// top edge (upper) both show here, and both cables leave here. It runs up through the rim (the
// lid caps it), so there is no bridge.
module front_window() {
  z0 = z_shield_top - 0.5;
  translate([0, cav_y/2, (z0 + base_h + 1)/2])
    cube([front_win_w, 3*wall, (base_h + 1) - z0], center = true);
}

// ---- Lid ------------------------------------------------------------------
module lid() {
  translate([0, 0, base_h + eps])
  union() {
    difference() {
      rrect(ext_x, ext_y, lid_t, out_r);
      vents();
      led_window();
      button_holes();
    }
    // friction rim dropping into the cavity
    translate([0, 0, -rim_h])
      difference() {
        rrect(cav_x - 2*snap_fit, cav_y - 2*snap_fit, rim_h + eps, max(in_r - snap_fit, 0.4));
        translate([0, 0, -eps])
          rrect(cav_x - 2*snap_fit - 2*rim_w,
                cav_y - 2*snap_fit - 2*rim_w,
                rim_h + 3*eps,
                max(in_r - snap_fit - rim_w, 0.4));
      }
  }
}

// LED window over the WS2812B. In this frame the lid plate spans z = 0 (inner face, over the
// boards) to z = lid_t (outer face), so the pocket is cut from BELOW: by default it stops
// led_win_membrane short of the outer face and leaves a thin translucent skin that diffuses the
// LED; led_through cuts on past the outer face for a clean hole (light pipe or resin drop).
// DO NOT start the cut at the membrane height and run it up to lid_t: that removes the outer skin
// and leaves the plate under it, which is a through-hole whatever led_through says.
module led_window() {
  top = led_through ? lid_t + 1 : lid_t - led_win_membrane;
  translate([led_x, led_y, -1])
    cylinder(h = top + 1, d = led_win_d);
}

// Poke-holes over RST and BOOT (C3 faces up): press the button with a pin through the hole.
// (s*btn_hole_x, btn_hole_y) is the button's CENTRE, so the cube is centred on it. DO NOT anchor
// it at a corner -- that shifts each hole by half its width and pushes it into the rim.
module button_holes() {
  for (s = [-1, 1])
    translate([s*btn_hole_x, btn_hole_y, lid_t/2])
      cube([btn_hole_w, btn_hole_w, lid_t + 2], center = true);
}

// Slots in the lid, kept in the back half so they never clash with the LED/button holes.
module vents() {
  sl = ext_y * 0.30;
  g  = 3;
  for (i = [0 : vent_slots - 1])
    translate([ (i - (vent_slots - 1)/2) * (vent_slot_w + g), -ext_y*0.22, lid_t/2 ])
      cube([vent_slot_w, sl, lid_t + 2], center = true);
}

// ---- Board mock (preview only) --------------------------------------------
// A translucent stand-in for the real stack (shield below, C3 above, tall parts in the gap).
// Not part of any printed body.
module board_mock() {
  color([0.15, 0.30, 0.55, 0.85]) translate([0, 0, z_shield_bot]) rrect(board_w, board_l, shield_thick, board_r);
  color([0.10, 0.40, 0.15, 0.85]) translate([0, 0, z_c3_bot])     rrect(board_w, board_l, c3_thick, board_r);
  // KF128 on the shield's front edge, standing in the gap
  color([0.10, 0.50, 0.20, 0.90]) translate([0,  board_l/2 - 5, z_shield_top + board_gap/2]) cube([11, 7, board_gap - 0.5], center = true);
  // optocouplers block in the gap
  color([0.12, 0.12, 0.12, 0.90]) translate([1, -3, z_shield_top + (board_gap-1)/2]) cube([13, 11, board_gap - 1], center = true);
  // USB-C on the C3 top front edge
  color([0.70, 0.70, 0.70, 0.90]) translate([0,  board_l/2 - 3, z_c3_top + 1.6]) cube([9, 7, 3.2], center = true);
  // WS2812B on the C3 top
  color([0.20, 0.85, 0.75, 0.95]) translate([led_x, led_y, z_c3_top + 0.8]) cube([3.5, 3.5, 1.6], center = true);
}

// ---- Part selector --------------------------------------------------------
module shells() {
  if (part == "base")      base();
  else if (part == "lid")  lid();
  else { base(); lid(); }              // "assembly" -- both shells
}

if (cut)                               // slice the +X half away, board mock solid, to inspect fit
  difference() {
    union() { shells(); board_mock(); }
    translate([big/2, 0, base_h/2])
      cube([big, ext_y + big, base_h + big], center = true);
  }
else {
  shells();
  if (part == "assembly") %board_mock();
}
