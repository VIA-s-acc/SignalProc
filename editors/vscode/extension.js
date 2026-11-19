const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');
const flt = require('./flt');
const preview = require('./preview');
const sig = require('./sig');

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
  if (b.python) {
    md.appendMarkdown(`\n${ru ? 'В Python' : 'In Python'}:\n`);
    md.appendCodeblock(b.python, 'python');
  }
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

function ownBlockDoc(b, ru) {
  const md = new vscode.MarkdownString();
  const args = b.params.map((p) => `${p.name}=${p.default}`).join(', ');
  md.appendCodeblock(`block ${b.name}${args ? `(${args})` : ''}`, 'signals-sig');
  if (b.about) md.appendMarkdown(`${b.about}\n\n`);
  md.appendMarkdown(preview.say(ru, 'ownPorts', { ins: b.ins.join(', ') || '—', outs: b.outs.join(', ') || '—' }));
  md.appendMarkdown(`\n\n_${preview.say(ru, 'ownBlock', { line: b.line + 1 })}_`);
  return md;
}

function nodeContext(document, position) {
  let start = position.line;
  for (; start >= 0 && position.line - start <= 50; --start) {
    const text = document.lineAt(start).text;
    if (/^\s*node\s/.test(text)) break;
    if (start !== position.line && (sig.BOUNDARY.test(text) || text.includes('->'))) return null;
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
  activateFlt(context, ru);
  activatePreview(context, ru);
  const selector = { language: 'signals-sig' };

  context.subscriptions.push(
    vscode.languages.registerHoverProvider(selector, {
      provideHover(document, position) {
        const name = sig.variableAt(document.lineAt(position.line).text, position.character);
        if (name) {
          const found = sig.letsOf(document.getText()).find((l) => l.name === name);
          const md = new vscode.MarkdownString();
          if (found) md.appendCodeblock(`let ${found.name} = ${found.value}`, 'signals-sig');
          else md.appendMarkdown(preview.say(ru, 'unknownVariable', { name }));
          return new vscode.Hover(md);
        }
        const ctx = nodeContext(document, position);
        if (!ctx) return null;
        const own = sig.blocksOf(document.getText()).find((b) => b.name === ctx.type);
        if (own) {
          const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
          return range && range.start.character === ctx.typeStart ? new vscode.Hover(ownBlockDoc(own, ru), range) : null;
        }
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
          if (sig.variablePrefix(text) !== null) {
            return sig.letsOf(document.getText()).map((l) => {
              const item = new vscode.CompletionItem(l.name, vscode.CompletionItemKind.Variable);
              item.detail = l.value;
              return item;
            });
          }
          if (/^\s*node\s+[A-Za-z_][A-Za-z0-9_]*\s*=\s*[A-Za-z_0-9]*$/.test(text)) {
            const own = sig.blocksOf(document.getText()).map((b) => {
              const item = new vscode.CompletionItem(b.name, vscode.CompletionItemKind.Module);
              item.detail = preview.say(ru, 'ownBlock', { line: b.line + 1 });
              item.documentation = ownBlockDoc(b, ru);
              item.insertText = new vscode.SnippetString(`${b.name}($1)`);
              item.sortText = `0${b.name}`;
              return item;
            });
            return own.concat([...blocks.values()].map((b) => {
              const item = new vscode.CompletionItem(b.id, vscode.CompletionItemKind.Function);
              item.detail = b.title;
              item.documentation = blockDoc(b, ru);
              const args = b.params.map((p, i) => `${p.name}=${valueSnippet(p.default, i + 1)}`).join(', ');
              item.insertText = new vscode.SnippetString(`${b.id}(${args})`);
              return item;
            }));
          }
          const ctx = nodeContext(document, position);
          if (ctx && ctx.insideParens) {
            const own = sig.blocksOf(document.getText()).find((b) => b.name === ctx.type);
            if (own) {
              if (ctx.inQuote || /=\s*[^,]*$/.test(text.slice(text.lastIndexOf('(') + 1))) return [];
              const used = new Set([...ctx.whole.matchAll(/([A-Za-z_][A-Za-z0-9_]*)\s*=/g)].map((m) => m[1]));
              return own.params
                .filter((p) => !used.has(p.name))
                .map((p) => {
                  const item = new vscode.CompletionItem(p.name, vscode.CompletionItemKind.Property);
                  item.detail = p.default;
                  item.insertText = new vscode.SnippetString(`${p.name}=\${1:${escapeSnippet(p.default)}}`);
                  return item;
                });
            }
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
            return sig.KEYWORDS.map((k) => new vscode.CompletionItem(k, vscode.CompletionItemKind.Keyword));
          }
          return [];
        },
      },
      '=',
      '(',
      ',',
      ' ',
      '$',
    ),
  );
}

function activateFlt(context, ru) {
  const selector = { language: 'signals-flt' };
  const diagnostics = vscode.languages.createDiagnosticCollection('signals-flt');
  context.subscriptions.push(diagnostics);
  const severity = { error: vscode.DiagnosticSeverity.Error, warning: vscode.DiagnosticSeverity.Warning };
  const refresh = (document) => {
    if (document.languageId !== 'signals-flt') return;
    const found = flt.check(document.getText(), ru).map((d) => {
      const item = new vscode.Diagnostic(new vscode.Range(document.positionAt(d.start), document.positionAt(d.end)), d.message, severity[d.severity]);
      item.source = 'signals';
      return item;
    });
    diagnostics.set(document.uri, found);
  };
  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument(refresh),
    vscode.workspace.onDidChangeTextDocument((e) => refresh(e.document)),
    vscode.workspace.onDidCloseTextDocument((d) => diagnostics.delete(d.uri)),
  );
  for (const d of vscode.workspace.textDocuments) refresh(d);

  context.subscriptions.push(
    vscode.languages.registerHoverProvider(selector, {
      provideHover(document, position) {
        const text = flt.hover(document.getText(), document.offsetAt(position), ru);
        return text ? new vscode.Hover(new vscode.MarkdownString(text)) : null;
      },
    }),
    vscode.languages.registerCompletionItemProvider(
      selector,
      {
        provideCompletionItems(document, position) {
          const line = document.lineAt(position.line).text.slice(0, position.character);
          return flt.completions(document.getText(), line, ru).map((c) => {
            const item = new vscode.CompletionItem(c.label, c.snippet ? vscode.CompletionItemKind.Property : vscode.CompletionItemKind.EnumMember);
            item.detail = c.detail;
            item.insertText = c.snippet ? new vscode.SnippetString(c.snippet) : c.insert;
            return item;
          });
        },
      },
      '=',
      '"',
      ' ',
    ),
  );
}

