// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { useEffect, useState } from "preact/hooks";
import { getOtRaw, startLineTest, startScan, type OtRawSnapshot } from "../../api/otRaw";
import { Notice } from "../settings/sections";
import type { Explanation } from "../settings/errors";
import { explainOtRawFailure } from "./errors";
import { formatAge } from "./decode";
import { RawTable } from "./RawTable";
import { t } from "../../i18n/index.ts";
import styles from "./Boiler.module.css";

/**
 * Boiler — a raw view of what the OpenTherm bus has already received from the slave.
 *
 * The page is a debug page and deliberately interprets nothing beyond arithmetic: it shows
 * the Data-ID number and every reading of its word at once (see decode.ts), because the
 * codec of an unknown ID cannot be guessed, while a plausible value is recognised by eye
 * instantly.
 *
 * It was the first navigation item and once the default route, because the one
 * question people came here with then was whether the device was talking to the boiler at all.
 * The Control page now holds that place (main.tsx).
 *
 * Notice and the Explanation type are borrowed from the settings page: it is the only place
 * in the project where the device's answer is drawn at the volume it has earned, and where
 * "404 — the firmware has not grown this yet" does not look like a breakage. The text is
 * this page's own (errors.ts next door), the markup is shared.
 */

/** The poll period. The bus answers faster, but there is no point: people read slower. */
const POLL_MS = 2000;

export function BoilerPage(_props: { path?: string; default?: boolean }) {
  const [snap, setSnap] = useState<OtRawSnapshot | null>(null);
  const [failure, setFailure] = useState<Explanation | null>(null);
  // Separate from snap: showing "empty" before the first answer is not allowed — an empty
  // table here means "the boiler is silent", and confusing that with "the page has not asked
  // yet" is not allowed either.
  const [asked, setAsked] = useState(false);

  useEffect(() => {
    let alive = true;
    // Requests do not overlap each other. If the device has dropped off, fetch hangs until
    // the browser's timeout — tens of seconds, in which setInterval would have queued up a
    // dozen and a half requests to a device that cannot manage even one.
    let inFlight = false;

    async function poll() {
      if (inFlight) return;
      inFlight = true;
      try {
        const next = await getOtRaw();
        if (!alive) return;
        setSnap(next);
        setFailure(null);
      } catch (err) {
        if (!alive) return;
        // The last successful snapshot is NOT wiped: while debugging it matters to see what
        // managed to arrive before the link went away, and next to it that it has gone.
        setFailure(explainOtRawFailure(err));
      } finally {
        inFlight = false;
        if (alive) setAsked(true);
      }
    }

    void poll();
    const timer = setInterval(() => void poll(), POLL_MS);
    // Unmounting must stop both the timer and the state writes: without the first the page
    // keeps prodding the device after it has been left, without the second preact gets a
    // setState on an unmounted component.
    return () => {
      alive = false;
      clearInterval(timer);
    };
  }, []);

  return (
    <div class={styles.page}>
      <h1>{t("boiler.title")}</h1>

      {failure && <Notice what={failure} />}

      {!asked && snap === null && !failure && (
        <div class="card">{t("boiler.loading")}</div>
      )}

      {snap !== null && (
        <>
          <div class="card">
            <Answering snap={snap} />
            <div class={styles.counters}>
              <Counter label={t("boiler.counter.cycles")} value={snap.cycles} />
              <Counter label={t("boiler.counter.answers")} value={snap.ok} />
              <Counter label={t("boiler.counter.failures")} value={snap.failed} warn={snap.failed > 0} />
              <Counter label={t("boiler.counter.overdue")} value={snap.overdue} warn={snap.overdue > 0} />
            </div>
            <InputLine duty={snap.in_duty} />
            <Scan snap={snap} />
            <LineTest />
            <p class={styles.hint}>
              {t("boiler.uptimeHint", {
                uptime: formatAge(snap.uptime_ms),
                seconds: POLL_MS / 1000,
              })}
            </p>
          </div>

          <div class="card">
            <RawTable ids={snap.ids} />
          </div>
        </>
      )}
    </div>
  );
}

/**
 * Whether the boiler answers or not — the largest thing on the page.
 *
 * Because while debugging that is the most frequent state and the most frequent question: an
 * empty table with a silent boiler looks the same whether the bus has not been wired up at
 * all or has been wired up and the slave does not answer. Saying it in words is cheaper than
 * explaining the emptiness.
 */
