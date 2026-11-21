const KEYWORDS = ['pipeline', 'let', 'node', 'probe'];
const BOUNDARY = /^\s*(pipeline|probe|let)\b/;
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

module.exports = { KEYWORDS, BOUNDARY, letsOf, variablePrefix, variableAt };
