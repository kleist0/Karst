/* ===========================================================================
 *  Karst script studio - built page checks
 * ===========================================================================
 *  Node, no browser needed:
 *      node tools\script-studio\tests\check_page.js
 *
 *  Building the page is not the same as the page working. The build step only
 *  substitutes three markers; everything else - the UI wiring - is unchecked by
 *  it. Two mistakes kept happening while writing this UI and both are silent
 *  until a human clicks:
 *
 *    * a typo in an element id ($('btnSave') when the markup says id="save"):
 *      the listener is attached to null and the button does nothing, with no
 *      error anywhere;
 *    * a syntax error in the inlined block library: the build still "succeeds",
 *      and the page comes up completely blank.
 *
 *  So: parse the inlined scripts, and cross-check every id the script looks up
 *  against the ids the markup actually defines.
 * ======================================================================== */

'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..');
const HTML = path.join(ROOT, 'KarstScriptStudio.html');

let failures = 0;

function ok(msg) { console.log('  [OK]   ' + msg); }
function bad(msg, detail) {
  failures++;
  console.log('  [FAIL] ' + msg);
  if (detail) { String(detail).split(/\r?\n/).slice(0, 20).forEach(l => console.log('         ' + l)); }
}

if (!fs.existsSync(HTML)) {
  console.log('  [FAIL] 还没有构建产物：' + HTML);
  console.log('         先运行 build_studio.ps1');
  process.exit(1);
}

const html = fs.readFileSync(HTML, 'utf8');
console.log('page: ' + HTML);
console.log('size: ' + html.length + ' bytes');
console.log('');

// --- 1. placeholders ------------------------------------------------------
console.log('=== 构建占位符 ===');
['/*__BLOCKS__*/', '/*__GAMEDATA__*/', '<!--__STAMP__-->', '__STAMP_TEXT__'].forEach(marker => {
  if (html.indexOf(marker) >= 0) {
    bad('占位符没有被替换：' + marker);
  } else {
    ok('占位符已替换：' + marker);
  }
});

if (!/var KARST_DATA\s*=\s*\{/.test(html)) {
  bad('没有找到注入的游戏数据 (var KARST_DATA)');
} else {
  ok('游戏数据已注入');
}

// --- 2. scripts parse -----------------------------------------------------
console.log('');
console.log('=== 内联脚本语法 ===');

const scripts = [];
const re = /<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/gi;
let m;
while ((m = re.exec(html)) !== null) { scripts.push(m[1]); }

if (scripts.length === 0) {
  bad('页面里没有内联脚本');
} else {
  ok('找到 ' + scripts.length + ' 段内联脚本');
}

scripts.forEach((code, i) => {
  try {
    // vm.Script compiles without running: a syntax error surfaces here and
    // nowhere else, because a broken inline script fails silently in the page.
    new vm.Script(code, { filename: 'inline-script-' + (i + 1) + '.js' });
    ok('第 ' + (i + 1) + ' 段（' + code.length + ' 字节）语法正确');
  } catch (e) {
    bad('第 ' + (i + 1) + ' 段语法错误', e.message);
  }
});

// --- 3. module wiring -----------------------------------------------------
console.log('');
console.log('=== 模块接线 ===');
['KarstRegistry', 'KarstMetadata', 'KarstCodegen', 'KarstValidate'].forEach(name => {
  if (html.indexOf('root.' + name + ' =') >= 0) {
    ok('积木模块已内联：' + name);
  } else {
    bad('缺少积木模块：' + name + '（build_studio.ps1 的 blockFiles 列表）');
  }
});

// Running the inlined library in a bare context must define the four globals and
// generate code - this catches a library that parses but throws on load.
try {
  const sandbox = { window: {}, globalThis: null, module: undefined, console: console };
  sandbox.globalThis = sandbox;
  vm.createContext(sandbox);
  const libCode = scripts.join('\n');
  // Only the library part: the app IIFE needs a DOM, so run everything up to it.
  const appStart = libCode.indexOf('App: UI state');
  const libOnly = appStart > 0 ? libCode.slice(0, libCode.lastIndexOf('/* ====', appStart)) : libCode;
  new vm.Script(libOnly, { filename: 'library.js' }).runInContext(sandbox);

  const R = sandbox.KarstRegistry, M = sandbox.KarstMetadata,
        C = sandbox.KarstCodegen, V = sandbox.KarstValidate;
  if (!R || !M || !C || !V) {
    bad('内联后模块没有挂到全局');
  } else {
    const doc = M.assignIds({ v: 1, cls: 'PageCheck', base: 'techno', binding: 'MTNK',
      events: [{ id: 'OnUpdate', conds: [{ t: 'everyNFrames', a: { n: 30 } }],
                 acts: [{ t: 'log', a: { level: 'info', msg: '页面自检' } }] }] });
    const code = C.generate(doc);
    if (code.indexOf('public class PageCheck : TechnoScriptable') < 0) {
      bad('内联生成器输出不对', code.slice(0, 400));
    } else {
      ok('内联生成器可用（生成 ' + code.length + ' 字节）');
    }
    const r = V.validate(doc);
    if (!r.ok) {
      bad('内联校验器对正常文档报错', JSON.stringify(r.errors));
    } else {
      ok('内联校验器可用');
    }
    const back = M.decode(code);
    if (!back || back.events.length !== 1) {
      bad('内联元数据往返失败');
    } else {
      ok('内联元数据往返可用');
    }

    // Extra: every block in the library must at least be reachable by the
    // picker, i.e. legal in some event in at least one combination.
    const neverUsable = [];
    Object.keys(R.CONDITIONS).forEach(id => {
      const def = R.CONDITIONS[id];
      const anyEvent = Object.keys(R.EVENTS).some(evId => {
        if (def.events && def.events.indexOf(evId) < 0) { return false; }
        const avail = Object.assign({ self: true }, R.EVENTS[evId].params);
        Object.keys(R.FIELDS).forEach(f => { avail[f] = true; });
        return (def.needs || []).every(n => avail[n]);
      });
      if (!anyEvent) { neverUsable.push('条件 ' + id); }
    });
    Object.keys(R.ACTIONS).forEach(id => {
      const def = R.ACTIONS[id];
      const anyEvent = Object.keys(R.EVENTS).some(evId => {
        if (def.events && def.events.indexOf(evId) < 0) { return false; }
        const avail = Object.assign({ self: true }, R.EVENTS[evId].params);
        Object.keys(R.FIELDS).forEach(f => { avail[f] = true; });
        return (def.needs || []).every(n => avail[n]);
      });
      if (!anyEvent) { neverUsable.push('动作 ' + id); }
    });
    if (neverUsable.length) {
      bad('有积木在任何事件里都用不了（用户永远点不到）', neverUsable.join(', '));
    } else {
      ok('所有积木都至少能用在某个事件里');
    }
  }
} catch (e) {
  bad('运行内联积木库时抛异常', e.message + '\n' + (e.stack || ''));
}

// --- 4. dom ids -----------------------------------------------------------
console.log('');
console.log('=== 脚本引用的 DOM id ===');

const domIds = new Set();
const idRe = /\bid\s*=\s*"([^"]+)"/g;
let im;
while ((im = idRe.exec(html)) !== null) { domIds.add(im[1]); }

