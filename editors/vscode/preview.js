const TEXT = require('./data/preview.json');

const MIN_VERSION = [0, 9, 0];
const PLOT_WIDTH = 640;
const PLOT_HEIGHT = 120;

function say(ru, key, args = {}) {
  let s = TEXT[ru ? 'ru' : 'en'][key];
  for (const [k, v] of Object.entries(args)) s = s.split(`{${k}}`).join(String(v));
  return s;
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
}

function number(v) {
  return typeof v === 'number' ? (Number.isInteger(v) ? String(v) : String(Number(v.toPrecision(6)))) : escapeHtml(v);
}

function parseLine(line) {
  const j = JSON.parse(line);
  if (j.error) return { kind: 'error', error: j.error };
  return { kind: 'run', run: j };
}

function diagnostics(error, uri) {
  const line = Math.max(0, (error.line || 1) - 1);
  return { uri, range: { start: { line, character: 0 }, end: { line, character: Number.MAX_SAFE_INTEGER } }, message: error.message, severity: 'error' };
}

function versionOk(text) {
  const m = /(\d+)\.(\d+)\.(\d+)/.exec(text || '');
  if (!m) return false;
  const v = m.slice(1, 4).map(Number);
  for (let k = 0; k < 3; ++k) if (v[k] !== MIN_VERSION[k]) return v[k] > MIN_VERSION[k];
  return true;
}

function programPath(setting) {
  return (setting && (setting.globalValue || setting.defaultValue)) || 'signals';
}

function resolveProgram(name, env, paths, exists, extensions) {
  const candidates = (base) => [base, ...extensions.map((e) => base + e.toLowerCase())];
  if (paths.isAbsolute(name)) return candidates(name).find(exists) || null;
  if (name.includes('/') || name.includes('\\')) return null;
  for (const dir of (env.PATH || env.Path || '').split(paths.delimiter)) {
    if (!dir || !paths.isAbsolute(dir)) continue;
    const found = candidates(paths.join(dir, name)).find(exists);
    if (found) return found;
  }
  return null;
}

function polyline(values, peak, index) {
  const n = values.length;
  const points = values.map((v, i) => {
    const x = n > 1 ? (i / (n - 1)) * PLOT_WIDTH : 0;
    const y = PLOT_HEIGHT / 2 - (peak > 0 ? v / peak : 0) * (PLOT_HEIGHT / 2 - 2);
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  });
  return `<polyline class="ch${index % 4}" points="${points.join(' ')}"/>`;
}

function signalPlot(probe) {
  const peak = Math.max(0, ...probe.values.map((c) => Math.max(0, ...c.map(Math.abs))));
  const lines = probe.values.map((c, k) => polyline(c, peak, k)).join('');
  return `<svg viewBox="0 0 ${PLOT_WIDTH} ${PLOT_HEIGHT}" preserveAspectRatio="none"><line class="axis" x1="0" y1="${PLOT_HEIGHT / 2}" x2="${PLOT_WIDTH}" y2="${PLOT_HEIGHT / 2}"/>${lines}</svg>`;
}

function table(head, rows) {
  const th = head.map((h) => `<th>${escapeHtml(h)}</th>`).join('');
  return `<table><tr>${th}</tr>${rows.map((r) => `<tr>${r.map((c) => `<td>${c}</td>`).join('')}</tr>`).join('')}</table>`;
}

function fieldsTable(fields, ru) {
  return table([say(ru, 'field'), say(ru, 'value')], Object.entries(fields || {}).map(([k, v]) => [escapeHtml(k), number(v)]));
}

function shapeText(shape) {
  return (shape || []).map((s) => (s.channels ? `${s.kind} ${s.channels}×${s.frames}` : s.kind)).join(', ');
}

