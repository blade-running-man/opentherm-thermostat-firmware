// The site theme: VitePress's default theme in this device's web UI cybercore look
// (cyber-palette.css, cyber.css). Nothing else.
//
// WHY NO GLOBAL COMPONENTS. The pages that render generated tables (reference/entities, the
// Boiler page) do it inline with a <script setup> over a build-time data loader, so the data
// reaches only the page that imports it and the theme chunk every page downloads stays CSS.
// A component added here later that can throw during the build must also set
// `app.config.throwUnhandledErrorInProduction = true` under `import.meta.env.SSR` in an
// enhanceApp: Vue's production default only logs the error and renders nothing, which would
// publish an empty table under a heading that promises one.
import DefaultTheme from 'vitepress/theme'
import type { Theme } from 'vitepress'
import './cyber-palette.css'
import './cyber.css'

export default {
  extends: DefaultTheme,
} satisfies Theme
