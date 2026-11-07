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
        img.src = `/servers/${server}/items/texture.png?${q}`;
      }, 250);
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
