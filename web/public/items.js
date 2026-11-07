'use strict';
// The item manager: icons that fall back to a letter, the editor's live preview, and the "copy from" search.
(function () {
  /* ---------- icons: a texture that isn't on the server shows the item's first letter ---------- */
  function mark(img) {
    const box = img.parentElement;
    if (!box || !box.classList.contains('item-ic') || !img.complete) return;
    const ok = img.naturalWidth > 0 && !img.hidden;
    box.classList.toggle('ok', ok);
    box.classList.toggle('none', !ok);
  }
  document.addEventListener('error', (e) => { if (e.target.tagName === 'IMG') mark(e.target); }, true);
  document.addEventListener('load', (e) => { if (e.target.tagName === 'IMG') mark(e.target); }, true);
  for (const img of document.querySelectorAll('.item-ic img')) mark(img);

  /* ---------- editor ---------- */
  const form = document.getElementById('item-form');
  if (form) {
    const server = form.dataset.server;
    const $ = (id) => document.getElementById(id);
    const name = $('f-name'); const info = $('f-info'); const rarity = $('f-rarity');
    const tex = $('f-texture'); const x = $('f-x'); const y = $('f-y'); const img = $('tex-img');
    function paint() {
      $('p-name').textContent = name.value.trim() || '(no name)';
      $('p-info').textContent = info.value.trim() || 'No description.';
      const r = Number(rarity.value);
      $('p-rarity').hidden = !(r > 0 && r < 999);
      $('p-rarity-n').textContent = String(r);
    }
    let timer = null;
    function texture() {
      clearTimeout(timer);
      timer = setTimeout(() => {
        const q = new URLSearchParams({ file: tex.value.trim(), x: x.value || '0', y: y.value || '0' });
        const url = `/servers/${server}/items/texture.png?${q}`;
        img.src = url;
        for (const live of document.querySelectorAll('.live-icon')) live.src = url; // @note the header and "In game" card follow too
        cell();
      }, 200);
    }

    /* ---------- texture picker: your uploads, or a game file ---------- */
    const source = $('tex-source'); const gameTex = $('game-tex'); const pick = $('sheet-pick');
    const pickImg = $('pick-img'); const pickCell = $('pick-cell');
    let scale = 1;
    function sheet() {
      const w = Number(pickImg.dataset.w); const h = Number(pickImg.dataset.h);
      if (!w || !h) return;
      const room = pickImg.closest('.sheet-wrap').clientWidth - 2;
      scale = Math.max(1, Math.min(w <= 128 && h <= 128 ? 4 : w <= 512 ? 2 : 1, Math.floor(room / w)));
      pickImg.style.width = `${w * scale}px`; pickImg.style.height = `${h * scale}px`;
      cell();
    }
    function cell() {
      if (!pickCell || pick.hidden) return;
      const cx = Number(x.value) || 0; const cy = Number(y.value) || 0;
      pickCell.hidden = false;
      pickCell.style.left = `${cx * 32 * scale}px`; pickCell.style.top = `${cy * 32 * scale}px`;
      pickCell.style.width = `${32 * scale}px`; pickCell.style.height = `${32 * scale}px`;
    }
    if (source) {
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
          tex.focus();
        }
        texture();
      });
      pickImg.addEventListener('click', (e) => {
        const r = pickImg.getBoundingClientRect();
        const cx = Math.floor((e.clientX - r.left) / scale / 32); const cy = Math.floor((e.clientY - r.top) / scale / 32);
        if (cx < 0 || cy < 0 || (cx + 1) * 32 > Number(pickImg.dataset.w) || (cy + 1) * 32 > Number(pickImg.dataset.h)) return;
        x.value = String(cx); y.value = String(cy);
        texture();
      });
      if (!pick.hidden) sheet();
    }
    form.addEventListener('input', (e) => {
      if ([name, info, rarity].includes(e.target)) paint();
      if ([tex, x, y].includes(e.target)) texture();
    });
  }

  /* ---------- new item: pick the item to copy ---------- */
  const create = document.getElementById('new-item');
  if (create) {
    const server = create.dataset.server;
    const search = document.getElementById('base-search');
    const results = document.getElementById('base-results');
    const baseId = document.getElementById('base-id');
    const chosen = document.getElementById('base-chosen');
    let timer = null;
    let seq = 0;
    function choose(it) {
      baseId.value = String(it.id);
      search.value = it.name;
      document.getElementById('base-name').textContent = it.name;
      document.getElementById('base-meta').textContent = `#${it.id} · ${it.type}`;
      const img = document.getElementById('base-img');
      img.hidden = false;
      img.src = `/servers/${server}/items/${it.id}/icon.png`;
      chosen.querySelector('.item-ic').dataset.letter = it.name.slice(0, 1).toUpperCase();
      chosen.hidden = false;
      results.replaceChildren();
    }
    search.addEventListener('input', () => {
      clearTimeout(timer);
      const q = search.value.trim();
      if (q.length < 2) { results.replaceChildren(); return; }
      timer = setTimeout(async () => {
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
            const b = document.createElement('strong'); b.textContent = it.name;
            const s = document.createElement('span'); s.className = 'muted small'; s.textContent = `#${it.id} · ${it.type}`;
            btn.append(b, s);
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
  }
})();
