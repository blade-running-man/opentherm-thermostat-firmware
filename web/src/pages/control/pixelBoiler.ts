// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The whole heating system on ONE pixel canvas, drawn after the real installation: a wall-hung
// condensing boiler with its front cover off (white case under a top hood, the flue rising out of
// it, inside the black air tube, the copper coil, the aluminium heat exchanger with the fan on it
// and the gas valve under it, and the control tray at the bottom), a cast-iron column radiator,
// and a wall-hung basin — joined by steel pipes with swept elbows, couplings and brass union nuts
// (pixelPlumbing.ts), every one of which meets the boiler at its BOTTOM:
//
//   GAS    the yellow gas line in from the left, through its ball valve, to the burner;
//   COLD   mains water in from the left into the boiler — it is a combi, so it heats tap water too;
//   FLOW   hot water out, along the floor and up into the radiator's top-left plug;
//   RETURN cooled water out of the radiator's bottom-left plug, back along the floor;
//   DHW    hot tap water out, along the floor and up behind the basin to its tap.
//
// The radiator is connected one-sided, both pipes on its left, as cast-iron radiators commonly are:
// that frees its right side, so it stands at the scene's right edge, the boiler at the left and the
// basin between them. No two pipes cross: the floor runs are stacked DHW · FLOW · RETURN from the
// top, each riser turns off its run where the runs below it have not reached yet, and the return
// riser stands between the flow riser and the radiator; on the left the gas line comes
// in above the cold one, so each drops to the boiler beside the other. The burner shows through a sight
// glass in the heat exchanger; the water shows in the pipes as a tinted glow and moving packets.
//
// Reuses the pure maths unchanged — the flame's column heights (pixelFire.ts), the loop's speed,
// brightness and glow (pixelFlow.ts) and the pipe geometry (pixelPipe.ts), each host-tested.
// prefers-reduced-motion draws one static frame; start()/stop() own the rAF loop. Colours are
// FIXED: the scene sits on the dark hero panel in both themes.

import { BASE, CORE, MID, OUTER, modScale, nextFrame, type FireFrame } from "./pixelFire";
import { intensity, speed, tempGlow, type LoopInputs } from "./pixelFlow";
import { pipeRun } from "./pixelPipe";
import { drawPackets, drawPipe, pipe, tone, BRASS_TONES, GAS_TONES, P_OUT, type Pipe, type Px } from "./pixelPlumbing";

export const W = 192, H = 146;

// The boiler: the case (with its white side walls), the hood over it, the tray under it.
const CX0 = 4, CX1 = 75, CY0 = 28, CY1 = 88;       // case, inclusive
const HOOD_Y = 21;                                  // the hood slab spans HOOD_Y .. CY0 - 1
const TRAY_Y0 = 89, TRAY_Y1 = 101;                  // the dark control tray; pipes leave its bottom
const COIL_X = 8, COIL_Y = 32, COIL_N = 6, COIL_PITCH = 8;   // the copper coil, left of the air tube
const TUBE_X0 = 19, TUBE_X1 = 28;                   // the black air/flue tube, and the white flue above it
const HX_X0 = 30, HX_X1 = 69, HX_Y0 = 30, HX_Y1 = 85;        // the aluminium heat exchanger
const FAN_X = 59, FAN_Y = 49, FAN_R = 10;           // the fan's volute on the exchanger
const WIN_X0 = 33, WIN_X1 = 52, WIN_Y0 = 55, WIN_Y1 = 84;    // the sight glass over the burner
const VALVE_X0 = 56, VALVE_X1 = 68, VALVE_Y0 = 67;  // the gas valve, down to the exchanger's foot
const FIRE_FLOOR = 82, FIRE_X0 = 35, FIRE_CW = 2, FIRE_CH = 2;

// The five boiler connections, evenly pitched along the tray.
const GAS_X = 12, COLD_X = 24, RET_X = 36, FLOW_X = 48, DHW_X = 60;
// The floor runs, top to bottom, and the gas and cold intakes' heights at the left edge.
const DHW_Y = 110, FLOW_Y = 122, RET_Y = 134, GAS_Y = 123, COLD_Y = 133;
const GAS_TAP_Y = 112;                              // the gas ball valve, just under the tray

// The cast-iron radiator: N sections, each a head at the top and the bottom joined by a column.
const RX0 = 136, R_SECT = 6, R_PITCH = 9, RY0 = 56, RY1 = 102, HEAD_H = 7;
const RX1 = RX0 + R_SECT * R_PITCH - 1;
const HUB_TOP = RY0 + 3, HUB_BOT = RY1 - 3;          // the plugs' centres, where the pipes screw in
const FLOW_RISER = 122, RET_RISER = 130;

