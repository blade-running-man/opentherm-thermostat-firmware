// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { useEffect, useRef, useState } from "preact/hooks";
import { getConfig } from "../../api/config";
import { getControl } from "../../api/control";
import { connection, deviceState } from "../../api/device";
import { getEntity } from "../../api/registry";
import { toast } from "../../components/Toast";
import { t } from "../../i18n/index.ts";
import { Notice } from "../settings/sections";
import type { Explanation } from "../settings/errors";
import { explainControlFailure, explainControlLoad, explainNotStarted } from "./errors";
import { bandText, controlFingerprint, executorStarted, shownChCommand } from "./model";
import { readCard, singleFlight } from "./refresh";
import { BoostCard, DhwCard, FailsafeBanner, HeatingCard, StatusCard, type Hints } from "./parts";
import { BoilerHero } from "./BoilerHero";
import type { ControlDocument } from "../../api/control";
import styles from "./Control.module.css";

/**
 * Control — the executor's card: what it is doing and why, and the controls that
 * change it.
 *
 * NOT ONE PRIVILEGED PATH. The card reads GET /api/control,
 * GET /api/config, GET /api/entities/dhw_setpoint and GET /api/entities/ch_enable, and writes
 * through POST /api/entities/<key> and POST /api/ops/boost | boost_off: routes curl uses on the
 * same terms -- anything the card does, `curl` can do.
 *
 * LIVE THROUGH /ws. The socket carries the executor's entities as they change, and a change of any
 * of them makes the card ask for the document again (controlFingerprint()). The reason, the cause
 * and the countdowns are carried by no entity, so a slow poll picks those up -- and while the
 * socket is down that poll also reads the CH command, which is an entity (refresh.ts, readCard()).
 *
 * NOTHING IS PRE-DISABLED BY MODE. Which write the device accepts in which mode is its own
 * decision (ot_command_check()); the card says who owns what, sends what the owner presses, and
 * shows the device's answer in the device's words. A control is disabled only while its own write
 * is in flight.
 */

/** The slow poll, for what no entity carries: the countdowns and the reason. */
const POLL_MS = 5000;

/** The executor steps once a second, so a write shows in its document within that. */
const SETTLE_MS = 1500;

// Hints only: a box works without them, so a failure here is silence rather than a notice. Read
// once per visit; a band changed on the settings page shows after the next visit.
async function loadHints(): Promise<Hints> {
  const [cfg, dhw] = await Promise.allSettled([getConfig(), getEntity("dhw_setpoint")]);
  return {
    flow: cfg.status === "fulfilled"
      ? bandText(cfg.value.flow_min_dc / 10, cfg.value.flow_max_dc / 10) : null,
    dhw: dhw.status === "fulfilled" ? bandText(dhw.value.meta.min, dhw.value.meta.max) : null,
  };
}

export function ControlPage(_props: { path?: string; default?: boolean }) {
  const [doc, setDoc] = useState<ControlDocument | null>(null);
  const [polledCh, setPolledCh] = useState<boolean | null>(null);
  const [loadFailure, setLoadFailure] = useState<Explanation | null>(null);
  const [wrote, setWrote] = useState<Explanation | null>(null);
  const [busy, setBusy] = useState<string | null>(null);
  const [hints, setHints] = useState<Hints>({ flow: null, dhw: null });

  // Reading the signals here subscribes the page to them: every /ws frame re-renders it, and the
  // fingerprint decides whether that frame is worth a request.
  const values = deviceState.value;
  const live = connection.value === "open";
  const fingerprint = controlFingerprint(values);

  const alive = useRef(true);

  // One request at a time, and a trigger that lands during one is not lost (refresh.ts, whose
  // suite pins both). The state setters are stable, so the closure below is made once; the
  // socket's state is read at the moment of the request, not at the moment this was built.
  const [refresh] = useState(() => singleFlight(async () => {
    try {
      const read = await readCard(connection.value === "open", getControl,
                                  () => getEntity("ch_enable"));
      if (!alive.current) return;
      setDoc(read.doc);
      setPolledCh(read.ch);
      setLoadFailure(null);
    } catch (err) {
      // The last document is NOT wiped: a card a few seconds old is more use than an empty one,
      // and the notice above it says the link is gone.
      if (alive.current) setLoadFailure(explainControlLoad(err));
    }
  }, () => alive.current));

  useEffect(() => {
    alive.current = true;
    void loadHints().then((h) => {
      if (alive.current) setHints(h);
    });
    const timer = setInterval(() => void refresh(), POLL_MS);
    return () => {
      alive.current = false;
      clearInterval(timer);
    };
  }, []);

  // On mount, whenever one of the executor's entities changes on the socket, and when the socket
  // opens or closes -- the last because with it down the CH command comes from the poll instead.
  useEffect(() => {
    void refresh();
  }, [fingerprint, live]);

  /** Every control writes through here: its answer shown, the card asked again after it.
   *  `id` is the stable internal spelling (busy comparisons in parts.tsx); `label` is what the
   *  toast and a failure's headline show (parts.tsx's Writer, the `what`-prop decouple). */
  async function act(id: string, label: string, run: () => Promise<void>) {
    setBusy(id);
    setWrote(null);
    try {
      await run();
      toast(t("control.accepted", { what: label }));
    } catch (err) {
      setWrote(explainControlFailure(label, err));
    } finally {
      setBusy(null);
      void refresh();
      setTimeout(() => {
        if (alive.current) void refresh();
      }, SETTLE_MS);
    }
  }

  const started = doc !== null && executorStarted(doc);
  const writer = { busy, act };
  return (
    <div class={styles.page}>
      <BoilerHero />

      <h1>{t("control.title")}</h1>

      {loadFailure && <Notice what={loadFailure} />}
      {wrote && <Notice what={wrote} />}

      {doc === null && !loadFailure && (
        <div class="card">{t("control.loading")}</div>
      )}

      {/* A document with nothing behind it is said to be that, never drawn as a state. */}
      {doc !== null && !started && <Notice what={explainNotStarted()} />}

      {doc !== null && started && (
        <>
          <FailsafeBanner doc={doc} />
          <StatusCard doc={doc} {...writer} />
          <HeatingCard doc={doc} ch={shownChCommand(live, values, polledCh)} hints={hints}
                       {...writer} />
          <DhwCard doc={doc} hints={hints} {...writer} />
          <BoostCard doc={doc} hints={hints} {...writer} />
          <p class={styles.hint}>
            {live ? t("control.live") : t("control.poll", { n: POLL_MS / 1000 })}
            {doc.stack_hwm !== null && ` ${t("control.stack", { n: doc.stack_hwm })}`}
          </p>
        </>
      )}
    </div>
  );
}
