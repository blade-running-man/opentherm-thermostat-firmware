// Build-time data for /reference/entities and the Boiler page: the entity table, taken from the
// firmware's own generated entity list rather than written out here.
//
// WHY THIS IMPORTS web/src/api/entities.ts. The project allows one entity list, ever:
// tools/opentherm_ids.py, projected by tools/generate_registry.py into the C registry
// (registry_generated.h), the Home Assistant discovery documents (ha_generated.h), the web UI's
// entities.ts and the web-only translated names. A hand-written table on this page would be a
// second list, and it would go on agreeing with itself while the firmware changed. entities.ts
// is the generator's TypeScript output, committed, and rewritten by tools/regenerate.py -- the
// PlatformIO pre-build hook -- on every firmware build, so reading it is reading the registry.
//
// WHY A PLAIN IMPORT, and not the alternatives:
//  * VitePress runs a data loader through Vite's loadConfigFromFile, which bundles the loader and
//    its relative imports with esbuild. A TypeScript module outside docs/ is therefore imported
//    as-is, with no extra tooling, and its file becomes a tracked dependency: `docs:dev` reloads
//    the table when the generator rewrites entities.ts. That is also why there is no `watch`
//    glob below -- the import already does that job.
//  * DO NOT parse entities.ts with regular expressions: the generator's line format is not a
//    contract, and a parser would break silently on the first formatting change.
//  * DO NOT run `python3 tools/generate_registry.py` from here: it would make the docs build need
//    the firmware's Python toolchain and would WRITE files into the repository during a docs
//    build. The committed output is the right input (`generate_registry.py --check` is what
//    says whether it is stale).
//
// WHY NO HOME ASSISTANT COLUMN. The discovery component is not a function of the kind alone:
// tools/render_discovery.py decides it from the row's executor command (which entities.ts does
// not carry) -- the heating season is a binary_sensor plus an off-only button, the write-only
// frame rows (IDs 14, 16, 24, 124, 126) get no document at all. A kind-to-component map here
// would be a second, wrong copy of that rule.
//
// The build fails loudly (load() throws) on an empty list, an unknown kind or a Data-ID with no
// message name in SPEC_NAMES, so a change in the generator's output shape cannot publish an empty
// or half-labelled page.
import { ENTITIES } from '../../web/src/api/entities'

export interface EntityRow {
  key: string
  name: string
  kind: string
  /** The OpenTherm Data-ID the value is read from or written to; null for a synthetic entity. */
  dataId: number | null
  /** How the 16-bit DATA-VALUE is parsed (f88, u16, s16, u8_hb, …); `none` off the wire. */
  codec: string
  unit: string | null
  deviceClass: string | null
  icon: string | null
  entityCategory: string | null
  readable: boolean
  writable: boolean
  min: number | null
  max: number | null
  options: string[] | null
}

export interface DataIdGroup {
  dataId: number
  /** The OpenTherm 2.2 name of the message (SPEC_NAMES below). */
  message: string
  /** True when any entity of this Data-ID is read from the boiler (it is in the poll ring). */
  read: boolean
  /** True when any entity of this Data-ID is written to the boiler. */
  write: boolean
  entities: EntityRow[]
}

export interface EntityData {
  total: number
  writable: number
  /** The entities of each kind, keyed by kind, in KINDS order; sensors sorted by Data-ID. */
  groups: Record<string, EntityRow[]>
  /** Every Data-ID the registry names, ascending, with the entities carried in its frame. */
  byDataId: DataIdGroup[]
  /** The entities with no Data-ID: the executor's inputs and outputs, never on the wire. */
  synthetic: EntityRow[]
  /** SPEC_NAMES, keyed by Data-ID, for pages that name an ID the registry does not carry. */
  specNames: Record<number, string>
}

