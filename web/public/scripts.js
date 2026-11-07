'use strict';
// Lua scripts pages: the script log, delete confirmations, the New script form, and saving for both editors.
(function () {
  const page = document.getElementById('server-page');
  if (!page) return;
  const serverId = page.dataset.server;

  /* ---------- confirm before deleting ---------- */
  for (const form of document.querySelectorAll('form[data-confirm]')) {
    form.addEventListener('submit', (e) => { if (!window.confirm(form.dataset.confirm)) e.preventDefault(); });
  }

  /* ---------- New script: the template list is only for code ---------- */
  const newForm = document.getElementById('new-script');
  if (newForm) {
    const pick = document.getElementById('template-pick');
    const sync = () => {
      const code = newForm.querySelector('input[name="mode"]:checked')?.value === 'code';
      pick.classList.toggle('dim', !code);
      pick.querySelector('select').disabled = !code;
    };
    newForm.addEventListener('change', sync);
    sync();
  }

  /* ---------- the script log ---------- */
  const logEl = document.getElementById('script-log');
  let lastId = 0;
  let first = true;
  function time(at) {
    const d = new Date(at);
    return Number.isNaN(d.getTime()) ? '' : d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  }
  async function pollLog() {
    if (!logEl) return;
    try {
      const q = new URLSearchParams({ after: String(lastId) });
      if (logEl.dataset.script) q.set('script', logEl.dataset.script);
      const res = await fetch(`/servers/${serverId}/scripts/logs.json?${q}`, { credentials: 'same-origin' });
      if (res.ok) {
        const body = await res.json();
        if (first) logEl.textContent = '';
        const atBottom = logEl.scrollHeight - logEl.scrollTop - logEl.clientHeight < 40;
        for (const row of body.logs) {
          lastId = Math.max(lastId, row.id);
          const line = document.createElement('div');
          line.className = `log-line ${row.level}`;
          line.textContent = `${time(row.at)}  ${row.script}  ${row.message}`;
          logEl.append(line);
        }
        while (logEl.childElementCount > 400) logEl.firstElementChild.remove();
        if (first && !body.logs.length) {
          logEl.textContent = body.online ? 'Nothing yet. What your scripts print() shows up here, and so do their errors.' : 'The server is offline. The log fills up once it runs.';
          logEl.dataset.empty = '1';
        } else if (body.logs.length && logEl.dataset.empty) {
          delete logEl.dataset.empty;
          for (const node of [...logEl.childNodes]) if (node.nodeType === Node.TEXT_NODE) node.remove();
        }
        if (atBottom || first) logEl.scrollTop = logEl.scrollHeight;
        first = false;
      }
    } catch { /* @note try again next time */ }
    setTimeout(pollLog, document.hidden ? 8000 : 2500);
  }
  pollLog();

  /* ---------- saving (used by script-code.js and script-blocks.js) ---------- */
  const form = document.getElementById('script-form');
  const state = document.getElementById('save-state');
  const saveBtn = document.getElementById('save');
  const nameInput = document.getElementById('script-name');
  let dirty = false;

  function say(text, kind = '') {
    if (!state) return;
    state.textContent = text;
    state.className = `small ${kind || 'muted'}`;
  }

  async function save(fields) {
    if (!form) return false;
    const body = new URLSearchParams();
    body.set('_csrf', form.querySelector('input[name="_csrf"]').value);
    if (nameInput) body.set('name', nameInput.value);
    for (const [k, val] of Object.entries(fields)) body.set(k, val);
    if (saveBtn) saveBtn.disabled = true;
    say('Saving…');
    try {
      const res = await fetch(form.action, {
        method: 'POST', credentials: 'same-origin', body,
        headers: { accept: 'application/json', 'content-type': 'application/x-www-form-urlencoded' },
      });
      const out = await res.json().catch(() => ({ ok: false, error: 'The panel did not answer. Try again.' }));
      if (!out.ok) { say(out.error || 'Not saved.', 'bad-text'); return false; }
      dirty = false;
      if (logEl && nameInput) logEl.dataset.script = nameInput.value.trim().replace(/\s+/g, ' ');
      say(out.running
        ? (out.live ? 'Saved, and loaded into the game. Errors show in the log below.' : 'Saved. The game didn\'t answer in time, so it loads within a few seconds.')
        : 'Saved. The server is offline, so it starts when the server does.', 'ok-text');
      return true;
    } catch {
      say('Not saved: the panel could not be reached. Try again.', 'bad-text');
      return false;
    } finally {
      if (saveBtn) saveBtn.disabled = false;
    }
  }

  function changed() {
    if (!dirty) say('Not saved yet.', 'warn-text');
    dirty = true;
  }
  if (nameInput) nameInput.addEventListener('input', changed);
  window.addEventListener('beforeunload', (e) => { if (dirty) { e.preventDefault(); e.returnValue = ''; } });

  window.psScripts = { save, changed, say, serverId, isDirty: () => dirty };
})();
