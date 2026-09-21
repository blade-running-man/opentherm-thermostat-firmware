// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { useRef, useState } from "preact/hooks";
import { provisionWifi, scanNetworks, type ScanResult } from "../../api/client";
import type { DeviceConfig } from "../../api/config";
import { explainError, type Explanation } from "./errors";
import {
  applyScan,
  chooseNetwork,
  listedSsid,
  mergeScan,
  provisionRequest,
  seedWifi,
  type WifiForm,
} from "./wifi";

/**
 * The Wi-Fi card's state and its two asynchronous paths, the scan and the provisioning.
 *
 * Split out of Settings.tsx to keep files under the 350-line ceiling, along
 * the seam the page already had: nothing else on the page touches the Wi-Fi boxes, and the page
 * hands this hook exactly two things -- the SSID of the document it rendered, and, through
 * noteConfig(), every document the instant it arrives. The comments below moved with the code.
 *
 * `section` is WifiSection's props minus the two the page derives from its own document
 * (`configuredSsid`, `knownGood`).
 */
export function useWifiCard(configuredSsid: string) {
  // THE THREE WI-FI BOXES ARE ONE ATOM, and that is what makes the two asynchronous paths on
  // this page correct rather than accidentally correct. Both of them -- the load and the scan --
  // resume after an await, in a closure that captured the values of a render the owner has
  // since typed over. Read from the closure, they reconcile against a form that no longer
  // exists: the network name typed during a sweep is replaced by the strongest thing the sweep
  // heard, and the SSID that arrives while a sweep is running is replaced the same way. As one
  // atom every such decision can be made from inside a setState UPDATER, which is handed the
  // value as it is now, and the rules themselves live in wifi.ts under test.
  //
  // DO NOT split these back into three useState calls "for readability". Three atoms cannot be
  // reconciled together, and the reconciliation is the whole subject.
  const [wifi, setWifi] = useState<WifiForm>({ ssid: "", manual: false, psk: "" });

  // The configured SSID as the device last reported it, in a ref because runScan() needs it
  // after its await and the page's document would still be the render's copy -- the ordering
  // this guards is Scan pressed BEFORE GET /api/config answers, where the render's copy is ""
  // and dropping the stored key follows from that alone. Written by noteConfig() the instant the
  // document arrives, which is the only place it can change.
  const configuredRef = useRef("");

  // null until a sweep has ANSWERED, which is not "the list is empty": mergeScan puts the
  // configured network in the list before any sweep has run, so a configured device would
  // otherwise offer to scan "again" before it had scanned once.
  const [scan, setScan] = useState<ScanResult[] | null>(null);
  const [scanning, setScanning] = useState(false);
  const [scanFailure, setScanFailure] = useState<Explanation | null>(null);

  const [provisioning, setProvisioning] = useState(false);
  const [provisionFailure, setProvisionFailure] = useState<Explanation | null>(null);
  const [accepted, setAccepted] = useState(false);

  const networks = mergeScan(scan ?? [], configuredSsid);

  /**
   * The page calls this with every document GET /api/config answers, synchronously, BEFORE it
   * sets any state of its own: runScan()'s continuation may resume before the page renders
   * again, and the key it keeps or drops depends on the ref being the document the device just
   * sent.
   *
   * The boxes are seeded only while they are still empty, and seedWifi() decides that from the
   * boxes AS THEY ARE when the answer lands -- not as they were when the page's closure was made.
   * On the mount path those are always "" and "", so a guard on captured values would be
   * unconditionally true: the owner who starts typing while GET is in flight (WifiSection is
   * rendered immediately, deliberately) would watch the name disappear with nothing on screen to
   * say it had been discarded.
   */
  function noteConfig(cfg: DeviceConfig) {
    configuredRef.current = cfg.wifi_ssid;
    setWifi((current) => seedWifi(current, cfg));
  }

  function chooseSsid(next: string) {
    // A stored key belongs to the network it was stored for; a typed one belongs to whatever
    // is being typed. chooseNetwork() knows which is which -- and it runs on every keystroke of
    // the manual box, so it has to.
    setWifi((current) => chooseNetwork(current, next, configuredRef.current));
  }

  function chooseManual(next: boolean) {
    if (next) {
      setWifi((current) => ({ ...current, manual: true }));
      return;
    }
    // Going back to the list with a name that is not in it would leave the <select> showing
    // some other option while the form still held the typed name. Whatever the list can show
    // is what the form gets -- and since the list carries a "— choose a network —" row, that
    // now includes "nothing", so an unlisted name becomes no choice rather than the loudest
    // network on the air.
    setWifi((current) =>
      chooseNetwork(
        { ...current, manual: false },
        listedSsid(networks, current.ssid),
        configuredRef.current,
      ),
    );
  }

  async function runScan() {
    setScanning(true);
    setScanFailure(null);
    try {
      const found = await scanNetworks();
      setScan(found);
      // From the updater's argument and from the ref, NEVER from `wifi` or `configuredSsid`
      // above. A sweep is thirteen channels and takes seconds; in those seconds the owner may
      // have typed a hidden network's name, and GET /api/config may have answered with the
      // stored one and its key. Reconciling against the render this click came from discards
      // whichever of the two happened -- which is the exact failure the list is here to
      // prevent, arriving through the code that prevents it.
      setWifi((current) => applyScan(current, found, configuredRef.current));
    } catch (err) {
      // A failed scan is not a failed page. The list is a convenience; the name can always be
      // typed, and on this device the scan can fail simply because the radio left the channel
      // this page is being served on (see scanNetworks()).
      setScanFailure(explainError(err));
    } finally {
      setScanning(false);
    }
  }

  async function saveWifi() {
    setProvisioning(true);
    setProvisionFailure(null);
    setAccepted(false);
    try {
      // provisionRequest() is what keeps the sentinel off this route: the form holds it
      // whenever the owner is re-provisioning the network the stored key belongs to, and it is
      // a storable 13-byte string (wifi.ts, api/client.ts ProvisionRequest).
      await provisionWifi(provisionRequest(wifi));
      setAccepted(true);
    } catch (err) {
      const why = explainError(err, { disconnectExpected: true });
      setProvisionFailure(why);
      // status === null means nothing answered, and after THIS request that is the ordinary
      // outcome: the device replies before it retunes and the reply can
      // lose the race with its own radio. A status means the device did answer, and answered
      // no -- which is a refusal, not a device that has gone to try.
      setAccepted(why.status === null);
    } finally {
      setProvisioning(false);
    }
  }

  return {
    noteConfig,
    section: {
      networks,
      ssid: wifi.ssid,
      manual: wifi.manual,
      psk: wifi.psk,
      scanned: scan !== null,
      scanning,
      scanFailure,
      provisioning,
      provisionFailure,
      accepted,
      onSsid: chooseSsid,
      onManual: chooseManual,
      onPsk: (v: string) => setWifi((current) => ({ ...current, psk: v })),
      onScan: () => void runScan(),
      onSave: () => void saveWifi(),
    },
  };
}
