// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { Card, FormRow, TextField, Toggle } from "../../../components/ui";
import { t } from "../../../i18n/index";
import { SecretField } from "./SecretField";
import { describeSecret, type SettingsForm } from "../model";
import styles from "./sections.module.css";

/**
 * The MQTT broker.
 *
 * Nothing here is validated on this side beyond what patchFromForm() has to do to build a
 * JSON number out of the port box. ot_config_check_host/user/prefix already hold the
 * real rules, and a copy of them here would be a second source of truth that can only ever be
 * wrong in the direction that matters -- refusing a value the device would have accepted, with
 * no way for the owner to get past it.
 */
export function BrokerSection({
  form,
  loaded,
  disabled,
  onChange,
}: {
  form: SettingsForm;
  /** The document as GET returned it, for describeSecret(): the sentinel, or "". */
  loaded: { mqtt_password: string };
  disabled: boolean;
  onChange: (patch: Partial<SettingsForm>) => void;
}) {
  // One call, because the note and the placeholder are two halves of one statement and were
  // wrong for as long as they were computed apart (model.ts, SecretNote.placeholder).
  const password = describeSecret(loaded.mqtt_password, form.mqtt_password);
  return (
    <Card
      title={t("settings.broker.title")}
      subtitle={t("settings.broker.subtitle")}
    >
      <div class={styles.stack}>
        <FormRow>
          <TextField
            label={t("settings.broker.host.label")}
            value={form.mqtt_host}
            // A hostname is machine-read: "Homeassistant.local" and "homeassistant.local"
            // resolve the same, but a phone that autocorrects "nas" to "gnaws" does not.
            verbatim
            disabled={disabled}
            placeholder={t("settings.broker.host.placeholder")}
            onChange={(v) => onChange({ mqtt_host: v })}
          />
          <TextField
            label={t("settings.broker.port.label")}
            value={form.mqtt_port}
            verbatim
            disabled={disabled}
            placeholder={t("settings.broker.port.placeholder")}
            onChange={(v) => onChange({ mqtt_port: v })}
          />
        </FormRow>

        <TextField
          label={t("settings.broker.username.label")}
          value={form.mqtt_user}
          verbatim
          disabled={disabled}
          onChange={(v) => onChange({ mqtt_user: v })}
        />

        <SecretField
          label={t("settings.broker.password.label")}
          value={form.mqtt_password}
          note={password.text}
          placeholder={password.placeholder}
          disabled={disabled}
          onChange={(v) => onChange({ mqtt_password: v })}
        />

        {/*
          Said out loud on the screen where it is typed rather than only in the README:
          there is no TLS, so this password crosses the LAN in the clear on every
          connection. The owner can only act on that
          if they are told before they choose the password, not after.
        */}
        <p class={styles.warn}>{t("settings.broker.password.warning")}</p>

        <TextField
          label={t("settings.broker.topicPrefix.label")}
          value={form.topic_prefix}
          verbatim
          disabled={disabled}
          placeholder={"opentherm/<device id>"}
          onChange={(v) => onChange({ topic_prefix: v })}
        />

        <Toggle
          label={t("settings.broker.discovery.label")}
          checked={form.ha_discovery}
          disabled={disabled}
          onChange={(v) => onChange({ ha_discovery: v })}
        />
        <p class={styles.hint}>{t("settings.broker.discovery.hint")}</p>
      </div>
    </Card>
  );
}
