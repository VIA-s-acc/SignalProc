const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const sig = require('../sig');

const grammar = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'syntaxes', 'sig.tmLanguage.json'), 'utf8'));
const text = 'pipeline "v" v2\nlet fs = 8000\nlet half = "$fs/2"  # half the rate\nnode g = gen(rate=$fs, path="a$$b.wav")\n';

test('lists the variables of a file with their lines', () => {
  assert.deepStrictEqual(sig.letsOf(text), [
    { name: 'fs', value: '8000', line: 1 },
    { name: 'half', value: '$fs/2', line: 2 },
  ]);
});

test('completes a variable after an odd run of dollars only', () => {
  assert.strictEqual(sig.variablePrefix('node g = gen(rate=$f'), 'f');
  assert.strictEqual(sig.variablePrefix('node g = gen(rate=$'), '');
  assert.strictEqual(sig.variablePrefix('path="a$$'), null);
  assert.strictEqual(sig.variablePrefix('path="a$$$h'), 'h');
  assert.strictEqual(sig.variablePrefix('rate=8000'), null);
});

test('finds the variable under the cursor', () => {
  const line = 'node g = gen(rate=$fs, path="a$$b.wav")';
  assert.strictEqual(sig.variableAt(line, line.indexOf('$fs') + 2), 'fs');
  assert.strictEqual(sig.variableAt(line, line.indexOf('$fs')), 'fs');
  assert.strictEqual(sig.variableAt(line, line.indexOf('$$b') + 2), null);
  assert.strictEqual(sig.variableAt(line, 2), null);
});

test('let is a keyword and ends a node for hovers', () => {
  assert.ok(sig.KEYWORDS.includes('let'));
  assert.ok(sig.BOUNDARY.test('let fs = 8000'));
  assert.ok(!sig.BOUNDARY.test('levels=2'));
});

test('the grammar marks let lines, variables and doubled dollars', () => {
  const r = grammar.repository;
  const head = new RegExp(r.let.begin).exec('let fs = 8000');
  assert.ok(head);
  assert.strictEqual(head[2], 'fs');
  const [escape, variable] = r.variable.patterns;
  assert.strictEqual(new RegExp(escape.match).exec('a$$b')[0], '$$');
  assert.strictEqual(new RegExp(variable.match).exec('rate=$fs)')[0], '$fs');
  for (const scope of ['params', 'string', 'let']) assert.ok(JSON.stringify(r[scope].patterns).includes('#variable'), scope);
  assert.ok(grammar.patterns.some((p) => p.include === '#let'));
});

const blocksText = [
  'pipeline "b" v2',
  'use "lib/banks.sig"',
  '# One level of a bank',
  '# with the sum',
  'block bank_level(wavelet=haar, gain="1") {',
  '    in x',
  '    in noise optional',
  '    out y, a',
  '    node h0 = convolve(filter=$wavelet.h0)',
  '    x -> h0 -> y',
  '    h0 -> a',
  '}',
  'node lv1 = bank_level(wavelet=db2)',
  'probe lv1/h0 as "inner"',
].join('\n');

test('block and use are keywords and end a node', () => {
  for (const word of ['block', 'use', 'in', 'out']) assert.ok(sig.KEYWORDS.includes(word), word);
  assert.ok(sig.BOUNDARY.test('block f(a=1) {'));
  assert.ok(sig.BOUNDARY.test('use "lib.sig"'));
  assert.ok(sig.BOUNDARY.test('    in x'));
  assert.ok(sig.BOUNDARY.test('}'));
  assert.ok(!sig.BOUNDARY.test('blocks=4'));
  assert.ok(!sig.BOUNDARY.test('index=2'));
});

test('lists own blocks with params, ports and the comment above', () => {
  const [b] = sig.blocksOf(blocksText);
  assert.strictEqual(b.name, 'bank_level');
  assert.strictEqual(b.line, 4);
  assert.deepStrictEqual(b.params, [
    { name: 'wavelet', default: 'haar' },
    { name: 'gain', default: '1' },
  ]);
  assert.deepStrictEqual(b.ins, ['x', 'noise (optional)']);
  assert.deepStrictEqual(b.outs, ['y', 'a']);
  assert.strictEqual(b.about, 'One level of a bank\nwith the sum');
  assert.deepStrictEqual(sig.usesOf(blocksText), [{ path: 'lib/banks.sig', line: 1 }]);
});

test('the grammar marks block, use, port lines and paths into calls', () => {
  const r = grammar.repository;
  const head = new RegExp(r.block.begin).exec('block bank_level(wavelet=haar) {');
  assert.ok(head);
  assert.strictEqual(head[2], 'bank_level');
  assert.ok(new RegExp(r.use.match).exec('use "lib.sig"'));
  const port = new RegExp(r.port.begin).exec('    in noise optional');
  assert.ok(port);
  assert.strictEqual(port[1], 'in');
  assert.strictEqual(new RegExp(r.probe.match).exec('probe lv1/h0 as "inner"')[2], 'lv1/h0');
  for (const name of ['#block', '#use', '#port', '#closing']) assert.ok(grammar.patterns.some((p) => p.include === name), name);
});

test('snippets define a block and a use line', () => {
  const snippets = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'snippets', 'sig.json'), 'utf8'));
  const prefixes = Object.values(snippets).map((s) => s.prefix);
  assert.ok(prefixes.includes('block'));
  assert.ok(prefixes.includes('use'));
});
