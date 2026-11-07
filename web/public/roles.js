'use strict';
// The role editor: live name preview, the selected-commands count and "Select all" per group.
(function () {
  const form = document.getElementById('role-form');
  if (!form) return;
  const preview = document.getElementById('preview');
  const tagOut = preview && preview.querySelector('[data-p="tag"]');
  const tag = document.getElementById('tag');
  const count = document.getElementById('cmd-count');

  function paint() {
    const picked = form.querySelector('input[name="color"]:checked');
    if (preview && picked) preview.className = `name-preview gt-${picked.dataset.key}`;
    if (tagOut && tag) tagOut.textContent = tag.value.trim();
    for (const p of form.querySelectorAll('.power')) p.classList.toggle('on', p.querySelector('input').checked);
  }

  function counted() {
    if (!count || form.querySelector('fieldset.cmd-group[disabled]')) return;
    count.textContent = `${form.querySelectorAll('input[name="commands"]:checked').length} selected`;
    for (const g of form.querySelectorAll('.cmd-group')) {
      const btn = g.querySelector('[data-all]');
      if (!btn) continue;
      const boxes = [...g.querySelectorAll('input[name="commands"]')];
      btn.textContent = boxes.every((b) => b.checked) ? 'Clear all' : 'Select all';
    }
  }

  form.addEventListener('input', () => { paint(); counted(); });
  form.addEventListener('change', () => { paint(); counted(); });
  form.addEventListener('click', (e) => {
    const btn = e.target.closest('[data-all]');
    if (!btn) return;
    const boxes = [...btn.closest('.cmd-group').querySelectorAll('input[name="commands"]')];
    const on = !boxes.every((b) => b.checked);
    for (const b of boxes) b.checked = on;
    counted();
  });
  paint();
  counted();
})();
