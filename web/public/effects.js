'use strict';
// The item effect editor: finding items (the item itself, and the bonus drop), and lighting up the effects that are set.
(function () {
  const form = document.getElementById('effect-form');
  if (!form) return;
  const server = form.dataset.server;

  function paint() {
    for (const p of form.querySelectorAll('.power')) {
      const input = p.querySelector('input');
      p.classList.toggle('on', input.type === 'checkbox' ? input.checked : Number(input.value) > 0);
    }
  }
  form.addEventListener('input', paint);
  form.addEventListener('change', paint);
  paint();

  /* @note an item search box: type, pick one from the list, its id goes in the hidden input */
  for (const box of form.querySelectorAll('[data-picker]')) {
    const el = (k) => box.querySelector(`[data-pick="${k}"]`);
    const search = el('search'); const results = el('results'); const itemId = el('id');
    const chosen = el('chosen'); const img = el('img'); const nameEl = el('name'); const meta = el('meta'); const clear = el('clear');
    let seq = 0; let timer = null;

    const choose = (it) => {
      itemId.value = String(it.id);
      search.value = it.name;
      img.src = `/servers/${server}/items/${it.id}/icon.png`;
      nameEl.textContent = it.name;
      meta.textContent = `#${it.id} · ${it.type}`;
      chosen.hidden = false;
      results.replaceChildren();
    };
    if (clear) clear.addEventListener('click', () => { itemId.value = ''; search.value = ''; chosen.hidden = true; results.replaceChildren(); });

    search.addEventListener('input', () => {
      clearTimeout(timer);
      const q = search.value.trim();
      if (q.length < 2 && !/^#?[0-9]+$/.test(q)) { results.replaceChildren(); return; }
      timer = setTimeout(async () => {
        const mine = ++seq;
        try {
          const seeds = box.dataset.picker === 'bonus' ? '1' : '0'; // @note a bonus drop can be a seed
          const res = await fetch(`/servers/${server}/items/search.json?custom=1&seeds=${seeds}&q=${encodeURIComponent(q)}`, { credentials: 'same-origin' });
          const body = await res.json();
          if (mine !== seq) return;
          const found = body.items.filter((it) => it.id !== 112 && (seeds === '1' || it.type !== 'Seed'));
          results.replaceChildren(...found.map((it) => {
            const li = document.createElement('li');
            const btn = document.createElement('button');
            btn.type = 'button';
            btn.className = 'ghost';
            const icon = document.createElement('img');
            icon.src = `/servers/${server}/items/${it.id}/icon.png`; icon.width = 24; icon.height = 24; icon.alt = ''; icon.style.imageRendering = 'pixelated';
            const b = document.createElement('strong'); b.textContent = it.name;
            const s = document.createElement('span'); s.className = 'muted small'; s.textContent = `#${it.id} · ${it.type}`;
            btn.append(icon, b, s);
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
  }
})();
