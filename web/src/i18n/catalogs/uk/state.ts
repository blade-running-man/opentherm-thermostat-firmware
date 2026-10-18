// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// State page: table column headers, the write action, the page chrome, and its failures.
import type { Message } from "../../format.ts";

export const state = {
  "state.col.entity": "Сутність",
  "state.col.value": "Значення",
  "state.col.availability": "Доступність",
  "state.col.age": "Вік",
  "state.col.write": "Запис",
  "state.write": "Запис: {name}",

  "state.title": "Стан",
  "state.loading": "Запитую в пристрою його реєстр сутностей…",
  "state.hint": "Значення оновлюються кожні {seconds} с. Межі введення — ті, що повідомив сам "
    + "котел; поки він їх не повідомив, діють власні межі таблиці. Запис відповідає „queued”: "
    + "черга шини утримує один запис, а чи прийняв його котел, буде видно за значенням у "
    + "таблиці за один цикл опитування пізніше.",
  "state.value.yes": "так",
  "state.value.no": "ні",
  "state.readOnly": "лише читання",
  "state.writeAction": "Записати",
  "state.notANumber": "не число",
  "state.queued": "{name}: у черзі",

  "error.state.noRoute": "Ця збірка прошивки не обслуговує реєстр сутностей",
  "error.state.noRouteDetail": "Кінцеві точки /api/entities і /api/state з'явилися разом із "
    + "моделлю стану; прошивка, що зараз на пристрої, їх не має. ({message})",
  "error.state.denied": "Пристрій відмовився показати свій стан",
  "error.state.fault": "Пристрій повідомив про несправність",
  "error.state.offlineHeadline": "Немає відповіді від пристрою",
  "error.state.offlineDetail": "Пристрій не було досягнуто, тож він нічого не сказав про свій "
    + "стан. ({reason})",
  "error.state.browserSilent": "Браузер не повідомив причину.",

  "error.state.write.readOnly": "Ця сутність лише для читання",
  "error.state.write.refused": "Цей запис відхилено незалежно від значення",
  "error.state.write.refusedDetail": "Річ не в числі: або котел двічі відповів, що не має "
    + "такого Data-ID, і пристрій перестав запитувати, або поточний режим керування віддає це "
    + "значення іншому джерелу. ({message})",
  "error.state.write.outOfBounds": "Значення виходить за межі",
  "error.state.write.outOfBoundsDetail": "Запит було розібрано й зрозуміло; відхилено саме "
    + "число. Межі біля поля введення — ті, що повідомив сам котел, або власні межі таблиці, "
    + "поки він цього не зробив. ({message})",
  "error.state.write.noEntity": "Ця збірка прошивки не має такої сутності",
  "error.state.write.badRequest": "Пристрій не зрозумів запит",
  "error.state.write.denied": "Пристрій відхилив цей запис",
  "error.state.write.notAccepted": "Пристрій не прийняв запис",
  "error.state.write.offlineDetail": "Пристрій не було досягнуто. Чи було записано значення — "
    + "невідомо; перевірте це в таблиці, щойно з'єднання відновиться. ({reason})",
} satisfies Partial<Record<string, Message>>;
