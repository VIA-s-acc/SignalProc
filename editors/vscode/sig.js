const KEYWORDS = ['pipeline', 'use', 'let', 'block', 'in', 'out', 'node', 'probe'];
const BOUNDARY = /^\s*(?:(?:pipeline|probe|let|block|use)\b|(?:in|out)\s+[A-Za-z_]|\})/;
const LET_LINE = /^\s*let\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$/;

function letValue(rest) {
  const text = rest.trim();
  if (!text.startsWith('"')) {
    const hash = text.indexOf('#');
    return (hash < 0 ? text : text.slice(0, hash)).trim();
  }
  let out = '';
  for (let i = 1; i < text.length; ++i) {
    if (text[i] === '\\' && i + 1 < text.length) out += text[++i];
    else if (text[i] === '"') break;
    else out += text[i];
  }
  return out;
}

function letsOf(text) {
  const out = [];
  text.split('\n').forEach((line, index) => {
    const m = LET_LINE.exec(line);
    if (m) out.push({ name: m[1], value: letValue(m[2]), line: index });
  });
  return out;
}

function variablePrefix(before) {
  const m = /(\$+)([A-Za-z_][A-Za-z0-9_]*)?$/.exec(before);
  if (!m || m[1].length % 2 === 0) return null;
  return m[2] || '';
}

function variableAt(line, character) {
  for (const m of line.matchAll(/(\$+)([A-Za-z_][A-Za-z0-9_]*)?/g)) {
    if (m[1].length % 2 === 0 || !m[2]) continue;
    const start = m.index + m[1].length - 1;
    const end = m.index + m[0].length;
    if (character >= start && character <= end) return m[2];
  }
  return null;
}

const BLOCK_LINE = /^\s*block\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\((.*)\))?\s*\{/;
const PORT_LINE = /^\s*(in|out)\s+([A-Za-z_].*)$/;
const USE_LINE = /^\s*use\s+"((?:[^"\\]|\\.)*)"/;

function paramsOf(text) {
  const out = [];
  for (const m of (text || '').matchAll(/([A-Za-z_][A-Za-z0-9_]*)\s*=\s*("(?:[^"\\]|\\.)*"|[^,]*)/g)) {
    const raw = m[2].trim();
    out.push({ name: m[1], default: raw.startsWith('"') ? letValue(raw) : raw });
  }
  return out;
}

function blocksOf(text) {
  const lines = text.split('\n');
  const out = [];
  lines.forEach((line, index) => {
    const m = BLOCK_LINE.exec(line);
    if (!m) return;
    const about = [];
    for (let k = index - 1; k >= 0 && /^\s*#/.test(lines[k]); --k) about.unshift(lines[k].replace(/^\s*#\s?/, ''));
    const b = { name: m[1], line: index, params: paramsOf(m[2]), ins: [], outs: [], about: about.join('\n') };
    for (let k = index + 1; k < lines.length && !/^\s*\}/.test(lines[k]); ++k) {
      const port = PORT_LINE.exec(lines[k].replace(/#.*$/, ''));
      if (!port) continue;
      for (const item of port[2].split(',')) {
        const [name, flag] = item.trim().split(/\s+/);
        if (name) (port[1] === 'in' ? b.ins : b.outs).push(flag === 'optional' ? `${name} (optional)` : name);
      }
    }
    out.push(b);
  });
  return out;
}

function usesOf(text) {
  const out = [];
  text.split('\n').forEach((line, index) => {
    const m = USE_LINE.exec(line);
    if (m) out.push({ path: m[1], line: index });
  });
  return out;
}

module.exports = { KEYWORDS, BOUNDARY, letsOf, variablePrefix, variableAt, blocksOf, usesOf };
