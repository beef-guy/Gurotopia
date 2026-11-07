'use strict';
// The title editor: live name preview, the icon picker (an item's picture, an upload or a game file) and the "who has it" fields.
(function () {
  const form = document.getElementById('title-form');
  if (!form) return;
  const server = form.dataset.server;
  const $ = (id) => document.getElementById(id);
  const plain = (s) => s.replace(/`./g, '');

  const name = $('p-name'); const badge = $('p-badge');
  const icon = $('p-icon'); const iconImg = $('p-icon-img');
  const prefix = $('f-prefix'); const suffix = $('f-suffix');
  const tex = $('f-texture'); const x = $('f-x'); const y = $('f-y');
  const itemId = $('f-icon-item');
  let itemIcon = itemId.value ? `/servers/${server}/items/${itemId.value}/icon.png` : '';

  function mode() { return (form.querySelector('input[name="icon"]:checked') || { value: 'none' }).value; }

  function paint() {
    const color = form.querySelector('input[name="color"]:checked');
    name.className = `name-preview gt-${color ? color.dataset.key : 'plain'}`;
    name.querySelector('[data-p="prefix"]').textContent = plain(prefix.value);
    name.querySelector('[data-p="suffix"]').textContent = plain(suffix.value);
    const b = form.querySelector('input[name="badge"]:checked');
    badge.hidden = !b || !b.value;
    if (b) badge.textContent = b.dataset.label;
    for (const p of form.querySelectorAll('.power')) p.classList.toggle('on', p.querySelector('input').checked);
    const m = mode();
    for (const pane of form.querySelectorAll('.icon-pane')) pane.hidden = pane.dataset.pane !== m;
    const grant = (form.querySelector('input[name="grant"]:checked') || { value: 'manual' }).value;
    for (const f of form.querySelectorAll('[data-grant]')) f.hidden = f.dataset.grant !== grant;
    showIcon();
  }

  let timer = null;
  function showIcon() {
    clearTimeout(timer);
    timer = setTimeout(() => {
      const m = mode();
      let src = '';
      if (m === 'item') src = itemIcon;
      if (m === 'texture' && tex.value.trim()) src = `/servers/${server}/items/texture.png?${new URLSearchParams({ file: tex.value.trim(), x: x.value || '0', y: y.value || '0' })}`;
      icon.hidden = !src;
      if (src && iconImg.getAttribute('src') !== src) iconImg.src = src;
      cell();
    }, 150);
  }

  /* ---------- texture picker: your uploads, or a game file ---------- */
  const source = $('tex-source'); const gameTex = $('game-tex'); const pick = $('sheet-pick');
  const pickImg = $('pick-img'); const pickCell = $('pick-cell');
  let scale = 1;
  function sheet() {
    const w = Number(pickImg.dataset.w); const h = Number(pickImg.dataset.h);
    if (!w || !h) return;
    const room = pickImg.closest('.sheet-wrap').clientWidth - 2;
    scale = Math.max(1, Math.min(w <= 128 && h <= 128 ? 4 : w <= 512 ? 2 : 1, Math.floor(room / w) || 1));
    pickImg.style.width = `${w * scale}px`; pickImg.style.height = `${h * scale}px`;
    cell();
  }
  function cell() {
    if (!pickCell || pick.hidden || pick.closest('.icon-pane').hidden) return;
    pickCell.hidden = false;
    pickCell.style.left = `${(Number(x.value) || 0) * 32 * scale}px`; pickCell.style.top = `${(Number(y.value) || 0) * 32 * scale}px`;
    pickCell.style.width = `${32 * scale}px`; pickCell.style.height = `${32 * scale}px`;
  }
  source.addEventListener('change', () => {
    const opt = source.selectedOptions[0];
    const own = Boolean(source.value);
    gameTex.hidden = own;
    pick.hidden = !own;
    if (own) {
      tex.value = source.value;
      pickImg.dataset.w = opt.dataset.w; pickImg.dataset.h = opt.dataset.h;
      pickImg.src = `/servers/${server}/resources/${encodeURIComponent(source.value)}/sheet.png`;
      x.value = '0'; y.value = '0';
      sheet();
    } else {
      tex.value = '';
      tex.focus();
    }
    showIcon();
  });
  pickImg.addEventListener('click', (e) => {
    const r = pickImg.getBoundingClientRect();
    const cx = Math.floor((e.clientX - r.left) / scale / 32); const cy = Math.floor((e.clientY - r.top) / scale / 32);
    if (cx < 0 || cy < 0 || (cx + 1) * 32 > Number(pickImg.dataset.w) || (cy + 1) * 32 > Number(pickImg.dataset.h)) return;
    x.value = String(cx); y.value = String(cy);
    showIcon();
  });

  /* ---------- an item's picture ---------- */
  const search = $('item-search'); const results = $('item-results'); const chosen = $('item-chosen');
  let seq = 0; let searchTimer = null;
  function choose(it) {
    itemId.value = String(it.id);
    search.value = it.name;
    chosen.textContent = `Using #${it.id} ${it.name}.`;
    itemIcon = `/servers/${server}/items/${it.id}/icon.png`;
    results.replaceChildren();
    showIcon();
  }
  search.addEventListener('input', () => {
    clearTimeout(searchTimer);
    const q = search.value.trim();
    if (q.length < 2) { results.replaceChildren(); return; }
    searchTimer = setTimeout(async () => {
      const mine = ++seq;
      try {
        const res = await fetch(`/servers/${server}/items/search.json?q=${encodeURIComponent(q)}`, { credentials: 'same-origin' });
        const body = await res.json();
        if (mine !== seq) return;
        results.replaceChildren(...body.items.map((it) => {
          const li = document.createElement('li');
          const btn = document.createElement('button');
          btn.type = 'button';
          btn.className = 'ghost';
          const img = document.createElement('img');
          img.src = `/servers/${server}/items/${it.id}/icon.png`; img.width = 24; img.height = 24; img.alt = ''; img.style.imageRendering = 'pixelated';
          const b = document.createElement('strong'); b.textContent = it.name;
          const s = document.createElement('span'); s.className = 'muted small'; s.textContent = `#${it.id} · ${it.type}`;
          btn.append(img, b, s);
          btn.addEventListener('click', () => choose(it));
          li.append(btn);
          return li;
        }));
        if (!body.items.length) {
          const li = document.createElement('li'); li.className = 'muted small'; li.textContent = 'No items match.';
          results.replaceChildren(li);
        }
      } catch { /* try again on the next key */ }
    }, 200);
  });
  search.addEventListener('keydown', (e) => { if (e.key === 'Enter') e.preventDefault(); });

  form.addEventListener('input', paint);
  form.addEventListener('change', paint);
  if (!pick.hidden) sheet();
  paint();
})();
