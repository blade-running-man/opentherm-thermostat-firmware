// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Boiler page. See en/boiler.ts for what stays untranslated (row.type, decode columns,
// formatAge, entity names).
import type { Message } from "../../format.ts";

export const boiler = {
  "boiler.title": "Котел",
  "boiler.loading": "Опитуємо пристрій про шину…",

  "boiler.counter.cycles": "цикли",
  "boiler.counter.answers": "відповіді",
  "boiler.counter.failures": "збої",
  "boiler.counter.overdue": "прострочено",
  "boiler.uptimeHint": "Пристрій працює вже {uptime}. Оновлення кожні {seconds} с.",

  "boiler.answering": "Котел відповідає",
  "boiler.silent": "Котел не відповідає",
  "boiler.silentHint": "Майстер надсилає запити{cycles}, і відповіді немає. Перевірте по "
    + "черзі: живлення плати інтерфейсу, полярність пари, інверсію входу.",
  "boiler.silentHint.cyclesSuffix": " ({count} циклів)",

  "boiler.line": "Лінія: {text}.",
  "boiler.line.idle": "вхід у стані спокою (0 %) — так, як і має бути, поки котел мовчить",
  "boiler.line.inverted": "вхід постійно активний ({duty} %) — схоже, полярність входу "
    + "інвертована",
  "boiler.line.toggling": "вхід перемикається ({duty} %) — на лінії є кадри",

  "boiler.scan.run": "Опитати кожен Data-ID (~4 хв)",
  "boiler.scan.running": "Прохід триває: {done} з {total}",
  "boiler.scan.hint": "Опитує котел по кожному ідентифікатору від 0 до 127 один раз і "
    + "заносить відповіді в таблицю нижче. Лише читання — прохід нічого не записує. "
    + "Ідентифікатор, якого немає в таблиці після проходу, взагалі не відповів; рядок із "
    + "\"unknown-dataid\" означає, що котел прямо сказав, що такого немає. Це різні речі, і "
    + "обидві корисні.",
  "boiler.scan.forbidden": "Пристрій ще не в мережі: під час першого налаштування дозволене "
    + "лише саме налаштування. Підключіть Wi-Fi на сторінці налаштувань.",
  "boiler.scan.failed": "Запит не пройшов.",

  "boiler.lineTest.run": "Перевірити лінію мультиметром",
  "boiler.lineTest.running": "Тест триває, 20 с…",
  "boiler.lineTest.hint": "Протягом двадцяти секунд лінія повільно перемикається, по дві "
    + "секунди на кожен стан. Виміряйте напругу на клемах котла: вона повинна впасти з "
    + "15–24 В до семи або нижче і повернутися назад. Якщо вона не змінюється, вихід "
    + "адаптера не працює або на шині немає живлення.",
  "boiler.lineTest.warning": "Тим часом кадри не надсилаються, тож котел, найімовірніше, "
    + "запалить пальник",
  "boiler.lineTest.warningNote": " — саме так поводиться котел, коли термостат замовкає "
    + "(мовчазний термостат сприймається як запит на тепло). Це очікувано.",
  "boiler.lineTest.failed": "Запит не пройшов: можливо, тест уже виконується.",

  "boiler.raw.empty": "Жоден Data-ID ще не відповів. Поки котел мовчить, таблиця порожня — "
    + "це стан, а не помилка сторінки.",
  "boiler.raw.answers": "{count} відповідей",
  "boiler.raw.col.id": "ID",
  "boiler.raw.col.name": "назва",
  "boiler.raw.col.type": "тип відповіді",
  "boiler.raw.col.raw": "сире значення",
  "boiler.raw.col.f88": "f8.8",
  "boiler.raw.col.u16": "u16",
  "boiler.raw.col.s16": "s16",
  "boiler.raw.col.hiLo": "старший / молодший байт",
  "boiler.raw.col.flags": "прапорці",
  "boiler.raw.col.age": "вік",
} satisfies Partial<Record<string, Message>>;
