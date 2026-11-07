const vscode = require('vscode');
const fs = require('fs');
const path = require('path');

const NODE_LINE = /^\s*node\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)/;

function loadBlocks(context) {
  const lang = vscode.env.language.toLowerCase().startsWith('ru') ? 'ru' : 'en';
  const file = path.join(context.extensionPath, 'data', `blocks.${lang}.json`);
  const blocks = new Map();
  for (const b of JSON.parse(fs.readFileSync(file, 'utf8'))) blocks.set(b.id, b);
  return { blocks, ru: lang === 'ru' };
}

function ports(list) {
  return list.length ? list.map((p) => `\`${p.name}\` ${p.type}${p.optional ? '?' : ''}`).join(', ') : '—';
}

function paramLine(p, ru) {
  let line = `\`${p.name}\` = ${p.default === '' ? '—' : p.default}`;
  if (p.min !== undefined && p.max !== undefined) line += ` (${p.min}…${p.max})`;
  if (p.about) line += ` — ${p.about}`;
  return line;
}

function blockDoc(b, ru) {
  const md = new vscode.MarkdownString();
  md.appendMarkdown(`**${b.id}** — ${b.title}\n\n${b.about}\n\n`);
  md.appendMarkdown(`${ru ? 'Входы' : 'Inputs'}: ${ports(b.inputs)}  \n${ru ? 'Выходы' : 'Outputs'}: ${ports(b.outputs)}\n\n`);
  for (const p of b.params) md.appendMarkdown(`- ${paramLine(p, ru)}\n`);
  return md;
}

function paramDoc(p, ru) {
  const md = new vscode.MarkdownString();
  md.appendMarkdown(paramLine(p, ru) + '\n\n');
  for (const v of p.choices) md.appendMarkdown(`- \`${v}\` — ${p.values[v] || ''}\n`);
  return md;
}

function escapeSnippet(text) {
  return text.replace(/[$}\\]/g, (c) => '\\' + c);
}

function sigValue(value) {
  return /^[A-Za-z0-9_.+\-]+$/.test(value) ? null : value;
}

function valueSnippet(value, index) {
  const placeholder = `\${${index}:${escapeSnippet(value.replace(/[\\"]/g, (c) => '\\' + c))}}`;
  return sigValue(value) === null ? placeholder : `"${placeholder}"`;
}

function nodeContext(document, position) {
  let start = position.line;
  for (; start >= 0 && position.line - start <= 50; --start) {
    const text = document.lineAt(start).text;
    if (/^\s*node\s/.test(text)) break;
    if (start !== position.line && (/^\s*(pipeline|probe)\b/.test(text) || text.includes('->'))) return null;
  }
  if (start < 0 || position.line - start > 50) return null;
  const head = document.lineAt(start).text;
  const m = NODE_LINE.exec(head);
  if (!m) return null;
  const line = document.lineAt(position.line).text;
  let before = '';
  for (let l = start; l < position.line; ++l) before += document.lineAt(l).text + '\n';
  before += line.slice(0, position.character);
  let depth = 0;
  let quote = false;
  for (let i = 0; i < before.length; ++i) {
    const c = before[i];
    if (quote) {
      if (c === '\\') ++i;
      else if (c === '"') quote = false;
      continue;
    }
    if (c === '"') quote = true;
    else if (c === '#') {
      const end = before.indexOf('\n', i);
      if (end < 0) break;
      i = end;
    } else if (c === '(') ++depth;
    else if (c === ')') --depth;
  }
  let whole = before + line.slice(position.character);
  for (let l = position.line + 1; l < document.lineCount && l <= position.line + 50; ++l) {
    if (whole.lastIndexOf(')') > whole.indexOf('(') && whole.indexOf('(') >= 0) break;
    whole += '\n' + document.lineAt(l).text;
  }
  const typeStart = position.line === start ? head.indexOf(m[2], head.indexOf('=') + 1) : -1;
  return { line, id: m[1], type: m[2], typeStart, insideParens: depth > 0, inQuote: quote, whole };
}

function activate(context) {
  const { blocks, ru } = loadBlocks(context);
  const selector = { language: 'signals-sig' };

  context.subscriptions.push(
    vscode.languages.registerHoverProvider(selector, {
      provideHover(document, position) {
        const ctx = nodeContext(document, position);
        if (!ctx) return null;
        const block = blocks.get(ctx.type);
        if (!block) return null;
        const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_.\-]*/);
        if (!range) return null;
        const word = document.getText(range);
        if (range.start.character === ctx.typeStart) return new vscode.Hover(blockDoc(block, ru), range);
        if (!ctx.insideParens) return null;
        const after = ctx.line.slice(range.end.character);
        const param = block.params.find((p) => p.name === word);
        if (param && /^\s*=/.test(after)) return new vscode.Hover(paramDoc(param, ru), range);
        const before = ctx.line.slice(0, range.start.character);
        const owner = /([A-Za-z_][A-Za-z0-9_]*)\s*=\s*$/.exec(before);
        if (owner) {
          const p = block.params.find((q) => q.name === owner[1]);
          if (p && p.values[word]) return new vscode.Hover(new vscode.MarkdownString(`\`${word}\` — ${p.values[word]}`), range);
          if (p) return new vscode.Hover(paramDoc(p, ru), range);
        }
        return null;
      },
    }),
  );

  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      selector,
      {
        provideCompletionItems(document, position) {
          const text = document.lineAt(position.line).text.slice(0, position.character);
          if (/^\s*node\s+[A-Za-z_][A-Za-z0-9_]*\s*=\s*[A-Za-z_0-9]*$/.test(text)) {
            return [...blocks.values()].map((b) => {
              const item = new vscode.CompletionItem(b.id, vscode.CompletionItemKind.Function);
              item.detail = b.title;
              item.documentation = blockDoc(b, ru);
              const args = b.params.map((p, i) => `${p.name}=${valueSnippet(p.default, i + 1)}`).join(', ');
              item.insertText = new vscode.SnippetString(`${b.id}(${args})`);
              return item;
            });
          }
          const ctx = nodeContext(document, position);
          if (ctx && ctx.insideParens) {
            const block = blocks.get(ctx.type);
            if (!block) return [];
            const value = /([A-Za-z_][A-Za-z0-9_]*)\s*=\s*[A-Za-z0-9_.\-]*$/.exec(text);
            if (value) {
              const p = block.params.find((q) => q.name === value[1]);
              if (!p) return [];
              return p.choices.map((v) => {
                const item = new vscode.CompletionItem(v, vscode.CompletionItemKind.EnumMember);
                item.detail = p.values[v] || '';
                return item;
              });
            }
            if (ctx.inQuote) return [];
            const used = new Set([...ctx.whole.matchAll(/([A-Za-z_][A-Za-z0-9_]*)\s*=/g)].map((m) => m[1]));
            return block.params
              .filter((p) => !used.has(p.name))
              .map((p) => {
                const item = new vscode.CompletionItem(p.name, vscode.CompletionItemKind.Property);
                item.detail = p.about;
                item.documentation = paramDoc(p, ru);
                item.insertText = new vscode.SnippetString(`${p.name}=${valueSnippet(p.default, 1)}`);
                return item;
              });
          }
          if (/^\s*[a-z]*$/.test(text)) {
            return ['node', 'probe', 'pipeline'].map((k) => new vscode.CompletionItem(k, vscode.CompletionItemKind.Keyword));
          }
          return [];
        },
      },
      '=',
      '(',
      ',',
      ' ',
    ),
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
