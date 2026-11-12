const TEXT = require('./data/flt.json');

const KEYS = {
  bank: ['name', 'kind', 'h0_offset', 'h0', 'f0_offset', 'f0', 'note'],
  filter: ['name', 'kind', 'h_offset', 'h', 'note'],
};
const ALL_KEYS = new Set([...KEYS.bank, ...KEYS.filter]);
// Same tolerance as make_bank for user banks in src/tui/data/user_filters.cpp.
const PR_TOL = 1e-9;

function say(ru, key, args = {}) {
  let s = TEXT[ru ? 'ru' : 'en'][key];
  for (const [k, v] of Object.entries(args)) s = s.split(`{${k}}`).join(String(v));
  return s;
}

function parse(text) {
  const entries = [];
  const errors = [];
  let i = 0;
  const n = text.length;
  const err = (start, end, key, args) => errors.push({ start, end: Math.max(end, start + 1), key, args });
  const skipBlank = (newlines) => {
    for (;;) {
      while (i < n && (text[i] === ' ' || text[i] === '\t' || text[i] === '\r' || (newlines && text[i] === '\n'))) ++i;
      if (i < n && text[i] === '#') {
        while (i < n && text[i] !== '\n') ++i;
        continue;
      }
      return;
    }
  };
  const lineEnd = (from) => {
    const e = text.indexOf('\n', from);
    return e < 0 ? n : e;
  };

  function parseString(quote) {
    const start = i;
    ++i;
    let out = '';
    while (i < n && text[i] !== quote && text[i] !== '\n') {
      if (quote === '"' && text[i] === '\\') {
        const c = text[i + 1];
        const map = { n: '\n', t: '\t', '"': '"', '\\': '\\', r: '\r' };
        if (c in map) {
          out += map[c];
          i += 2;
          continue;
        }
        if (c === 'u' && /^[0-9A-Fa-f]{4}$/.test(text.slice(i + 2, i + 6))) {
          out += String.fromCharCode(parseInt(text.slice(i + 2, i + 6), 16));
          i += 6;
          continue;
        }
        err(i, i + 2, 'syntax.escape');
        i += 2;
        continue;
      }
      out += text[i++];
    }
    if (text[i] !== quote) {
      err(start, i, 'syntax.unclosed_string');
      return { type: 'string', value: out, start, end: i };
    }
    ++i;
    return { type: 'string', value: out, start, end: i };
  }

  function parseScalar() {
    const start = i;
    const m = /^[+-]?(?:inf|nan|(?:0|[1-9](?:_?\d)*)(?:\.\d(?:_?\d)*)?(?:[eE][+-]?\d(?:_?\d)*)?)/.exec(text.slice(i));
    if (m && m[0].length > 0 && !/[A-Za-z0-9_.]/.test(text[i + m[0].length] || '')) {
      i += m[0].length;
      const raw = m[0].replace(/_/g, '');
      const integer = /^[+-]?\d+$/.test(raw);
      const value = /inf$/.test(raw) ? (raw.startsWith('-') ? -Infinity : Infinity) : /nan$/.test(raw) ? NaN : Number(raw);
      return { type: 'number', integer, value, start, end: i };
    }
    const word = /^[A-Za-z0-9_.+-]+/.exec(text.slice(i));
    if (word && (word[0] === 'true' || word[0] === 'false')) {
      i += word[0].length;
      return { type: 'bool', value: word[0] === 'true', start, end: i };
    }
    const len = word ? word[0].length : 1;
    err(start, start + len, 'syntax.value', { text: text.slice(start, start + len) });
    i += len;
    return null;
  }

  function parseValue() {
    if (text[i] === '"' || text[i] === "'") return parseString(text[i]);
    if (text[i] === '[') {
      const start = i;
      ++i;
      const items = [];
      for (;;) {
        skipBlank(true);
        if (i >= n) {
          err(start, start + 1, 'syntax.unclosed_array');
          return { type: 'array', items, start, end: i };
        }
        if (text[i] === ']') {
          ++i;
          return { type: 'array', items, start, end: i };
        }
        const item = parseValue();
        if (item) items.push(item);
        skipBlank(true);
        if (text[i] === ',') {
          ++i;
          continue;
        }
        if (text[i] !== ']') {
          if (i >= n) continue;
          err(i, i + 1, 'syntax.comma');
          while (i < n && text[i] !== ',' && text[i] !== ']' && text[i] !== '\n') ++i;
          if (text[i] === ',') ++i;
        }
      }
    }
    return parseScalar();
  }

  const seen = new Map();
  for (;;) {
    skipBlank(true);
    if (i >= n) break;
    if (text[i] === '[') {
      const end = lineEnd(i);
      err(i, end, 'syntax.table');
      i = end;
      continue;
    }
    const key = /^[A-Za-z0-9_-]+/.exec(text.slice(i));
    if (!key) {
      const end = lineEnd(i);
      err(i, end, 'syntax.key');
      i = end;
      continue;
    }
    const keyStart = i;
    i += key[0].length;
    skipBlank(false);
    if (text[i] !== '=') {
      const end = lineEnd(i);
      err(keyStart, end, 'syntax.equals', { key: key[0] });
      i = end;
      continue;
    }
    ++i;
    skipBlank(false);
    const value = i < n && text[i] !== '\n' ? parseValue() : (err(keyStart, i, 'syntax.no_value', { key: key[0] }), null);
    skipBlank(false);
    if (i < n && text[i] !== '\n') {
      const end = lineEnd(i);
      err(i, end, 'syntax.trailing');
      i = end;
    }
    const entry = { key: key[0], keyStart, keyEnd: keyStart + key[0].length, value };
    if (seen.has(entry.key)) err(entry.keyStart, entry.keyEnd, 'syntax.duplicate', { key: entry.key });
    else seen.set(entry.key, entry);
    entries.push(entry);
  }
  return { entries, keys: seen, errors };
}

