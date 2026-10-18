// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { useEffect, useState } from "preact/hooks";
import { Button } from "../../components/ui";
import { toast } from "../../components/Toast";
import { getConfig, saveConfig, type DeviceConfig } from "../../api/config";
import { t } from "../../i18n/index";
import { explainError, type Explanation } from "./errors";
import { reloadLoaded } from "./executor";
import { formFromConfig, patchFromForm, type SettingsForm } from "./model";
import { useWifiCard } from "./useWifiCard";
import {
  BrokerSection,
  DeviceSection,
  ExecutorSection,
  Notice,
  RoomMqttSection,
  WifiSection,
} from "./sections";
import styles from "./Settings.module.css";

/**
 * Settings.
 *
 * Two forms and two buttons, because they are two different acts and one of them is not
 * reversible from here:
 *
 *   * The Wi-Fi form posts to /api/provision. It is the ONLY route an unprovisioned device
 *     accepts (ot_http_policy.c), it is the one with a rollback behind it
 *     (ot_provision.c), and it takes the device off the network this page is
 *     being served over.
 *   * Everything else posts the whole configuration document to /api/config, which cannot
 *     touch the network at all: the patch simply has no key for it (ConfigPatch in
 *     api/config.ts). Saving the broker can therefore never strand the device, however this
 *     page is misused.
 *
 * All the logic worth being wrong about lives in model.ts, wifi.ts and errors.ts, where the
 * host suite in tests/ can reach it; the Wi-Fi card's state is useWifiCard.ts. What is left here
 * is state and layout.
 */
