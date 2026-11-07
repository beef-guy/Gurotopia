'use strict';
const { config } = require('./config');

const base = `http://${config.supervisor.host}:${config.supervisor.port}`;

class SupervisorDown extends Error {}

async function call(method, path, timeoutMs = 45_000) {
  let res;
  try {
    res = await fetch(base + path, {
      method,
      headers: { authorization: `Bearer ${config.supervisor.token}` },
      signal: AbortSignal.timeout(timeoutMs),
    });
  } catch (err) {
    throw new SupervisorDown(`the supervisor is not reachable (${err.cause?.code || err.message})`);
  }
  const body = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(body.error || `supervisor answered ${res.status}`);
  return body;
}

module.exports = {
  SupervisorDown,
  status: (id) => call('GET', `/servers/${id}`, 5000),
  all: () => call('GET', '/servers', 5000),
  logs: (id, bytes = 20000) => call('GET', `/servers/${id}/logs?bytes=${bytes}`, 5000),
  metrics: (id, minutes = 30) => call('GET', `/servers/${id}/metrics?minutes=${minutes}`, 5000),
  start: (id) => call('POST', `/servers/${id}/start`),
  stop: (id) => call('POST', `/servers/${id}/stop`),
  restart: (id) => call('POST', `/servers/${id}/restart`, 90_000),
  remove: (id) => call('DELETE', `/servers/${id}`),
};