function Answering({ snap }: { snap: OtRawSnapshot }) {
  if (snap.answering)
    return (
      <p class={`${styles.verdict} ${styles.answering}`}>{t("boiler.answering")}</p>
    );

  const cycles =
    snap.cycles > 0 ? t("boiler.silentHint.cyclesSuffix", { count: snap.cycles }) : "";
  return (
    <div>
      <p class={`${styles.verdict} ${styles.silent}`}>{t("boiler.silent")}</p>
      <p class={styles.hint}>{t("boiler.silentHint", { cycles })}</p>
    </div>
  );
}

/**
 * The level on the OpenTherm input, in words.
 *
 * The percentage on its own tells nobody anything, yet the question it settles is an
 * important one: the idle line is a low level, so a connected but silent boiler is
 * expected to show zero here. A steady hundred means the input polarity is the opposite
 * of the assumed one, and the cure for that is flipping ot_in_inverted in the board
 * descriptor, not hunting through the decoder. An intermediate value -- the line is
 * toggling.
 *
 * Zero with the boiler DISCONNECTED means nothing: the internal pull-down produces it
 * with no interface attached at all. That is why the text speaks of silence and not of
 * being in working order.
 */
function InputLine({ duty }: { duty: number }) {
  const text =
    duty === 0
      ? t("boiler.line.idle")
      : duty >= 95
        ? t("boiler.line.inverted", { duty })
        : t("boiler.line.toggling", { duty });
  return <p class={styles.hint}>{t("boiler.line", { text })}</p>;
}

/**
 * A sweep of the Data-ID space: ask the boiler about every identifier once.
 *
 * Progress is shown as a number and not only as a bar: "83 of 128" says how much
 * longer to wait, and a bar without a number does not.
 *
 * A 403 refusal is expected here and is explained in words right in the interface: on a
 * device not yet on the network, the policy lets through only the provisioning itself.
 * Sending a person off to read documentation for that would be an insult.
 */
function Scan({ snap }: { snap: OtRawSnapshot }) {
  const [failed, setFailed] = useState<string | null>(null);
  const running = snap.scan_total > 0;

  function run() {
    setFailed(null);
    startScan().catch((e: unknown) => {
      const status = (e as { status?: number }).status;
      setFailed(
        status === 403 ? t("boiler.scan.forbidden") : t("boiler.scan.failed")
      );
    });
  }

  return (
    <div class={styles.linetest}>
      <button type="button" onClick={run} disabled={running}>
        {running
          ? t("boiler.scan.running", { done: snap.scan_done, total: snap.scan_total })
          : t("boiler.scan.run")}
      </button>
      <p class={styles.hint}>
        {t("boiler.scan.hint")}
        {failed !== null && <> <b>{failed}</b></>}
      </p>
    </div>
  );
}

/**
 * The button for checking the adapter's output stage with a multimeter.
 *
 * Set apart from everything else, because it is the only action on the page that
 * INTERFERES with the bus: for twenty seconds the frames stop. The warning about the
 * boiler firing up sits next to the button rather than in the documentation: nobody is
 * going to read documentation while standing at the boiler.
 */
function LineTest() {
  const [state, setState] = useState<"idle" | "running" | "failed">("idle");

  function run() {
    setState("running");
    startLineTest()
      .then(() => window.setTimeout(() => setState("idle"), 20000))
      .catch(() => setState("failed"));
  }

  return (
    <div class={styles.linetest}>
      <button type="button" onClick={run} disabled={state === "running"}>
        {state === "running" ? t("boiler.lineTest.running") : t("boiler.lineTest.run")}
      </button>
      <p class={styles.hint}>
        {t("boiler.lineTest.hint")}
        {" "}
        <b>{t("boiler.lineTest.warning")}</b>{t("boiler.lineTest.warningNote")}
        {state === "failed" && <> {t("boiler.lineTest.failed")}</>}
      </p>
    </div>
  );
}

function Counter({ label, value, warn }: { label: string; value: number; warn?: boolean }) {
  return (
    <div class={styles.counter}>
      <span class={`${styles.counterValue}${warn ? ` ${styles.counterWarn}` : ""}`}>
        {value}
      </span>
      <span class={styles.counterLabel}>{label}</span>
    </div>
  );
}
