// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Ukrainian translation of ../en/settings.ts. Firmware identifiers and every {placeholder} are
// kept verbatim, as en/settings.ts requires. Draft wording; the owner reviews it later
// (non-blocking, per the i18n Phase B spec).
import type { Message } from "../../format.ts";

export const settings = {
  "settings.title": "Налаштування",
  "settings.readOnly.headline": "Ці налаштування записані новішою прошивкою",
  "settings.readOnly.detail":
    "Ця збірка не розуміє формат збереженої конфігурації, тому відмовляється "
    + "перезаписувати її, щоб не пошкодити. Перепрошийте пристрій попередньою прошивкою "
    + "або скиньте налаштування кнопкою, перш ніж щось тут змінювати.",
  "settings.load.hint":
    "Налаштування брокера та пристрою неможливо показати, доки їх не вдасться прочитати. "
    + "Форма Wi-Fi вище від них не залежить.",
  "settings.tryAgain": "Спробувати ще раз",
  "settings.loading": "Зчитування конфігурації пристрою…",
  "settings.save": "Зберегти налаштування",
  "settings.saved": "Налаштування збережено",
  "settings.saveBar.hint": "Зберігає налаштування брокера та пристрою разом. Мережі це не торкається.",
  "settings.notSent": "Не надіслано — ця сторінка нічого не залишила",
  "settings.refusedValue": "Пристрій відхилив це значення; причина — у повідомленні нижче.",
  "settings.nothingChanged": "Нічого не змінено.",
  "settings.sends": "Надсилає {keys} і більше нічого.",

  "settings.broker.title": "MQTT-брокер",
  "settings.broker.subtitle": "Куди пристрій публікує дані та де їх знаходить Home Assistant.",
  "settings.broker.host.label": "Хост брокера",
  "settings.broker.host.placeholder": "192.168.1.10 або homeassistant.local",
  "settings.broker.port.label": "Порт брокера",
  "settings.broker.port.placeholder": "1883",
  "settings.broker.username.label": "Ім'я користувача брокера",
  "settings.broker.password.label": "Пароль брокера",
  "settings.broker.password.warning":
    "У цій версії це з'єднання не шифрується, тому цей пароль видно всьому, що є у вашій "
    + "мережі. Не використовуйте пароль, яким ви користуєтесь деінде.",
  "settings.broker.topicPrefix.label": "Префікс тем",
  "settings.broker.discovery.label": "Публікувати виявлення Home Assistant",
  "settings.broker.discovery.hint":
    "Зміна префікса переносить кожну тему. Home Assistant зберігає вже відомі за старим "
    + "префіксом об'єкти, доки пристрій їх не відкличе, тож деякий час після зміни очікуйте "
    + "обидва варіанти.",

  "settings.device.title": "Пристрій",
  "settings.device.subtitle": "Як він називається і хто може це змінювати.",
  "settings.device.name.label": "Назва пристрою",
  "settings.device.name.placeholder": "Вентиляція",
  "settings.device.name.hint":
    "Лише назва для показу. Ідентичність пристрою — його client id брокера та device id "
    + "у Home Assistant — походить від його MAC-адреси, тож перейменування тут не може "
    + "створити другий пристрій у Home Assistant.",
  "settings.device.password.label": "Пароль вебінтерфейсу",
  "settings.device.password.warnTitle": "Для цього пристрою немає «забув пароль».",
  "settings.device.password.warn1a":
    "Якщо ви його втратите, єдиний шлях назад — кнопка на пристрої: утримуйте п'ять "
    + "секунд ",
  "settings.device.password.warn1em": "після того, як він увімкнувся",
  "settings.device.password.warn1b":
    ". Це разом очистить облікові дані Wi-Fi та цей пароль і не торкнеться налаштувань "
    + "брокера.",
  "settings.device.password.warn2a": "Утримання кнопки ",
  "settings.device.password.warn2em": "під час",
  "settings.device.password.warn2b":
    " увімкнення живлення робить дещо зовсім інше — переводить чіп у завантажувач "
    + "прошивки, що виглядає точнісінько як мертвий пристрій. Спершу дайте йому увімкнутися, "
    + "зачекайте, і лише тоді утримуйте.",
  "settings.device.password.noneSet":
    "Пароль не встановлено. Будь-хто, хто може дістатися цього пристрою в мережі, може "
    + "змінити ці налаштування — прийнятний вибір для пристрою в домашній мережі, і це "
    + "свідоме значення за замовчуванням.",

  "settings.executor.title": "Контролер",
  "settings.executor.subtitle": "Режим, сторожовий таймер, аварійний режим і діапазон подачі.",
  "settings.executor.mode.local": "Локальний — власне керування цього пристрою",
  "settings.executor.mode.ha": "Home Assistant — потрібен брокер",
  "settings.executor.save": "Зберегти налаштування контролера",
  "settings.executor.saved": "Налаштування контролера збережено",
  "settings.executor.label.control_mode": "Режим керування",
  "settings.executor.label.watchdog_s": "Сторожовий таймер (с)",
  "settings.executor.label.failsafe_setpoint_dc": "Аварійна уставка подачі (°C)",
  "settings.executor.label.failsafe_room_target_dc": "Аварійна цільова температура кімнати (°C)",
  "settings.executor.label.failsafe_heat_days": "Дні опалення в аварійному режимі",
  "settings.executor.label.failsafe_min_cycle_s": "Мінімальний цикл аварійного режиму (с)",
  "settings.executor.label.flow_min_dc": "Найнижча уставка подачі (°C)",
  "settings.executor.label.flow_max_dc": "Найвища уставка подачі (°C)",
  "settings.executor.note.watchdog_s":
    "Як довго Home Assistant може мовчати про CH, перш ніж аварійний режим візьме "
    + "керування на себе.",
  "settings.executor.note.failsafe_setpoint_dc":
    "Уставка подачі, з якою гріє аварійний режим, і значення, що утримується від запуску.",
  "settings.executor.note.failsafe_room_target_dc":
    "Температура кімнати, яку утримує аварійний режим, доки датчик кімнати свіжий.",
  "settings.executor.note.failsafe_heat_days":
    "Аварійний режим гріє, лише якщо Home Assistant запитував тепло протягом цієї "
    + "кількості днів.",
  "settings.executor.note.failsafe_min_cycle_s":
    "Найкоротший час увімкнення та найкоротший час вимкнення аварійного режиму.",
  "settings.executor.note.flow_min_dc":
    "Жодна уставка CH нижче цього значення не приймається, ні від кого. Тримайте її на "
    + "рівні або вище власного параметра E котла: запит нижче E не виконується.",
  "settings.executor.note.flow_max_dc": "Жодна уставка CH вище цього значення не приймається, ні від кого.",
  "settings.executor.problem": "{label}: потрібне {kind}.",
  "settings.executor.kind.decimal": "значення температури в градусах, не більше ніж з одним знаком після коми",
  "settings.executor.kind.whole": "ціле число",

  "settings.room.title": "Джерело кімнати (MQTT)",
  "settings.room.subtitle":
    "Температура кімнати, яку Home Assistant публікує на пристрій (docs/ha-room-source.md).",
  "settings.room.role.room": "Кімната — керує аварійним режимом",
  "settings.room.role.ambient": "Довкілля — лише показується, ніколи не керує",
  "settings.room.stale.hint":
    "Як довго пристрій чекає без нових публікацій, перш ніж вважати це джерело застарілим.",
  "settings.room.forwarded.hint":
    "Якщо увімкнено, застаріле значення змушує аварійний режим гріти наосліп замість "
    + "утримання останнього значення кімнати (випадок ha_blind).",
  "settings.room.save": "Зберегти налаштування джерела кімнати",
  "settings.room.saved": "Налаштування джерела кімнати збережено",
  "settings.room.label.room_mqtt_enable": "Використовувати MQTT-джерело кімнати",
  "settings.room.label.room_mqtt_role": "Роль",
  "settings.room.label.room_mqtt_stale_s": "Застаріває через (с)",
  "settings.room.label.room_mqtt_ha_forwarded": "Home Assistant передає застаріле значення",
  "settings.room.problem": "{label}: потрібне ціле число.",

  "settings.wifi.title": "Wi-Fi",
  "settings.wifi.subtitle": "Список показує, що чує цей пристрій, а не те, що показує ваш телефон.",
  "settings.wifi.network.label": "Мережа",
  "settings.wifi.network.choose": "— оберіть мережу —",
  "settings.wifi.network.currentlyConfigured": "налаштована зараз",
  "settings.wifi.network.open": "відкрита",
  "settings.wifi.network.manualOption": "Інша / прихована мережа — введіть назву…",
  "settings.wifi.network.ssidLabel": "Назва мережі (SSID)",
  "settings.wifi.network.ssidPlaceholder": "точно так, як транслює маршрутизатор",
  "settings.wifi.scan.again": "Сканувати ще раз",
  "settings.wifi.scan.start": "Пошук мереж",
  "settings.wifi.chooseFromList": "Обрати зі списку",
  "settings.wifi.typeInstead": "Ввести назву замість цього",
  "settings.wifi.scan.hint":
    "Назва, яка є на вашому телефоні, але відсутня в цьому списку, — звичайна причина, "
    + "чому пристрій \"не знаходить\" мережу, яка явно існує: більшість маршрутизаторів "
    + "транслюють одну назву в обох діапазонах, а цей пристрій не має радіомодуля 5 ГГц. "
    + "Введіть її вручну, якщо так сталося — приховані мережі ніколи не з'являються в "
    + "жодному скануванні, їх можна лише ввести.",
  "settings.wifi.password.label": "Пароль Wi-Fi",
  "settings.wifi.password.fallbackNetworkName": "збережена мережа",
  "settings.wifi.notice.newNetworkFallback": "нова мережа",
  "settings.wifi.password.hintUnencrypted":
    "Якщо ви перебуваєте у власній мережі налаштування пристрою, ця мережа відкрита: цей "
    + "ключ один раз перетне її незашифрованим. Ніщо на сторінці браузера, що подається "
    + "через звичайний HTTP, не може цьому запобігти.",
  "settings.wifi.network.openHint": "{ssid} — відкрита, залиште поле пароля порожнім.",
  "settings.wifi.network.securedWarning":
    "{ssid} захищена, а поле пароля порожнє. Пристрій спробує підключитися й отримає "
    + "відмову.",
  "settings.wifi.beforeYouPress": "Перш ніж натиснути це",
  "settings.wifi.save": "Зберегти Wi-Fi і перепідключитися",
  "settings.wifi.accepted": "Прийнято — пристрій зараз пробує",

  "settings.wifi.signal.notSeen": "не виявлено в цьому скануванні",
  "settings.wifi.signal.strong": "сильний",
  "settings.wifi.signal.good": "добрий",
  "settings.wifi.signal.weak": "слабкий",
  "settings.wifi.signal.veryWeak": "дуже слабкий",

  "settings.wifi.key.stored":
    "Ключ, уже збережений для {ssid}, буде використано знову. Він ніколи не надсилається "
    + "назад на цю сторінку, тому поле порожнє; введіть, щоб замінити його.",
  "settings.wifi.key.empty":
    "Порожнє: пристрій спробує підключитися без ключа. Правильно для відкритої мережі, "
    + "і ні для чого іншого.",
  "settings.wifi.key.typed": "Цей ключ буде надіслано пристрою так, як він введений.",

  "settings.wifi.notice.line1":
    "Пристрій відповідає на цю форму, перш ніж почати підключення, тож підтвердження тут "
    + "означає, що облікові дані прийнято — а не що {target} їх застосувала.",
  "settings.wifi.notice.line2":
    "Поки триває спроба, мережа налаштування, в якій ви зараз перебуваєте, і {target} "
    + "спільно використовують одне радіо. Ця сторінка, найімовірніше, перестане відповідати "
    + "протягом кількох секунд — це тут нормальний результат, а не збій.",
  "settings.wifi.notice.line3":
    "Після цього пристрій перебуватиме в {target}, з тією адресою, яку видасть ваш "
    + "маршрутизатор. Список підключених клієнтів вашого маршрутизатора — місце, де його "
    + "шукати.",
  "settings.wifi.notice.fallback":
    "Якщо ключ невірний, пристрій сам відновить {previous} і повернеться туди — це "
    + "остання мережа, яка справді дала цьому пристрою адресу, що й робить її єдиною "
    + "вартою повернення. Нічого скидати не потрібно.",
  "settings.wifi.notice.noFallback":
    "Цей пристрій ніколи не досягав жодної мережі, тому йому немає куди повертатися. "
    + "Якщо ключ невірний, він продовжує спроби, а власна мережа налаштування лишається в "
    + "ефірі приблизно 15 хвилин від моменту появи. Після цього мережа налаштування зникає "
    + "до перезапуску пристрою, який повертає її на кілька хвилин.",

  "settings.secret.stored.text":
    "Збережено на пристрої. Ніколи не надсилається назад, тому поле порожнє; введіть, "
    + "щоб замінити.",
  "settings.secret.stored.placeholder": "без змін — введіть, щоб замінити",
  "settings.secret.willClear.text": "Очищено. Збереження видалить збережене значення.",
  "settings.secret.willClear.placeholder": "порожньо — збереження видалить збережене значення",
  "settings.secret.willReplace.text": "Збереження замінить збережене значення.",
  "settings.secret.notSet.text": "Не встановлено.",
  "settings.secret.notSet.placeholder": "не встановлено",
  "settings.secret.willSet.text": "Збереження встановить це значення вперше.",
  "settings.error.portRange": "Порт брокера має бути цілим числом від 1 до 65535.",

  "settings.error.notImplemented.headline": "У пристрою є цей маршрут, але коду за ним ще немає",
  "settings.error.noHandler.headline": "Ця збірка прошивки не обслуговує цю кінцеву точку",
  "settings.error.passwordNeeded.headline": "Пристрій вимагає пароль вебінтерфейсу",
  "settings.error.writeRefused.headline": "Пристрій відхилив цей запис",
  "settings.error.badRequest.headline": "Пристрій не захотів це прийняти",
  "settings.error.fault.headline": "Пристрій повідомив про несправність",
  "settings.error.browserGaveNoReason": "Браузер не назвав причини.",
  "settings.error.disconnectExpected.headline": "Немає відповіді — і це тут якраз успіх",
  "settings.error.disconnectExpected.detail":
    "Пристрій відповідає до того, як перелаштує своє радіо, тож запит, що обривається "
    + "на півдорозі, зазвичай означає, що він уже намагається. ({browserSaid})",
  "settings.error.offline.headline": "Немає відповіді від пристрою",
  "settings.error.offline.detail": "Ніщо не досягло пристрою, тож він нічого про це не сказав. ({browserSaid})",
} satisfies Partial<Record<string, Message>>;
