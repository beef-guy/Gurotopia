'use strict';
// The machine editor: quick capacity picks, lighting up the picked options, and (when creating) filling in
// the copied machine's numbers and showing the magplant settings only for magplants.
(function () {
  const form = document.getElementById('machine-form');
  if (!form) return;
  const capacity = form.querySelector('input[name="capacity"]');
  const magOnly = form.querySelector('[data-magplant-only]');
  const vendingHint = form.querySelector('[data-vending-hint]');

  function paint() {
    for (const p of form.querySelectorAll('.power')) {
      const input = p.querySelector('input');
      p.classList.toggle('on', input.checked);
    }
  }

  for (const b of form.querySelectorAll('[data-cap]')) {
    b.addEventListener('click', () => { capacity.value = b.dataset.cap; capacity.dispatchEvent(new Event('input', { bubbles: true })); });
  }

  /* @note picking the machine to copy fills in its numbers */
  for (const pick of form.querySelectorAll('.base-pick input')) {
    pick.addEventListener('change', () => {
      const card = pick.closest('.base-pick');
      const kind = card.dataset.kind;
      capacity.value = card.dataset.capacity;
      capacity.min = kind === 'vending' ? '0' : '1';
      if (vendingHint) vendingHint.hidden = kind !== 'vending';
      if (magOnly) magOnly.hidden = kind !== 'magplant';
      const accepts = form.querySelector(`input[name="accepts"][value="${card.dataset.accepts}"]`);
      if (accepts) accepts.checked = true;
      const remote = form.querySelector('input[name="remote"]');
      if (remote) remote.checked = card.dataset.remote === '1';
      paint();
    });
  }
  form.addEventListener('change', paint);
  paint();
})();
