// Web flasher UI: Web Serial + esptool-js. Decisions about what to write live
// in flasher-core.js; this file only drives the browser and the device.
import { ESPLoader, Transport } from "https://cdn.jsdelivr.net/npm/esptool-js@0.6.1/bundle.js";
import SparkMD5 from "https://cdn.jsdelivr.net/npm/spark-md5@3.0.2/+esm";
import {
  DEVICE_LABELS,
  PARTITION_TABLE_OFFSET,
  PARTITION_TABLE_SIZE,
  parseImageHeader,
  parsePartitionTable,
  planFlash,
} from "./flasher-core.js";

const BAUD = 921600;
const $ = (id) => document.getElementById(id);
const ui = {
  release: $("release"), localFile: $("localFile"), fwInfo: $("fwInfo"), backup: $("backup"),
  connect: $("connect"), flash: $("flash"), disconnect: $("disconnect"),
  state: $("state"), progress: $("progress"), log: $("log"),
};

let firmware = null;  // { name, bytes }
let manifest = null;
let transport = null;
let loader = null;
let busy = false;

function log(line) {
  ui.log.hidden = false;
  ui.log.textContent += line + "\n";
  ui.log.scrollTop = ui.log.scrollHeight;
}
function setState(text, cls = "") {
  ui.state.textContent = text;
  ui.state.className = "status " + cls;
}
function setProgress(done, total) {
  ui.progress.hidden = total === 0;
  ui.progress.max = total || 1;
  ui.progress.value = done;
}
function refreshButtons() {
  ui.connect.disabled = busy || !!loader;
  ui.flash.disabled = busy || !loader || !firmware;
  ui.disconnect.disabled = busy || !loader;
}

const terminal = { clean() {}, writeLine: log, write: (s) => log(s) };

// --- firmware selection -------------------------------------------------------
async function loadManifest() {
  try {
    const res = await fetch("firmware/manifest.json", { cache: "no-cache" });
    if (!res.ok) throw new Error(res.status);
    manifest = await res.json();
  } catch {
    manifest = null;
  }
  ui.release.innerHTML = "";
  if (!manifest || !manifest.assets || manifest.assets.length === 0) {
    ui.release.add(new Option("No release published yet — use a local .bin", ""));
    return;
  }
  ui.release.add(new Option("Select your device…", ""));
  for (const a of manifest.assets) {
    ui.release.add(new Option(`${DEVICE_LABELS[a.device] || a.device} — ${manifest.tag}`, a.file));
  }
}

async function sha256Hex(bytes) {
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function showFirmware(name, bytes, downloadUrl) {
  const header = parseImageHeader(bytes);
  if (!header.ok) {
    firmware = null;
    ui.fwInfo.className = "status bad";
    ui.fwInfo.textContent = header.error;
  } else {
    firmware = { name, bytes };
    ui.fwInfo.className = "status ok";
    ui.fwInfo.textContent = `${name}: ${(bytes.length / 1048576).toFixed(2)} MB, built for ${header.chipName}. `;
    if (downloadUrl) {
      const a = document.createElement("a");
      a.href = downloadUrl;
      a.download = name;
      a.textContent = "Download for SD-card update";
      ui.fwInfo.append(a);
    }
  }
  refreshButtons();
}

ui.release.addEventListener("change", async () => {
  ui.localFile.value = "";
  const asset = manifest && manifest.assets.find((a) => a.file === ui.release.value);
  if (!asset) {
    firmware = null;
    ui.fwInfo.textContent = "";
    refreshButtons();
    return;
  }
  ui.fwInfo.className = "status";
  ui.fwInfo.textContent = "Downloading…";
  try {
    const url = "firmware/" + asset.file;
    const bytes = new Uint8Array(await (await fetch(url)).arrayBuffer());
    if ((await sha256Hex(bytes)) !== asset.sha256) throw new Error("checksum mismatch");
    showFirmware(asset.file, bytes, url);
  } catch (e) {
    firmware = null;
    ui.fwInfo.className = "status bad";
    ui.fwInfo.textContent = `Download failed (${e.message}). Try again.`;
    refreshButtons();
  }
});

ui.localFile.addEventListener("change", async () => {
  const file = ui.localFile.files[0];
  if (!file) return;
  ui.release.value = "";
  showFirmware(file.name, new Uint8Array(await file.arrayBuffer()), null);
});

// --- device -------------------------------------------------------------------
async function withBusy(fn) {
  busy = true;
  refreshButtons();
  try {
    await fn();
  } catch (e) {
    setState(e.message || String(e), "bad");
    log("Error: " + (e.message || e));
  } finally {
    busy = false;
    setProgress(0, 0);
    refreshButtons();
  }
}

async function disconnect() {
  try {
    if (transport) await transport.disconnect();
  } catch {
    // Port already gone (device reset or unplugged).
  }
  transport = null;
  loader = null;
}

ui.connect.addEventListener("click", () =>
  withBusy(async () => {
    let port;
    try {
      port = await navigator.serial.requestPort();
    } catch {
      setState("No device picked. If it never appears in the list, see “Device doesn't show up?” below.", "warn");
      document.getElementById("locked").scrollIntoView({ behavior: "smooth" });
      return;
    }
    transport = new Transport(port, false);
    loader = new ESPLoader({ transport, baudrate: BAUD, terminal });
    setState("Connecting…");
    try {
      await loader.main();
    } catch (e) {
      await disconnect();
      throw new Error(`Could not talk to the device (${e.message || e}). Wake it and try again.`);
    }
    setState(`Connected: ${loader.chip.CHIP_NAME}.`, "ok");
  }),
);

ui.disconnect.addEventListener("click", () => withBusy(async () => {
  await disconnect();
  setState("Disconnected.");
}));

function saveFile(bytes, name) {
  const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}

ui.flash.addEventListener("click", () =>
  withBusy(async () => {
    setState("Reading partition table…");
    const table = await loader.readFlash(PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE);
    const plan = planFlash({ image: firmware.bytes, connectedChip: loader.chip.CHIP_NAME, partitions: parsePartitionTable(table) });
    if (!plan.ok) throw new Error(plan.error);

    if (ui.backup.checked) {
      const size = parseInt(await loader.detectFlashSize(), 10) * 1048576;
      setState(`Backing up ${size / 1048576} MB… keep the device connected.`);
      const image = await loader.readFlash(0, size, (_pkt, done, total) => setProgress(done, total));
      saveFile(image, `xteink-backup-${new Date().toISOString().slice(0, 10)}.bin`);
      log("Backup saved.");
    }

    for (const w of plan.writes) log("Write: " + w.what);
    setState("Flashing… don't unplug the device.");
    await loader.writeFlash({
      fileArray: plan.writes.map(({ address, data }) => ({ address, data })),
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: (_i, written, total) => setProgress(written, total),
      calculateMD5Hash: (image) => SparkMD5.ArrayBuffer.hash(image),
    });
    await loader.after("hard_reset");
    await disconnect();
    setState("Done. The device is restarting into the new firmware.", "ok");
  }),
);

if (!("serial" in navigator)) {
  $("unsupported").hidden = false;
  ui.connect.disabled = true;
}
navigator.serial?.addEventListener("disconnect", () => {
  if (!busy) disconnect().then(() => { setState("Device disconnected."); refreshButtons(); });
});
loadManifest();
refreshButtons();
