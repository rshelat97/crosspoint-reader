// Pure flashing logic for the web flasher: firmware image and partition table
// parsing, board identity, and the write plan. No DOM or Web Serial here so it
// can be tested under Node (tools/web-flasher/test/flasher-core.test.mjs).

// esp_image_header_t: magic at 0, chip_id (uint16 LE) at 12. An app image's
// first segment starts with esp_app_desc_t (magic 0xABCD5432) at offset 32.
export const IMAGE_MAGIC = 0xe9;
const APP_DESC_MAGIC = 0xabcd5432;
export const CHIPS = {
  5: "ESP32-C3",
  9: "ESP32-S3",
};

// ESP-IDF partition table: 32-byte entries at 0x8000, magic 0x50AA.
export const PARTITION_TABLE_OFFSET = 0x8000;
export const PARTITION_TABLE_SIZE = 0xc00;
const ENTRY_SIZE = 32;
const ENTRY_MAGIC = 0x50aa;
const TYPE_APP = 0x00;
const TYPE_DATA = 0x01;
const SUBTYPE_FACTORY = 0x00;
const SUBTYPE_OTA_0 = 0x10;
const SUBTYPE_TEST = 0x20;
const SUBTYPE_OTADATA = 0x00;

// Every CrossPoint image embeds "CROSSPOINT-BOARD-V1:<board>;" once
// (src/network/FirmwareBoardTag.h). S3 boards share a chip id, so this is
// what tells them apart.
const BOARD_TAG = "CROSSPOINT-BOARD-V1:";
const MAX_BOARD_NAME = 23;

const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const hex = (n) => "0x" + n.toString(16);

export function parseImageHeader(bytes) {
  if (!bytes || bytes.length < 64) return { ok: false, error: "File is too small to be firmware." };
  if (bytes[0] !== IMAGE_MAGIC) return { ok: false, error: "Not an ESP32 firmware image (bad magic byte)." };
  if (u32(bytes, 32) !== APP_DESC_MAGIC) {
    return { ok: false, error: "Not an app image (a bootloader or merged image?). Use the firmware .bin." };
  }
  const chipId = u16(bytes, 12);
  return { ok: true, chipId, chipName: CHIPS[chipId] || `unknown chip (id ${chipId})`, board: findBoardTag(bytes) };
}

// Board name from the first CrossPoint board tag in `bytes`, or null.
export function findBoardTag(bytes) {
  const first = BOARD_TAG.charCodeAt(0);
  outer: for (let i = bytes.indexOf(first); i !== -1; i = bytes.indexOf(first, i + 1)) {
    for (let j = 1; j < BOARD_TAG.length; j++) {
      if (bytes[i + j] !== BOARD_TAG.charCodeAt(j)) continue outer;
    }
    let name = "";
    for (let k = i + BOARD_TAG.length; k < bytes.length && name.length <= MAX_BOARD_NAME; k++) {
      if (bytes[k] === 0x3b) return name || null;  // ';'
      name += String.fromCharCode(bytes[k]);
    }
  }
  return null;
}

// Release asset suffix → board tag. The combined C3 image is "x3-x4" but tags "x4".
export function boardForAsset(device) {
  return device === "x3-x4" ? "x4" : device;
}

export function parsePartitionTable(bytes) {
  const entries = [];
  for (let o = 0; o + ENTRY_SIZE <= bytes.length; o += ENTRY_SIZE) {
    if (u16(bytes, o) !== ENTRY_MAGIC) break;  // 0xEBEB (MD5 row) or 0xFFFF ends the table
    let label = "";
    for (let i = 0; i < 16 && bytes[o + 12 + i] !== 0; i++) label += String.fromCharCode(bytes[o + 12 + i]);
    entries.push({ type: bytes[o + 2], subtype: bytes[o + 3], offset: u32(bytes, o + 4), size: u32(bytes, o + 8), label });
  }
  return entries;
}

export const findOtadata = (partitions) => partitions.find((p) => p.type === TYPE_DATA && p.subtype === SUBTYPE_OTADATA);

