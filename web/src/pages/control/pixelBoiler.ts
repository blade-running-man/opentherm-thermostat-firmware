// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The whole heating system on ONE pixel canvas, plumbed like the owner's schematic: EVERY pipe
// meets the boiler at its BOTTOM. Cold mains water enters the boiler bottom; the burner heats it;
// hot water (orange) leaves the bottom on the FLOW pipe, U-flows up and back down through the
// RADIATOR and returns cold (cyan) to the boiler bottom; and a DHW branch drops out of the boiler
// bottom, crosses the floor and rises into a wall-hung BASIN, where a tap runs hot water into the bowl.
// Inside the boiler the water is the flame, not packets.
//
// Reuses the pure maths of both sub-systems unchanged — the flame's column heights (pixelFire.ts)
// and the loop geometry/speed/glow (pixelFlow.ts), each still host-tested. prefers-reduced-motion
// draws one static frame; start()/stop() own the rAF loop.

import { BASE, CORE, MID, OUTER, modScale, nextFrame, type FireFrame } from "./pixelFire";
import {
  buildPath, intensity, locate, packetDists, speed, tempGlow, warmAt,
  type LoopInputs, type LoopPath,
} from "./pixelFlow";

const W = 150, H = 112;
const BX0 = 16, BX1 = 54, BBY0 = 26, BBY1 = 72;   // boiler box (all pipes meet its bottom, y=BBY1)
const RX0 = 108, RX1 = 138, RRY0 = 44, RRY1 = 86; // radiator box
const RAD_COLS = [110, 117, 124, 131], RAD_CW = 5; // a column radiator: four vertical steel sections
const FIRE_FLOOR = 68, FIRE_X0 = 28, FIRE_CW = 2, FIRE_CH = 3;

// The four boiler connections are spaced symmetrically along its bottom edge (centre 35):
//   COLD 23 · RETURN 31 · FLOW 39 · DHW 47.
// The circuit, in flow order: out of the boiler bottom (FLOW) to the radiator and back along a lower
// parallel pipe (RETURN) into the boiler bottom. The three horizontal floor pipes are locked at an
// even 9px pitch so they read as one bundle leaving the boiler: DHW supply (y=82) · FLOW (y=91) ·
// RETURN (y=100), the middle FLOW pipe centred between the fixed outer two. FLOW and RETURN run
// parallel and never cross. The path still snakes internally, but that part is hidden — drawRadiator
// paints a solid column radiator over the box and drawLoop suppresses the raw pipe/packets inside it.
const LOOP: Array<[number, number]> = [
  [39, 72], [39, 91], [113, 91],
  [113, 48], [120, 48], [120, 82], [127, 82], [127, 48], [134, 48], [134, 100],
  [31, 100], [31, 72], [39, 72],
];
const WARM_DIST = 190; // hot through the FLOW pipe and the first passes; cools as it snakes down

const SH_OUT = 47;                                    // the boiler's DHW outlet tap (also the U-bend's hot leg)
const SINK_MID = 80;                                  // wall-hung basin centre on the canvas
const RIM_Y = 50, RIM_HW = 16;                        // the rim slab the bowl hangs from (spans SINK_MID ± RIM_HW)
const BOWL_TOP = 53, BOWL_ROWS = 13;                  // bowl mouth (just below the rim) and its depth in rows
const BOWL_TOP_HW = 14, BOWL_BOT_HW = 3;              // bowl half-width, mouth -> rounded base (an elliptic funnel)
const FAUCET_X = 86, SPOUT_X = 77, FAUCET_TOP = 35;   // gooseneck: post right of centre, spout over the bowl
const DHW_FLOOR = 82;                                 // DHW supply floor run; top of the locked 9px triplet DHW 82 · FLOW 91 · RETURN 100
const IN_Y = 104, IN_UP_X = 23;                      // cold-water intake
const XCH_L = 31, XCH_R = 39;    // CH return/flow taps on the boiler bottom (match LOOP's 31 / 39)
const DHW_TOP = 38, CH_TOP = 52; // heights of the two internal heat-exchanger U-bends (below header)

const N_PACKETS = 9, TAIL = 4, CHEV_GAP = 14;
const PPF_MAX = 2.2, SCENE_FPS = 30, FIRE_EVERY = 4;

