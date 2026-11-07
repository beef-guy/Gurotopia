'use strict';
// The Lua code editor: CodeMirror, a syntax check while typing, Insert buttons in the reference, Save (and Ctrl+S).
(function () {
  const area = document.getElementById('source');
  if (!area || !window.CodeMirror || !window.psScripts) return;
  const ps = window.psScripts;

  const editor = window.CodeMirror.fromTextArea(area, {
    mode: 'lua',
    lineNumbers: true,
    indentUnit: 2,
    tabSize: 2,
    indentWithTabs: false,
    matchBrackets: true,
    autoCloseBrackets: true,
    lineWrapping: false,
    viewportMargin: 50,
    extraKeys: {
      Tab: (cm) => (cm.somethingSelected() ? cm.indentSelection('add') : cm.replaceSelection('  ', 'end')),
      'Shift-Tab': (cm) => cm.indentSelection('subtract'),
      'Ctrl-S': () => saveNow(),
      'Cmd-S': () => saveNow(),
    },
  });
  editor.setSize('100%', '100%');

  /* ---------- syntax check ---------- */
  const syntax = document.getElementById('syntax');
  let marked = null;
  let timer = 0;
  function check() {
    if (marked) { editor.removeLineClass(marked, 'background', 'cm-error-line'); marked = null; }
    if (!window.luaparse) { syntax.textContent = ''; return; }
    try {
      window.luaparse.parse(editor.getValue(), { luaVersion: '5.3', comments: false, locations: false });
      syntax.textContent = 'No mistakes found.';
      syntax.className = 'syntax ok';
    } catch (err) {
      const line = typeof err.line === 'number' ? err.line : null;
      const text = String(err.message || err).replace(/^\[\d+:\d+\]\s*/, '');
      syntax.textContent = line ? `Line ${line}: ${text}` : text;
      syntax.className = 'syntax bad';
      if (line) marked = editor.addLineClass(line - 1, 'background', 'cm-error-line');
    }
  }
  editor.on('change', () => {
    ps.changed();
    clearTimeout(timer);
    timer = setTimeout(check, 400);
  });
  check();

  /* ---------- reference ---------- */
  document.addEventListener('click', (e) => {
    const btn = e.target.closest('[data-snippet]');
    if (!btn) return;
    const doc = editor.getDoc();
    const cursor = doc.getCursor();
    const text = btn.dataset.snippet;
    const atLineStart = doc.getLine(cursor.line).slice(0, cursor.ch).trim() === '';
    doc.replaceRange(atLineStart ? text : `\n${text}`, cursor);
    editor.focus();
  });
  const filter = document.getElementById('ref-filter');
  if (filter) {
    filter.addEventListener('input', () => {
      const q = filter.value.trim().toLowerCase();
      for (const section of document.querySelectorAll('.ref-section')) {
        let any = false;
        for (const entry of section.querySelectorAll('.ref-entry')) {
          const show = !q || entry.textContent.toLowerCase().includes(q);
          entry.hidden = !show;
          any = any || show;
        }
        section.hidden = !any && Boolean(q);
        if (q && any) section.open = true;
      }
    });
  }

  /* ---------- saving ---------- */
  async function saveNow() {
    check();
    await ps.save({ source: editor.getValue() });
  }
  document.getElementById('save').addEventListener('click', saveNow);
})();
