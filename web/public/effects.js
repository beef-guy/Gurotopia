'use strict';
// The item effect editor: finding the item, and lighting up the effects that are set.
(function () {
  const form = document.getElementById('effect-form');
  if (!form) return;
  const server = form.dataset.server;
  const $ = (id) => document.getElementById(id);

  function paint() {
    for (const p of form.querySelectorAll('.power')) {
      const input = p.querySelector('input');
      p.classList.toggle('on', input.type === 'checkbox' ? input.checked : Number(input.value) > 0);
    }
  }
  form.addEventListener('input', paint);
  form.addEventListener('change', paint);
  paint();

  const search = $('item-search');
  if (!search) return; // @note editing: the item is fixed
  const results = $('item-results'); const itemId = $('f-item');
  const chosen = $('item-chosen'); const chosenImg = $('item-chosen-img');
  const chosenName = $('item-chosen-name'); const chosenMeta = $('item-chosen-meta');
  let seq = 0; let timer = null;

  function choose(it) {
    itemId.value = String(it.id);
    search.value = it.name;
    chosenImg.src = `/servers/${server}/items/${it.id}/icon.png`;
    chosenName.textContent = it.name;
    chosenMeta.textContent = `#${it.id} · ${it.type}`;
    chosen.hidden = false;
    results.replaceChildren();
  }

  search.addEventListener('input', () => {
    clearTimeout(timer);
    const q = search.value.trim();
    if (q.length < 2 && !/^#?[0-9]+$/.test(q)) { results.replaceChildren(); return; }
    timer = setTimeout(async () => {
      const mine = ++seq;
      try {
        const res = await fetch(`/servers/${server}/items/search.json?custom=1&seeds=0&q=${encodeURIComponent(q)}`, { credentials: 'same-origin' });
        const body = await res.json();
        if (mine !== seq) return;
        const found = body.items.filter((it) => it.type !== 'Seed');
        results.replaceChildren(...found.map((it) => {
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
        if (!found.length) {
          const li = document.createElement('li'); li.className = 'muted small'; li.textContent = 'No items match.';
          results.replaceChildren(li);
        }
      } catch { /* try again on the next key */ }
    }, 200);
  });
  search.addEventListener('keydown', (e) => { if (e.key === 'Enter') e.preventDefault(); });
})();