const used = new Set();
const useRe = /\$\(\s*'([^']+)'\s*\)|getElementById\(\s*'([^']+)'\s*\)/g;
let um;
while ((um = useRe.exec(html)) !== null) { used.add(um[1] || um[2]); }

const missing = [];
used.forEach(id => { if (!domIds.has(id)) { missing.push(id); } });

if (missing.length) {
  bad('脚本引用了不存在的 id（这些控件的监听器会挂到 null 上，点了没反应）',
    missing.join(', '));
} else {
  ok('脚本引用的 ' + used.size + ' 个 id 全部存在');
}

// --- 5. data sanity -------------------------------------------------------
console.log('');
console.log('=== 注入的游戏数据 ===');
try {
  const sandbox2 = { window: {}, globalThis: null, console: console };
  sandbox2.globalThis = sandbox2;
  vm.createContext(sandbox2);
  new vm.Script('var KARST_DATA = ' + /var KARST_DATA\s*=\s*(\{[\s\S]*?\});/.exec(html)[1] + ';')
    .runInContext(sandbox2);
  const d = sandbox2.KARST_DATA;
  ['weapons', 'warheads', 'infantry', 'vehicles', 'buildings'].forEach(k => {
    if (!Array.isArray(d[k]) || d[k].length === 0) {
      bad('游戏数据为空：' + k);
    } else {
      ok(k + ': ' + d[k].length + ' 项');
    }
  });
  if (d.warheads.indexOf('BlimpHE') < 0) {
    bad('弹头列表里没有 BlimpHE —— 积木的默认值会指向不存在的类型');
  } else {
    ok('默认弹头 BlimpHE 在列表里');
  }
  if (d.weapons.indexOf('RedEye2') < 0) {
    bad('武器列表里没有 RedEye2 —— 积木的默认值会指向不存在的类型');
  } else {
    ok('默认武器 RedEye2 在列表里');
  }
} catch (e) {
  bad('解析注入的游戏数据失败', e.message);
}

console.log('');
if (failures) {
  console.log('FAILED: ' + failures + ' 个问题');
  process.exit(1);
}
console.log('PASSED');
