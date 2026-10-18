// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { useEffect, useState } from "preact/hooks";
import { EXECUTOR_KEYS, saveExecutor, type DeviceConfig, type ExecutorKey } from "../../../api/config";
import { Button, Card, Select, TextField } from "../../../components/ui";
import { toast } from "../../../components/Toast";
import { t } from "../../../i18n/index";
import type { MsgKey } from "../../../i18n/index";
import type { Explanation } from "../errors";
import {
  executorFormFrom,
  executorLabel,
  executorPatch,
  rebaseExecutor,
  refusalOutcome,
  settleExecutor,
  type ExecutorCard,
} from "../executor";
import { Notice } from "./Notice";
import styles from "./sections.module.css";

type NumberKey = Exclude<ExecutorKey, "control_mode">;
const NUMBER_KEYS = EXECUTOR_KEYS.filter((k): k is NumberKey => k !== "control_mode");

/**
 * What each box is for. Not its bounds: those are the device's, and its refusal names them.
 *
 * A function, not an object: t() reads the reactive locale signal and must be called at render
 * time (executor.ts's executorLabel(), the same reason).
 */
const executorNote = (key: NumberKey): string => t(`settings.executor.note.${key}` as MsgKey);

/**
 * The controller's settings: the mode, the watchdog, the failsafe and the
 * flow band. Its own card and its own Save, because it is its own POST: only the boxes the owner
 * changed are sent (executor.ts), never the whole document the broker card sends.
 *
 * The heating season, CH, hot water and their setpoints are NOT here: the executor owns them,
 * /api/config refuses them by name, and the Control page writes them through the entity route.
 */
export function ExecutorSection({
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
  const [card, setCard] = useState<ExecutorCard>(() => {
    const seed = executorFormFrom(loaded);
    return { seed, form: seed };
  });
  const [saving, setSaving] = useState(false);
  const [failure, setFailure] = useState<Explanation | null>(null);
  const [bad, setBad] = useState<ExecutorKey[]>([]);

  // Every newer document goes under the boxes: untouched ones follow it, edited ones keep what
  // was typed (rebaseExecutor()). The broker card's save reloads it too.
  useEffect(() => setCard((c) => rebaseExecutor(c, loaded)), [loaded]);

  const built = executorPatch(card.seed, card.form);
  const changed = built.ok ? (Object.keys(built.patch) as ExecutorKey[]) : [];

  function edit(key: ExecutorKey, text: string) {
    setCard((c) => ({ ...c, form: { ...c.form, [key]: text } }));
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
      await saveExecutor(built.patch);
      toast(t("settings.executor.saved"));
      setCard((c) => settleExecutor(c, sentForm, changed));
      await onSaved();
    } catch (err) {
      const out = refusalOutcome(err, changed);
      setFailure(out.failure);
      setBad(out.bad);
    } finally {
      setSaving(false);
    }
  }

  return (
    <Card title={t("settings.executor.title")} subtitle={t("settings.executor.subtitle")}>
      <div class={styles.stack}>
        <Select
          label={executorLabel("control_mode")}
          value={card.form.control_mode}
          options={[
            { value: "0", label: t("settings.executor.mode.local") },
            { value: "1", label: t("settings.executor.mode.ha") },
          ]}
          disabled={disabled}
          onChange={(v) => edit("control_mode", String(v))}
        />
        {bad.includes("control_mode") && <p class={styles.warn}>{t("settings.refusedValue")}</p>}

        {NUMBER_KEYS.map((key) => (
          <TextField
            key={key}
            label={executorLabel(key)}
            value={card.form[key]}
            verbatim
            disabled={disabled}
            note={bad.includes(key) ? t("settings.refusedValue") : executorNote(key)}
            onChange={(v) => edit(key, v)}
          />
        ))}

        <div class={styles.actions}>
          <Button variant="primary" onClick={() => void save()} loading={saving}
                  disabled={disabled || (built.ok && changed.length === 0)}>
            {t("settings.executor.save")}
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