function activatePreview(context, ru) {
  const diagnostics = vscode.languages.createDiagnosticCollection('signals-run');
  const open = new Map();
  context.subscriptions.push(diagnostics, { dispose: () => open.forEach((s) => s.child && s.child.kill()) });

  const start = async () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'signals-sig') return;
    const document = editor.document;
    if (document.isDirty) await document.save();
    const key = document.uri.toString();
    if (open.has(key)) return open.get(key).panel.reveal(vscode.ViewColumn.Beside, true);
    if (!vscode.workspace.isTrusted) return vscode.window.showWarningMessage(preview.say(ru, 'untrusted'));
    const wanted = preview.programPath(vscode.workspace.getConfiguration('signals').inspect('path'));
    const isFile = (p) => {
      try {
        return fs.statSync(p).isFile();
      } catch {
        return false;
      }
    };
    const exe = preview.resolveProgram(wanted, process.env, path, isFile, process.platform === 'win32' ? ['.EXE', '.COM'] : []);
    const ask = (args) => new Promise((resolve) => childProcess.execFile(exe, args, (error, stdout) => resolve(error ? null : stdout)));
    const version = exe && (await ask(['--version']));
    if (!version) return vscode.window.showErrorMessage(preview.say(ru, 'missing', { path: wanted }));
    if (!preview.formatOk(await ask(['--version', '--json']))) return vscode.window.showErrorMessage(preview.say(ru, 'old', { version: version.trim() }));
    const file = document.uri.fsPath;
    const panel = vscode.window.createWebviewPanel('signalsPreview', preview.say(ru, 'title', { name: path.basename(file) }), { viewColumn: vscode.ViewColumn.Beside, preserveFocus: true }, {});
    panel.webview.html = preview.renderNote(preview.say(ru, 'waiting'));
    const child = childProcess.spawn(exe, ['run', file, '--watch', '--json', '--values', '2000'], { cwd: path.dirname(file) });
    const session = { panel, child };
    open.set(key, session);
    const onLine = (line) => {
      let item;
      try {
        item = preview.parseLine(line);
      } catch {
        return;
      }
      if (item.kind === 'run') {
        diagnostics.delete(document.uri);
        panel.webview.html = preview.renderPanel(item.run, ru);
        return;
      }
      const d = preview.diagnostics(item.error, document.uri);
      const range = new vscode.Range(d.range.start.line, 0, d.range.start.line, Number.MAX_SAFE_INTEGER);
      const diagnostic = new vscode.Diagnostic(range, d.message, vscode.DiagnosticSeverity.Error);
      diagnostic.source = 'signals';
      diagnostics.set(document.uri, [diagnostic]);
      panel.webview.html = preview.renderError(item.error, ru);
    };
    preview.followProcess(child, {
      onLine,
      onExit: (code) => {
        if (open.get(key) === session && code !== null && code !== 0) panel.webview.html = preview.renderNote(preview.say(ru, 'stopped', { code }));
      },
      onError: (error) => {
        if (open.get(key) === session) panel.webview.html = preview.renderNote(preview.say(ru, 'stopped', { code: error.code || error.message }));
      },
    });
    panel.onDidDispose(() => {
      child.kill();
      open.delete(key);
      diagnostics.delete(document.uri);
    });
  };
  context.subscriptions.push(vscode.commands.registerCommand('signals.preview', start));
}

function deactivate() {}

module.exports = { activate, deactivate };
