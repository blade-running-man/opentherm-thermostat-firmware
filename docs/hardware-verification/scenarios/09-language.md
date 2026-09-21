# 09 — The web interface language

This scenario proves the SPA's language layer (English, German, Dutch, Ukrainian). It changes
only what the browser shows — nothing about what the firmware does. The SPA is part of the
firmware build (`pio run -e lolin_c3_mini` embeds `web/dist`), so the translations ship with the
binary.

Crucially, the language is a **web-only overlay**: entity names sent to Home Assistant via MQTT
discovery stay English, and the firmware's own log strings stay English. The browser translates
them best-effort on the way to the screen.

Setup: a laptop browser with devtools open (Application tab, to read `localStorage`), on the
device's network, and a way to force a command refusal (e.g.
`curl -s -X POST http://$OT/api/entities/ch_setpoint -d '{"value":9999}'`) to see a translated
error. With a password set, add `-u ":$OTPASS"` (see
[`00-setup-and-safety.md`](00-setup-and-safety.md)).

## 1. The switcher and persistence

- [ ] The nav shows a language control; switching re-renders the current page immediately, with no
      reload.
- [ ] Switch through all four (EN/DE/NL/UK) on the Control page: nav labels, the page heading and
      the card body change language **together** — no part stays in the old language, no two
      languages mixed.
- [ ] Pick a non-English language and refresh the browser: the same language is still shown, and
      devtools → Application → Local Storage holds the chosen code in `ot.lang`.
- [ ] `document.documentElement.lang` (the `<html>` tag) updates to match (`de`/`nl`/`uk`/`en`).
- [ ] Clear `ot.lang` and reload: the SPA falls back to **English**, not the browser's own
      language — there is no `navigator.language` auto-detect.

## 2. Every page, every language (for each of DE, NL, UK)

- [ ] Visit Control, Boiler, State, Log and Firmware Update: every nav label, heading and body
      line is in the selected language — no leftover English in the page chrome.
- [ ] On Control and Settings, hover or hold every button and toggle: labels and confirmation text
      are translated.
- [ ] Numbers, units and OpenTherm-wire tokens are untouched: `°C`, `%`, `min`, the `ms`/`s` age
      suffixes, and raw register/token names (e.g. `ch_enable`, `flow_min_dc`) stay fixed in every
      language, not mangled.

## 3. Firmware log and error prose (best-effort)

- [ ] On the Log page, find a line matching a known template (a watchdog trip, a failsafe
      entry/exit, a broker connect/disconnect): in a non-English language it renders translated,
      with the embedded values (durations, ids) still correct.
- [ ] Find or provoke a line with no matching template: it renders in English while the rest of the
      page is translated — expected, not a bug.
- [ ] Force a command refusal (a 409 or 422): the error headline is translated; the `detail`
      sentence is translated if it matches a known pattern and stays English otherwise — either way
      not garbled or duplicated.

## 4. Entity names — the web overlay vs Home Assistant discovery

- [ ] On the State page and the raw OpenTherm table, entity names show in the selected language
      (DE/NL/UK) via the web-only name overlay.
- [ ] In Home Assistant, the entities published via MQTT discovery still show **English** names —
      the discovery documents are generated once in English and the overlay is web-only, never
      reaching Home Assistant. The HA entity list changing language would be a regression.

## 5. Layout under longer words

- [ ] On the Control page's hero diagram (boiler, burner, pipes), switch to German and Ukrainian
      (the longest strings): every label and tag stays inside its box or callout — no overflow,
      overlap or clipped word.
- [ ] Resize to a narrow, phone-width viewport in German and Ukrainian: the nav links wrap without
      cutting a word, and no button label overflows its border.

## Notes

- The firmware-line templates live in `web/src/i18n/firmwareText.ts`; the DE/NL/UK translations of
  entity display names are a web overlay keyed by the same entity keys, generated into the SPA and
  never touching discovery, so Home Assistant stays English.
