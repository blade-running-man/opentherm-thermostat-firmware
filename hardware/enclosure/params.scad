// Enclosure parameters for the LOLIN C3 mini + DIYLess OpenTherm Master Shield stack.
//
// Values derived from datasheets/standard parts and from the owner's photos of the real
// assembly (see docs/enclosure-design.md section 2). Change a number here and re-render --
// nothing in case.scad is hard-coded. Kept separate from the geometry so both files stay well
// under the 350-line hand-written-source ceiling (CLAUDE.md).

// ---- Which part to render -------------------------------------------------
part = "assembly";      // "base" | "lid" | "assembly" (preview with a translucent board mock)
cut  = false;           // debug preview: slice the +X half away to inspect the internal fit

// ---- Board stack -- C3 on TOP, shield on BOTTOM (confirmed from the owner's photos) -------
// The tall parts (KF128 terminal + optocouplers) sit BETWEEN the boards, so the board gap
// already CONTAINS them -- their height is NOT added on top of the stack. This is the fix for
// the box being too tall: the KF128 lives in the gap, it does not stack above the shield.
board_l      = 34.3;    // long side  (C3 34.3 / shield 34.2 -> take the larger)
board_w      = 25.6;    // short side (C3 25.4 / shield 25.6 -> take the larger)
board_r      = 2.0;     // PCB corner radius (D1-mini family)
c3_thick     = 1.6;     // LOLIN C3 mini PCB (the TOP board)
shield_thick = 1.6;     // DIYLess shield PCB (the BOTTOM board)
board_gap    = 10.0;    // shield top -> C3 bottom. Holds the ~9.5 mm KF128 + optocouplers.
                        // DOMINANT height number now; measure the assembled gap to trim it.
below_h      = 3.0;     // clearance under the shield for the down-pins, trimmed nearly flush
top_clear    = 3.5;     // headroom over the C3's top face: just the USB-C shell (~3.2 mm) + buttons

// ---- Shell (mm) -----------------------------------------------------------
wall     = 2.0;         // side-wall thickness (5x 0.4 mm perimeters)
floor_t  = 2.0;         // base floor
lid_t    = 2.0;         // lid top plate
fit      = 0.3;         // clearance around the PCB edge, per side (FDM slot tolerance)

// ---- Lid join (friction fit) ----------------------------------------------
rim_h    = 3.0;         // how deep the lid rim sinks into the base
rim_w    = 1.6;         // rim wall thickness
snap_fit = 0.2;         // clearance between lid rim and cavity wall, per side

// ---- Front (+Y) short edge: USB-C (C3 level) + boiler KF128 (gap level), same end ---------
// The owner wires both out the same short edge. One tall front window exposes the KF128 in the
// gap (lower) and the USB-C on the C3 top edge (upper); both cables leave here.
front_win_w = 15.0;     // width of the front window (covers USB-C plug + KF128 + wires)

// ---- Lid top: RGB LED window + button poke-holes --------------------------
// C3 is the top board, so its LED, RST and BOOT face UP toward the lid -- reached from the top.
led_win_d        = 5.0;    // LED window diameter (over the WS2812B, GPIO7)
led_win_membrane = 0.8;    // leave this thin translucent wall to diffuse the LED
led_through      = false;  // true = clean through-hole (for a light pipe / resin drop)
led_x            = 0;      // LED position on the C3 top (from the photo: front-of-centre)
led_y            = 6.0;    //   +Y is the USB/front edge
btn_hole_w       = 4.0;    // poke-hole over each button (press RST/BOOT through the lid)
btn_hole_x       = board_w/2 - 4.0;   // near the front corners
btn_hole_y       = board_l/2 - 4.0;   // near the front (USB) edge

// ---- Ventilation (lid, back half so it clears the LED/buttons) -------------
vent_slot_w = 1.6;
vent_slots  = 5;

// ---- Rendering ------------------------------------------------------------
$fn = 48;
