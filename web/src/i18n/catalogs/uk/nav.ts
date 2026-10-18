// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Nav + app-chrome strings (the nav links, theme toggle, language label).
import type { Message } from "../../format.ts";

export const nav = {
  "nav.control": "Керування",
  "nav.boiler": "Котел",
  "nav.state": "Стан",
  "nav.log": "Журнал",
  "nav.settings": "Налаштування",
  "nav.update": "Оновлення прошивки",
  "nav.language": "Мова",
  "nav.theme.toggle": "Перемкнути тему",
  "nav.theme.toLight": "Увімкнути світлу тему",
  "nav.theme.toDark": "Увімкнути темну тему",
  "conn.connected": "Підключено",
  "conn.connecting": "Підключення",
  "conn.disconnected": "Немає з'єднання",
  "conn.connected.title": "Пристрій надсилає зміни одразу, як вони стаються",
  "conn.connecting.title": "Відкриття з'єднання з пристроєм",
  "conn.disconnected.title": "Немає з'єднання — показання можуть бути застарілими",
} satisfies Partial<Record<string, Message>>;
