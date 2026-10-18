// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { render } from "preact";
import Router from "preact-router";
import { connectDevice } from "./api/device";
import { Nav } from "./components/Nav";
import { ToastContainer } from "./components/Toast";
import { BoilerPage } from "./pages/boiler";
import { ControlPage } from "./pages/control";
import { LogPage } from "./pages/log";
import { SettingsPage } from "./pages/settings";
import { StatePage } from "./pages/state";
import { UpdatePage } from "./pages/update";
import "./styles/global.css";
import styles from "./main.module.css";

// Per-route document titles (WCAG 2.4.2 / EN 9.2.4.2): a single-page app must still name the
// screen the user is on, not leave one static title on every view. onChange fires on the first
// route too, so the title is correct from load.
const BASE_TITLE = "OpenTherm Thermostat";
const PAGE_TITLES: Record<string, string> = {
  "/": "Control",
  "/control": "Control",
  "/boiler": "Boiler",
  "/state": "State",
  "/log": "Log",
  "/settings": "Settings",
  "/update": "Firmware Update",
};

function onRouteChange(e: { url: string }) {
  const name = PAGE_TITLES[e.url] ?? "Control";
  document.title = `${name} · ${BASE_TITLE}`;
}

function App() {
  return (
    <div>
      <Nav />
      <main class={styles.main}>
        {/*
          Control both on "/" and as the default: the question this page is opened with
          is what the executor is doing, and why. The Boiler page once stood here as the default route,
          when the one question was whether the device was talking to the boiler at all.
        */}
        <Router onChange={onRouteChange}>
          <ControlPage path="/" />
          <ControlPage path="/control" />
          <BoilerPage path="/boiler" />
          <StatePage path="/state" />
          <LogPage path="/log" />
          <SettingsPage path="/settings" />
          <UpdatePage path="/update" />
          <ControlPage default />
        </Router>
      </main>
      <ToastContainer />
    </div>
  );
}

// connectDevice() came together with /ws: the firmware brought up the
// socket and the single-use ticket that opens it, so the connection badge now speaks of a
// real connection rather than of its absence. From here, not from a component: there is one
// socket per application and it outlives any page, and DeviceSocket reopens itself —
// mounting and unmounting it along with a screen would mean dropping the connection on
// every navigation.
//
// Before render(), because the first thing connect() does is fetch a ticket over the
// network; the shell gets drawn while that is in flight.
connectDevice();

render(<App />, document.getElementById("app")!);
