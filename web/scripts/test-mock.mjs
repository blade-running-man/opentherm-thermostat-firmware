// web/scripts/test-mock.mjs
// Standalone runner for the mock's host tests. Kept separate from scripts/test.mjs so a
// mock test can never stall the firmware-facing `npm test` gate, and so it can scan a
// directory outside src/ (the main runner hard-codes suites("src")). Same tally contract:
// a suite prints "<name>: P/T passed" and exits non-zero on failure.
import { readdirSync, statSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "mock", "tests");
function suites(dir) {
  const out = [];
  for (const name of readdirSync(dir).sort()) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) out.push(...suites(p));
    else if (name.endsWith(".test.ts")) out.push(p);
  }
  return out;
}

let files;
try { files = suites(root); } catch { files = []; }
let okSuites = 0, checks = 0, passed = 0, failedSuites = 0;
for (const file of files) {
  const r = spawnSync(process.execPath, [file], { encoding: "utf8" });
  process.stdout.write(r.stdout ?? "");
  if (r.stderr) process.stderr.write(r.stderr);
  const m = (r.stdout ?? "").match(/: (\d+)\/(\d+) passed\s*$/m);
  if (r.status === 0 && m && m[1] === m[2]) {
    okSuites++; passed += Number(m[1]); checks += Number(m[2]);
  } else {
    failedSuites++;
    if (m) { passed += Number(m[1]); checks += Number(m[2]); }
    console.log(`  SUITE FAILED: ${file}${m ? "" : " (no tally)"}`);
  }
}
console.log(`mock suites: ${okSuites}/${files.length} suites, ${passed}/${checks} checks passed`);
process.exit(failedSuites > 0 ? 1 : 0);
