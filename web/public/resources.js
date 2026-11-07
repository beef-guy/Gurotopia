'use strict';
// Resources: read the picked file in the browser and send it with the form, and the sheet's cell picker.
(function () {
  const form = document.getElementById('upload-form');
  if (form) {
    const file = document.getElementById('up-file');
    const drop = document.getElementById('drop');
    const send = document.getElementById('up-send');
    const title = document.getElementById('drop-title');
    function take(f) {
      send.disabled = true;
      if (!f) return;
      if (f.size > 2 * 1024 * 1024) { title.textContent = `${f.name} is bigger than 2 MB.`; return; }
      title.textContent = `Reading ${f.name}…`;
      const reader = new FileReader();
      reader.onload = () => {
        const url = String(reader.result);
        document.getElementById('up-data').value = url.slice(url.indexOf(',') + 1);
        document.getElementById('up-name').value = f.name;
        title.textContent = `${f.name} · ${Math.max(1, Math.round(f.size / 1024))} KB`;
        send.disabled = false;
      };
      reader.onerror = () => { title.textContent = 'That file could not be read.'; };
      reader.readAsDataURL(f);
    }
    file.addEventListener('change', () => take(file.files[0]));
    for (const ev of ['dragenter', 'dragover']) drop.addEventListener(ev, () => drop.classList.add('over'));
    for (const ev of ['dragleave', 'drop']) drop.addEventListener(ev, () => drop.classList.remove('over'));
    form.addEventListener('submit', () => { send.disabled = true; send.textContent = 'Uploading…'; });
  }

  const view = document.getElementById('res-view');
  if (view) {
    const w = Number(view.dataset.w); const h = Number(view.dataset.h);
    const img = document.getElementById('sheet-img');
    const room = img.closest('.sheet-wrap').clientWidth - 2;
    const scale = Math.max(1, Math.min(w <= 128 && h <= 128 ? 4 : w <= 512 ? 2 : 1, Math.floor(room / w))); // @note fits a phone
    const cell = document.getElementById('sheet-cell');
    img.style.width = `${w * scale}px`;
    img.style.height = `${h * scale}px`;
    function pick(cx, cy) {
      cell.hidden = false;
      cell.style.left = `${cx * 32 * scale}px`; cell.style.top = `${cy * 32 * scale}px`;
      cell.style.width = `${32 * scale}px`; cell.style.height = `${32 * scale}px`;
      document.getElementById('use-x').textContent = String(cx);
      document.getElementById('use-y').textContent = String(cy);
    }
    img.addEventListener('click', (e) => {
      const r = img.getBoundingClientRect();
      const cx = Math.floor((e.clientX - r.left) / scale / 32); const cy = Math.floor((e.clientY - r.top) / scale / 32);
      if (cx >= 0 && cy >= 0 && (cx + 1) * 32 <= w && (cy + 1) * 32 <= h) pick(cx, cy);
    });
    if (w >= 32 && h >= 32) pick(0, 0);
    const copy = document.getElementById('use-copy');
    copy.addEventListener('click', async () => {
      try { await navigator.clipboard.writeText(view.dataset.file); copy.lastChild.textContent = 'Copied'; } catch { copy.lastChild.textContent = 'Copy failed'; }
      setTimeout(() => { copy.lastChild.textContent = 'Copy name'; }, 1500);
    });
    document.getElementById('del-form').addEventListener('submit', (e) => {
      if (!window.confirm('Delete this texture? Items that use it show a blank square.')) e.preventDefault();
    });
  }
})();