// The basin.
const SINK_MID = 100;
const RIM_Y = 62, RIM_HW = 16;
const BOWL_TOP = 65, BOWL_ROWS = 13;
const BOWL_TOP_HW = 14, BOWL_BOT_HW = 3;
const FAUCET_X = 106, SPOUT_X = 97, FAUCET_TOP = 47;
const MIXER_X = 86, MIXER_Y = 54;
const DHW_RISER = 112;

const ELBOW_R = 5;
const PPF_MAX = 1.6, SCENE_FPS = 30, FIRE_EVERY = 4;
const LOOP_PACKETS = 3, DHW_PACKETS = 2;

const HOT = "#ff8a3d", COOL = "#00f0ff", GAS = "#fff6b0";
const CASE = ["#f4f6f8", "#e2e6ea", "#c9ced6", "#a9afba"];   // white enamel, lit -> shaded
const BACK = "#14151a", BACK_HI = "#1d1f26";
const TRAY = "#26282e", TRAY_HI = "#3a3d45", PANEL = "#3d5873", PANEL_HI = "#5b7896", LCD = "#0c1a1c";
const LED_ON = "#05ffa1", LED_OFF = "#1b3a30", SEG = "#ff3b30", SEG_OFF = "#3a1414";
const TUBE = ["#5a5d64", "#3c3f45", "#2a2c31", "#1c1d21", "#121316"];
const ALU = ["#e6e9ee", "#cdd2d9", "#b3b9c3", "#959ca8", "#787f8c"];
const COPPER = ["#ffd1ae", "#ec9a68", "#c8743f", "#9a5228", "#6b3519"];
const MOTOR = "#1a1b1f", MOTOR_HI = "#3a3c42", MOTOR_DOT = "#3aa0ff";
const IRON = ["#f0f1ed", "#dcded8", "#c5c8c1", "#abaea7", "#8f928b", "#6e716b"];   // painted cast iron
const FIXTURE = "#9aa4bf", FIX_HI = "#c2cde4", FIX_LINE = "#2a3550";
const WATER = "#6fe0ff", STEAM = "#bfe9ff";

export type BoilerInputs = LoopInputs;

/** Drives the whole heating-system scene on one canvas. setInputs() feeds it live state. */
export class PixelBoiler {
  private ctx: CanvasRenderingContext2D;
  private inp: BoilerInputs = {
    chActive: false, dhwActive: false, flame: false,
    flowTemp: null, returnTemp: null, modulation: null,
  };
  private loopPhase = 0;
  private dhwPhase = 0;
  private gasPhase = 0;
  private frames = 0;
  private fire: FireFrame = nextFrame(0.7, () => 0.5);
  private raf = 0;
  private prev = 0;
  private reduced: boolean;
  private readonly px: Px;
  private readonly gas: Pipe;
  private readonly cold: Pipe;
  private readonly flow: Pipe;
  private readonly ret: Pipe;
  private readonly dhw: Pipe;