// The OpenTherm 2.2 name of each message the site names, keyed by Data-ID.
//
// WHY A HAND-WRITTEN MAP, AND WHY HERE. The message name is a fact of the specification that no
// registry field carries (an entity has a display name, a Data-ID has none), so it cannot come from
// entities.ts. It is NOT a second entity list: it names messages, not entities, and nothing in the
// firmware reads it. It lives in this loader, once, so that every page that names a Data-ID (the
// Boiler page and the per-boiler pages) reads the same words instead of keeping its own copy.
//
// A Data-ID the registry carries with no name here FAILS THE BUILD (load() throws): a new row in
// tools/opentherm_ids.py must be named here before the site builds, rather than rendering a
// nameless row. The map may name IDs beyond the registry (ID 2, which the bus writes at start but
// no entity carries).
const SPEC_NAMES: Record<number, string> = {
  0: 'Status',
  1: 'Control setpoint (TSet)',
  2: 'Master configuration / MemberID',
  3: 'Slave configuration / MemberID',
  5: 'Application-specific fault flags / OEM fault code',
  6: 'Remote-parameter flags',
  9: 'Remote override room setpoint',
  14: 'Maximum relative modulation setting',
  15: 'Boiler capacity and modulation limits',
  16: 'Room setpoint (TrSet)',
  17: 'Relative modulation level',
  18: 'CH water pressure',
  19: 'DHW flow rate',
  24: 'Room temperature (Tr)',
  25: 'Boiler flow temperature (Tboiler)',
  26: 'DHW temperature (Tdhw)',
  27: 'Outside temperature (Toutside)',
  28: 'Return water temperature (Tret)',
  33: 'Exhaust temperature (Texhaust)',
  48: 'DHW setpoint bounds',
  49: 'Max CH setpoint bounds',
  56: 'DHW setpoint (TdhwSet)',
  57: 'Max CH water setpoint (MaxTSet)',
  100: 'Remote override function',
  113: 'Unsuccessful burner starts',
  114: 'Flame signal too low count',
  116: 'Burner starts',
  117: 'CH pump starts',
  118: 'DHW pump/valve starts',
  119: 'DHW burner starts',
  120: 'Burner operation hours',
  121: 'CH pump operation hours',
  122: 'DHW pump/valve operation hours',
  123: 'DHW burner operation hours',
  124: 'OpenTherm version, master',
  125: 'OpenTherm version, slave',
  126: 'Master product version',
  127: 'Slave product version',
}

// Mirrors KINDS in tools/registry_model.py, in its order. Every kind the generator emits must
// appear here; an unknown one throws below rather than rendering under no heading.
const KINDS = ['sensor', 'binary', 'number', 'switch', 'enum'] as const

export default {
  load(): EntityData {
    if (!Array.isArray(ENTITIES) || ENTITIES.length === 0) {
      throw new Error('entities.data.ts: web/src/api/entities.ts exported no ENTITIES')
    }
    const groups: Record<string, EntityRow[]> = Object.fromEntries(KINDS.map((k) => [k, []]))
    const rows: EntityRow[] = []
    for (const e of ENTITIES) {
      const group = groups[e.kind]
      if (!group || typeof e.key !== 'string' || typeof e.name !== 'string') {
        throw new Error(`entities.data.ts: cannot place entity ${JSON.stringify(e)} ` +
          '(unknown kind or missing key/name) -- update KINDS to match tools/registry_model.py')
      }
      const row: EntityRow = {
        key: e.key,
        name: e.name,
        kind: e.kind,
        dataId: e.dataId,
        codec: e.codec,
        unit: e.unit,
        deviceClass: e.deviceClass,
        icon: e.icon,
        entityCategory: e.entityCategory,
        readable: e.readable,
        writable: e.writable,
        min: e.min,
        max: e.max,
        options: e.options ? [...e.options] : null,
      }
      group.push(row)
      rows.push(row)
    }
    // Sensors read best in Data-ID order (it groups related readings), synthetic ones last;
    // everything else keeps the generator's order, which is the table's own and, for options,
    // contract-visible. Array.prototype.sort is stable, so equal Data-IDs keep that order too.
    groups.sensor.sort((a, b) => (a.dataId ?? Infinity) - (b.dataId ?? Infinity))

    const byId = new Map<number, DataIdGroup>()
    for (const r of rows) {
      if (r.dataId === null) continue
      let g = byId.get(r.dataId)
      if (!g) {
        const message = SPEC_NAMES[r.dataId]
        if (!message) {
          throw new Error(`entities.data.ts: Data-ID ${r.dataId} (entity ${r.key}) has no ` +
            'OpenTherm message name -- add it to SPEC_NAMES')
        }
        g = { dataId: r.dataId, message, read: false, write: false, entities: [] }
        byId.set(r.dataId, g)
      }
      g.read ||= r.readable
      g.write ||= r.writable
      g.entities.push(r)
    }
    return {
      total: rows.length,
      writable: rows.filter((r) => r.writable).length,
      groups,
      byDataId: [...byId.values()].sort((a, b) => a.dataId - b.dataId),
      synthetic: rows.filter((r) => r.dataId === null),
      specNames: { ...SPEC_NAMES },
    }
  },
}
