// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { Card, TextField } from "../../../components/ui";
import { t } from "../../../i18n/index";
import { SecretField } from "./SecretField";
import { describeSecret, type SettingsForm } from "../model";
import styles from "./sections.module.css";

/**
 * The name, and the password that locks this page.
 *
 * The warning under the password is not decoration: the way back must be on the
 * screen where the password is SET, before it is set and not after
 * A password nobody can undo, on a device mounted
 * behind the front panel of a running ventilation unit, is a way to lose the device -- which
 * is the reason the password is optional at all.
 *
 * The gesture is described without naming a pin: CLAUDE.md keeps GPIO numbers inside
 * components/board, and "the button on the device" is what the owner can act on anyway. The
 * "after it has started" half is the part that must not be dropped -- that button is also the
 * download strap, so holding it while power comes up enters the bootloader instead and looks
 * exactly like a device that has died.
 */
export function DeviceSection({
  form,
  loaded,
  passwordSet,
  disabled,
  onChange,
}: {
  form: SettingsForm;
  loaded: { ui_password: string };
  /** ot_config_public_t.ui_password_set -- derived by the device from the stored record. */
  passwordSet: boolean;
  disabled: boolean;
  onChange: (patch: Partial<SettingsForm>) => void;
}) {
  // One call: the note and the placeholder are two halves of one statement (model.ts).
  const password = describeSecret(loaded.ui_password, form.ui_password);
  return (
    <Card title={t("settings.device.title")} subtitle={t("settings.device.subtitle")}>
      <div class={styles.stack}>
        <TextField
          label={t("settings.device.name.label")}
          value={form.device_name}
          // NOT verbatim: this one IS prose. "Kitchen ventilation" should keep its capital,
          // and nothing reads this string but a human.
          disabled={disabled}
          placeholder={t("settings.device.name.placeholder")}
          onChange={(v) => onChange({ device_name: v })}
        />
        <p class={styles.hint}>{t("settings.device.name.hint")}</p>

        <SecretField
          label={t("settings.device.password.label")}
          value={form.ui_password}
          note={password.text}
          placeholder={password.placeholder}
          disabled={disabled}
          // Distinct from the household Wi-Fi key and from the broker's: this one is the
          // credential for this page, and it is the only one a password manager should be
          // offering to create.
          autocomplete="new-password"
          onChange={(v) => onChange({ ui_password: v })}
        />

        <div class={styles.warnBlock}>
          <strong>{t("settings.device.password.warnTitle")}</strong>
          <p>
            {t("settings.device.password.warn1a")}
            <em>{t("settings.device.password.warn1em")}</em>
            {t("settings.device.password.warn1b")}
          </p>
          <p>
            {t("settings.device.password.warn2a")}
            <em>{t("settings.device.password.warn2em")}</em>
            {t("settings.device.password.warn2b")}
          </p>
        </div>

        {!passwordSet && (
          <p class={styles.hint}>{t("settings.device.password.noneSet")}</p>
        )}
      </div>
    </Card>
  );
}
