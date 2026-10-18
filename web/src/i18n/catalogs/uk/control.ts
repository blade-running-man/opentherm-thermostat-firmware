// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Ukrainian translation of ../en/control.ts. Firmware identifiers and every {placeholder} are
// kept verbatim, as en/control.ts requires. Draft wording; the owner reviews it later
// (non-blocking).
import type { Message } from "../../format.ts";

export const control = {
  "failsafe.entries": { one: "запис", few: "записи", many: "записів", other: "запису" },

  "control.title": "Керування",
  "control.loading": "Опитування пристрою, що він зараз робить…",
  "control.live": "Наживо: зміни пристрою надходять одразу, як стаються.",
  "control.poll": "Активне з'єднання втрачено; картка опитує кожні {n} с, включно з "
    + "командою CH.",
  "control.stack": "Запас стека задачі термостата: {n} байт.",
  "control.accepted": "{what}: прийнято",

  "control.onoff.on": "увімк",
  "control.onoff.off": "вимк",

  "control.failsafe.count": "{count} {word} failsafe з моменту запуску",
  "control.failsafe.active": "Failsafe активний",
  "control.failsafe.ran": "Failsafe спрацьовував з моменту запуску",
  "control.failsafe.last": "; останній тривав {duration}",

  "control.sinceHa.ha": "Остання прийнята команда CH від Home Assistant: {counted} тому.",
  "control.sinceHa.waiting": "Жодної команди CH від Home Assistant не було прийнято з моменту "
    + "запуску цього пристрою або переходу в режим Home Assistant; watchdog нарахував "
    + "{counted} (це значення зберігається після перезапуску).",
  "control.sinceHa.other": "Watchdog нарахував {counted} без прийнятої команди CH від Home "
    + "Assistant.",

  "control.season.label": "Опалювальний сезон",

  "control.heating.title": "Центральне опалення",
  "control.ch.label": "CH",
  "control.ch.commandRow": "Команда CH",
  "control.ch.askedRow": "CH запитано в котла",
  "control.flow.label": "Уставка подачі",
  "control.flow.heldRow": "Утримувана уставка подачі",
  "control.flow.newRow": "Нова уставка подачі",
  "control.ch.explain": "Команда — це те, що запросив власник котла; \"запитано в котла\" — "
    + "це біт CH, який пристрій надіслав останнім. Вони на мить розходяться після нової "
    + "уставки — CH чекає, поки уставка не буде надіслана, — і щоразу, коли сезон або "
    + "failsafe перекриває команду.",

  "control.dhw.label": "Гаряча вода",
  "control.dhw.commandRow": "Команда гарячої води",
  "control.dhw.askedRow": "Гарячу воду запитано в котла",
  "control.dhw.setpointLabel": "Уставка гарячої води",
  "control.dhw.newSetpointRow": "Нова уставка гарячої води",
  "control.dhw.unset": "не встановлено: котел зберігає власне значення",

  "control.boost.label": "Boost",
  "control.boost.stopLabel": "Зупинити boost",
  "control.boost.runningRow": "Виконується",
  "control.boost.running": "{sp}, залишилось {time}",
  "control.boost.none": "Жоден boost не виконується.",
  "control.boost.bothNeeded": "в обидва поля потрібно ввести число",
  "control.boost.setpointAria": "Уставка подачі для boost",
  "control.boost.lengthAria": "Тривалість boost",
  "control.boost.lengthAriaMinutes": "Тривалість boost, хвилин",
  "control.boost.explain": "Boost гріє на уставці подачі протягом заданих хвилин, а потім "
    + "повертає керування команді CH. Пристрій вирішує, коли це може виконуватися, і "
    + "повідомляє, чому ні, якщо не може.",

  "control.button.on": "Увімк",
  "control.button.off": "Вимк",
  "control.button.set": "Встановити",
  "control.button.stop": "Стоп",
  "control.button.start": "Старт",
  "control.validation.notANumber": "не число",

  "control.hero.ariaLabel": "Стан котла",
  "control.hero.heatingOn": "▲ Опалення",
  "control.hero.heatingOff": "Опалення вимкнено",
  "control.hero.dhwOn": "▲ Гаряча вода",
  "control.hero.dhwOff": "Гаряча вода вимкнена",
  "control.hero.flow": "Подача",
  "control.hero.return": "Зворот",
  "control.hero.modulation": "Модуляція",
  "control.hero.bar": "Бар",
  "control.hero.tag.flow": "ПОДАЧА",
  "control.hero.tag.return": "ЗВОРОТ",
  "control.hero.tag.rad": "РАД.",
  "control.hero.tag.sink": "МИЙКА",
  "control.hero.tag.cold": "ХОЛОД",
  "control.hero.tag.boiler": "КОТЕЛ",
  "control.hero.tag.standby": "Очікування",

  "error.control.refused": "{what}: відхилено пристроєм",
  "error.control.valueRefused": "{what}: значення відхилено",
  "error.control.notKept": "{what}: пристрій не зміг це зберегти",
  "error.control.notCarriedOut": "{what}: на пристрої немає нічого, що б це виконало",
  "error.control.passwordNeeded": "Пристрій вимагає пароль веб-інтерфейсу",
  "error.control.writeRefused": "{what}: пристрій відхилив цей запис",
  "error.control.noRoute": "{what}: ця збірка прошивки не має такого маршруту",
  "error.control.badRequest": "{what}: пристрій не зрозумів запит",
  "error.control.notAccepted": "{what}: пристрій не прийняв це",
  "error.control.offlineHeadline": "{what}: немає відповіді від пристрою",
  "error.control.offlineDetail": "Нічого не повернулося, тож невідомо, чи це подіяло; картка "
    + "покаже це, щойно пристрій знову відповість. ({reason})",
  "error.control.notStarted": "Виконавець не запущено",
  "error.control.notStartedDetail": "Пристрій відповідає, але його задача термостата не "
    + "виконується: немає стану для показу, і кожне керування тут отримає відмову з кодом "
    + "503. Через перші секунди після завантаження це означає, що задача взагалі не "
    + "запустилася, і сторінка журналу пояснює чому.",
  "error.control.load.noRoute": "Ця збірка прошивки не обслуговує GET /api/control",
  "error.control.load.noRouteDetail": "Документ виконавця недоступний. ({message})",
  "error.control.load.denied": "Пристрій відмовився показати свої елементи керування",
  "error.control.load.badDocument": "Відповідь пристрою не є документом керування, який ця "
    + "сторінка може прочитати",
  "error.control.load.fault": "Пристрій повідомив про несправність",
  "error.control.load.offlineHeadline": "Немає відповіді від пристрою",
  "error.control.load.offlineDetail": "Пристрій не було досягнуто, тож він нічого не сказав про "
    + "свої елементи керування; картка зберігає останню відповідь, яку мала. ({reason})",
  "error.control.browserSilent": "Браузер не повідомив причину.",
} satisfies Partial<Record<string, Message>>;
