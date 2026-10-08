// One self-contained page: interpreter, shell, font and the game's
// resources in a single HTML file that makes no network requests.
//
//   node tools/bundle.js build out.html
//
// There is no bundler to depend on, so this does the small job itself:
// each module under js/ becomes a function that returns its exports.

import { readFileSync, writeFileSync, existsSync, readdirSync } from 'node:fs';
import { join, dirname, resolve, posix } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const JS_DIR = join(dirname(fileURLToPath(import.meta.url)), '..', 'js');
const NAME = '[A-Za-z_$][\\w$]*';

function importList(list) {
  return list.split(',').map((s) => s.trim()).filter(Boolean)
    .map((s) => s.replace(/\s+as\s+/, ': ')).join(', ');
}

// Rewrite one module's source. Returns { code, deps }.
export function transformModule(source, id) {
  const deps = [];
  const exports = [];
  const dep = (spec) => {
    const to = posix.normalize(posix.join(posix.dirname(id), spec));
    if (!deps.includes(to)) deps.push(to);
    return `__m[${JSON.stringify(to)}]`;
  };
  let code = source;
  // export { a, b as c } from './x.js';
  code = code.replace(new RegExp(`^export\\s*\\{([^}]*)\\}\\s*from\\s*['"]([^'"]+)['"];?`, 'gm'), (m, list, spec) => {
    const from = dep(spec);
    for (const part of list.split(',').map((s) => s.trim()).filter(Boolean)) {
      const [a, b] = part.split(/\s+as\s+/);
      exports.push(`${b || a}: ${from}.${a}`);
    }
    return '';
  });
  // import { a, b as c } from './x.js';
  code = code.replace(new RegExp(`^import\\s*\\{([^}]*)\\}\\s*from\\s*['"]([^'"]+)['"];?`, 'gm'),
    (m, list, spec) => `const { ${importList(list)} } = ${dep(spec)};`);
  code = code.replace(new RegExp(`^import\\s*\\*\\s*as\\s+(${NAME})\\s*from\\s*['"]([^'"]+)['"];?`, 'gm'),
    (m, name, spec) => `const ${name} = ${dep(spec)};`);
  code = code.replace(new RegExp(`^import\\s+(${NAME})\\s*from\\s*['"]([^'"]+)['"];?`, 'gm'),
    (m, name, spec) => `const ${name} = ${dep(spec)}.default;`);
  code = code.replace(/^import\s*['"]([^'"]+)['"];?/gm, (m, spec) => { dep(spec); return ''; });
  // export { a, b as c };
  code = code.replace(/^export\s*\{([^}]*)\};?/gm, (m, list) => {
    for (const part of list.split(',').map((s) => s.trim()).filter(Boolean)) {
      const [a, b] = part.split(/\s+as\s+/);
      exports.push(b ? `${b}: ${a}` : a);
    }
    return '';
  });
  code = code.replace(new RegExp(`^export\\s+((?:async\\s+)?function\\*?|class|const|let|var)\\s+(${NAME})`, 'gm'),
    (m, kind, name) => { exports.push(name); return `${kind} ${name}`; });
  code = code.replace(/^export\s+default\s+/gm, () => { exports.push('default: __default'); return 'const __default = '; });
  if (/^(import|export)\b/m.test(code)) {
    const line = code.split('\n').find((l) => /^(import|export)\b/.test(l));
    throw new Error(`${id}: cannot bundle this line: ${line}`);
  }
  return { code: `${code}\nreturn { ${exports.join(', ')} };`, deps };
}

// All modules reachable from the entry, dependencies first.
export function bundleModules(entry = 'shell.js', dir = JS_DIR) {
  const done = new Map();
  const order = [];
  const visit = (id, trail) => {
    if (done.has(id)) return;
    if (trail.includes(id)) throw new Error(`import cycle: ${[...trail, id].join(' -> ')}`);
    const path = join(dir, id);
    if (!existsSync(path)) throw new Error(`missing module js/${id}`);
    const t = transformModule(readFileSync(path, 'utf8'), id);
    for (const d of t.deps) visit(d, [...trail, id]);
    done.set(id, t.code);
    order.push(id);
  };
  visit(entry, []);
  let out = '"use strict";\nconst __m = {};\n';
  for (const id of order) {
    out += `// ---- ${id} ----\n__m[${JSON.stringify(id)}] = (function () {\n${done.get(id)}\n})();\n`;
  }
  return out;
}

// The built game as { name: base64 }, the shape shell.js looks for.
export function inlineResources(buildDir) {
  const out = {};
  for (const name of readdirSync(buildDir).sort()) {
    if (!/^(LOGDIR|PICDIR|VIEWDIR|SNDDIR|WORDS\.TOK|OBJECT|game\.json|VOL\.\d+)$/.test(name)) continue;
    out[name] = readFileSync(join(buildDir, name)).toString('base64');
  }
  // Recorded plays ("Let the AI play"), under the name the shell asks for.
  const demoDir = join(buildDir, 'demo');
  if (existsSync(demoDir)) {
    for (const name of readdirSync(demoDir).sort()) {
      if (/^\d+\.txt$/.test(name)) out[`demo/${name}`] = readFileSync(join(demoDir, name)).toString('base64');
    }
  }
  if (!out.LOGDIR || !out['VOL.0']) throw new Error(`${buildDir} has no built game (run tools/pack.js first)`);
  return out;
}

export function bundleHtml(buildDir) {
  const page = readFileSync(join(JS_DIR, 'index.html'), 'utf8');
  const tag = '<script type="module" src="./shell.js"></script>';
  if (!page.includes(tag)) throw new Error('js/index.html has no shell.js script tag to replace');
  // A closing script tag inside the code would end the element early.
  const safe = (s) => s.replace(/<\/(script)/gi, '<\\/$1').replace(/<!--/g, '<\\!--');
  const resources = `<script>window.JRQ_RESOURCES = ${safe(JSON.stringify(inlineResources(buildDir)))};</script>`;
  const code = `<script type="module">\n${safe(bundleModules())}</script>`;
  return page.replace(tag, () => `${resources}\n${code}`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const [buildDir, outFile] = process.argv.slice(2);
  if (!buildDir || !outFile) {
    console.error('usage: node tools/bundle.js build out.html');
    process.exitCode = 2;
  } else {
    try {
      const html = bundleHtml(buildDir);
      writeFileSync(outFile, html);
      console.log(`${outFile}: ${html.length} bytes, one file, no network requests`);
    } catch (e) {
      console.error(e.message);
      process.exitCode = 1;
    }
  }
}
