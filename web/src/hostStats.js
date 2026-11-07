'use strict';
/*
 * Load of the whole VPS for the dashboard: CPU, memory, disk, network and uptime.
 * CPU and network are rates, so they are sampled every few seconds in the background.
 * Off Linux (testing on a PC) the /proc parts are simply left out.
 */
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const os = require('node:os');

const SAMPLE_MS = 5000;

let last = null; // @note { cpu: { idle, total }, net: { rx, tx }, at }
let current = { cpu: null, rxPerSec: null, txPerSec: null };
let timer = null;

async function readCpu() {
  try {
    const text = await fsp.readFile('/proc/stat', 'utf8');
    const nums = text.split('\n')[0].trim().split(/\s+/).slice(1).map(Number);
    const idle = nums[3] + (nums[4] || 0); // @note idle + iowait
    const total = nums.reduce((a, b) => a + b, 0);
    return { idle, total };
  } catch {
    return null;
  }
}

async function readNet() {
  try {
    const text = await fsp.readFile('/proc/net/dev', 'utf8');
    let rx = 0;
    let tx = 0;
    for (const line of text.split('\n').slice(2)) {
      const [name, rest] = line.split(':');
      if (!rest || name.trim() === 'lo') continue;
      const f = rest.trim().split(/\s+/).map(Number);
      rx += f[0];
      tx += f[8];
    }
    return { rx, tx };
  } catch {
    return null;
  }
}

async function sample() {
  const [cpu, net] = await Promise.all([readCpu(), readNet()]);
  const at = Date.now();
  if (last) {
    const seconds = (at - last.at) / 1000;
    if (cpu && last.cpu && cpu.total > last.cpu.total) {
      const busy = 1 - (cpu.idle - last.cpu.idle) / (cpu.total - last.cpu.total);
      current.cpu = Math.round(Math.max(0, busy) * 1000) / 10;
    }
    if (net && last.net && seconds > 0) {
      current.rxPerSec = Math.max(0, (net.rx - last.net.rx) / seconds);
      current.txPerSec = Math.max(0, (net.tx - last.net.tx) / seconds);
    }
  }
  last = { cpu, net, at };
}

function startSampling() {
  if (timer) return;
  sample().catch(() => {});
  timer = setInterval(() => sample().catch(() => {}), SAMPLE_MS);
  timer.unref();
}

async function memory() {
  try { // @note MemAvailable counts cache the kernel can hand back, which is what "used" should leave out
    const text = await fsp.readFile('/proc/meminfo', 'utf8');
    const kb = (key) => Number((new RegExp(`^${key}:\\s+(\\d+)`, 'm').exec(text) || [])[1]) * 1024;
    const total = kb('MemTotal');
    const available = kb('MemAvailable');
    if (total && !Number.isNaN(available)) return { used: total - available, total };
  } catch { /* @note not Linux */ }
  return { used: os.totalmem() - os.freemem(), total: os.totalmem() };
}

async function disk() {
  if (typeof fs.statfs !== 'function') return null;
  try {
    const s = await fsp.statfs('/');
    const total = s.blocks * s.bsize;
    return { used: total - s.bfree * s.bsize, total };
  } catch {
    return null;
  }
}

async function snapshot() {
  const [mem, dsk] = await Promise.all([memory(), disk()]);
  return {
    cpu: current.cpu,
    cores: os.cpus().length,
    memory: mem,
    disk: dsk,
    rxPerSec: current.rxPerSec,
    txPerSec: current.txPerSec,
    uptimeSeconds: Math.round(os.uptime()),
  };
}

module.exports = { startSampling, snapshot };
