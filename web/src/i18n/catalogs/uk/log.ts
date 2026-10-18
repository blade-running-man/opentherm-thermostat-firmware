// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Ukrainian translation of ../en/log.ts (page chrome + LOG_TEMPLATES entries).
import type { Message } from "../../format.ts";

export const log = {
  "log.title": "Журнал",
  "log.follow": "Стежити (кожні {n} с)",
  "log.hint": "Пристрій зберігає останні 80 рядків. Час — від моменту запуску.",
  "log.loading": "Отримання журналу від пристрою…",
  "log.empty": "Журнал порожній: пристрій щойно запустився.",

  "log.bus.answering": "котел відповідає",
  "log.bus.not_answering": "котел не відповідає ({1}) після {2} спроб; опитування триває",
  "log.bus.id_unsupported": "ID {1} не підтримується котлом; вилучено з кільця опитування",
  "log.bus.line_test_finished": "перевірку лінії завершено, опитування відновлено",

  "log.mqtt.connected": "брокер підключено",
  "log.mqtt.lost": "з'єднання з брокером втрачено; повтор кожні 10 с, більше нічого не змінюється",
  "log.mqtt.refused": "брокер відхилив з'єднання: {1} (код {2})",
  "log.mqtt.unreachable": "брокер недоступний; спроб дотепер: {1}, повтор кожні 10 с",
  "log.mqtt.not_configured": "брокер не налаштовано; MQTT вимкнено",
  "log.mqtt.command_refused": "команду відхилено: {1} (ще {2} з моменту останнього рядка)",
  "log.mqtt.acl_refused": "брокер відхилив підписку на команди: перевірте ACL",
  "log.mqtt.discovery_failed": "discovery для {1} не опубліковано: воно не відображається",

  "log.net.captive_up": "портал налаштування активний: будь-яке ім'я веде на {1}",
  "log.net.captive_dns_failed": "Captive DNS не запустився ({1}); сторінка налаштування досі доступна за {2}",
} satisfies Partial<Record<string, Message>>;
