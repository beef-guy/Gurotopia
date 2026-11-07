'use strict';
// Keeps the dashboard's network tiles and load meters live.
(function () {
  const root = document.getElementById('dash');
  if (!root) return;
  const f = window.psFmt;
  const $ = (k) => document.querySelector(`[data-k="${k}"]`);
  const set = (k, text) => { const el = $(k); if (el) el.textContent = text; };
  const bar = (k, value, max) => { const el = $(k); if (el) { if (max) el.max = max; el.value = value || 0; } };
  const withTotal = (k, used, total) => {
    const el = $(k);
    if (!el) return;
    const small = document.createElement('small');
    small.textContent = ` / ${f.bytes(total)}`;
    el.replaceChildren(document.createTextNode(f.bytes(used)), small);
  };

  async function refresh() {
    if (document.hidden) return;
    try {
      const res = await fetch('/dashboard.json', { credentials: 'same-origin' });
      if (!res.ok) return;
      const { platform, host } = await res.json();
      set('online', platform.online);
      set('total', platform.total);
      set('players', platform.players);
      set('topName', platform.top ? platform.top.name : 'Nobody yet');
      set('topFoot', platform.top ? `${platform.top.online} players right now` : 'The first server with players shows up here');
      set('cpu', f.percent(host.cpu));
      bar('cpuBar', host.cpu, 100);
      withTotal('mem', host.memory.used, host.memory.total);
      bar('memBar', host.memory.used, host.memory.total);
      if (host.disk) { withTotal('disk', host.disk.used, host.disk.total); bar('diskBar', host.disk.used, host.disk.total); }
      set('uptime', f.duration(host.uptimeSeconds));
      set('rx', host.rxPerSec === null ? '–' : `${f.bytes(host.rxPerSec)}/s`);
      set('tx', host.txPerSec === null ? '–' : `${f.bytes(host.txPerSec)}/s`);
    } catch { /* try again next tick */ }
  }
  setInterval(refresh, 5000);
  document.addEventListener('visibilitychange', refresh);
})();