const HOT = "#ff8a3d", COOL = "#00f0ff", PIPE_DIM = "#3a3350";
const BOILER_BG = "#160a12", BOILER_LINE = "#ff2a6d", RAD_LINE = "#2a3550";
const RAD_BODY = "#8894b0", RAD_HI = "#c2cde4"; // steel radiator section + its highlight stripe
const BOILER_SCAN = "#2a1a35"; // CRT scanlines inside the boiler window (the 80s furnace display)
const FIXTURE = "#9aa4bf", WATER = "#6fe0ff", STEAM = "#bfe9ff"; // chrome fittings, running water, steam

export type BoilerInputs = LoopInputs;

/** Drives the whole heating-system scene on one canvas. setInputs() feeds it live state. */
export class PixelBoiler {
  private ctx: CanvasRenderingContext2D;
  private path: LoopPath;
  private inp: BoilerInputs = {
    chActive: false, dhwActive: false, flame: false,
    flowTemp: null, returnTemp: null, modulation: null,
  };
  private phase = 0;
  private frames = 0;
  private fire: FireFrame = nextFrame(0.7, () => 0.5);
  private raf = 0;
  private prev = 0;
  private reduced: boolean;

  constructor(canvas: HTMLCanvasElement) {
    canvas.width = W;
    canvas.height = H;
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("pixelBoiler: no 2d context");
    this.ctx = ctx;
    this.path = buildPath(LOOP, WARM_DIST);
    this.reduced = typeof matchMedia === "function"
      && matchMedia("(prefers-reduced-motion: reduce)").matches;
  }

  setInputs(inp: BoilerInputs): void {
    this.inp = inp;
    if (this.reduced) this.draw();
  }

  start(): void {
    if (this.reduced) { this.draw(); return; }
    const loop = (now: number) => {
      this.raf = requestAnimationFrame(loop);
      if (now - this.prev < 1000 / SCENE_FPS) return;
      this.prev = now;
      this.phase += PPF_MAX * speed(this.inp);
      if (this.frames++ % FIRE_EVERY === 0) this.fire = nextFrame(modScale(this.inp.modulation));
      this.draw();
    };
    this.raf = requestAnimationFrame(loop);
  }

  stop(): void {
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = 0;
  }

  private px(x: number, y: number, color: string, alpha: number, w = 1, h = 1): void {
    const ctx = this.ctx;
    ctx.globalAlpha = Math.max(0, Math.min(1, alpha));
    ctx.fillStyle = color;
    ctx.fillRect(Math.round(x), Math.round(y), w, h);
  }

  private chevron(x: number, y: number, dx: number, dy: number, color: string, a: number): void {
    this.px(x, y, color, a);
    if (dx !== 0) { this.px(x - dx, y - 1, color, a); this.px(x - dx, y + 1, color, a); }
    else { this.px(x - 1, y - dy, color, a); this.px(x + 1, y - dy, color, a); }
  }

  // A point is "inside" the boiler when it is within the box — its water is the flame, not packets.
  private inside(x: number, y: number): boolean {
    return x >= BX0 && x <= BX1 && y >= BBY0 && y <= BBY1;
  }

  // Inside the radiator the serpentine's fins are drawn by drawRadiator, so the baseline pipe and
  // chevrons are suppressed there — but the PACKETS still flow through, snaking over the fins.
  private inRadiator(x: number, y: number): boolean {
    return x >= RX0 && x <= RX1 && y >= RRY0 && y <= RRY1;
  }

  private drawLoop(inten: number): void {
    this.drawRadiator(inten);
    for (let d = 0; d < this.path.total; d++) {
      const p = locate(this.path, d);
      if (!this.inside(p.x, p.y) && !this.inRadiator(p.x, p.y)) this.px(p.x, p.y, PIPE_DIM, 0.55 * inten);
    }
    for (let d = CHEV_GAP / 2; d < this.path.total; d += CHEV_GAP) {
      const p = locate(this.path, d);
      if (!this.inside(p.x, p.y) && !this.inRadiator(p.x, p.y)) {
        this.chevron(p.x, p.y, p.dx, p.dy, warmAt(this.path, d) ? HOT : COOL, 0.55 * inten);
      }
    }
    for (const b of packetDists(this.phase, N_PACKETS, this.path.total)) {
      const warm = warmAt(this.path, b);
      for (let k = 0; k < TAIL; k++) {
        const p = locate(this.path, b - k);
        if (this.inside(p.x, p.y) || this.inRadiator(p.x, p.y)) continue;
        this.px(p.x, p.y, warm ? HOT : COOL, (1 - k / TAIL) * inten, k === 0 ? 2 : 1, k === 0 ? 2 : 1);
      }
    }
  }