// `default` alongside `path`: in main.tsx this same page stands both as a route and as the
// default route (preact-router reads both props). DashboardPage was declared the same way in
// the source project; without the second field tsc rejects <SettingsPage default />.
export function SettingsPage(_props: { path?: string; default?: boolean }) {
  // The document as the device sent it. Kept beside the form because every secret decision --
  // "stored" against "not set", whether the stored key still belongs to the chosen network --
  // is a comparison between what arrived and what is on screen now.
  const [loaded, setLoaded] = useState<DeviceConfig | null>(null);
  const [form, setForm] = useState<SettingsForm | null>(null);
  const [loading, setLoading] = useState(true);
  const [loadFailure, setLoadFailure] = useState<Explanation | null>(null);

  // For rendering, and for the Wi-Fi hook's list. The hook's asynchronous paths read the
  // document through noteConfig() instead -- the same value everywhere except in the window
  // between the document arriving and the next render, which is precisely the window one of them
  // resumes in.
  const configuredSsid = loaded?.wifi_ssid ?? "";
  const readOnly = loaded?.read_only === true;
  const wifiCard = useWifiCard(configuredSsid);

  const [saving, setSaving] = useState(false);
  const [saveFailure, setSaveFailure] = useState<Explanation | null>(null);

  /**
   * Reads the document. Called on mount, on the retry button, and after a successful save --
   * the last so that a password just typed comes back as the sentinel and its note stops
   * reading "will be replaced" over a value that already has been.
   *
   * The Wi-Fi boxes are seeded by useWifiCard's noteConfig(), which says when and why.
   */
  async function load() {
    setLoading(true);
    try {
      const cfg = await getConfig();
      // Before the state updates, and not from a later render (noteConfig() says why).
      wifiCard.noteConfig(cfg);
      setLoaded(cfg);
      setForm(formFromConfig(cfg));
      setLoadFailure(null);
    } catch (err) {
      setLoadFailure(explainError(err));
    } finally {
      setLoading(false);
    }
  }

  /**
   * After the controller card saves: the new document for `loaded`, and nothing else -- NOT
   * load(), which re-seeds the broker form too. reloadLoaded() (executor.ts) says why, and its
   * suite pins it. The controller card lays the new document under its own boxes.
   */
  const refresh = () => reloadLoaded(getConfig, wifiCard.noteConfig, setLoaded);

  useEffect(() => {
    void load();
    // Once, on mount. Re-running it on every render would fight the form for the input values.
  }, []);

  async function saveSettings() {
    if (form === null) return;
    const built = patchFromForm(form);
    if (!built.ok) {
      // Not an Explanation from the device, and it must not look like one: nothing left this
      // page, so the device has said nothing about it.
      setSaveFailure({
        tone: "error",
        status: null,
        headline: t("settings.notSent"),
        detail: built.problem,
      });
      return;
    }

    setSaving(true);
    setSaveFailure(null);
    try {
      await saveConfig(built.patch);
      toast(t("settings.saved"));
      await load();
    } catch (err) {
      setSaveFailure(explainError(err));
    } finally {
      setSaving(false);
    }
  }

  const patch = (fields: Partial<SettingsForm>) =>
    setForm((current) => (current === null ? current : { ...current, ...fields }));

  return (
    <div class={styles.page}>
      <h1>{t("settings.title")}</h1>

      {readOnly && (
        <Notice
          what={{
            tone: "denied",
            status: null,
            headline: t("settings.readOnly.headline"),
            detail: t("settings.readOnly.detail"),
          }}
        />
      )}

      {/*
        The Wi-Fi card is rendered even when the configuration could not be loaded, and it is
        the ONE thing `read_only` does not disable. Both for the same reason: the device that
        most needs this form is the device whose settings cannot be read or written -- a fresh
        one, or one whose flash holds a document this build does not understand. The decisions
        document's first invariant is that no state exists with no network, no access point and
        no way back, and a network form that switches
        itself off when the configuration is unreadable builds exactly that state.

        It also means the firmware must NOT gate POST /api/provision on read_only.
        ot_config_nvs_save_wifi() is already a separate write for a related reason
        (ot_config_nvs.h:57-60); this is the other one.

        Today GET /api/config answers 404 on every build of this firmware -- no handler is
        registered and asset_get refuses to hand back the SPA shell for an /api/ path
        (ot_http.c:221-227) -- so this is not a rare path, it is the only one.
      */}
      <WifiSection
        {...wifiCard.section}
        configuredSsid={configuredSsid}
        // Absent until the device grows the field, and absent has to read as FALSE: the notice
        // it drives promises the device will put the old network back by itself, and a promise
        // made because a flag was missing is the one this page must never make.
        knownGood={loaded?.wifi_known_good === true}
      />

      {loadFailure && (
        <div class="card">
          <div class={styles.stack}>
            <Notice what={loadFailure} />
            <p class={styles.hint}>{t("settings.load.hint")}</p>
            <div>
              <Button onClick={() => void load()} loading={loading}>
                {t("settings.tryAgain")}
              </Button>
            </div>
          </div>
        </div>
      )}

      {loading && form === null && !loadFailure && (
        <div class="card">{t("settings.loading")}</div>
      )}

      {form !== null && loaded !== null && (
        <>
          <BrokerSection form={form} loaded={loaded} disabled={readOnly} onChange={patch} />
          <DeviceSection
            form={form}
            loaded={loaded}
            passwordSet={loaded.ui_password_set}
            disabled={readOnly}
            onChange={patch}
          />

          {/*
            ONE button for both cards, because there is one document and one POST. Two buttons
            would imply two writes, and the second would silently resubmit the first card's
            values -- which is exactly the situation the sentinel scheme exists to make safe,
            and exactly the situation not worth relying on.
          */}
          <div class={styles.saveBar}>
            <Button
              variant="primary"
              onClick={() => void saveSettings()}
              loading={saving}
              disabled={readOnly}
            >
              {t("settings.save")}
            </Button>
            <span class={styles.hint}>{t("settings.saveBar.hint")}</span>
          </div>

          {saveFailure && <Notice what={saveFailure} />}

          <ExecutorSection loaded={loaded} disabled={readOnly} onSaved={refresh} />
          <RoomMqttSection loaded={loaded} disabled={readOnly} onSaved={refresh} />
        </>
      )}
    </div>
  );
}
