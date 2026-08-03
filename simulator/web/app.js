// CrossPoint Simulator web frontend.
//
// The wasm module runs the REAL firmware; this file only:
//   - blits the simulated e-ink framebuffer (800x480 RGBA) onto a canvas,
//     rotated for how the device is held (default: portrait),
//   - injects button presses (on-screen buttons + keyboard) and touch events
//     (pointer events, converted to panel-native normalized coordinates —
//     the firmware's own tapToLogical() applies the orientation transform),
//   - copies dropped EPUB/TXT/XTC files into the simulated SD card (/simfs,
//     persisted to IndexedDB).
'use strict';

(function () {
  var PANEL_W = 800;
  var PANEL_H = 480;
  var FB_BYTES = PANEL_W * PANEL_H * 4;
  var REFRESH_NAMES = ['full', 'half', 'fast'];

  // --- DOM ------------------------------------------------------------------
  var canvas = document.getElementById('screen');
  var ctx = canvas.getContext('2d');
  var flashEl = document.getElementById('flash-overlay');
  var bootOverlay = document.getElementById('boot-overlay');
  var bootDetail = document.getElementById('boot-detail');
  var sleepOverlay = document.getElementById('sleep-overlay');
  var dropHint = document.getElementById('drop-hint');
  var logEl = document.getElementById('log');
  var toastEl = document.getElementById('toast');
  var stFw = document.getElementById('st-fw');
  var stFrames = document.getElementById('st-frames');
  var stRefresh = document.getElementById('st-refresh');

  // Offscreen canvas holds the panel-native (800x480) image; the visible
  // canvas draws it through a rotation transform.
  var off = document.createElement('canvas');
  off.width = PANEL_W;
  off.height = PANEL_H;
  var offCtx = off.getContext('2d');
  var imageData = offCtx.createImageData(PANEL_W, PANEL_H);

  var api = null; // cwrap'd exports, set once the runtime is up
  var lastFrame = -1;
  var booted = false; // first frame presented
  var asleep = false;
  var viewRotation = parseInt(localStorage.getItem('simViewRotation') || '90', 10);
  if ([0, 90, 180, 270].indexOf(viewRotation) < 0) viewRotation = 90;

  // --- logging --------------------------------------------------------------
  var logLines = 0;
  function log(text, isErr) {
    (isErr ? console.error : console.log)(text);
    if (logLines > 500) {
      logEl.textContent = '';
      logLines = 0;
    }
    var span = document.createElement('span');
    if (isErr) span.className = 'err';
    span.textContent = text + '\n';
    logEl.appendChild(span);
    logEl.scrollTop = logEl.scrollHeight;
    logLines++;
  }

  var toastTimer = 0;
  function toast(text, warn) {
    toastEl.textContent = text;
    toastEl.className = warn ? 'show warn' : 'show';
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { toastEl.className = ''; }, 3500);
  }

  // --- view rotation --------------------------------------------------------
  function applyRotation() {
    var portrait = viewRotation === 90 || viewRotation === 270;
    canvas.width = portrait ? PANEL_H : PANEL_W;
    canvas.height = portrait ? PANEL_W : PANEL_H;
    document.body.classList.toggle('landscape-view', !portrait);
    blit();
  }

  function blit() {
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    switch (viewRotation) {
      case 90: // panel rotated clockwise = firmware Portrait orientation
        ctx.setTransform(0, 1, -1, 0, PANEL_H, 0);
        break;
      case 180:
        ctx.setTransform(-1, 0, 0, -1, PANEL_W, PANEL_H);
        break;
      case 270:
        ctx.setTransform(0, -1, 1, 0, 0, PANEL_W);
        break;
      default:
        break;
    }
    ctx.drawImage(off, 0, 0);
    ctx.setTransform(1, 0, 0, 1, 0, 0);
  }

  // View-canvas pixel -> panel-native pixel (inverse of the blit transform).
  function viewToPanel(vx, vy) {
    switch (viewRotation) {
      case 90: return [vy, PANEL_H - 1 - vx];
      case 180: return [PANEL_W - 1 - vx, PANEL_H - 1 - vy];
      case 270: return [PANEL_W - 1 - vy, vx];
      default: return [vx, vy];
    }
  }

  // --- framebuffer polling --------------------------------------------------
  function heapU8() {
    // ALLOW_MEMORY_GROWTH can detach the view; re-read it every frame.
    if (Module.HEAPU8 && Module.HEAPU8.byteLength) return Module.HEAPU8;
    if (Module.wasmMemory) return new Uint8Array(Module.wasmMemory.buffer);
    return new Uint8Array(Module.asm.memory.buffer);
  }

  function drawLoop() {
    if (api && !asleep) {
      var frame = api.fbFrame();
      if (frame !== lastFrame) {
        var ptr = api.fb();
        imageData.data.set(heapU8().subarray(ptr, ptr + FB_BYTES));
        offCtx.putImageData(imageData, 0, 0);
        blit();
        var mode = api.refreshMode();
        stFrames.textContent = String(frame);
        stRefresh.textContent = REFRESH_NAMES[mode] || String(mode);
        if (booted && mode === 0) {
          flashEl.classList.remove('flash');
          void flashEl.offsetWidth; // restart the CSS animation
          flashEl.classList.add('flash');
        }
        if (!booted) {
          booted = true;
          bootOverlay.hidden = true;
          stFw.textContent = 'running';
        }
        lastFrame = frame;
      }
    }
    requestAnimationFrame(drawLoop);
  }

  // --- buttons --------------------------------------------------------------
  function sendButton(idx, down) {
    if (api && !asleep) api.button(idx, down ? 1 : 0);
  }

  var devButtons = document.querySelectorAll('.dev-btn[data-btn]');
  devButtons.forEach(function (el) {
    var idx = parseInt(el.dataset.btn, 10);
    var pressed = false;
    el.addEventListener('pointerdown', function (e) {
      e.preventDefault();
      el.setPointerCapture(e.pointerId);
      pressed = true;
      el.classList.add('held');
      sendButton(idx, true);
    });
    var release = function () {
      if (!pressed) return;
      pressed = false;
      el.classList.remove('held');
      sendButton(idx, false);
    };
    el.addEventListener('pointerup', release);
    el.addEventListener('pointercancel', release);
    el.addEventListener('click', function (e) { e.preventDefault(); el.blur(); });
  });

  var KEYMAP = {
    Escape: 0, Backspace: 0,
    Enter: 1,
    ArrowLeft: 2,
    ArrowRight: 3,
    ArrowUp: 4, PageUp: 4,
    ArrowDown: 5, PageDown: 5,
    KeyP: 6
  };
  var keysDown = {};
  window.addEventListener('keydown', function (e) {
    var idx = KEYMAP.hasOwnProperty(e.code) ? KEYMAP[e.code] : KEYMAP[e.key];
    if (idx === undefined) return;
    e.preventDefault();
    if (e.repeat || keysDown[idx]) return;
    keysDown[idx] = true;
    sendButton(idx, true);
  });
  window.addEventListener('keyup', function (e) {
    var idx = KEYMAP.hasOwnProperty(e.code) ? KEYMAP[e.code] : KEYMAP[e.key];
    if (idx === undefined) return;
    e.preventDefault();
    if (!keysDown[idx]) return;
    keysDown[idx] = false;
    sendButton(idx, false);
  });
  window.addEventListener('blur', function () {
    for (var idx in keysDown) {
      if (keysDown[idx]) sendButton(parseInt(idx, 10), false);
      keysDown[idx] = false;
    }
  });

  // --- touch ----------------------------------------------------------------
  function sendTouch(type, e) {
    if (!api || asleep) return;
    var r = canvas.getBoundingClientRect();
    var vx = (e.clientX - r.left) * canvas.width / r.width;
    var vy = (e.clientY - r.top) * canvas.height / r.height;
    var p = viewToPanel(vx, vy);
    var nx = Math.min(1, Math.max(0, (p[0] + 0.5) / PANEL_W));
    var ny = Math.min(1, Math.max(0, (p[1] + 0.5) / PANEL_H));
    api.touch(type, nx, ny);
  }

  var touchActive = false;
  canvas.addEventListener('pointerdown', function (e) {
    if (e.button !== 0 && e.pointerType === 'mouse') return;
    e.preventDefault();
    canvas.setPointerCapture(e.pointerId);
    touchActive = true;
    sendTouch(0, e);
  });
  canvas.addEventListener('pointermove', function (e) {
    if (!touchActive) return;
    e.preventDefault();
    sendTouch(1, e);
  });
  function touchEnd(e) {
    if (!touchActive) return;
    touchActive = false;
    sendTouch(2, e);
  }
  canvas.addEventListener('pointerup', touchEnd);
  canvas.addEventListener('pointercancel', touchEnd);
  canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });

  // --- book upload ----------------------------------------------------------
  var BOOK_EXT = /\.(epub|txt|xtc)$/i;

  function addBooks(files) {
    if (!api || !booted) {
      toast('Wait for the firmware to boot before adding books.', true);
      return;
    }
    var accepted = Array.prototype.filter.call(files, function (f) { return BOOK_EXT.test(f.name); });
    if (!accepted.length) {
      toast('Only .epub, .txt and .xtc files are supported.', true);
      return;
    }
    var remaining = accepted.length;
    accepted.forEach(function (f) {
      f.arrayBuffer().then(function (buf) {
        var name = f.name.replace(/[\\/]/g, '_');
        try { Module.FS.mkdir('/simfs/books'); } catch (e) { /* exists */ }
        Module.FS.writeFile('/simfs/books/' + name, new Uint8Array(buf));
        log('[WEB] added book: ' + name + ' (' + buf.byteLength + ' bytes)');
        if (--remaining === 0) {
          api.saveFs();
          toast('Added ' + accepted.length + ' book(s) to /books — open the file browser to read.');
        }
      }).catch(function (err) {
        log('[WEB] failed to read ' + f.name + ': ' + err, true);
        if (--remaining === 0) api.saveFs();
      });
    });
  }

  var fileInput = document.getElementById('file-input');
  document.getElementById('tb-addbook').addEventListener('click', function () { fileInput.click(); });
  fileInput.addEventListener('change', function () {
    addBooks(fileInput.files);
    fileInput.value = '';
  });

  var dragDepth = 0;
  window.addEventListener('dragenter', function (e) {
    e.preventDefault();
    dragDepth++;
    dropHint.hidden = false;
  });
  window.addEventListener('dragover', function (e) { e.preventDefault(); });
  window.addEventListener('dragleave', function (e) {
    e.preventDefault();
    if (--dragDepth <= 0) { dragDepth = 0; dropHint.hidden = true; }
  });
  window.addEventListener('drop', function (e) {
    e.preventDefault();
    dragDepth = 0;
    dropHint.hidden = true;
    if (e.dataTransfer && e.dataTransfer.files.length) addBooks(e.dataTransfer.files);
  });

  // --- toolbar --------------------------------------------------------------
  document.getElementById('tb-screenshot').addEventListener('click', function () {
    var a = document.createElement('a');
    a.href = canvas.toDataURL('image/png');
    a.download = 'crosspoint-sim.png';
    a.click();
  });

  document.getElementById('tb-rotate').addEventListener('click', function () {
    viewRotation = (viewRotation + 90) % 360;
    localStorage.setItem('simViewRotation', String(viewRotation));
    applyRotation();
  });

  document.getElementById('tb-reset').addEventListener('click', function () {
    if (!confirm('Wipe the simulated SD card (books, settings, progress) and reboot?')) return;
    var url = new URL(location.href);
    url.searchParams.set('wipe', '1');
    location.href = url.href;
  });

  // --- persistence ----------------------------------------------------------
  setInterval(function () {
    if (api && booted && !asleep) api.saveFs();
  }, 15000);
  document.addEventListener('visibilitychange', function () {
    if (document.visibilityState === 'hidden' && api && booted) api.saveFs();
  });
  window.addEventListener('beforeunload', function () {
    if (api && booted) api.saveFs();
  });

  // --- sleep ----------------------------------------------------------------
  sleepOverlay.addEventListener('click', function () { location.reload(); });

  // --- module bootstrap -----------------------------------------------------
  window.Module = {
    print: function (t) { log(t); },
    printErr: function (t) { log(t, true); },
    onRuntimeInitialized: function () {
      api = {
        button: Module.cwrap('sim_button', null, ['number', 'number']),
        touch: Module.cwrap('sim_touch', null, ['number', 'number', 'number']),
        fb: Module.cwrap('sim_fb', 'number', []),
        fbFrame: Module.cwrap('sim_fb_frame', 'number', []),
        refreshMode: Module.cwrap('sim_refresh_mode', 'number', []),
        saveFs: Module.cwrap('sim_save_fs', null, [])
      };
      bootDetail.textContent = 'starting firmware';
    },
    onDeviceSleep: function () {
      asleep = true;
      sleepOverlay.hidden = false;
    },
    onAbort: function (what) {
      bootOverlay.hidden = false;
      bootDetail.textContent = 'crashed: ' + what;
      stFw.textContent = 'crashed';
    }
  };

  function boot() {
    var script = document.createElement('script');
    script.src = 'crosspoint_sim.js';
    script.onerror = function () {
      bootDetail.textContent = 'failed to load crosspoint_sim.js';
    };
    document.body.appendChild(script);
  }

  applyRotation();
  requestAnimationFrame(drawLoop);

  // ?wipe=1 deletes the IndexedDB store BEFORE the wasm mounts it — the only
  // moment the database is guaranteed to have no open connection.
  var params = new URLSearchParams(location.search);
  if (params.get('wipe') === '1') {
    params.delete('wipe');
    history.replaceState(null, '', location.pathname + (params.toString() ? '?' + params : ''));
    bootDetail.textContent = 'wiping storage';
    var req = indexedDB.deleteDatabase('/simfs');
    req.onsuccess = req.onerror = req.onblocked = function () {
      bootDetail.textContent = 'loading wasm';
      boot();
    };
  } else {
    boot();
  }
})();
