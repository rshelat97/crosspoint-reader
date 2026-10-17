// Run: node --test tools/web-flasher/test/flasher-core.test.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import {
  activeAppPartition,
  boardForAsset,
  findBoardTag,
  parseImageHeader,
  parsePartitionTable,
  planFlash,
} from "../flasher-core.js";

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

// Minimal app image: header, esp_app_desc_t magic at 32, optional board tag.
function image(chipId, size = 1024, board = null) {
  const img = new Uint8Array(size);
  img[0] = 0xe9;
  img[12] = chipId & 0xff;
  img[13] = chipId >> 8;
  new DataView(img.buffer).setUint32(32, 0xabcd5432, true);
  if (board) [...`CROSSPOINT-BOARD-V1:${board};`].forEach((c, i) => (img[512 + i] = c.charCodeAt(0)));
  return img;
}

// otadata with one valid entry per sequence number; CRCs from Python's binascii.crc32(seq, 0xFFFFFFFF).
const SEQ_CRC = { 1: 0x4743989a, 2: 0x55f63774 };
function otadata(seq0, seq1 = null, state0 = 0xffffffff) {
  const b = new Uint8Array(0x2000).fill(0xff);
  const view = new DataView(b.buffer);
  for (const [base, seq, state] of [[0, seq0, state0], [0x1000, seq1, 0xffffffff]]) {
    if (seq === null) continue;
    view.setUint32(base, seq, true);
    view.setUint32(base + 24, state, true);
    view.setUint32(base + 28, SEQ_CRC[seq], true);
  }
  return b;
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
  const bootloader = image(5);
  bootloader.fill(0, 32, 36);  // no esp_app_desc_t: a bootloader or merged image
  assert.match(parseImageHeader(bootloader).error, /Not an app image/);
});

test("finds the board tag and maps release names to it", () => {
  assert.equal(findBoardTag(image(9, 1024, "x4pro")), "x4pro");
  assert.equal(findBoardTag(image(9)), null);
  assert.equal(parseImageHeader(image(9)).board, null);  // untagged: page refuses it as a release asset
  assert.equal(parseImageHeader(image(5, 1024, "x4")).board, "x4");
  assert.equal(boardForAsset("x3-x4"), "x4");
  assert.equal(boardForAsset("sticky"), "sticky");
});

test("finds the slot the bootloader boots from otadata", () => {
  const parts = parsePartitionTable(tableFromCsv());
  assert.equal(activeAppPartition(new Uint8Array(0x2000).fill(0xff), parts).label, "app0");
  assert.equal(activeAppPartition(otadata(1), parts).label, "app0");
  assert.equal(activeAppPartition(otadata(1, 2), parts).label, "app1");
  assert.equal(activeAppPartition(otadata(2, null, 3), parts).label, "app0");  // invalid entry ignored
  const badCrc = otadata(2);
  badCrc[28] ^= 1;
  assert.equal(activeAppPartition(badCrc, parts).label, "app0");
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

test("refuses firmware for a different board on the same chip", () => {
  const parts = parsePartitionTable(tableFromCsv());
  const plan = planFlash({ image: image(9, 1024, "x4c"), connectedChip: "ESP32-S3", partitions: parts, deviceBoard: "x4pro" });
  assert.equal(plan.ok, false);
  assert.match(plan.error, /x4c.*x4pro/);
  assert.equal(planFlash({ image: image(9, 1024, "x4pro"), connectedChip: "ESP32-S3", partitions: parts, deviceBoard: "x4pro" }).ok, true);
  // Stock firmware has no tag: allowed, like the firmware's own update guard.
  assert.equal(planFlash({ image: image(9, 1024, "x4pro"), connectedChip: "ESP32-S3", partitions: parts, deviceBoard: null }).ok, true);
});

test("refuses when a factory app would boot instead of ota_0", () => {
  const parts = parsePartitionTable(tableFromCsv());
  parts.push({ type: 0, subtype: 0x00, offset: 0x20000, size: 0x100000, label: "factory" });
  assert.equal(planFlash({ image: image(5), connectedChip: "ESP32-C3", partitions: parts }).ok, false);
});
