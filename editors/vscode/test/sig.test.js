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
