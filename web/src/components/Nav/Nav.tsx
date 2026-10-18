// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { route } from "preact-router";
import { signal } from "@preact/signals";
import { ConnectionBadge } from "../ConnectionBadge";
import { LangSwitcher } from "../LangSwitcher";
import { t } from "../../i18n/index";
import styles from "./Nav.module.css";

const currentPath = signal(window.location.pathname);
const theme = signal<"dark" | "light">(
  (localStorage.getItem("theme") as "dark" | "light") || "dark"
);
document.documentElement.setAttribute("data-theme", theme.value);

export function toggleTheme() {
  theme.value = theme.value === "dark" ? "light" : "dark";
  document.documentElement.setAttribute("data-theme", theme.value);
  localStorage.setItem("theme", theme.value);
}

window.addEventListener("popstate", () => {
  currentPath.value = window.location.pathname;
});

const links = [
  { href: "/control", key: "nav.control" },
  { href: "/boiler", key: "nav.boiler" },
  { href: "/state", key: "nav.state" },
  { href: "/log", key: "nav.log" },
  { href: "/settings", key: "nav.settings" },
  // The page's own heading (Update.tsx), so the link and the page it opens use the same name.
  { href: "/update", key: "nav.update" },
] as const;

export function Nav() {
  // "/" is the same Control page (the default route in main.tsx). Without this substitution no
  // item is highlighted when the root is opened, and the page looks like nobody's.
  const path = currentPath.value === "/" ? "/control" : currentPath.value;

  function navigate(e: Event, href: string) {
    e.preventDefault();
    route(href);
    currentPath.value = href;
  }

  return (
    <nav class={styles.nav}>
      <strong class={styles.logo} data-text={"OpenTherm Thermostat"}>
        OpenTherm&nbsp;Thermostat
      </strong>
      {links.map((l) => (
        <a
          key={l.href}
          href={l.href}
          onClick={(e: Event) => navigate(e, l.href)}
          class={`${styles.link}${path === l.href ? ` ${styles.linkActive}` : ""}`}
        >
          {t(l.key)}
        </a>
      ))}
      <span class={styles.spacer} />
      {/*
        The connection badge sits in the navigation rather than on the state page, because
        it does not speak for one page: there is one socket per application, and "the
        readings are stale" is true everywhere readings appear. Here it is also visible on
        every screen.
      */}
      <ConnectionBadge />
      <LangSwitcher />
      <button
        class={`theme-toggle ${styles.themeToggle}`}
        onClick={toggleTheme}
        title={t("nav.theme.toggle")}
        aria-label={
          theme.value === "dark" ? t("nav.theme.toLight") : t("nav.theme.toDark")
        }
      >
        {theme.value === "dark" ? "☀" : "☽"}
      </button>
    </nav>
  );
}