function numbers(entry) {
  if (!entry || !entry.value || entry.value.type !== 'array') return null;
  if (!entry.value.items.every((v) => v.type === 'number')) return null;
  return entry.value.items.map((v) => v.value);
}

function offsetOf(keys, name) {
  const e = keys.get(name);
  return e && e.value && e.value.type === 'number' && e.value.integer ? e.value.value : 0;
}

function convolve(a, b) {
  const out = new Array(a.length + b.length - 1).fill(0);
  for (let i = 0; i < a.length; ++i) for (let j = 0; j < b.length; ++j) out[i + j] += a[i] * b[j];
  return out;
}

function perfectReconstruction(h0, h0Offset, f0, f0Offset) {
  const p = convolve(f0, h0);
  const offset = h0Offset + f0Offset;
  const odd = [];
  for (let k = 0; k < p.length; ++k) if (Math.abs(offset + k) % 2 === 1) odd.push({ power: offset + k, d: 2 * p[k] });
  const big = odd.filter((o) => Math.abs(o.d) > PR_TOL);
  if (big.length === 1 && Math.abs(big[0].d - 2) <= PR_TOL) return { ok: true, delay: big[0].power };
  const best = odd.reduce((b, o) => (b === null || Math.abs(o.d) > Math.abs(b.d) ? o : b), null);
  let deviation = 0;
  for (const o of odd) deviation = Math.max(deviation, Math.abs(o.d - (best && o === best ? 2 : 0)));
  if (!best) deviation = 2;
  return { ok: false, deviation };
}

function kindOf(keys) {
  const e = keys.get('kind');
  if (!e || !e.value) return 'bank';
  return e.value.type === 'string' ? e.value.value : null;
}

function check(text, ru) {
  const parsed = parse(text);
  const out = parsed.errors.map((e) => ({ start: e.start, end: e.end, severity: 'error', message: say(ru, e.key, e.args) }));
  const { keys } = parsed;
  const kind = kindOf(keys);
  const kindEntry = keys.get('kind');
  if (kindEntry && kindEntry.value && kind !== 'bank' && kind !== 'filter')
    out.push({ start: kindEntry.value.start, end: kindEntry.value.end, severity: 'error', message: say(ru, 'check.kind') });
  const wanted = kind === 'filter' ? KEYS.filter : KEYS.bank;
  for (const e of parsed.entries) {
    if (!ALL_KEYS.has(e.key)) out.push({ start: e.keyStart, end: e.keyEnd, severity: 'warning', message: say(ru, 'check.unknown', { key: e.key }) });
    else if ((kind === 'bank' || kind === 'filter') && !wanted.includes(e.key))
      out.push({ start: e.keyStart, end: e.keyEnd, severity: 'warning', message: say(ru, kind === 'bank' ? 'check.not_for_bank' : 'check.not_for_filter', { key: e.key }) });
    if (!e.value) continue;
    if ((e.key === 'name' || e.key === 'note' || e.key === 'kind') && e.value.type !== 'string')
      out.push({ start: e.value.start, end: e.value.end, severity: 'error', message: say(ru, 'check.string', { key: e.key }) });
    if (e.key.endsWith('_offset') && !(e.value.type === 'number' && e.value.integer))
      out.push({ start: e.value.start, end: e.value.end, severity: 'error', message: say(ru, 'check.offset', { key: e.key }) });
    if (e.key === 'h0' || e.key === 'f0' || e.key === 'h') {
      if (e.value.type !== 'array') {
        out.push({ start: e.value.start, end: e.value.end, severity: 'error', message: say(ru, 'check.array', { key: e.key }) });
        continue;
      }
      if (e.value.items.length === 0) out.push({ start: e.value.start, end: e.value.end, severity: 'error', message: say(ru, 'check.empty', { key: e.key }) });
      for (const item of e.value.items) {
        if (item.type !== 'number') out.push({ start: item.start, end: item.end, severity: 'error', message: say(ru, 'check.number', { key: e.key }) });
        else if (!Number.isFinite(item.value)) out.push({ start: item.start, end: item.end, severity: 'error', message: say(ru, 'check.finite', { key: e.key }) });
      }
    }
  }
  const missing = (name) => !keys.has(name);
  const anchor = parsed.entries.length ? { start: parsed.entries[0].keyStart, end: parsed.entries[0].keyEnd } : { start: 0, end: 1 };
  if (kind === 'filter' && missing('h')) out.push({ ...anchor, severity: 'error', message: say(ru, 'check.missing', { key: 'h' }) });
  if (kind === 'bank')
    for (const k of ['h0', 'f0']) if (missing(k)) out.push({ ...anchor, severity: 'error', message: say(ru, 'check.missing', { key: k }) });
  if (kind === 'bank') {
    const h0 = numbers(keys.get('h0'));
    const f0 = numbers(keys.get('f0'));
    if (h0 && f0 && h0.length && f0.length && h0.concat(f0).every(Number.isFinite)) {
      const pr = perfectReconstruction(h0, offsetOf(keys, 'h0_offset'), f0, offsetOf(keys, 'f0_offset'));
      if (!pr.ok) {
        const e = keys.get('f0');
        out.push({ start: e.keyStart, end: e.value.end, severity: 'error', message: say(ru, 'check.pr', { deviation: pr.deviation.toExponential(2) }) });
      }
    }
  }
  return out;
}