function probeBlock(name, probe, ru) {
  let body = '';
  if (probe.kind === 'signal') {
    body += `<p>${escapeHtml(say(ru, 'signal_line', { channels: probe.channels, length: probe.length, rate: probe.rate, peak: number(probe.peak) }))}</p>`;
    if (probe.values && probe.values.length) {
      body += signalPlot(probe);
      if (probe.values_cut) body += `<p class="dim">${escapeHtml(say(ru, 'values_cut', { shown: probe.values[0].length, length: probe.length }))}</p>`;
    }
  } else if (probe.kind === 'coeffs') {
    body += `<p>${escapeHtml(say(ru, 'coeffs_line', { wavelet: probe.wavelet, levels: probe.levels, length: probe.length }))}</p>`;
    if (probe.bands) body += table([say(ru, 'band'), say(ru, 'length'), say(ru, 'energy')], probe.bands.map((b) => [escapeHtml(b.name), number(b.length), number(b.energy)]));
  } else if (probe.kind === 'report') {
    body += fieldsTable(probe.fields, ru);
  } else if ('value' in probe) {
    body += `<p>${number(probe.value)}</p>`;
  }
  return `<section><h3>${escapeHtml(name)} <span class="dim">${escapeHtml(probe.kind)}</span></h3>${body}</section>`;
}

const STYLE = `body{font-family:var(--vscode-font-family);color:var(--vscode-foreground);background:var(--vscode-editor-background);padding:0 12px}
table{border-collapse:collapse;margin:4px 0 12px}td,th{border-bottom:1px solid var(--vscode-panel-border);padding:2px 10px 2px 0;text-align:left;font-variant-numeric:tabular-nums}
svg{width:100%;height:${PLOT_HEIGHT}px;display:block}polyline{fill:none;stroke-width:1;vector-effect:non-scaling-stroke}
.ch0{stroke:var(--vscode-charts-blue)}.ch1{stroke:var(--vscode-charts-orange)}.ch2{stroke:var(--vscode-charts-green)}.ch3{stroke:var(--vscode-charts-purple)}
.axis{stroke:var(--vscode-panel-border);vector-effect:non-scaling-stroke}.dim{color:var(--vscode-descriptionForeground)}.ok{color:var(--vscode-testing-iconPassed)}.bad{color:var(--vscode-errorForeground)}`;

function page(title, body) {
  return `<!DOCTYPE html><html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline';"><title>${escapeHtml(title)}</title><style>${STYLE}</style></head><body>${body}</body></html>`;
}

function renderPanel(run, ru) {
  const state = run.checks_passed ? `<p class="ok">${escapeHtml(say(ru, 'checks_passed'))}</p>` : `<p class="bad">${escapeHtml(say(ru, 'checks_failed'))}</p>`;
  const nodes = table(
    [say(ru, 'node'), say(ru, 'block'), say(ru, 'state'), say(ru, 'shape'), say(ru, 'message')],
    run.nodes.map((n) => [escapeHtml(n.id), escapeHtml(n.type), escapeHtml(n.state), escapeHtml(shapeText(n.shape)), escapeHtml(n.message || '')]),
  );
  const probes = Object.entries(run.probes || {}).map(([name, p]) => probeBlock(name, p, ru)).join('');
  const reports = (run.reports || [])
    .map((r) => `<section><h3>${escapeHtml(r.node)} <span class="${r.passed ? 'ok' : 'bad'}">${escapeHtml(say(ru, r.passed ? 'passed' : 'failed'))}</span></h3>${fieldsTable(r.fields, ru)}</section>`)
    .join('');
  const title = say(ru, 'title', { name: run.pipeline || '—' });
  return page(title, `<h2>${escapeHtml(title)}</h2>${state}<h2>${escapeHtml(say(ru, 'probes'))}</h2>${probes}<h2>${escapeHtml(say(ru, 'reports'))}</h2>${reports}<h2>${escapeHtml(say(ru, 'nodes'))}</h2>${nodes}`);
}

function renderError(error, ru) {
  const where = error.line ? ` · ${say(ru, 'line', { line: error.line })}` : '';
  return page(say(ru, 'error'), `<h2 class="bad">${escapeHtml(say(ru, 'error'))}${escapeHtml(where)}</h2><p>${escapeHtml(error.message)}</p>`);
}

function renderNote(text) {
  return page('Signals', `<p class="dim">${escapeHtml(text)}</p>`);
}

module.exports = { say, parseLine, diagnostics, versionOk, programPath, resolveProgram, renderPanel, renderError, renderNote };
