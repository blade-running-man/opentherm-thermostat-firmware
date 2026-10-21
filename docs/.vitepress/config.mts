import { defineConfig, type DefaultTheme } from 'vitepress'

// The site root is docs/. Files under docs/public/ (the vendor PDFs in public/manuals/) are copied
// to the site root verbatim and linked by their served path, e.g. /manuals/esp32-c3-mini/<file>.pdf.

// A relative link whose target resolves outside docs/. Every site page sits at most one directory
// deep (docs/<section>/<page>.md), so two leading `../` always leave docs/; a root page
// (index.md) cannot be told apart from a section page here because VitePress hands the predicate
// the link alone, not its source file -- root pages must therefore not link `../` into the repo.
// VitePress normalises relative links to a leading `./` before the check, hence the optional prefix.
const LEAVES_DOCS = /^(\.\/)?(\.\.\/){2,}/

const sidebar: DefaultTheme.SidebarMulti = {
  '/guide/': [
    {
      text: 'Guide',
      items: [
        { text: 'Introduction', link: '/guide/introduction' },
        { text: 'Installation', link: '/guide/installation' },
        { text: 'First setup', link: '/guide/first-setup' },
        { text: 'Configuration', link: '/guide/configuration' },
        { text: 'Home Assistant', link: '/guide/home-assistant' },
        { text: 'Updating', link: '/guide/updating' },
        { text: 'Troubleshooting', link: '/guide/troubleshooting' },
      ],
    },
  ],
  '/architecture/': [
    {
      text: 'Architecture',
      items: [
        { text: 'Overview', link: '/architecture/overview' },
        { text: 'Components', link: '/architecture/components' },
        { text: 'Runtime', link: '/architecture/runtime' },
        { text: 'Executor', link: '/architecture/executor' },
        { text: 'Failure model', link: '/architecture/failure-model' },
        { text: 'Security', link: '/architecture/security' },
      ],
    },
  ],
  '/protocol/': [
    {
      text: 'Protocol',
      items: [
        { text: 'Frame', link: '/protocol/frame' },
        { text: 'Physical layer', link: '/protocol/physical-layer' },
        { text: 'Bus', link: '/protocol/bus' },
        { text: 'Commands', link: '/protocol/commands' },
      ],
    },
  ],
  '/interfaces/': [
    {
      text: 'Interfaces',
      items: [
        { text: 'REST API', link: '/interfaces/rest-api' },
        { text: 'WebSocket', link: '/interfaces/websocket' },
        { text: 'MQTT', link: '/interfaces/mqtt' },
        { text: 'Home Assistant discovery', link: '/interfaces/home-assistant-discovery' },
        { text: 'Web UI', link: '/interfaces/web-ui' },
      ],
    },
  ],
  '/device/': [
    {
      text: 'Device',
      items: [
        { text: 'Board', link: '/device/board' },
        { text: 'Indication', link: '/device/indication' },
        { text: 'Networking', link: '/device/networking' },
        { text: 'Room sources', link: '/device/room-sources' },
        { text: 'Configuration storage', link: '/device/config-storage' },
        { text: 'OTA and system', link: '/device/ota-and-system' },
        { text: 'Enclosure', link: '/device/enclosure' },
      ],
    },
  ],
  '/boiler/': [
    {
      text: 'Boiler',
      items: [
        { text: 'OpenTherm interface', link: '/boiler/' },
        { text: 'Intergas Kombi Kompakt HRE', link: '/boiler/intergas-kombi-kompakt-hre' },
      ],
    },
  ],
  '/development/': [
    {
      text: 'Development',
      items: [
        { text: 'Building', link: '/development/building' },
        { text: 'Testing', link: '/development/testing' },
        { text: 'Entity registry', link: '/development/entity-registry' },
        { text: 'Conventions', link: '/development/conventions' },
      ],
    },
  ],
  // The owner's open hardware test scenarios, in the order they are walked.
  '/hardware-verification/': [
    {
      text: 'Hardware verification',
      items: [
        { text: 'Overview', link: '/hardware-verification/' },
        { text: '00 · Setup and safety', link: '/hardware-verification/00-setup-and-safety' },
        { text: '01 · First bring-up', link: '/hardware-verification/01-first-bring-up' },
        { text: '02 · Conversation with the boiler', link: '/hardware-verification/02-boiler-conversation' },
        { text: '03 · Writes and refusals', link: '/hardware-verification/03-writes-and-refusals' },
        { text: '04 · The live web interface', link: '/hardware-verification/04-live-web-and-ui' },
        { text: '05 · Asking for heat', link: '/hardware-verification/05-asking-for-heat' },
        { text: '06 · Home Assistant', link: '/hardware-verification/06-home-assistant' },
        { text: '07 · Watchdog and failsafe', link: '/hardware-verification/07-watchdog-and-failsafe' },
        { text: '08 · Room sources', link: '/hardware-verification/08-room-sources' },
        { text: '09 · Language', link: '/hardware-verification/09-language' },
        { text: '10 · Status LED', link: '/hardware-verification/10-status-led' },
      ],
    },
  ],
  '/reference/': [
    {
      text: 'Reference',
      items: [
        { text: 'Overview', link: '/reference/' },
        { text: 'Entities', link: '/reference/entities' },
      ],
    },
    {
      // The same documents, in the same order, as reference/index.md.
      text: 'Hardware',
      collapsed: false,
      items: [
        { text: 'LOLIN C3 mini', link: '/reference/esp32-c3-mini' },
        { text: 'ESP32-C6 SuperMini', link: '/reference/esp32-c6-supermini' },
        { text: 'DIYLESS OpenTherm master shield', link: '/reference/diyless-opentherm-master-shield' },
      ],
    },
  ],
}

