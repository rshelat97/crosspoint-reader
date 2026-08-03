// CrossPoint Simulator web frontend.
//
// The wasm module runs the REAL firmware; this file:
//   - blits the simulated e-ink framebuffer (800x480 RGBA) onto the device
//     mock's canvas (the panel is mounted landscape; the firmware renders
//     portrait via its own orientation transform — we rotate 90° when
//     blitting, and the whole device mock can be turned sideways on top),
//   - injects button presses (device keys + keyboard) and touch events
//     (pointer events, converted to panel-native normalized coordinates —
//     the firmware's own tapToLogical() applies the orientation transform),
//   - copies dropped EPUB/TXT/XTC files into the simulated SD card (/simfs,
//     persisted to IndexedDB),
//   - bridges the service worker to the firmware's web server so the REAL
//     File Transfer web interface works under ./device/.
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
  var stServer = document.getElementById('st-server');
  var webuiBtn = document.getElementById('tb-webui');

  // Offscreen canvas holds the panel-native (800x480) image; the visible
  // canvas draws it rotated 90° clockwise = the firmware's Portrait mapping.
  var off = document.createElement('canvas');
  off.width = PANEL_W;
  off.height = PANEL_H;
  var offCtx = off.getContext('2d');
  var imageData = offCtx.createImageData(PANEL_W, PANEL_H);

  var api = null; // cwrap'd exports, set once the runtime is up
  var lastFrame = -1;
  var booted = false; // first frame presented
  var asleep = false;
  var serverRunning = false;
  var swReady = false;
  var deviceRotation = parseInt(localStorage.getItem('simDeviceRotation') || '0', 10);
  if ([0, 90, 180, 270].indexOf(deviceRotation) < 0) deviceRotation = 0;

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
    toastTimer = setTimeout(function () { toastEl.className = ''; }, 4000);
  }

  // --- device rotation ------------------------------------------------------
  function applyDeviceRotation() {
    document.body.className = document.body.className.replace(/\bdev-rot-\d+\b/g, '').trim();
    if (deviceRotation) document.body.classList.add('dev-rot-' + deviceRotation);
  }

  // --- framebuffer blit -----------------------------------------------------
  function blit() {
    ctx.setTransform(0, 1, -1, 0, PANEL_H, 0); // panel rotated clockwise = firmware Portrait
    ctx.drawImage(off, 0, 0);
    ctx.setTransform(1, 0, 0, 1, 0, 0);
  }

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

  document.querySelectorAll('[data-btn]').forEach(function (el) {
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
  // offsetX/offsetY are in the canvas's own (untransformed) coordinate space,
  // which stays correct when the whole device mock is CSS-rotated.
  function sendTouch(type, e) {
    if (!api || asleep) return;
    var vx = e.offsetX * canvas.width / canvas.clientWidth;
    var vy = e.offsetY * canvas.height / canvas.clientHeight;
    // Visible canvas (portrait 480x800) -> panel-native (800x480): inverse of
    // the clockwise blit rotation.
    var px = vy;
    var py = PANEL_H - 1 - vx;
    var nx = Math.min(1, Math.max(0, (px + 0.5) / PANEL_W));
    var ny = Math.min(1, Math.max(0, (py + 0.5) / PANEL_H));
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
  webuiBtn.addEventListener('click', function () {
    if (!serverRunning) {
      toast('Start File Transfer on the device first: Home → File Transfer → any mode.', true);
    }
    window.open('device/', '_blank');
  });

  document.getElementById('tb-screenshot').addEventListener('click', function () {
    var a = document.createElement('a');
    a.href = canvas.toDataURL('image/png');
    a.download = 'crosspoint-sim.png';
    a.click();
  });

  document.getElementById('tb-rotate').addEventListener('click', function () {
    deviceRotation = (deviceRotation + 90) % 360;
    localStorage.setItem('simDeviceRotation', String(deviceRotation));
    applyDeviceRotation();
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

  // --- device web server bridge ---------------------------------------------
  function announceToSw() {
    if (navigator.serviceWorker && navigator.serviceWorker.controller) {
      navigator.serviceWorker.controller.postMessage({ type: 'sim-ready' });
    }
  }

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('sw.js').then(function () {
      return navigator.serviceWorker.ready;
    }).then(function () {
      swReady = true;
      webuiBtn.disabled = false;
      announceToSw();
    }).catch(function (err) {
      log('[WEB] service worker registration failed: ' + err, true);
    });
    navigator.serviceWorker.addEventListener('controllerchange', announceToSw);

    navigator.serviceWorker.addEventListener('message', function (e) {
      var msg = e.data;
      if (!msg || msg.type !== 'device-http') return;
      var port = e.ports[0];
      if (!api || !Module._sim_http_request) {
        port.postMessage({ status: 0, headers: [], body: null });
        return;
      }
      var headerBlock = '';
      for (var i = 0; i + 1 < msg.headers.length; i += 2) {
        headerBlock += msg.headers[i] + '\n' + msg.headers[i + 1] + '\n';
      }
      var bodyPtr = 0, bodyLen = 0;
      if (msg.body && msg.body.byteLength) {
        bodyLen = msg.body.byteLength;
        bodyPtr = Module._malloc(bodyLen);
        heapU8().set(new Uint8Array(msg.body), bodyPtr);
      }
      var status;
      try {
        status = Module.ccall('sim_http_request', 'number',
            ['string', 'string', 'number', 'number', 'string'],
            [msg.method, msg.path, bodyPtr, bodyLen, headerBlock]);
      } finally {
        if (bodyPtr) Module._free(bodyPtr);
      }
      var respHeaders = [];
      if (status > 0) {
        var hb = Module.ccall('sim_http_response_headers', 'string', [], []);
        var parts = hb.split('\n');
        for (var j = 0; j + 1 < parts.length; j += 2) respHeaders.push(parts[j], parts[j + 1]);
      }
      var respBody = null;
      if (status > 0) {
        var ptr = Module._sim_http_response_body();
        var len = Module._sim_http_response_body_len();
        respBody = heapU8().slice(ptr, ptr + len).buffer;
      }
      // Persist whatever the request changed (uploads, deletes, settings).
      if (msg.method !== 'GET' && msg.method !== 'HEAD') api.saveFs();
      port.postMessage({ status: status, headers: respHeaders, body: respBody }, respBody ? [respBody] : []);
    });
  } else {
    log('[WEB] service workers unavailable — the device web UI bridge is disabled', true);
  }

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
    onDeviceServer: function (running) {
      serverRunning = !!running;
      stServer.textContent = serverRunning ? 'running' : 'stopped';
      if (serverRunning) {
        toast('Device web server started — use "Device web UI" in the toolbar.');
      }
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

  applyDeviceRotation();
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
