const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const preview = require('../preview');

const fixture = path.join(__dirname, '..', '..', '..', 'tests', 'fixtures', 'vscode', 'first_steps.ndjson');
const lines = () => fs.readFileSync(fixture, 'utf8').split('\n').filter(Boolean);

test('parses every line of the saved run', () => {
  const parsed = lines().map(preview.parseLine);
  assert.ok(parsed.length > 0);
  for (const p of parsed) assert.strictEqual(p.kind, 'run');
  assert.ok('noisy' in parsed[0].run.probes);
  assert.ok('clean' in parsed[0].run.probes);
});

test('turns an error into a diagnostic at its line', () => {
  const p = preview.parseLine('{"error":{"file":"/work/g.sig","line":5,"message":"no block nosuchblock"}}');
  assert.strictEqual(p.kind, 'error');
  const d = preview.diagnostics(p.error, 'file:///work/g.sig');
  assert.strictEqual(d.range.start.line, 4);
  assert.strictEqual(d.message, 'no block nosuchblock');
  assert.strictEqual(d.severity, 'error');
  assert.strictEqual(preview.diagnostics({ line: 0, message: 'cannot read' }, 'x').range.start.line, 0);
});

test('draws an svg polyline for every signal probe', () => {
  const run = preview.parseLine(lines()[0]).run;
  for (const ru of [true, false]) {
    const html = preview.renderPanel(run, ru);
    const channels = Object.values(run.probes).filter((p) => p.kind === 'signal').reduce((n, p) => n + p.values.length, 0);
    assert.strictEqual((html.match(/<polyline /g) || []).length, channels);
    for (const name of Object.keys(run.probes)) assert.ok(html.includes(name));
    for (const n of run.nodes) assert.ok(html.includes(n.id));
    assert.ok(!html.includes('<script'));
  }
});

test('shows coefficient bands and escapes text', () => {
  const run = {
    pipeline: '<g>',
    checks_passed: false,
    nodes: [{ id: 'c', type: 'dwt', state: 'done', shape: [], message: '' }],
    probes: { c: { kind: 'coeffs', channels: 1, levels: 1, length: 8, wavelet: 'db2', bands: [{ name: 'a1', length: 4, energy: 2 }, { name: 'd1', length: 4, energy: 0.5 }] } },
    reports: [],
  };
  const html = preview.renderPanel(run, false);
  assert.ok(html.includes('a1'));
  assert.ok(html.includes('d1'));
  assert.ok(html.includes('&lt;g&gt;'));
  assert.ok(!html.includes('<g>'));
});

test('refuses a signals older than 0.9.0', () => {
  assert.strictEqual(preview.versionOk('signals 0.8.0'), false);
  assert.strictEqual(preview.versionOk('signals 0.9.0'), true);
  assert.strictEqual(preview.versionOk('signals 1.2.3\n'), true);
  assert.strictEqual(preview.versionOk('garbage'), false);
});

test('takes the program only from user settings, never from the workspace', () => {
  assert.strictEqual(preview.programPath({ defaultValue: 'signals', workspaceValue: '/tmp/evil', workspaceFolderValue: '/tmp/evil' }), 'signals');
  assert.strictEqual(preview.programPath({ defaultValue: 'signals', globalValue: '/opt/signals/bin/signals', workspaceValue: '/tmp/evil' }), '/opt/signals/bin/signals');
  assert.strictEqual(preview.programPath(undefined), 'signals');
});

test('finds the program on PATH by an absolute path, never in the current folder', () => {
  const files = new Set(['/usr/local/bin/signals', '/work/repo/signals', 'C:\\Tools\\signals.exe']);
  const exists = (p) => files.has(p);
  const posix = { delimiter: ':', sep: '/', isAbsolute: (p) => p.startsWith('/'), join: (a, b) => `${a}/${b}` };
  assert.strictEqual(preview.resolveProgram('signals', { PATH: '.::/usr/local/bin' }, posix, exists, []), '/usr/local/bin/signals');
  assert.strictEqual(preview.resolveProgram('signals', { PATH: '/nowhere' }, posix, exists, []), null);
  assert.strictEqual(preview.resolveProgram('/work/repo/signals', { PATH: '' }, posix, exists, []), '/work/repo/signals');
  assert.strictEqual(preview.resolveProgram('bin/signals', { PATH: '/usr/local/bin' }, posix, exists, []), null);
  const win = { delimiter: ';', sep: '\\', isAbsolute: (p) => /^[A-Za-z]:\\/.test(p), join: (a, b) => `${a}\\${b}` };
  assert.strictEqual(preview.resolveProgram('signals', { PATH: 'C:\\Tools' }, win, exists, ['.EXE', '.CMD']), 'C:\\Tools\\signals.exe');
});