function stats(values, offset) {
  let sum = 0, alt = 0, energy = 0;
  values.forEach((v, k) => {
    sum += v;
    alt += Math.abs(offset + k) % 2 ? -v : v;
    energy += v * v;
  });
  return { taps: values.length, sum, alt, energy };
}

function fmt(v) {
  return Number.isInteger(v) ? String(v) : Number(v.toPrecision(10)).toString();
}

function entryAt(parsed, offset) {
  for (const e of parsed.entries) {
    const end = e.value ? e.value.end : e.keyEnd;
    if (offset >= e.keyStart && offset <= end) return e;
  }
  return null;
}

function hover(text, offset, ru) {
  const parsed = parse(text);
  const e = entryAt(parsed, offset);
  if (!e) return null;
  const lines = [];
  const about = TEXT[ru ? 'ru' : 'en'][`key.${e.key}`];
  lines.push(about ? `**${e.key}** — ${about}` : say(ru, 'check.unknown', { key: e.key }));
  if (e.key === 'kind' && e.value && e.value.type === 'string' && TEXT[ru ? 'ru' : 'en'][`kind.${e.value.value}`])
    lines.push(`\`${e.value.value}\` — ${say(ru, `kind.${e.value.value}`)}`);
  const values = numbers(e);
  if (values && values.length && values.every(Number.isFinite)) {
    const off = offsetOf(parsed.keys, `${e.key}_offset`);
    const s = stats(values, off);
    lines.push(say(ru, 'hover.stats', { taps: s.taps, offset: off, sum: fmt(s.sum), alt: fmt(s.alt), energy: fmt(s.energy) }));
    if ((e.key === 'h0' || e.key === 'f0') && kindOf(parsed.keys) === 'bank') {
      const h0 = numbers(parsed.keys.get('h0'));
      const f0 = numbers(parsed.keys.get('f0'));
      if (h0 && f0 && h0.length && f0.length && h0.concat(f0).every(Number.isFinite)) {
        const pr = perfectReconstruction(h0, offsetOf(parsed.keys, 'h0_offset'), f0, offsetOf(parsed.keys, 'f0_offset'));
        lines.push(pr.ok ? say(ru, 'hover.pr_ok', { delay: pr.delay }) : say(ru, 'check.pr', { deviation: pr.deviation.toExponential(2) }));
      }
    }
  }
  return lines.join('\n\n');
}

function completions(text, lineText, ru) {
  if (!/^\s*[A-Za-z0-9_]*$/.test(lineText)) {
    if (/^\s*kind\s*=\s*"?[a-z]*$/.test(lineText))
      return ['bank', 'filter'].map((k) => ({ label: k, detail: say(ru, `kind.${k}`), insert: /"[a-z]*$/.test(lineText) ? k : `"${k}"` }));
    return [];
  }
  const parsed = parse(text);
  const kind = kindOf(parsed.keys);
  const keys = kind === 'filter' ? KEYS.filter : KEYS.bank;
  const values = {
    name: '"${1:my_bank}"',
    kind: '"${1|bank,filter|}"',
    note: '"${1}"',
    h0: '[${1:0.7071067811865476, 0.7071067811865476}]',
    f0: '[${1:0.7071067811865476, 0.7071067811865476}]',
    h: '[${1:0.25, 0.5, 0.25}]',
    h0_offset: '${1:0}',
    f0_offset: '${1:0}',
    h_offset: '${1:0}',
  };
  return keys
    .filter((k) => !parsed.keys.has(k))
    .map((k) => ({ label: k, detail: TEXT[ru ? 'ru' : 'en'][`key.${k}`], snippet: `${k} = ${values[k]}` }));
}

module.exports = { parse, check, hover, completions, perfectReconstruction };