// Same as binascii.crc32(data, 0xFFFFFFFF), which is how ESP-IDF checks otadata.
function otaSeqCrc(bytes) {
  let crc = 0;
  for (const b of bytes) {
    crc ^= b;
    for (let k = 0; k < 8; k++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return ~crc >>> 0;
}

const OTA_IMG_INVALID = 3;
const OTA_IMG_ABORTED = 4;

// The app partition the bootloader boots now, from otadata's two esp_ota_select_entry_t
// copies (ota_seq u32, label[20], state u32, crc u32), one per flash sector.
export function activeAppPartition(otadataBytes, partitions) {
  const slots = partitions.filter((p) => p.type === TYPE_APP && p.subtype >= SUBTYPE_OTA_0 && p.subtype < SUBTYPE_TEST)
    .sort((a, b) => a.subtype - b.subtype);
  let best = 0;
  for (const base of [0, 0x1000]) {
    if (base + 32 > otadataBytes.length) continue;
    const seq = u32(otadataBytes, base);
    if (seq === 0 || seq === 0xffffffff) continue;
    if (u32(otadataBytes, base + 28) !== otaSeqCrc(otadataBytes.subarray(base, base + 4))) continue;
    const state = u32(otadataBytes, base + 24);
    if (state === OTA_IMG_INVALID || state === OTA_IMG_ABORTED) continue;  // bootloader skips these
    best = Math.max(best, seq);
  }
  if (slots.length === 0) return null;
  return best === 0 ? slots[0] : slots[(best - 1) % slots.length];
}

// Decide what to write. The image goes into ota_0; otadata is blanked so the
// bootloader boots ota_0 even if the device last booted ota_1 after an OTA
// update (writing ota_0 alone would leave the old firmware running).
// `deviceBoard` is the board tag found in the device's running firmware (null
// for stock or unknown firmware); a different tag in the image is refused.
export function planFlash({ image, connectedChip, partitions, deviceBoard = null }) {
  const header = parseImageHeader(image);
  if (!header.ok) return { ok: false, error: header.error };
  if (header.chipName !== connectedChip) {
    return {
      ok: false,
      error: `This firmware is built for ${header.chipName}, but the connected device is ${connectedChip}. Pick the build for your device.`,
    };
  }
  if (deviceBoard && header.board && deviceBoard !== header.board) {
    return {
      ok: false,
      error: `This firmware is for the "${header.board}" board, but the device runs CrossPoint for "${deviceBoard}". Pick the build for your device.`,
    };
  }
  // With a factory or test app present, blank otadata boots that app instead of ota_0.
  if (partitions.some((p) => p.type === TYPE_APP && (p.subtype === SUBTYPE_FACTORY || p.subtype === SUBTYPE_TEST))) {
    return { ok: false, error: "The device has a factory/test app partition, so the new firmware would not boot. Refusing." };
  }
  const ota0 = partitions.find((p) => p.type === TYPE_APP && p.subtype === SUBTYPE_OTA_0);
  const otadata = findOtadata(partitions);
  if (!ota0 || !otadata) {
    return {
      ok: false,
      error: "The device's partition table has no ota_0/otadata partitions. Refusing to guess where firmware goes.",
    };
  }
  if (image.length > ota0.size) {
    return { ok: false, error: `Firmware (${image.length} bytes) is larger than the ota_0 partition (${ota0.size} bytes).` };
  }
  return {
    ok: true,
    writes: [
      { address: ota0.offset, data: image, what: `firmware → ota_0 (${hex(ota0.offset)})` },
      { address: otadata.offset, data: new Uint8Array(otadata.size).fill(0xff), what: `reset boot slot (otadata ${hex(otadata.offset)})` },
    ],
  };
}

// Release asset names are crosspoint-<version>-<device>.bin (release.yml).
export const DEVICE_LABELS = {
  "x3-x4": "Xteink X3 / X4",
  x4pro: "Xteink X4 Pro",
  x4c: "Xteink X4 Classic",
  papermono: "M5Stack PaperMono",
  sticky: "Seeed reTerminal Sticky",
  eego_a4: "EEGO A4",
  metalio_eink4: "Metalio eInk4",
};
