/**
 * Thin fetch wrapper for the AI backend.
 *
 * Why no axios?
 * The project already has zero HTTP libraries; relying on the native fetch
 * keeps the bundle small. All endpoints return JSON (or 302 in the case of
 * /events/:id/clip, which the browser follows transparently).
 *
 * URL convention:
 *   - In dev: vite.config.js proxies /api/ai/* → http://192.168.3.34:8000/*
 *   - In prod: any reverse proxy should do the same
 */

const BASE = '/api/ai'

async function http(path, opts = {}) {
  const res = await fetch(BASE + path, {
    headers: { 'content-type': 'application/json' },
    ...opts,
  })
  if (!res.ok) {
    let detail = ''
    try { detail = (await res.json()).detail || '' } catch { /* ignore */ }
    throw new Error(`${res.status} ${res.statusText}${detail ? `: ${detail}` : ''}`)
  }
  return res.status === 204 ? null : res.json()
}

/** Build absolute URL for asset endpoints (snapshot/clip) */
export function assetUrl(path) {
  return BASE + path
}

export function listEvents(params = {}) {
  const qs = new URLSearchParams()
  Object.entries(params).forEach(([k, v]) => {
    if (v !== undefined && v !== null && v !== '' && v !== 'all') {
      qs.set(k, v)
    }
  })
  const suffix = qs.toString() ? `?${qs}` : ''
  return http(`/events${suffix}`)
}

export function getEvent(id) {
  return http(`/events/${encodeURIComponent(id)}`)
}

export function reanalyzeEvent(id) {
  return http(`/events/${encodeURIComponent(id)}/reanalyze`, { method: 'POST' })
}

export function getSummary(date) {
  const qs = date ? `?date=${encodeURIComponent(date)}` : ''
  return http(`/summary${qs}`)
}

export function listSummaryDates() {
  return http('/summary/dates')
}

export function snapshotUrl(eventId, idx) {
  return assetUrl(`/events/${encodeURIComponent(eventId)}/snapshot/${idx}`)
}

export function clipUrl(eventId) {
  return assetUrl(`/events/${encodeURIComponent(eventId)}/clip`)
}

export function ingestExternal(payload) {
  return http('/events/external', {
    method: 'POST',
    body: JSON.stringify(payload),
  })
}
