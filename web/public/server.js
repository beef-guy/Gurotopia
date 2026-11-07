'use strict';
// A server's pages: keeps the status, resources, console and graphs up to date.
(function () {
  const page = document.getElementById('server-page');
  if (!page) return;
  const id = page.dataset.server;
  const f = window.psFmt;
  const $ = (k) => document.querySelector(`[data-k="${k}"]`);
  const set = (k, text) => { const el = $(k); if (el) el.textContent = text; };

  /* ---------- status ---------- */
  let submitting = false;
  const badge = document.getElementById('status-badge');
  const detail = document.getElementById('status-detail');
  const online = document.getElementById('online');
  const uptime = document.getElementById('uptime');
  const icon = document.querySelector('.side-head .srv-icon');

  async function refreshStatus() {
    try {
      const res = await fetch(`/servers/${id}/status.json`, { credentials: 'same-origin' });
      if (!res.ok) return;
      const body = await res.json();
      const span = document.createElement('span');
      span.className = `badge ${body.view.key}`;
      const dot = document.createElement('span');
      dot.className = 'dot';
      span.append(dot, body.view.label);
      if (badge) badge.replaceChildren(span);
      if (icon) icon.className = `srv-icon ${body.view.key}`;
      if (detail && (body.view.detail || !detail.dataset.keep)) detail.textContent = body.view.detail || detail.dataset.idle || '';
      if (online) online.textContent = String(body.online);
      set('players', body.online);
      const pb = $('playersBar');
      if (pb) pb.value = body.online;
      set('cpu', f.percent(body.cpu));
      const cb = $('cpuBar');
      if (cb) cb.value = body.cpu || 0;
      set('mem', f.bytes(body.mem));
      if (!submitting) {
        const up = ['online', 'starting', 'busy'].includes(body.view.key);
        for (const b of document.querySelectorAll('.controls button')) {
          b.disabled = b.dataset.action === 'start' ? up : body.view.key === 'offline';
        }
      }
      if (uptime) {
        const running = body.view.key === 'online' || body.view.key === 'starting';
        uptime.dataset.started = running && body.startedAt ? body.startedAt : '';
        tickUptime();
      }
    } catch { /* try again next tick */ }
  }

  function tickUptime() {
    if (!uptime) return;
    const started = uptime.dataset.started;
    uptime.textContent = started ? f.duration((Date.now() - new Date(started).getTime()) / 1000) : '–';
  }

  if (detail) detail.dataset.idle = detail.textContent;

  /* ---------- console ---------- */
  const log = document.getElementById('log');
  const follow = document.getElementById('follow');
  const filter = document.getElementById('log-filter');
  let fullLog = '';

  function showLog() {
    const q = filter ? filter.value.trim().toLowerCase() : '';
    const text = q ? fullLog.split('\n').filter((l) => l.toLowerCase().includes(q)).join('\n') : fullLog;
    if (text === log.textContent) return;
    const atBottom = log.scrollHeight - log.scrollTop - log.clientHeight < 40;
    log.textContent = text || (q ? '(no lines match)' : '');
    if (follow.checked && (atBottom || log.dataset.first !== 'no')) log.scrollTop = log.scrollHeight;
    log.dataset.first = 'no';
  }

  async function refreshLog() {
    if (!log) return;
    try {
      const res = await fetch(`/servers/${id}/logs.json`, { credentials: 'same-origin' });
      const body = await res.json();
      fullLog = body.error ? `(${body.error})` : (body.text || '(nothing in the log yet)');
      showLog();
    } catch { /* try again next tick */ }
  }
  if (filter) filter.addEventListener('input', showLog);

  /* ---------- graphs ---------- */
  const charts = [...document.querySelectorAll('.chart')];
  let minutes = 30;
  const SVG = 'http://www.w3.org/2000/svg';
  const el = (name, attrs, parent) => {
    const node = document.createElementNS(SVG, name);
    for (const [k, v] of Object.entries(attrs || {})) node.setAttribute(k, v);
    if (parent) parent.appendChild(node);
    return node;
  };
  const clock = (t) => new Date(t).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  const fmtValue = (unit, v) => {
    if (v === null || v === undefined) return '–';
    if (unit === 'bytes') return f.bytes(v);
    if (unit === 'percent') return f.percent(v);
    return String(Math.round(v));
  };

  function niceMax(max, unit) {
    if (unit === 'percent') return Math.max(10, Math.ceil(max / 10) * 10);
    if (unit === 'players') return Math.max(5, Math.ceil(max / 5) * 5);
    const mb = 1024 * 1024;
    const step = max > 512 * mb ? 256 * mb : max > 128 * mb ? 64 * mb : 16 * mb;
    return Math.max(step, Math.ceil(max / step) * step);
  }

  function draw(box, samples, from, to) {
    const metric = box.dataset.metric;
    const unit = box.dataset.unit;
    const W = Math.max(box.clientWidth, 200);
    const H = box.clientHeight;
    const pad = { l: 58, r: 10, t: 10, b: 24 };
    const iw = W - pad.l - pad.r;
    const ih = H - pad.t - pad.b;
    const values = samples.map((s) => s[metric]);
    const known = values.filter((v) => v !== null && v !== undefined);
    const top = niceMax(known.length ? Math.max(...known) : 0, unit);
    const x = (t) => pad.l + ((t - from) / (to - from)) * iw;
    const y = (v) => pad.t + ih - (v / top) * ih;

    const svg = el('svg', { viewBox: `0 0 ${W} ${H}`, role: 'img', 'aria-label': `${box.closest('.card').querySelector('h2').textContent}, last ${minutes} minutes` });
    const defs = el('defs', {}, svg);
    const grad = el('linearGradient', { id: 'chart-fill', x1: 0, y1: 0, x2: 0, y2: 1 }, defs);
    el('stop', { offset: '0', 'stop-color': '#3ddc84', 'stop-opacity': '0.32' }, grad);
    el('stop', { offset: '1', 'stop-color': '#3ddc84', 'stop-opacity': '0.02' }, grad);

    const grid = el('g', { class: 'grid' }, svg);
    const axis = el('g', { class: 'axis' }, svg);
    for (let i = 0; i <= 4; i++) {
      const v = (top / 4) * i;
      el('line', { x1: pad.l, x2: W - pad.r, y1: y(v), y2: y(v) }, grid);
      const label = el('text', { x: pad.l - 8, y: y(v) + 4, 'text-anchor': 'end' }, axis);
      label.textContent = fmtValue(unit, v);
    }
    const ticks = W < 420 ? 3 : 5;
    for (let i = 0; i <= ticks; i++) {
      const t = from + ((to - from) / ticks) * i;
      const label = el('text', { x: x(t), y: H - 6, 'text-anchor': i === 0 ? 'start' : i === ticks ? 'end' : 'middle' }, axis);
      label.textContent = clock(t);
    }

    // @note one path per unbroken run, so a stopped server shows a gap instead of a fake line
    const runs = [];
    let run = [];
    for (const s of samples) {
      const v = s[metric];
      if (v === null || v === undefined) { if (run.length) runs.push(run); run = []; } else run.push([x(s.t), y(v)]);
    }
    if (run.length) runs.push(run);
    for (const r of runs) {
      const line = r.map(([px, py], i) => `${i ? 'L' : 'M'}${px.toFixed(1)},${py.toFixed(1)}`).join('');
      if (r.length > 1) el('path', { class: 'area', d: `${line}L${r[r.length - 1][0].toFixed(1)},${y(0)}L${r[0][0].toFixed(1)},${y(0)}Z` }, svg);
      el('path', { class: 'line', d: r.length > 1 ? line : `${line}l0.1,0` }, svg);
    }
    if (!known.length) {
      const msg = el('text', { class: 'empty-msg', x: pad.l + iw / 2, y: pad.t + ih / 2, 'text-anchor': 'middle' }, svg);
      msg.textContent = 'No data yet. Graphs fill in while the server runs.';
    }

    // @note hover: crosshair, dot and a tooltip for the nearest sample
    const cross = el('line', { class: 'cross', y1: pad.t, y2: pad.t + ih, visibility: 'hidden' }, svg);
    const dot = el('circle', { class: 'hover-dot', r: 4.5, visibility: 'hidden' }, svg);
    const tip = document.createElement('div');
    tip.className = 'chart-tip';
    tip.hidden = true;
    const hit = el('rect', { x: pad.l, y: 0, width: iw, height: H, fill: 'transparent' }, svg);
    const hide = () => { cross.setAttribute('visibility', 'hidden'); dot.setAttribute('visibility', 'hidden'); tip.hidden = true; };
    hit.addEventListener('pointermove', (e) => {
      if (!samples.length) return;
      const rect = svg.getBoundingClientRect();
      const px = ((e.clientX - rect.left) / rect.width) * W;
      const t = from + ((px - pad.l) / iw) * (to - from);
      let best = samples[0];
      for (const s of samples) if (Math.abs(s.t - t) < Math.abs(best.t - t)) best = s;
      const v = best[metric];
      const bx = x(best.t);
      cross.setAttribute('x1', bx); cross.setAttribute('x2', bx); cross.setAttribute('visibility', 'visible');
      if (v === null || v === undefined) dot.setAttribute('visibility', 'hidden');
      else { dot.setAttribute('cx', bx); dot.setAttribute('cy', y(v)); dot.setAttribute('visibility', 'visible'); }
      const b = document.createElement('b');
      b.textContent = v === null || v === undefined ? 'Offline' : fmtValue(unit, v);
      const when = document.createElement('span');
      when.textContent = new Date(best.t).toLocaleTimeString();
      tip.replaceChildren(b, when);
      tip.hidden = false;
      tip.style.left = `${(bx / W) * 100}%`;
      tip.style.top = `${((v === null || v === undefined ? pad.t + ih / 2 : y(v)) / H) * 100}%`;
    });
    hit.addEventListener('pointerleave', hide);

    box.replaceChildren(svg, tip);

    const latest = [...samples].reverse().find((s) => s[metric] !== null && s[metric] !== undefined);
    const now = document.querySelector(`[data-now="${metric}"]`);
    if (now) now.textContent = latest && samples[samples.length - 1] === latest ? fmtValue(unit, latest[metric]) : '–';
  }

  let lastMetrics = null;
  async function refreshCharts() {
    if (!charts.length) return;
    try {
      const res = await fetch(`/servers/${id}/metrics.json?minutes=${minutes}`, { credentials: 'same-origin' });
      const body = await res.json();
      lastMetrics = { samples: body.samples || [], to: Date.now(), minutes };
      renderCharts();
      if (body.storage) {
        set('dbSize', f.bytes(body.storage.database));
        set('fileSize', f.bytes(body.storage.files));
      }
    } catch { /* try again next tick */ }
  }

  function renderCharts() {
    if (!lastMetrics) return;
    const to = lastMetrics.to;
    const from = to - lastMetrics.minutes * 60 * 1000;
    for (const box of charts) draw(box, lastMetrics.samples, from, to);
  }

  const range = document.getElementById('range');
  if (range) {
    range.addEventListener('click', (e) => {
      const btn = e.target.closest('button[data-minutes]');
      if (!btn) return;
      minutes = Number(btn.dataset.minutes);
      for (const b of range.querySelectorAll('button')) b.classList.toggle('on', b === btn);
      refreshCharts();
    });
  }
  let resizeTimer = null;
  window.addEventListener('resize', () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(renderCharts, 150); });

  /* ---------- loop ---------- */
  let ticks = 0;
  function tick() {
    if (document.hidden) return;
    refreshStatus();
    refreshLog();
    if (ticks++ % 3 === 0) refreshCharts(); // @note samples arrive every 10 s, no need to ask more often
  }
  tick();
  setInterval(tick, 3000);
  setInterval(tickUptime, 30000);
  document.addEventListener('visibilitychange', tick);

  // Stop and restart can take up to 30 seconds while the server saves.
  for (const form of document.querySelectorAll('.controls form')) {
    form.addEventListener('submit', () => {
      submitting = true;
      for (const b of document.querySelectorAll('.controls button')) b.disabled = true;
      const btn = form.querySelector('button');
      const label = btn.querySelector('span') || btn;
      label.textContent = btn.dataset.action === 'start' ? 'Starting…' : 'Saving…';
    });
  }
})();