export default defineConfig({
  lang: 'en-US',
  title: 'OpenTherm Thermostat Firmware',
  description:
    'Own ESP-IDF firmware that makes an ESP32 the sole OpenTherm master of a gas boiler: an ' +
    'executor and guard with Home Assistant as the controller -- MQTT with discovery, REST and ' +
    'a web UI over one entity list.',

  // The site may be published under a sub-path (e.g. GitHub Pages /<repo>/).
  base: process.env.DOCS_BASE ?? '/',
  // Separate dirs per build so parallel builds (several agents at once) never share state.
  ...(process.env.DOCS_CACHE_DIR ? { cacheDir: process.env.DOCS_CACHE_DIR } : {}),
  ...(process.env.DOCS_OUT_DIR ? { outDir: process.env.DOCS_OUT_DIR } : {}),

  srcExclude: [
    '**/node_modules/**',
  ],

  // Dark first, like the device's own web UI the site is styled after (theme/cyber.css); the
  // toggle still switches to the light variant and remembers the reader's choice.
  appearance: 'dark',
  head: [
    ['meta', { name: 'theme-color', content: '#0a0a0f', media: '(prefers-color-scheme: dark)' }],
    ['meta', { name: 'theme-color', content: '#f4f5fa', media: '(prefers-color-scheme: light)' }],
  ],

  cleanUrls: true,
  lastUpdated: false,

  // DO NOT set this to `true`: a dead link between site pages is a real bug. Only links that
  // genuinely point outside the built site are let through.
  ignoreDeadLinks: [LEAVES_DOCS],

  themeConfig: {
    // Five top-level entries, not one per section: nine do not fit a laptop-width bar. The
    // sections behind a dropdown keep their own sidebars, so nothing is harder to reach once inside.
    nav: [
      { text: 'Guide', link: '/guide/introduction', activeMatch: '^/guide/' },
      {
        text: 'Firmware',
        activeMatch: '^/(architecture|interfaces|protocol|device)/',
        items: [
          { text: 'Architecture', link: '/architecture/overview', activeMatch: '^/architecture/' },
          { text: 'Interfaces', link: '/interfaces/rest-api', activeMatch: '^/interfaces/' },
          { text: 'Protocol', link: '/protocol/frame', activeMatch: '^/protocol/' },
          { text: 'Device', link: '/device/board', activeMatch: '^/device/' },
        ],
      },
      { text: 'Boiler', link: '/boiler/', activeMatch: '^/boiler/' },
      {
        text: 'Development',
        activeMatch: '^/(development|hardware-verification)/',
        items: [
          { text: 'Building and testing', link: '/development/building', activeMatch: '^/development/' },
          { text: 'Hardware verification', link: '/hardware-verification/', activeMatch: '^/hardware-verification/' },
        ],
      },
      { text: 'Reference', link: '/reference/', activeMatch: '^/reference/' },
    ],
    sidebar,
    outline: [2, 3],
    search: { provider: 'local' },
  },
})
