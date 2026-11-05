'use strict';
// Keeps the status badge, player count and log up to date on a server's page.
(function () {
  const log = document.getElementById('log');
  if (!log) return;
  const id = log.dataset.server;
  const follow = document.getElementById('follow');
  const badge = document.getElementById('status-badge');
  const detail = document.getElementById('status-detail');
  const online = document.getElementById('online');

  async function refreshLog() {
    try {
      const res = await fetch(`/servers/${id}/logs.json`, { credentials: 'same-origin' });
      const body = await res.json();
      const text = body.error ? `(${body.error})` : (body.text || '(nothing in the log yet)');
      if (text !== log.textContent) {
        const atBottom = log.scrollHeight - log.scrollTop - log.clientHeight < 40;
        log.textContent = text;
        if (follow.checked && (atBottom || log.dataset.first !== 'no')) log.scrollTop = log.scrollHeight;
        log.dataset.first = 'no';
      }
    } catch { /* try again next tick */ }
  }

  async function refreshStatus() {
    try {
      const res = await fetch(`/servers/${id}/status.json`, { credentials: 'same-origin' });
      if (!res.ok) return;
      const body = await res.json();
      const span = document.createElement('span');
      span.className = `badge ${body.view.key}`;
      span.textContent = body.view.label;
      badge.replaceChildren(span);
      detail.textContent = body.view.detail || '';
      online.textContent = String(body.online);
    } catch { /* try again next tick */ }
  }

  function tick() {
    if (document.hidden) return;
    refreshLog();
    refreshStatus();
  }
  tick();
  setInterval(tick, 3000);
  document.addEventListener('visibilitychange', tick);

  // Stop and restart can take up to 30 seconds while the server saves.
  for (const form of document.querySelectorAll('.controls form')) {
    form.addEventListener('submit', () => {
      for (const b of document.querySelectorAll('.controls button')) b.disabled = true;
      const btn = form.querySelector('button');
      btn.textContent = btn.dataset.action === 'start' ? 'Starting…' : 'Saving and stopping…';
    });
  }
})();