  // A cast-column radiator (the owner's reference): four vertical steel sections with a bright
  // highlight stripe and a dark right edge, joined by top and bottom manifolds, with a thermostatic
  // valve at the return corner. The sections warm toward orange with the flow temperature and give
  // off a heat shimmer when hot. The circulation is shown in the FLOW/RETURN pipes outside; the raw
  // path inside the box is suppressed by drawLoop, so the radiator reads as one solid object.
  private drawRadiator(inten: number): void {
    const glow = tempGlow(this.inp.flowTemp);
    const warm = 0.25 + 0.75 * glow;                 // how hot the sections read
    const h = RRY1 - RRY0 - 6;                        // section height between the two manifolds
    this.px(RX0 + 1, RRY0, RAD_LINE, 0.9 * inten, RX1 - RX0 - 2, 3);      // top manifold
    this.px(RX0 + 1, RRY1 - 3, RAD_LINE, 0.9 * inten, RX1 - RX0 - 2, 3);  // bottom manifold
    for (const cx of RAD_COLS) {
      this.px(cx, RRY0 + 3, RAD_BODY, 0.9 * inten, RAD_CW, h);            // steel body
      this.px(cx, RRY0 + 3, RAD_HI, 0.7 * inten, 1, h);                   // left highlight
      this.px(cx + RAD_CW - 1, RRY0 + 3, RAD_LINE, 0.8 * inten, 1, h);    // dark right edge
      this.px(cx + 1, RRY0 + 3, HOT, 0.4 * warm * inten, RAD_CW - 2, h);  // heat tint
    }
    this.px(RX1 - 4, RRY1 - 6, HOT, 0.9 * inten, 3, 4);                   // thermostatic valve
    if (glow > 0.4) {
      for (let i = 0; i < 3; i++) {
        const sy = RRY0 - 2 - ((Math.floor(this.phase / 2) + i * 3) % 8);
        this.px(RX0 + 6 + i * 8, sy, HOT, 0.3 * glow * inten);
      }
    }
  }

  // A heat-exchanger U-bend inside the boiler: cool water rises on the left tap, is heated crossing
  // the top (right over the flame), and descends hot to the right tap — the pipe the flame heats.
  // Both taps line up with the external pipes at the bottom edge, so the circuit reads as continuous
  // through the boiler wall. `hotA` fades the hot half in/out with demand.
  private uBend(lx: number, rx: number, top: number, hotA: number): void {
    const bot = BBY1 - 1;
    this.px(lx, top, COOL, 0.5, 1, bot - top);   // cool riser: mains-in, or the CH return
    this.px(lx, top, HOT, hotA, rx - lx + 1, 1); // heated crossbar over the burner
    this.px(rx, top, HOT, hotA, 1, bot - top);   // hot riser: to the shower, or the CH flow
  }

