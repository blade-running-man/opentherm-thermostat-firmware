// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { useEffect, useRef, useState } from "preact/hooks";
import { ApiError, getLog } from "../../api/client";
import { Toggle } from "../../components/ui";
import { Notice } from "../settings/sections";
import { explainError, type Explanation } from "../settings/errors";
import { t } from "../../i18n/index.ts";
import { parseLog, type LogLine } from "./parse";
import { logRow } from "./row";
import styles from "./Log.module.css";

/**
 * Log — the device's own log over GET /api/log: the only way to read it without a
 * USB cable. The device keeps its last 80 lines (OT_LOG_LINES, components/ot_log); they are shown
 * oldest first, as the console shows them, and asked for again while "Follow" is on.
 *
 * The same route and the same document curl gets. The escaped bytes of a non-ASCII line are
 * decoded back where they are UTF-8 (parse.ts, recoverUtf8()) -- the device's own strings are
 * English by rule, but the owner's device name need not be.
 */

const POLL_MS = 3000;

export function LogPage(_props: { path?: string }) {
  const [lines, setLines] = useState<LogLine[] | null>(null);
  const [failure, setFailure] = useState<Explanation | null>(null);
  const [follow, setFollow] = useState(true);
  const box = useRef<HTMLDivElement>(null);
  const alive = useRef(true);
  const inFlight = useRef(false);

  async function load() {
    if (inFlight.current) return;
    inFlight.current = true;
    try {
      const doc: unknown = await getLog();
      const parsed = parseLog(doc);
      if (parsed === null) throw new ApiError(200, "the answer is not a log (a JSON array of lines)");
      if (alive.current) {
        setLines(parsed);
        setFailure(null);
      }
    } catch (err) {
      // The last lines stay: they are the ones most worth reading when the device has just gone
      // quiet.
      if (alive.current) setFailure(explainError(err));
    } finally {
      inFlight.current = false;
    }
  }

  useEffect(() => {
    alive.current = true;
    void load();
    return () => {
      alive.current = false;
    };
  }, []);

  useEffect(() => {
    if (!follow) return;
    const timer = setInterval(() => void load(), POLL_MS);
    return () => clearInterval(timer);
  }, [follow]);

  // Following means staying at the newest line, as a console does.
  useEffect(() => {
    if (follow && box.current) box.current.scrollTop = box.current.scrollHeight;
  }, [lines, follow]);

  return (
    <div class={styles.page}>
      <h1>{t("log.title")}</h1>

      {failure && <Notice what={failure} />}

      <div class={styles.bar}>
        <button type="button" onClick={() => void load()}>{t("common.refresh")}</button>
        <Toggle
          label={t("log.follow", { n: POLL_MS / 1000 })}
          checked={follow}
          onChange={setFollow}
        />
        <span class={styles.hint}>{t("log.hint")}</span>
      </div>

      {lines === null && !failure && <div class="card">{t("log.loading")}</div>}

      {lines !== null && (
        <div class={`card ${styles.log}`} ref={box}>
          {lines.length === 0 ? (
            <p class={styles.hint}>{t("log.empty")}</p>
          ) : (
            // Every cell comes from logRow(), which is where the classes and the level letter are
            // tested (row.ts). DO NOT reach back into `l` here: a cell built in the JSX is a cell
            // no suite can see, and that is how three of the five levels rendered
            // class="line undefined" unnoticed.
            lines.map((l, i) => {
              const row = logRow(styles, l);
              return (
                <div key={i} class={row.class}>
                  <span class={styles.level}>{row.level}</span>
                  <span class={styles.stamp}>{row.stamp}</span>
                  <span class={styles.tag}>{row.tag}</span>
                  <span class={styles.text}>{row.text}</span>
                </div>
              );
            })
          )}
        </div>
      )}
    </div>
  );
}
