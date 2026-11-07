'use strict';
// Formatters shared by the panel's scripts. Keep in step with fmt in src/panel.js.
window.psFmt = {
  bytes(n) {
    if (n === null || n === undefined || Number.isNaN(n)) return '–';
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let i = 0;
    let v = Number(n);
    while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
    return `${v >= 100 || i === 0 ? Math.round(v) : v.toFixed(1)} ${units[i]}`;
  },
  duration(seconds) {
    if (seconds === null || seconds === undefined) return '–';
    const s = Math.max(0, Math.floor(seconds));
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    if (d) return `${d}d ${h}h`;
    if (h) return `${h}h ${m}m`;
    return `${m}m`;
  },
  percent(n) {
    return n === null || n === undefined ? '–' : `${Number(n).toFixed(1)}%`;
  },
};
