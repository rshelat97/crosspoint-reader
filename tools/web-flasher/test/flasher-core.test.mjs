// Run: node --test tools/web-flasher/test/
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import { parseImageHeader, parsePartitionTable, planFlash } from "../flasher-core.js";

// Binary partition table built from the repo's partitions.csv, as gen_esp32part.py lays it out.
function tableFromCsv() {
  const types = { app: 0x00, data: 0x01 };
  const subtypes = { nvs: 0x02, ota: 0x00, ota_0: 0x10, ota_1: 0x11, spiffs: 0x82, coredump: 0x03 };
  const rows = readFileSync(new URL("../../../partitions.csv", import.meta.url), "utf8")
    .split("\n")
    .filter((l) => l.trim() && !l.startsWith("#"))
    .map((l) => l.split(",").map((f) => f.trim()));
  const out = new Uint8Array(0xc00).fill(0xff);
  const view = new DataView(out.buffer);
  rows.forEach(([name, type, subtype, offset, size], i) => {
    const o = i * 32;
    view.setUint16(o, 0x50aa, true);
    out[o + 2] = types[type];
    out[o + 3] = subtypes[subtype];
    view.setUint32(o + 4, parseInt(offset, 16), true);
    view.setUint32(o + 8, parseInt(size, 16), true);
    out.fill(0, o + 12, o + 28);
    [...name].forEach((c, j) => (out[o + 12 + j] = c.charCodeAt(0)));
    view.setUint32(o + 28, 0, true);
  });
  view.setUint16(rows.length * 32, 0xebeb, true);  // MD5 row terminates the table
  return out;
}

function image(chipId, size = 1024) {
  const img = new Uint8Array(size);
  img[0] = 0xe9;
  img[12] = chipId & 0xff;
  img[13] = chipId >> 8;
  return img;
}

test("parses the repo partition table", () => {
  const parts = parsePartitionTable(tableFromCsv());
  assert.deepEqual(
    parts.map((p) => [p.label, p.offset]),
    [["nvs", 0x9000], ["otadata", 0xe000], ["app0", 0x10000], ["app1", 0x650000], ["spiffs", 0xc90000], ["coredump", 0xff0000]],
  );
});

test("reads the chip from the image header", () => {
  assert.equal(parseImageHeader(image(5)).chipName, "ESP32-C3");
  assert.equal(parseImageHeader(image(9)).chipName, "ESP32-S3");
  assert.equal(parseImageHeader(new Uint8Array(64)).ok, false);
});

test("plans ota_0 write plus otadata reset for a matching image", () => {
  const plan = planFlash({ image: image(5), connectedChip: "ESP32-C3", partitions: parsePartitionTable(tableFromCsv()) });
  assert.equal(plan.ok, true);
  assert.deepEqual(plan.writes.map((w) => [w.address, w.data.length]), [[0x10000, 1024], [0xe000, 0x2000]]);
  assert.ok(plan.writes[1].data.every((b) => b === 0xff));
});

test("refuses an image for the other chip", () => {
  const plan = planFlash({ image: image(9), connectedChip: "ESP32-C3", partitions: parsePartitionTable(tableFromCsv()) });
  assert.equal(plan.ok, false);
  assert.match(plan.error, /ESP32-S3.*ESP32-C3/);
});

test("refuses an image larger than ota_0", () => {
  const plan = planFlash({ image: image(5, 0x640001), connectedChip: "ESP32-C3", partitions: parsePartitionTable(tableFromCsv()) });
  assert.equal(plan.ok, false);
});

test("refuses to guess without ota partitions", () => {
  const plan = planFlash({ image: image(5), connectedChip: "ESP32-C3", partitions: [] });
  assert.equal(plan.ok, false);
});
