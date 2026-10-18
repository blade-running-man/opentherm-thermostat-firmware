// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { useEffect, useState } from "preact/hooks";
import { saveRoomMqtt, type DeviceConfig, type RoomKey } from "../../../api/config";
import { Button, Card, Select, TextField, Toggle } from "../../../components/ui";
import { toast } from "../../../components/Toast";
import { t } from "../../../i18n/index";
import type { Explanation } from "../errors";
import {
  rebaseRoom,
  roomFormFrom,
  roomLabel,
  roomPatch,
  roomRefusalOutcome,
  settleRoom,
  type RoomCard,
} from "../room";
import { Notice } from "./Notice";
import styles from "./sections.module.css";

/**
 * The MQTT room source (docs/ha-room-source.md): an inbound topic Home Assistant publishes to,
 * off by default because enabling it changes what the bounded failsafe does. Its own card and its
 * own Save, for the same reason the controller card has its own: only the boxes the owner changed
 * are sent (room.ts), never the whole document the broker card sends.
 */
export function RoomMqttSection({
  loaded,
  disabled,
  onSaved,
}: {
  loaded: DeviceConfig;
  /** read_only: a store written by a newer firmware refuses every write. */
  disabled: boolean;
  /** Reloads the document; the effect below lays it under the boxes. */
  onSaved: () => Promise<void>;
}) {
  const [card, setCard] = useState<RoomCard>(() => {
    const seed = roomFormFrom(loaded);
    return { seed, form: seed };
  });
  const [saving, setSaving] = useState(false);
  const [failure, setFailure] = useState<Explanation | null>(null);
  const [bad, setBad] = useState<RoomKey[]>([]);

  // Every newer document goes under the boxes: untouched ones follow it, edited ones keep what
  // was typed (rebaseRoom()). Any other card's save reloads it too.
  useEffect(() => setCard((c) => rebaseRoom(c, loaded)), [loaded]);

  const built = roomPatch(card.seed, card.form);
  const changed = built.ok ? (Object.keys(built.patch) as RoomKey[]) : [];

  function editText(key: "room_mqtt_role" | "room_mqtt_stale_s", text: string) {
    setCard((c) => ({ ...c, form: { ...c.form, [key]: text } }));
  }

  function editBool(key: "room_mqtt_enable" | "room_mqtt_ha_forwarded", v: boolean) {
    setCard((c) => ({ ...c, form: { ...c.form, [key]: v } }));
  }

  async function save() {
    if (!built.ok) {
      // Not the device's answer, and it must not look like one: nothing left this page.
      setFailure({ tone: "error", status: null, headline: t("settings.notSent"),
                   detail: built.problem });
      setBad([built.key]);
      return;
    }
    if (changed.length === 0) return;
    const sentForm = card.form;
    setSaving(true);
    setFailure(null);
    setBad([]);
    try {
      await saveRoomMqtt(built.patch);
      toast(t("settings.room.saved"));
      setCard((c) => settleRoom(c, sentForm, changed));
      await onSaved();
    } catch (err) {
      const out = roomRefusalOutcome(err, changed);
      setFailure(out.failure);
      setBad(out.bad);
    } finally {
      setSaving(false);
    }
  }

  return (
    <Card title={t("settings.room.title")} subtitle={t("settings.room.subtitle")}>
      <div class={styles.stack}>
        <Toggle
          label={roomLabel("room_mqtt_enable")}
          checked={card.form.room_mqtt_enable}
          disabled={disabled}
          onChange={(v) => editBool("room_mqtt_enable", v)}
        />
        {bad.includes("room_mqtt_enable") && <p class={styles.warn}>{t("settings.refusedValue")}</p>}

        <Select
          label={roomLabel("room_mqtt_role")}
          value={card.form.room_mqtt_role}
          options={[
            { value: "1", label: t("settings.room.role.room") },
            { value: "0", label: t("settings.room.role.ambient") },
          ]}
          disabled={disabled}
          onChange={(v) => editText("room_mqtt_role", String(v))}
        />
        {bad.includes("room_mqtt_role") && <p class={styles.warn}>{t("settings.refusedValue")}</p>}

        <TextField
          label={roomLabel("room_mqtt_stale_s")}
          value={card.form.room_mqtt_stale_s}
          verbatim
          disabled={disabled}
          note={bad.includes("room_mqtt_stale_s") ? t("settings.refusedValue")
            : t("settings.room.stale.hint")}
          onChange={(v) => editText("room_mqtt_stale_s", v)}
        />

        <Toggle
          label={roomLabel("room_mqtt_ha_forwarded")}
          checked={card.form.room_mqtt_ha_forwarded}
          disabled={disabled}
          onChange={(v) => editBool("room_mqtt_ha_forwarded", v)}
        />
        {bad.includes("room_mqtt_ha_forwarded") && <p class={styles.warn}>{t("settings.refusedValue")}</p>}
        <p class={styles.hint}>{t("settings.room.forwarded.hint")}</p>

        <div class={styles.actions}>
          <Button variant="primary" onClick={() => void save()} loading={saving}
                  disabled={disabled || (built.ok && changed.length === 0)}>
            {t("settings.room.save")}
          </Button>
          <span class={styles.hint}>
            {!built.ok ? built.problem
              : changed.length === 0 ? t("settings.nothingChanged")
              : t("settings.sends", { keys: changed.join(", ") })}
          </span>
        </div>

        {failure && <Notice what={failure} />}
      </div>
    </Card>
  );
}