  constructor(canvas: HTMLCanvasElement) {
    canvas.width = W;
    canvas.height = H;
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("pixelBoiler: no 2d context");
    this.ctx = ctx;
    this.px = (x, y, color, alpha, w = 1, h = 1) => {
      ctx.globalAlpha = Math.max(0, Math.min(1, alpha));
      ctx.fillStyle = color;
      ctx.fillRect(Math.round(x), Math.round(y), w, h);
    };
    // Each run in the direction its water travels. A run starts or ends on the tray's bottom edge,
    // where its union nut screws onto the boiler; the cold intake comes in past the canvas edge.
    this.gas = pipe(pipeRun([[-3, GAS_Y], [GAS_X, GAS_Y], [GAS_X, TRAY_Y1 + 1]], ELBOW_R),
      { nutEnd: true, tones: GAS_TONES });
    this.cold = pipe(pipeRun([[-3, COLD_Y], [COLD_X, COLD_Y], [COLD_X, TRAY_Y1 + 1]], ELBOW_R), { nutEnd: true });
    this.flow = pipe(pipeRun([
      [FLOW_X, TRAY_Y1 + 1], [FLOW_X, FLOW_Y], [FLOW_RISER, FLOW_Y], [FLOW_RISER, HUB_TOP], [RX0 - 1, HUB_TOP],
    ], ELBOW_R), { nutStart: true, nutEnd: true, ringPitch: 26, ringFrom: 30 });
    this.ret = pipe(pipeRun([
      [RX0 - 1, HUB_BOT], [RET_RISER, HUB_BOT], [RET_RISER, RET_Y], [RET_X, RET_Y], [RET_X, TRAY_Y1 + 1],
    ], ELBOW_R), { nutStart: true, nutEnd: true, ringPitch: 26, ringFrom: 40 });
    this.dhw = pipe(pipeRun([
      [DHW_X, TRAY_Y1 + 1], [DHW_X, DHW_Y], [DHW_RISER, DHW_Y], [DHW_RISER, RIM_Y + 1],
    ], ELBOW_R), { nutStart: true, ringPitch: 24, ringFrom: 22 });
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
      this.loopPhase += PPF_MAX * speed(this.inp);
      // Tap water moves only while it is drawn: a closed tap is a standing column, not a slow one.
      if (this.inp.dhwActive) this.dhwPhase += PPF_MAX * 0.8;
      // Gas moves only while it burns.
      if (this.inp.flame) this.gasPhase += PPF_MAX * 0.6;
      if (this.frames++ % FIRE_EVERY === 0) this.fire = nextFrame(modScale(this.inp.modulation));
      this.draw();
    };
    this.raf = requestAnimationFrame(loop);
  }

  stop(): void {
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = 0;
  }

  // ---- the boiler ---------------------------------------------------------------------------

  // The case seen with its front cover off: white side walls lit from the left round a dark back
  // panel, a hood slab overhanging the top, and the white concentric flue rising out of the hood.
  private drawCase(): void {
    const px = this.px;
    // The flue: a white pipe off the top of the canvas with a collar where it meets the hood.
    const fw = TUBE_X1 - TUBE_X0 + 3;
    px(TUBE_X0 - 1, 0, P_OUT, 1, fw + 2, HOOD_Y);
    px(TUBE_X0, 0, CASE[1], 1, fw, HOOD_Y);
    px(TUBE_X0, 0, CASE[0], 1, 3, HOOD_Y);
    px(TUBE_X0 + fw - 3, 0, CASE[3], 1, 3, HOOD_Y);
    px(TUBE_X0 - 2, HOOD_Y - 5, P_OUT, 1, fw + 4, 4);
    px(TUBE_X0 - 1, HOOD_Y - 4, CASE[0], 1, fw + 2, 1);
    px(TUBE_X0 - 1, HOOD_Y - 3, CASE[2], 1, fw + 2, 1);

    // The back panel, then the two white side walls standing proud of it.
    px(CX0, CY0, BACK, 1, CX1 - CX0 + 1, CY1 - CY0 + 1);
    px(CX0 + 3, CY0, BACK_HI, 1, CX1 - CX0 - 5, 2);
    for (const [x, tones] of [[CX0, [CASE[0], CASE[1], CASE[2]]], [CX1 - 2, [CASE[2], CASE[3], CASE[3]]]] as const) {
      px(x - 1, CY0, P_OUT, 1, 5, CY1 - CY0 + 1);
      for (let i = 0; i < 3; i++) px(x + i, CY0, tones[i], 1, 1, CY1 - CY0 + 1);
    }

    // The hood: a slab two pixels wider than the case, a bright top face and a shaded lip.
    const hx = CX0 - 2, hw = CX1 - CX0 + 5;
    px(hx - 1, HOOD_Y - 1, P_OUT, 1, hw + 2, CY0 - HOOD_Y + 2);
    px(hx, HOOD_Y, CASE[0], 1, hw, 2);
    px(hx, HOOD_Y + 2, CASE[1], 1, hw, CY0 - HOOD_Y - 3);
    px(hx, CY0 - 1, CASE[3], 1, hw, 1);
  }

  // The copper coil left of the air tube: a stack of U-bends lying on their side, each a pair of
  // copper tubes coming out from behind the air tube and swept round on the left — the C shapes
  // in the reference. It warms toward orange while the burner heats the water passing through it.
  private drawCoil(heat: number): void {
    const px = this.px, x = COIL_X, w = TUBE_X0 - COIL_X;
    for (let i = 0; i < COIL_N; i++) {
      const y = COIL_Y + i * COIL_PITCH;
      px(x + 1, y, P_OUT, 1, w - 1, 7);                       // the outline, its left corners cut
      px(x, y + 1, P_OUT, 1, 3, 5);
      for (const [ty, a, b] of [[y + 1, COPPER[0], COPPER[2]], [y + 4, COPPER[1], COPPER[3]]] as const) {
        px(x + 2, ty, a, 1, w - 2, 1);                        // each tube: a lit row over a shaded one
        px(x + 2, ty + 1, b, 1, w - 2, 1);
      }
      px(x + 1, y + 1, COPPER[1], 1, 1, 5);                   // the swept bend joining the two
      px(x + 2, y + 2, COPPER[2], 1, 1, 3);
      px(x + 3, y + 3, P_OUT, 1, w - 3, 1);                   // the gap between the tubes
      px(x + 1, y + 1, HOT, 0.3 * heat, w - 1, 5);
    }
    // The coil's two legs, running down past the tube's collar into the tray.
    const legY = COIL_Y + COIL_N * COIL_PITCH;
    for (const x of [COIL_X + 2, COIL_X + 6]) {
      px(x - 1, legY - 1, P_OUT, 1, 4, TRAY_Y0 - legY + 1);
      px(x, legY - 1, COPPER[2], 1, 2, TRAY_Y0 - legY + 1);
      px(x, legY - 1, COPPER[1], 1, 1, TRAY_Y0 - legY + 1);
    }
  }

  // The black air tube down the left of the exchanger, lit from the left, with its ribbed collar
  // where it seats on the exchanger's foot.
  private drawTube(): void {
    const px = this.px, w = TUBE_X1 - TUBE_X0 + 1, top = CY0, bot = HX_Y1 - 4;
    px(TUBE_X0 - 1, top, P_OUT, 1, w + 2, bot - top);
    for (let i = 0; i < w; i++) px(TUBE_X0 + i, top, tone(TUBE, 1 - (2 * i) / (w - 1)), 1, 1, bot - top);
    px(TUBE_X0, top, TUBE[0], 1, w, 1);
    for (const y of [bot - 1, bot + 4]) {
      px(TUBE_X0 - 3, y - 1, P_OUT, 1, w + 6, 5);
      px(TUBE_X0 - 2, y, TUBE[1], 1, w + 4, 3);
      px(TUBE_X0 - 2, y, TUBE[0], 1, w + 4, 1);
    }
  }

  // The aluminium heat exchanger: a cast body lit from the upper left with ribs fanning out from
  // the burner, an air box bolted across its top, and the bolts round its rim.
  private drawExchanger(): void {
    const px = this.px, w = HX_X1 - HX_X0 + 1, h = HX_Y1 - HX_Y0 + 1;
    px(HX_X0 - 1, HX_Y0 - 1, P_OUT, 1, w + 2, h + 2);
    for (let i = 0; i < w; i++) px(HX_X0 + i, HX_Y0, tone(ALU, 0.6 - (1.4 * i) / w), 1, 1, h);
    px(HX_X0, HX_Y0, ALU[0], 1, w, 1);
    px(HX_X0, HX_Y0, ALU[0], 1, 1, h);
    // The ribs: a few cast lines radiating from the burner, under the window and the fan.
    const cx = (WIN_X0 + WIN_X1) / 2, cy = WIN_Y0 + 6;
    for (const [dx, dy] of [[-1, -1.3], [0.2, -1], [1, -0.6], [1, 0.4], [1, 1]] as const) {
      for (let t = 4; t < 40; t++) {
        const x = cx + dx * t, y = cy + dy * t;
        if (x <= HX_X0 + 1 || x >= HX_X1 - 1 || y <= HX_Y0 + 9 || y >= HX_Y1 - 1) break;
        px(x, y, ALU[4], 0.55);
        px(x - 1, y, ALU[0], 0.35);
      }
    }
    // The air box across the top, and the rim's bolts.
    px(HX_X0 + 3, HX_Y0 + 1, P_OUT, 1, w - 6, 8);
    px(HX_X0 + 4, HX_Y0 + 2, ALU[1], 1, w - 8, 6);
    px(HX_X0 + 4, HX_Y0 + 2, ALU[0], 1, w - 8, 1);
    px(HX_X0 + 4, HX_Y0 + 7, ALU[3], 1, w - 8, 1);
    for (const [x, y] of [[HX_X0 + 2, HX_Y0 + 12], [HX_X0 + 2, HX_Y1 - 3], [HX_X1 - 2, HX_Y1 - 22], [HX_X0 + 6, HX_Y0 + 4], [HX_X1 - 6, HX_Y0 + 4]]) {
      px(x - 1, y - 1, P_OUT, 0.8, 3, 3);
      px(x, y, ALU[0], 1);
    }
  }

  // The fan on the exchanger: a pale spiral volute round a black motor. Its three blue clips turn
  // while the burner is lit — the fan runs whenever there is a flame to feed.
  private drawFan(): void {
    const px = this.px;
    for (let y = -FAN_R - 1; y <= FAN_R + 1; y++) {
      for (let x = -FAN_R - 1; x <= FAN_R + 1; x++) {
        const d = Math.hypot(x, y);
        if (d > FAN_R + 0.5) continue;
        if (d > FAN_R - 0.6) { px(FAN_X + x, FAN_Y + y, P_OUT, 1); continue; }
        if (d > 5.5) { px(FAN_X + x, FAN_Y + y, tone(ALU, -(x + y) / FAN_R), 1); continue; }
        px(FAN_X + x, FAN_Y + y, d > 4.6 ? P_OUT : (x + y < -2 ? MOTOR_HI : MOTOR), 1);
      }
    }
    // The volute's outlet sweeping up into the air box.
    px(FAN_X - FAN_R + 1, HX_Y0 + 8, P_OUT, 1, 6, FAN_Y - HX_Y0 - 8);
    px(FAN_X - FAN_R + 2, HX_Y0 + 8, ALU[1], 1, 4, FAN_Y - HX_Y0 - 9);
    px(FAN_X - FAN_R + 2, HX_Y0 + 8, ALU[0], 1, 1, FAN_Y - HX_Y0 - 9);
    const a0 = this.inp.flame ? this.frames * 0.35 : 0.4;
    for (let i = 0; i < 3; i++) {
      const a = a0 + (i * 2 * Math.PI) / 3;
      px(FAN_X + Math.cos(a) * 3.2, FAN_Y + Math.sin(a) * 3.2, MOTOR_DOT, 1);
    }
    px(FAN_X, FAN_Y, MOTOR_HI, 1);
  }

  // The gas valve under the fan: a grey block with its black coil box, and the copper gas line
  // rising from it to the fan's venturi, a brass nut at each end.
  private drawValve(): void {
    const px = this.px, w = VALVE_X1 - VALVE_X0 + 1, h = HX_Y1 + 3 - VALVE_Y0;
    const lx = VALVE_X0 + 4, ly = FAN_Y + FAN_R;
    px(lx - 1, ly, P_OUT, 1, 4, VALVE_Y0 - ly);
    px(lx, ly, COPPER[2], 1, 2, VALVE_Y0 - ly);
    px(lx, ly, COPPER[0], 1, 1, VALVE_Y0 - ly);
    for (const y of [ly + 1, VALVE_Y0 - 3]) {
      px(lx - 2, y - 1, P_OUT, 1, 6, 4);
      px(lx - 1, y, BRASS_TONES[1], 1, 4, 2);
      px(lx - 1, y, BRASS_TONES[0], 1, 4, 1);
    }
    px(VALVE_X0 - 1, VALVE_Y0 - 1, P_OUT, 1, w + 2, h + 2);
    px(VALVE_X0, VALVE_Y0, ALU[2], 1, w, h);
    px(VALVE_X0, VALVE_Y0, ALU[1], 1, w, 2);
    px(VALVE_X0, VALVE_Y0, ALU[1], 1, 2, h);
    for (let y = VALVE_Y0 + 4; y < VALVE_Y0 + h - 2; y += 3) px(VALVE_X0 + 2, y, ALU[4], 0.7, w - 7, 1);
    px(VALVE_X1 - 3, VALVE_Y0 + 2, MOTOR, 1, 5, 8);           // the coil box
    px(VALVE_X1 - 3, VALVE_Y0 + 2, MOTOR_HI, 1, 1, 8);
  }

  // The sight glass over the burner: a dark round-cornered window in a pale rim. The fire burns in
  // it (drawFire, next); with the burner off the glass shows only a faint warm floor.
  private drawWindow(): void {
    const px = this.px, w = WIN_X1 - WIN_X0 + 1, h = WIN_Y1 - WIN_Y0 + 1;
    px(WIN_X0, WIN_Y0 - 1, P_OUT, 1, w, h + 2);
    px(WIN_X0 - 1, WIN_Y0, P_OUT, 1, w + 2, h);
    px(WIN_X0 + 1, WIN_Y0, "#07080b", 1, w - 2, h);
    px(WIN_X0, WIN_Y0 + 1, "#07080b", 1, w, h - 2);
    px(WIN_X0 + 1, WIN_Y0 + 1, "#23262e", 1, w - 2, 1);
    const lit = this.inp.flame;
    for (let i = 0; i < 5; i++) px(WIN_X0 + 1, WIN_Y1 - 2 - i, OUTER, (lit ? 0.45 : 0.1) * (1 - i / 5), w - 2, 1);
    px(WIN_X0 + 2, WIN_Y1 - 1, "#4a3b33", 1, w - 4, 1);       // the burner deck
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
    this.px(WIN_X0 + 1, WIN_Y0 + 1, "#ffffff", 0.18, 2, 6);  // a glint on the glass, over the flame
  }

  // The control tray under the case: dark plastic with the blue-grey control panel — its display,
  // the power LED, the burner LED (lit while there is a flame) and a three-cell red meter of the
  // modulation.
  private drawTray(): void {
    const px = this.px, x0 = CX0 - 1, w = CX1 - CX0 + 3, h = TRAY_Y1 - TRAY_Y0 + 1;
    px(x0 - 1, TRAY_Y0 - 1, P_OUT, 1, w + 2, h + 2);
    px(x0, TRAY_Y0, TRAY, 1, w, h);
    px(x0, TRAY_Y0, TRAY_HI, 1, w, 2);
    const p0 = 16, pw = 46, py = TRAY_Y0 + 4;
    px(p0 - 1, py - 1, P_OUT, 1, pw + 2, 7);
    px(p0, py, PANEL, 1, pw, 5);
    px(p0, py, PANEL_HI, 1, pw, 1);
    px(p0 + 3, py + 1, LCD, 1, 9, 3);
    px(p0 + 4, py + 2, LED_ON, 0.5, 2, 1);
    const pulse = 0.5 + 0.5 * Math.sin(this.loopPhase * 0.2);
    px(p0 + 16, py + 2, LED_ON, 0.9);
    px(p0 + 19, py + 2, this.inp.flame ? LED_ON : LED_OFF, this.inp.flame ? 0.5 + 0.5 * pulse : 1);
    const m = this.inp.flame ? Math.max(1, Math.ceil(((this.inp.modulation ?? 50) / 100) * 3)) : 0;
    for (let i = 0; i < 3; i++) px(p0 + 26 + i * 3, py + 1, i < m ? SEG : SEG_OFF, 1, 2, 3);
    for (let i = 0; i < 4; i++) px(p0 + 37 + i * 2, py + 2, PANEL_HI, 0.8);
  }

  private drawBoiler(): void {
    const glow = tempGlow(this.inp.flowTemp);
    this.drawCase();
    this.drawCoil(this.inp.flame ? glow : 0);
    this.drawExchanger();
    this.drawTube();
    this.drawFan();
    this.drawValve();
    this.drawWindow();
    if (this.inp.flame) this.drawFire();
    this.drawTray();
  }

  // ---- the radiator -------------------------------------------------------------------------

  // A cast-iron column radiator, the owner's reference: painted sections, each a rounded head top
  // and bottom joined by a column, the heads butting into one another along the hubs, with the
  // open windows between the columns and a round plug at each end of both hubs. The paint warms
  // toward orange with the water in it — the flow's heat at the top, the return's at the bottom —
  // and gives off a shimmer when hot.
  private drawRadiator(inten: number): void {
    const px = this.px;
    const top = tempGlow(this.inp.flowTemp), bot = tempGlow(this.inp.returnTemp ?? this.inp.flowTemp);
    const heat = (y: number) => (0.05 + 0.35 * (top + (bot - top) * ((y - RY0) / (RY1 - RY0)))) * inten;
    const colTop = RY0 + HEAD_H, colH = RY1 - RY0 - 2 * HEAD_H + 1;
    for (let s = 0; s < R_SECT; s++) {
      const x = RX0 + s * R_PITCH;
      // The column: six pixels across, toned from the lit left to the shaded right.
      px(x, colTop, P_OUT, 1, 8, colH);
      for (let i = 0; i < 6; i++) px(x + 1 + i, colTop, tone(IRON, 0.9 - (2 * i) / 5), 1, 1, colH);
      // The two heads: rounded blocks the full pitch wide, lit on top, shaded underneath.
      for (const y of [RY0, RY1 - HEAD_H + 1]) {
        px(x + 1, y, P_OUT, 1, R_PITCH - 1, HEAD_H);
        px(x, y + 1, P_OUT, 1, R_PITCH + 1, HEAD_H - 2);
        px(x + 1, y + 1, IRON[2], 1, R_PITCH - 1, HEAD_H - 2);
        px(x + 1, y + 1, IRON[0], 1, R_PITCH - 2, 1);
        px(x + 1, y + 2, IRON[1], 1, 2, HEAD_H - 4);
        px(x + 1, y + HEAD_H - 2, IRON[4], 1, R_PITCH - 1, 1);
        if (s > 0) px(x, y + 2, IRON[5], 0.8, 1, HEAD_H - 4);    // the seam where two heads meet
      }
      // The heat tint, row by row: the heads' full width, the column's six pixels.
      for (let y = RY0 + 1; y < RY1; y++) {
        const head = y < colTop || y >= colTop + colH;
        px(x + 1, y, HOT, heat(y), head ? R_PITCH - 1 : 6, 1);
      }
    }
    // The plugs: a round boss at each end of both hubs.
    for (const [x, y] of [[RX0 - 2, HUB_TOP], [RX1 + 1, HUB_TOP], [RX0 - 2, HUB_BOT], [RX1 + 1, HUB_BOT]]) {
      px(x - 1, y - 3, P_OUT, 1, 4, 7);
      px(x, y - 2, IRON[1], 1, 2, 5);
      px(x, y - 2, IRON[0], 1, 1, 2);
    }
    if (top > 0.4) {
      for (let i = 0; i < 4; i++) {
        const sy = RY0 - 3 - ((Math.floor(this.loopPhase / 2) + i * 3) % 9);
        px(RX0 + 6 + i * 13, sy, HOT, 0.3 * top * inten);
      }
    }
  }

  // ---- the basin ----------------------------------------------------------------------------

  // The DHW fixture: a wall-hung basin — a rim slab, an elliptic bowl that narrows to a small
  // rounded base with a drain stub, a tall gooseneck tap and a shorter mixer arm to its left, all
  // chrome with dark outlines and bright faces. The DHW pipe rises behind the bowl (drawn before
  // it); while dhw_active the tap RUNS into the bowl, steam rising. Water animates off the frame
  // counter — a static stream under reduced-motion.
  private drawSink(): void {
    const px = this.px, mid = SINK_MID;
    const base = BOWL_TOP + BOWL_ROWS - 1;
    const hwAt = (i: number) =>
      Math.round(BOWL_BOT_HW + (BOWL_TOP_HW - BOWL_BOT_HW) * Math.sqrt(1 - (i / (BOWL_ROWS - 1)) ** 2));

    // Drain stub under the bowl.
    px(mid - 2, base + 1, FIXTURE, 0.9, 5, 5);
    px(mid - 3, base + 1, FIX_LINE, 0.9, 1, 5);
    px(mid + 3, base + 1, FIX_LINE, 0.9, 1, 5);
    px(mid - 2, base + 5, FIX_LINE, 0.9, 5, 1);

    // Gooseneck tap, drawn before the rim so its base tucks behind the slab.
    const armW = FAUCET_X - SPOUT_X + 2, postH = RIM_Y - FAUCET_TOP - 1;
    px(SPOUT_X - 1, FAUCET_TOP - 1, FIX_LINE, 0.9, armW + 3, 1);
    px(SPOUT_X, FAUCET_TOP, FIXTURE, 0.95, armW, 2);
    px(FAUCET_X, FAUCET_TOP + 2, FIXTURE, 0.95, 2, postH);
    px(FAUCET_X + 2, FAUCET_TOP + 1, FIX_LINE, 0.9, 1, postH + 1);
    px(FAUCET_X, FAUCET_TOP + 2, FIX_HI, 0.5, 1, postH - 2);
    px(SPOUT_X, FAUCET_TOP + 2, FIXTURE, 0.95, 2, 8);
    px(SPOUT_X - 1, FAUCET_TOP + 2, FIX_LINE, 0.9, 1, 9);
    px(SPOUT_X, FAUCET_TOP + 10, FIXTURE, 0.95, 2, 1);

    // The shorter mixer arm to the left: a riser and a right-pointing arm.
    px(MIXER_X - 1, MIXER_Y - 1, FIX_LINE, 0.9, 10, 1);
    px(MIXER_X, MIXER_Y, FIXTURE, 0.9, 8, 2);
    px(MIXER_X, MIXER_Y, FIXTURE, 0.9, 2, RIM_Y - MIXER_Y);
    px(MIXER_X - 1, MIXER_Y, FIX_LINE, 0.9, 1, RIM_Y - MIXER_Y);
    px(MIXER_X + 6, MIXER_Y + 2, FIXTURE, 0.9, 2, 2);

    // The bowl: an elliptic funnel, each row a chrome span outlined on both sloped edges, with a
    // checkerboard dither over the lower body for its rounded volume.
    for (let i = 0; i < BOWL_ROWS; i++) {
      const hw = hwAt(i), y = BOWL_TOP + i;
      px(mid - hw, y, FIXTURE, 1, 2 * hw + 1, 1);
      px(mid - hw, y, FIX_LINE, 1);
      px(mid + hw, y, FIX_LINE, 1);
    }
    px(mid - BOWL_BOT_HW, base, FIX_LINE, 1, 2 * BOWL_BOT_HW + 1, 1);
    for (let i = 4; i < BOWL_ROWS - 2; i++) {
      const hw = hwAt(i), y = BOWL_TOP + i;
      for (let x = mid - hw + 1; x < mid + hw; x++) if ((x + y) % 2 === 0) px(x, y, FIX_HI, 0.22);
    }

    // Water always sits in the mouth of the bowl; it fills deeper while dhw_active.
    const wl = this.inp.dhwActive ? 3 : 2;
    for (let i = 0; i < wl; i++) {
      const hw = hwAt(i), y = BOWL_TOP + i;
      px(mid - hw + 1, y, WATER, i === 0 ? 0.85 : 0.5, 2 * hw - 1, 1);
    }

    // The rim slab, drawn last of the static parts so it covers the tap bases and the bowl's join.
    const rx = mid - RIM_HW, rw = 2 * RIM_HW + 1;
    px(rx, RIM_Y - 1, FIX_LINE, 1, rw, 1);
    px(rx, RIM_Y, FIX_HI, 1, rw, 1);
    px(rx, RIM_Y + 1, FIXTURE, 1, rw, 1);
    px(rx, RIM_Y + 2, FIX_LINE, 1, rw, 1);

    if (!this.inp.dhwActive) return;
    for (let y = FAUCET_TOP + 11; y < BOWL_TOP + 1; y++) {
      px(SPOUT_X, y, WATER, (this.frames + y) % 3 === 0 ? 1 : 0.65, 2, 1);
    }
    const sp = this.frames % 3;
    px(SPOUT_X - 2 - sp, BOWL_TOP, WATER, 0.5);
    px(SPOUT_X + 3 + sp, BOWL_TOP, WATER, 0.5);
    for (let i = 0; i < 4; i++) {
      const sy = BOWL_TOP - 6 - ((Math.floor(this.frames / 3) + i * 3) % 9);
      px(mid - 6 + i * 4, sy, STEAM, 0.2);
    }
  }

  // The gas ball valve under the tray: a brass body on the gas line with its yellow lever turned
  // along the pipe — open, as it always is while the boiler is in service.
  private drawGasTap(): void {
    const px = this.px, x = GAS_X, y = GAS_TAP_Y;
    px(x - 4, y - 3, P_OUT, 1, 9, 7);
    px(x - 3, y - 2, BRASS_TONES[2], 1, 7, 5);
    px(x - 3, y - 2, BRASS_TONES[0], 1, 7, 1);
    px(x - 3, y - 2, BRASS_TONES[1], 1, 1, 5);
    px(x - 3, y + 2, BRASS_TONES[4], 1, 7, 1);
    px(x + 4, y - 4, P_OUT, 1, 4, 10);                      // the lever, standing proud of the body
    px(x + 5, y - 3, GAS_TONES[1], 1, 2, 8);
    px(x + 5, y - 3, GAS_TONES[0], 1, 1, 8);
  }

  // ---- the scene ----------------------------------------------------------------------------

  // Back to front: the pipes first (each screws in under its fitting, and the DHW riser passes
  // behind the basin), then the radiator, the basin and the boiler over their ends.
  private draw(): void {
    this.ctx.clearRect(0, 0, W, H);
    const loop = intensity(this.inp);
    const tap = this.inp.dhwActive ? 1 : 0.3;
    // The mains are always under pressure, so the cold line never fades as far as a closed tap's.
    const mains = this.inp.dhwActive ? 1 : 0.6, gas = this.inp.flame ? 1 : 0.2;
    drawPipe(this.px, this.ret, COOL, loop);
    drawPipe(this.px, this.flow, HOT, loop);
    drawPipe(this.px, this.dhw, HOT, tap);
    drawPipe(this.px, this.cold, COOL, mains);
    drawPipe(this.px, this.gas, GAS, gas);
    this.drawGasTap();
    drawPackets(this.px, this.ret, COOL, this.loopPhase, loop, LOOP_PACKETS);
    drawPackets(this.px, this.flow, HOT, this.loopPhase, loop, LOOP_PACKETS);
    drawPackets(this.px, this.dhw, HOT, this.dhwPhase, tap, DHW_PACKETS);
    drawPackets(this.px, this.cold, COOL, this.dhwPhase, mains, 1);
    if (this.inp.flame) drawPackets(this.px, this.gas, GAS, this.gasPhase, 0.8, 1);
    this.drawRadiator(loop);
    this.drawSink();
    this.drawBoiler();
    this.ctx.globalAlpha = 1;
  }
}
