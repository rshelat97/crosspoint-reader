// Web flasher UI: Web Serial + esptool-js. Decisions about what to write live
// in flasher-core.js; this file only drives the browser and the device.
import { ESPLoader, Transport } from "https://cdn.jsdelivr.net/npm/esptool-js@0.6.1/bundle.js";
import SparkMD5 from "https://cdn.jsdelivr.net/npm/spark-md5@3.0.2/+esm";
import {
  DEVICE_LABELS,
  PARTITION_TABLE_OFFSET,
  PARTITION_TABLE_SIZE,
  activeAppPartition,
  boardForAsset,
  findBoardTag,
  findOtadata,
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
let selection = 0;  // bumped per pick, so a slow download can't replace a newer choice
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

function showError(text) {
  firmware = null;
  ui.fwInfo.className = "status bad";
  ui.fwInfo.textContent = text;
  refreshButtons();
}

// `expectedBoard`: for a release asset, the board its name promises.
function showFirmware(name, bytes, downloadUrl, expectedBoard = null) {
  const header = parseImageHeader(bytes);
  if (!header.ok) {
    showError(header.error);
  } else if (expectedBoard && header.board !== expectedBoard) {
    showError(`${name} contains firmware for "${header.board}", not "${expectedBoard}". Not flashing it.`);
  } else {
    firmware = { name, bytes };
    ui.fwInfo.className = "status ok";
    const board = header.board ? ` for the "${header.board}" board` : "";
    ui.fwInfo.textContent = `${name}: ${(bytes.length / 1048576).toFixed(2)} MB, built${board} (${header.chipName}). `;
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
  const pick = ++selection;
  firmware = null;
  refreshButtons();
  ui.localFile.value = "";
  const asset = manifest && manifest.assets.find((a) => a.file === ui.release.value);
  if (!asset) {
    ui.fwInfo.textContent = "";
    return;
  }
  ui.fwInfo.className = "status";
  ui.fwInfo.textContent = "Downloading…";
  try {
    const url = "firmware/" + asset.file;
    const bytes = new Uint8Array(await (await fetch(url)).arrayBuffer());
    const sha = await sha256Hex(bytes);
    if (pick !== selection) return;
    if (sha !== asset.sha256) throw new Error("checksum mismatch");
    showFirmware(asset.file, bytes, url, boardForAsset(asset.device));
  } catch (e) {
    if (pick === selection) showError(`Download failed (${e.message}). Try again.`);
  }
});

ui.localFile.addEventListener("change", async () => {
  const pick = ++selection;
  firmware = null;
  refreshButtons();
  const file = ui.localFile.files[0];
  if (!file) return;
  ui.release.value = "";
  const bytes = new Uint8Array(await file.arrayBuffer());
  if (pick === selection) showFirmware(file.name, bytes, null);
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

// Board tag of the firmware the device boots now, read in chunks until the tag
// or the blank end of the image. null for stock or non-CrossPoint firmware.
async function readDeviceBoard(partitions) {
  const otadata = findOtadata(partitions);
  const slot = otadata && activeAppPartition(await loader.readFlash(otadata.offset, otadata.size), partitions);
  if (!slot) return null;
  const CHUNK = 0x40000;
  let tail = new Uint8Array(0);  // overlap so a tag split across chunks is still found
  for (let off = 0; off < slot.size; off += CHUNK) {
    setProgress(off, slot.size);
    const chunk = await loader.readFlash(slot.offset + off, Math.min(CHUNK, slot.size - off));
    const buf = new Uint8Array(tail.length + chunk.length);
    buf.set(tail);
    buf.set(chunk, tail.length);
    const board = findBoardTag(buf);
    if (board) return board;
    if (chunk.every((b) => b === 0xff)) return null;
    tail = chunk.subarray(chunk.length - 64);
  }
  return null;
}

// Full flash backup. Refuses rather than saving a partial image.
async function backup() {
  const sizeName = loader.DETECTED_FLASH_SIZES[(await loader.readFlashId()) >> 16 & 0xff];
  const size = parseInt(sizeName, 10) * 1048576;
  if (!sizeName || !sizeName.endsWith("MB") || !Number.isFinite(size)) {
    throw new Error("Couldn't detect the flash size, so the backup can't be trusted. Nothing was written.");
  }
  setState(`Backing up ${sizeName}… keep the device connected.`);
  const image = await loader.readFlash(0, size, (_pkt, done, total) => setProgress(done, total));
  if (image.length !== size) throw new Error(`Backup came back short (${image.length} of ${size} bytes). Nothing was written.`);
  saveFile(image, `xteink-backup-${new Date().toISOString().slice(0, 10)}.bin`);
  log(`Backup saved (${size} bytes).`);
}

ui.flash.addEventListener("click", () =>
  withBusy(async () => {
    const fw = firmware;
    setState("Reading partition table…");
    const partitions = parsePartitionTable(await loader.readFlash(PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE));
    setState("Checking which board this device is…");
    const deviceBoard = await readDeviceBoard(partitions);
    log(deviceBoard ? `Device runs CrossPoint for "${deviceBoard}".` : "Device firmware has no CrossPoint board tag (stock firmware?).");
    const plan = planFlash({ image: fw.bytes, connectedChip: loader.chip.CHIP_NAME, partitions, deviceBoard });
    if (!plan.ok) throw new Error(plan.error);

    if (ui.backup.checked) await backup();

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