  // The boiler is an 80s-arcade "furnace window": a neon magenta tube framed by cyan HUD corner
  // brackets, a bloom halo, CRT scanlines behind the glass, a header plinth under the ◈ BOILER plate
  // with two status LEDs, and a warm floor glow that seats the flame. The structural frame stays
  // full-strength so the boiler is always legible; only its glow, LEDs and floor sleep in standby
  // (via `inten`) and breathe with `phase` — static under reduced-motion, where phase never advances.
  // Drawn BEFORE drawFire so all of this interior detail sits behind the flame, never over it.
  private drawBoiler(inten: number): void {
    const bw = BX1 - BX0, bh = BBY1 - BBY0;
    const lit = this.inp.flame;
    const pulse = 0.5 + 0.5 * Math.sin(this.phase * 0.15); // a slow neon breath from the loop phase

    // 1) Bloom halo: two dim magenta rings just outside the box, brighter when lit and breathing.
    const halo = (0.10 + 0.14 * inten) * (0.6 + 0.4 * pulse);
    for (let r = 1; r <= 2; r++) {
      const a = halo / r;
      this.px(BX0 - r, BBY0 - r, BOILER_LINE, a, bw + 2 * r, 1);
      this.px(BX0 - r, BBY1 - 1 + r, BOILER_LINE, a, bw + 2 * r, 1);
      this.px(BX0 - r, BBY0 - r, BOILER_LINE, a, 1, bh + 2 * r);
      this.px(BX1 - 1 + r, BBY0 - r, BOILER_LINE, a, 1, bh + 2 * r);
    }

    // 2) The window: dark glass, faint CRT scanlines, and a warm floor glow rising under the flame.
    this.px(BX0, BBY0, BOILER_BG, 1, bw, bh);
    for (let y = BBY0 + 4; y < BBY1 - 2; y += 3) this.px(BX0 + 2, y, BOILER_SCAN, 0.22, bw - 4, 1);
    for (let i = 0; i < 6; i++) {
      this.px(BX0 + 2, BBY1 - 4 - i, OUTER, (lit ? 0.5 : 0.12) * (1 - i / 6) * inten, bw - 4, 1);
    }

    // 2b) The internal plumbing: the mains-in tap joined to the shower tap (the DHW exchanger) and
    //     the CH return joined to the CH flow in the middle (the CH exchanger). Both brighten and
    //     breathe with their own demand; drawn here, so the fire (next) covers what sits in it.
    this.uBend(IN_UP_X, SH_OUT, DHW_TOP, this.inp.dhwActive ? 0.5 + 0.35 * pulse : 0.5);
    this.uBend(XCH_L, XCH_R, CH_TOP, this.inp.chActive ? 0.5 + 0.35 * pulse : 0.5);

    // 3) Header plinth for the ◈ BOILER plate, a divider, and two status LEDs: cyan power (steady),
    //    magenta burner (lights and breathes only while the flame is on).
    this.px(BX0 + 2, BBY0 + 2, BOILER_LINE, 0.16 * inten, bw - 4, 5);
    this.px(BX0 + 2, BBY0 + 7, BOILER_LINE, 0.5, bw - 4, 1);
    this.px(BX1 - 6, BBY0 + 4, COOL, 0.9);
    this.px(BX1 - 4, BBY0 + 4, BOILER_LINE, lit ? 0.4 + 0.6 * pulse : 0.15);

    // 4) Neon frame: a 2px magenta tube with a 1px cyan reflection inset, capped by cyan HUD corner
    //    brackets. The magenta/cyan pairing is the whole synthwave look.
    this.px(BX0, BBY0, BOILER_LINE, 0.9, bw, 2);
    this.px(BX0, BBY1 - 2, BOILER_LINE, 0.9, bw, 2);
    this.px(BX0, BBY0, BOILER_LINE, 0.9, 2, bh);
    this.px(BX1 - 2, BBY0, BOILER_LINE, 0.9, 2, bh);
    this.px(BX0 + 2, BBY0 + 2, COOL, 0.22, bw - 4, 1);
    this.px(BX0 + 2, BBY0 + 2, COOL, 0.22, 1, bh - 4);
    const arm = 4; // cyan L-brackets at the four corners — the HUD-panel signature
    for (const [cx, sx] of [[BX0, 1], [BX1 - 1, -1]] as const) {
      for (const [cy, sy] of [[BBY0, 1], [BBY1 - 1, -1]] as const) {
        this.px(sx < 0 ? cx - arm + 1 : cx, cy, COOL, 0.95, arm, 1);
        this.px(cx, sy < 0 ? cy - arm + 1 : cy, COOL, 0.95, 1, arm);
      }
    }
  }

  private bar(modelX: number, base: number, top: number, color: string): void {
    const x = FIRE_X0 + (modelX - 4) * FIRE_CW;
    const y = FIRE_FLOOR - Math.round(top * FIRE_CH);
    const h = Math.round((top - base) * FIRE_CH);
    if (h > 0) this.px(x, y, color, 1, FIRE_CW, h);
  }

  private drawFire(): void {
    const f = this.fire;
    for (let i = 0; i < 8; i++) this.bar(4 + i, BASE[i], f.outer[i], OUTER);
    for (let m = 0; m < 6; m++) this.bar(5 + m, BASE[1 + m] + 1, f.mid[m], MID);
    for (let c = 0; c < 2; c++) this.bar(7 + c, BASE[3 + c], f.core[c], CORE);
  }

