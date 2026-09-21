// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Ukrainian translation of ../en/tokens.ts. Firmware identifiers (watchdog_s, failsafe_heat_days,
// control_mode ha/local) and every {placeholder} are kept verbatim, as en/tokens.ts requires.
// Draft wording; the owner reviews it later (non-blocking).
import type { Message } from "../../format.ts";

export const tokens = {
  "token.state.season_off.label": "Опалювальний сезон вимкнено",
  "token.state.season_off.note": "Тепло не запитується, незалежно від перемикача CH. На гаряче водопостачання це не впливає.",
  "token.state.boost.label": "Прискорення",
  "token.state.boost.note": "Тимчасове прискорення утримує свою уставку подачі та запитує тепло, поки не завершиться.",
  "token.state.local.label": "Локальне керування",
  "token.state.local.note": "Власний перемикач CH і уставка подачі пристрою керують котлом.",
  "token.state.ha_waiting.label": "Очікування Home Assistant",
  "token.state.ha_waiting.note": "Home Assistant керує котлом і не надіслав жодної команди CH відтоді, як узяв керування. CH залишається вимкненим, доки це не станеться, або доки не спливе таймер очікування.",
  "token.state.failsafe.label": "Аварійний режим",
  "token.state.failsafe.note": "Home Assistant замовк або втратив дані, і пристрій виконує власний аварійний режим.",
  "token.state.ha.label": "Керує Home Assistant",
  "token.state.ha.note": "Команди Home Assistant керують котлом.",
  "token.state.unknown.label": "Невідомий стан \"{state}\"",
  "token.state.unknown.note": "Ця сторінка не знає стану, який повідомляє пристрій; показано власну назву пристрою для нього.",

  "token.reason.await_setpoint": "CH потрібен; пристрій чекає, поки нова уставка подачі дійде до котла, перш ніж запитати тепло.",
  "token.reason.fs_disarmed": "CH утримується вимкненим: Home Assistant не запитував тепло протягом failsafe_heat_days.",
  "token.reason.fs_blind": "Немає свіжої температури приміщення: обігрів наосліп за аварійною уставкою.",
  "token.reason.fs_room_cold": "Температура в приміщенні опустилася нижче аварійної цілі: обігрів.",
  "token.reason.fs_room_warm": "Приміщення досягло аварійної цілі: CH вимкнено.",
  "token.reason.min_cycle": "Біт CH утримується протягом мінімального часу циклу аварійного режиму.",
  "token.reason.other": "Причина: {reason}",

  "token.cause.watchdog": "Немає команди CH від Home Assistant довше, ніж watchdog_s.",
  "token.cause.ha_blind": "Власний датчик температури Home Assistant застарів, хоча його команди й надалі надходять.",
  "token.cause.other": "Причина: {cause}",

  "token.mode.ha": "Home Assistant керує командами CH і гарячої води (control_mode ha). Запис з цієї сторінки все одно надсилається, і відповідь пристрою показується.",
  "token.mode.local": "Власні органи керування цього пристрою керують командами CH і гарячої води (control_mode local): ця сторінка або будь-який REST-клієнт. Команди Home Assistant відхиляються.",

  "token.avail.ok": "доступно",
  "token.avail.invalid": "недійсні дані",
  "token.avail.unsupported": "не підтримується котлом",
  "token.avail.unknown": "ще немає відповіді",

  "error.detail.no_entity": "такої сутності не існує",
  "error.detail.read_only": "сутність доступна лише для читання",
  "error.detail.out_of_range": "значення поза межами діапазону",
  "error.detail.unsupported_id": "котел не підтримує цей data-ID",
  "error.detail.owned_ha": "належить Home Assistant: {1}",
  "error.detail.owned_local": "належить термостату: {1}",
  "error.detail.season_off_boost": "heating_season вимкнено: прискорення не вмикатиме обігрів",
} satisfies Partial<Record<string, Message>>;
