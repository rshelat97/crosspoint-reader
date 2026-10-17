// Pure flashing logic for the web flasher: firmware image and partition table
// parsing, and the write plan. No DOM or Web Serial here so it can be tested
// under Node (tools/web-flasher/test/flasher-core.test.mjs).

// esp_image_header_t: magic at 0, chip_id (uint16 LE) at 12.
export const IMAGE_MAGIC = 0xe9;
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
const SUBTYPE_OTA_0 = 0x10;
const SUBTYPE_OTADATA = 0x00;

const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const hex = (n) => "0x" + n.toString(16);

export function parseImageHeader(bytes) {
  if (!bytes || bytes.length < 24) return { ok: false, error: "File is too small to be firmware." };
  if (bytes[0] !== IMAGE_MAGIC) return { ok: false, error: "Not an ESP32 firmware image (bad magic byte)." };
  const chipId = u16(bytes, 12);
  return { ok: true, chipId, chipName: CHIPS[chipId] || `unknown chip (id ${chipId})` };
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

// Decide what to write. The image goes into ota_0; otadata is blanked so the
// bootloader boots ota_0 even if the device last booted ota_1 after an OTA
// update (writing ota_0 alone would leave the old firmware running).
export function planFlash({ image, connectedChip, partitions }) {
  const header = parseImageHeader(image);
  if (!header.ok) return { ok: false, error: header.error };
  if (header.chipName !== connectedChip) {
    return {
      ok: false,
      error: `This firmware is built for ${header.chipName}, but the connected device is ${connectedChip}. Pick the build for your device.`,
    };
  }
  const ota0 = partitions.find((p) => p.type === TYPE_APP && p.subtype === SUBTYPE_OTA_0);
  const otadata = partitions.find((p) => p.type === TYPE_DATA && p.subtype === SUBTYPE_OTADATA);
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