  // Cold mains water into the boiler bottom-left, with an inward arrow.
  private drawIntake(inten: number): void {
    this.px(2, IN_Y, COOL, 0.6 * inten, IN_UP_X - 2, 2);
    this.px(IN_UP_X, BBY1 - 2, COOL, 0.6 * inten, 2, IN_Y - (BBY1 - 2) + 2);
    this.chevron(8, IN_Y + 1, 1, 0, COOL, 0.75);
  }

  // The DHW fixture: a wall-hung basin in the reference's style — a rim slab, an elliptic bowl that
  // narrows to a small rounded base with a drain stub, a tall gooseneck tap and a shorter mixer arm
  // to its left, all chrome (FIXTURE) with dark outlines (RAD_LINE) and bright faces (RAD_HI). Hot
  // water rises from the boiler's DHW outlet up to the tap; while dhw_active it RUNS from the spout
  // into the bowl, steam rising. Bowl, rim and taps are always drawn; the supply pipe is drawn first
  // so the basin and tap paint over the part behind them, and only the feed below the bowl shows.
  // Water animates off the frame counter — a static stream under reduced-motion.
  private drawSink(): void {
    const mid = SINK_MID;
    const base = BOWL_TOP + BOWL_ROWS - 1;             // the bowl's bottom row
    const hwAt = (i: number) =>                        // elliptic half-width: wide mouth -> rounded base
      Math.round(BOWL_BOT_HW + (BOWL_TOP_HW - BOWL_BOT_HW) * Math.sqrt(1 - (i / (BOWL_ROWS - 1)) ** 2));

    // Hot supply: boiler DHW outlet -> down to a floor run beside the CH pipes -> up on the right to
    // the tap (drawn first, so the basin and faucet paint over the part behind them; only the feed
    // below the bowl shows). The floor run sits just above the CH FLOW pipe so the plumbing reads as
    // one bundle leaving the boiler, not a stray branch high on the wall.
    this.px(SH_OUT, BBY1, HOT, 0.5, 2, DHW_FLOOR - BBY1 + 2);
    this.px(SH_OUT, DHW_FLOOR, HOT, 0.5, FAUCET_X - SH_OUT + 2, 2);
    this.px(FAUCET_X, RIM_Y, HOT, 0.5, 2, DHW_FLOOR + 2 - RIM_Y);

    // Drain stub under the bowl: a short chrome pipe with a dark outline.
    this.px(mid - 2, base + 1, FIXTURE, 0.9, 5, 5);
    this.px(mid - 3, base + 1, RAD_LINE, 0.9, 1, 5);
    this.px(mid + 3, base + 1, RAD_LINE, 0.9, 1, 5);
    this.px(mid - 2, base + 5, RAD_LINE, 0.9, 5, 1);

    // Gooseneck tap: a tall post behind the rim, an arch over the mouth, a down-spout ending in a
    // nozzle, and a highlight down the post — the reference silhouette. Drawn before the rim so its
    // base tucks behind the slab.
    const armW = FAUCET_X - SPOUT_X + 2, postH = RIM_Y - FAUCET_TOP - 1;
    this.px(SPOUT_X - 1, FAUCET_TOP - 1, RAD_LINE, 0.9, armW + 3, 1);     // dark top edge of the arch
    this.px(SPOUT_X, FAUCET_TOP, FIXTURE, 0.95, armW, 2);                 // arch across the top
    this.px(FAUCET_X, FAUCET_TOP + 2, FIXTURE, 0.95, 2, postH);           // post (right leg)
    this.px(FAUCET_X + 2, FAUCET_TOP + 1, RAD_LINE, 0.9, 1, postH + 1);   // post dark right edge
    this.px(FAUCET_X, FAUCET_TOP + 2, RAD_HI, 0.5, 1, postH - 2);         // post highlight
    this.px(SPOUT_X, FAUCET_TOP + 2, FIXTURE, 0.95, 2, 8);                // down-spout (left leg)
    this.px(SPOUT_X - 1, FAUCET_TOP + 2, RAD_LINE, 0.9, 1, 9);            // spout dark left edge
    this.px(SPOUT_X, FAUCET_TOP + 10, FIXTURE, 0.95, 2, 1);               // nozzle tip over the bowl

    // A shorter mixer arm to the left (the reference's second spout): a riser and a right-pointing arm.
    const ltx = 66, lty = 42;
    this.px(ltx - 1, lty - 1, RAD_LINE, 0.9, 10, 1);                      // dark top edge
    this.px(ltx, lty, FIXTURE, 0.9, 8, 2);                                // arm to the right
    this.px(ltx, lty, FIXTURE, 0.9, 2, RIM_Y - lty);                      // riser
    this.px(ltx - 1, lty, RAD_LINE, 0.9, 1, RIM_Y - lty);                // riser dark left edge
    this.px(ltx + 6, lty + 2, FIXTURE, 0.9, 2, 2);                        // spout nub

    // The bowl: an elliptic funnel. Each row is a lavender chrome span outlined dark on both sloped
    // edges; a checkerboard dither over the lower body gives it the reference's rounded volume.
    for (let i = 0; i < BOWL_ROWS; i++) {
      const hw = hwAt(i), y = BOWL_TOP + i;
      this.px(mid - hw, y, FIXTURE, 0.92, 2 * hw + 1, 1);
      this.px(mid - hw, y, RAD_LINE, 0.9);
      this.px(mid + hw, y, RAD_LINE, 0.9);
    }
    this.px(mid - BOWL_BOT_HW, base, RAD_LINE, 0.9, 2 * BOWL_BOT_HW + 1, 1); // rounded base outline
    for (let i = 4; i < BOWL_ROWS - 2; i++) {
      const hw = hwAt(i), y = BOWL_TOP + i;
      for (let x = mid - hw + 1; x < mid + hw; x++) {
        if ((x + y) % 2 === 0) this.px(x, y, RAD_HI, 0.22);
      }
    }

    // Water always sits in the mouth of the bowl (its identity); it fills deeper while dhw_active.
    const wl = this.inp.dhwActive ? 3 : 2;
    for (let i = 0; i < wl; i++) {
      const hw = hwAt(i), y = BOWL_TOP + i;
      this.px(mid - hw + 1, y, WATER, i === 0 ? 0.85 : 0.5, 2 * hw - 1, 1);
    }

    // The rim slab the bowl hangs from: a chrome bar wider than the mouth, dark-edged top and bottom,
    // a bright top face and a faint cyan halo above it (the scene's 80s neon cue). Drawn last of the
    // static parts so it covers the tap bases and the bowl/rim join.
    const rx = mid - RIM_HW, rw = 2 * RIM_HW + 1;
    this.px(rx, RIM_Y - 2, COOL, 0.14, rw, 1);                            // faint neon halo
    this.px(rx, RIM_Y - 1, RAD_LINE, 0.9, rw, 1);                         // dark top edge
    this.px(rx, RIM_Y, RAD_HI, 0.85, rw, 1);                              // bright top face
    this.px(rx, RIM_Y + 1, FIXTURE, 0.95, rw, 1);                         // chrome body
    this.px(rx, RIM_Y + 2, RAD_LINE, 0.9, rw, 1);                         // dark bottom edge

    if (!this.inp.dhwActive) return;
    // Running water streams from the spout into the bowl; a moving highlight gives the stream motion.
    for (let y = FAUCET_TOP + 11; y < BOWL_TOP + 1; y++) {
      this.px(SPOUT_X, y, WATER, (this.frames + y) % 3 === 0 ? 1 : 0.65, 2, 1);
    }
    const sp = this.frames % 3;                                           // splash at the surface
    this.px(SPOUT_X - 2 - sp, BOWL_TOP, WATER, 0.5);
    this.px(SPOUT_X + 3 + sp, BOWL_TOP, WATER, 0.5);
    for (let i = 0; i < 4; i++) {                                         // steam off the hot water
      const sy = BOWL_TOP - 6 - ((Math.floor(this.frames / 3) + i * 3) % 9);
      this.px(mid - 6 + i * 4, sy, STEAM, 0.2);
    }
  }

  private draw(): void {
    this.ctx.clearRect(0, 0, W, H);
    const inten = intensity(this.inp);
    this.drawIntake(inten);
    this.drawLoop(inten);
    this.drawBoiler(inten);
    if (this.inp.flame) this.drawFire();
    this.drawSink();
    this.ctx.globalAlpha = 1;
  }
}
