// Service worker: makes the firmware's web server reachable in the browser.
//
// Fetches under <scope>device/ are forwarded to the simulator tab, which
// injects them into the REAL CrossPointWebServer handlers inside the wasm
// module and returns the response — so <scope>device/files IS the device's
// File Manager page, served by firmware code.
//
// The real pages address the server with absolute paths ("/api/status",
// "/files", "/js/jszip.min.js"). Those fall outside this worker's scope, but
// interception is per-CLIENT, not per-URL: any fetch made by a page this
// worker controls is dispatched here. Requests coming FROM a device page are
// therefore proxied too, and absolute-path navigations (link clicks) are
// redirected back under <scope>device/.
'use strict';

let simClientId = null;

self.addEventListener('install', function () {
  self.skipWaiting();
});
self.addEventListener('activate', function (e) {
  e.waitUntil(self.clients.claim());
});

self.addEventListener('message', function (e) {
  if (e.data && e.data.type === 'sim-ready') {
    simClientId = e.source && e.source.id;
  }
});

async function findSimClient() {
  if (simClientId) {
    const c = await self.clients.get(simClientId);
    if (c) return c;
  }
  const all = await self.clients.matchAll({ type: 'window' });
  return all.find(c => !new URL(c.url).pathname.includes('/device')) || null;
}

async function proxyToSim(request, devicePath) {
  const client = await findSimClient();
  if (!client) {
    return new Response('The simulator tab is not running. Open the simulator first, then retry.', {
      status: 503, headers: { 'Content-Type': 'text/plain' }
    });
  }
  const body = request.method === 'GET' || request.method === 'HEAD' ? null : await request.arrayBuffer();
  const headers = [];
  for (const [k, v] of request.headers.entries()) headers.push(k, v);

  const reply = await new Promise(function (resolve) {
    const ch = new MessageChannel();
    const timer = setTimeout(function () { resolve(null); }, 30000);
    ch.port1.onmessage = function (m) { clearTimeout(timer); resolve(m.data); };
    client.postMessage(
      { type: 'device-http', method: request.method, path: devicePath, headers: headers, body: body },
      body ? [ch.port2, body] : [ch.port2]
    );
  });

  if (!reply || !reply.status) {
    return new Response(
      'The device web server is not running.\nOn the simulated device: Home -> File Transfer -> any mode.',
      { status: 503, headers: { 'Content-Type': 'text/plain' } }
    );
  }
  const respHeaders = new Headers();
  let gzipped = false;
  for (let i = 0; i + 1 < reply.headers.length; i += 2) {
    if (reply.headers[i].toLowerCase() === 'content-encoding' && reply.headers[i + 1].includes('gzip')) {
      gzipped = true;  // synthesized responses bypass network-layer decoding
      continue;
    }
    respHeaders.append(reply.headers[i], reply.headers[i + 1]);
  }
  let respBody = reply.body;
  if (gzipped && respBody && respBody.byteLength) {
    respBody = await new Response(
      new Blob([respBody]).stream().pipeThrough(new DecompressionStream('gzip'))
    ).arrayBuffer();
  }
  return new Response(respBody, { status: reply.status, headers: respHeaders });
}

self.addEventListener('fetch', function (event) {
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin) return;
  const scopePath = new URL(self.registration.scope).pathname;
  // Path relative to the scope, or null when the URL sits outside the scope
  // (server-absolute paths like /api/... when hosted under a project path).
  const rel = url.pathname.startsWith(scopePath) ? url.pathname.slice(scopePath.length) : null;

  // Direct hits on the device UI mount point.
  if (rel === 'device' || (rel !== null && rel.startsWith('device/'))) {
    let devicePath = rel === 'device' ? '/' : rel.slice('device'.length);
    if (devicePath === '') devicePath = '/';
    event.respondWith(proxyToSim(event.request, devicePath + url.search));
    return;
  }

  // The real pages link with server-absolute paths ("/files", "/api/...").
  // Navigations from a device page get redirected back under device/;
  // subresource fetches from a device page get proxied in place.
  const absolutePath = rel !== null ? '/' + rel : url.pathname;
  if (event.request.mode === 'navigate') {
    if (event.request.referrer && event.request.referrer.includes('/device')) {
      event.respondWith(Response.redirect(scopePath + 'device' + absolutePath + url.search, 302));
    }
    return;
  }
  if (!event.clientId) return;
  event.respondWith((async function () {
    const client = await self.clients.get(event.clientId);
    if (client && new URL(client.url).pathname.includes('/device')) {
      return proxyToSim(event.request, absolutePath + url.search);
    }
    return fetch(event.request);
  })());
});
