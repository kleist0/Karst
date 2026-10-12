/* ===========================================================================
 *  Karst script studio - test suite
 * ===========================================================================
 *  Run with Node:
 *      node tools\script-studio\tests\run_tests.js
 *      node tools\script-studio\tests\run_tests.js --compile
 *
 *  What it checks:
 *
 *    1. EVERY BLOCK COMPILES. One .cs is generated per (block x event it is
 *       allowed in), then all of them are compiled through verify_compile.ps1 -
 *       the same reference set and options the in-game Roslyn host uses. A block
 *       whose snippet has a typo therefore fails HERE, in a test, instead of in
 *       the user's patcher.log after they dropped the file into the game.
 *
 *    2. REAL COMBINATIONS COMPILE. Single blocks are the easy case; the bugs
 *       live in the interactions - two blocks in one event both declaring a
 *       local, a condition that provides an out-variable used by an action, a
 *       state field that must appear because a distant block needs it.
 *
 *    3. THE METADATA ROUND TRIP IS LOSSLESS. File -> doc -> file must reproduce
 *       the same text, or "reopen and edit" would quietly rewrite the user's
 *       script on every save.
 *
 *    4. VALIDATION CATCHES REAL MISTAKES. A block that needs pTarget placed in
 *       OnUpdate must be reported as an error, not rendered as broken C#.
 *
 *  --compile shells out to PowerShell. Without it the suite still runs checks
 *  3 and 4 and only writes the fixtures, which is useful when iterating on the
 *  generator without a compiler at hand.
 * ======================================================================== */

'use strict';

const fs = require('fs');
const path = require('path');
const cp = require('child_process');

const HERE = __dirname;
const ROOT = path.resolve(HERE, '..');
const CASES = path.join(HERE, 'cases');

const REG = require(path.join(ROOT, 'blocks', 'registry.js'));
const META = require(path.join(ROOT, 'blocks', 'metadata.js'));
const CODEGEN = require(path.join(ROOT, 'blocks', 'codegen.js'));
const VALIDATE = require(path.join(ROOT, 'blocks', 'validate.js'));

const wantCompile = process.argv.indexOf('--compile') >= 0;

let failures = 0;
let checks = 0;

function ok(msg) {
  checks++;
  console.log('  [OK]   ' + msg);
}
function bad(msg, detail) {
  checks++;
  failures++;
  console.log('  [FAIL] ' + msg);
  if (detail) {
    String(detail).split(/\r?\n/).forEach(l => console.log('         ' + l));
  }
}
function section(title) {
  console.log('');
  console.log('=== ' + title + ' ===');
}

// --- helpers --------------------------------------------------------------

function emptyDoc(events) {
  return {
    v: META.VERSION,
    cls: 'StudioCase',
    base: 'techno',
    binding: '[MTNK]',
    events: events || []
  };
}

function defaultArgs(def) {
  const a = {};
  (def.args || []).forEach(arg => { a[arg.k] = arg.def; });
  return a;
}

// Can this block legally live in this event? Mirrors validate.js: BOTH the
// explicit `events` whitelist AND the `needs` must be satisfied. Checking only
// the whitelist generated a fixture (createBulletToTarget in OnUpdate) that the
// validator rejects - i.e. the test was building documents no user could build.
function allowedIn(def, evId) {
  if (def.events && def.events.indexOf(evId) < 0) { return false; }
  const ev = REG.EVENTS[evId];
  const avail = Object.assign({ self: true }, ev.params);
  Object.keys(REG.FIELDS).forEach(f => { avail[f] = true; });
  return (def.needs || []).every(n => avail[n]);
}

// Deterministic ids: assigned in document order, condition ids before action
// ids. Stable ids are what make generated variable names reproducible.
function withIds(doc) {
  return META.assignIds(doc);
}

// --- 1. single-block fixtures --------------------------------------------

function buildSingleBlockCases() {
  const cases = [];

  Object.keys(REG.EVENTS).forEach(evId => {
    // conditions + a trivial action so the method body is not empty
    Object.keys(REG.CONDITIONS).forEach(cId => {
      const def = REG.CONDITIONS[cId];
      if (!allowedIn(def, evId)) { return; }
      const doc = withIds(emptyDoc([{
        id: evId,
        conds: [{ t: cId, a: defaultArgs(def) }],
        acts: [{ t: 'log', a: { level: 'info', msg: 'cond ' + cId } }]
      }]));
      cases.push({
        name: 'cond_' + cId + '__' + evId,
        doc,
        why: '条件积木 ' + cId + ' 用在 ' + evId
      });
    });

    // every action on its own
    Object.keys(REG.ACTIONS).forEach(aId => {
      const def = REG.ACTIONS[aId];
      if (!allowedIn(def, evId)) { return; }
      const doc = withIds(emptyDoc([{
        id: evId,
        conds: [],
        acts: [{ t: aId, a: defaultArgs(def) }]
      }]));
      cases.push({
        name: 'act_' + aId + '__' + evId,
        doc,
        why: '动作积木 ' + aId + ' 用在 ' + evId
      });
    });
  });

  return cases;
}

// --- 2. combination fixtures ---------------------------------------------

function buildComboCases() {
  const cases = [];

  const add = (name, why, events) => {
    cases.push({ name: 'combo_' + name, doc: withIds(emptyDoc(events)), why });
  };

  // The MissileRain example, rebuilt out of blocks: remember on fire, strike
  // every frame. Exercises rememberTarget -> hasRemembered -> damageRemembered
  // across two different events, plus the shared state field.
  add('remember_and_strike', '开火记目标 + 每帧打击（等价示例 MissileRain）', [
    { id: 'OnFire', conds: [], acts: [{ t: 'rememberTarget', a: {} }, { t: 'log', a: { level: 'info', msg: '记下目标' } }] },
    {
      id: 'OnUpdate',
      conds: [{ t: 'hasRemembered', a: {} }, { t: 'everyNFrames', a: { n: 15 } }],
      acts: [
        { t: 'damageRemembered', a: { wh: 'BlimpHE', dmg: 10 } },
        { t: 'logRememberedPos', a: {} }
      ]
    }
  ]);

  // Damage reduction plus a warning, on the damage event.
  add('damage_reduction', '受伤减半 + 记日志', [
    {
      id: 'OnReceiveDamage',
      conds: [{ t: 'damageOver', a: { n: 50 } }],
      acts: [
        { t: 'modifyDamage', a: { mode: 'mul', v: 50 } },
        { t: 'log', a: { level: 'warn', msg: '受伤减半' } }
      ]
    }
  ]);

  // Damage amplifier: add fixed damage.
  add('damage_amp', '受伤时加伤（add 模式）', [
    {
      id: 'OnReceiveDamage',
      conds: [],
      acts: [{ t: 'modifyDamage', a: { mode: 'add', v: 200 } }]
    }
  ]);

  // Deep condition nesting: three conditions, one of which uses self.
  add('deep_conditions', '三层条件嵌套 + 范围伤害', [
    {
      id: 'OnUpdate',
      conds: [
        { t: 'everyNFrames', a: { n: 30 } },
        { t: 'chance', a: { n: 20 } },
        { t: 'hpBelow', a: { n: 30 } }
      ],
      acts: [
        { t: 'damageAreaAtSelf', a: { wh: 'BlimpHE', dmg: 200 } },
        { t: 'log', a: { level: 'error', msg: '低血自爆' } }
      ]
    }
  ]);

  // Target filter providing targetTechno, used by following actions.
  add('target_filter', '开火：目标是单位时才发射子弹', [
    {
      id: 'OnFire',
      conds: [{ t: 'targetIsTechno', a: {} }],
      acts: [
        { t: 'createBulletToTarget', a: { weapon: 'RedEye2', wh: 'BlimpHE', dmg: 100, speed: 100, bright: true } },
        { t: 'logSelfType', a: {} }
      ]
    }
  ]);

  // Repeated same-kind blocks in one event: this is what catches local variable
  // name collisions (two `wh_bN`, two `pos_bN`, two `hp_bN`).
  add('repeated_blocks', '同一事件里重复同类积木（变量重名检查）', [
    {
      id: 'OnUpdate',
      conds: [{ t: 'everyNFrames', a: { n: 10 } }],
      acts: [
        { t: 'logSelf', a: {} },
        { t: 'logSelf', a: {} },
        { t: 'changeHealth', a: { delta: 10 } },
        { t: 'changeHealth', a: { delta: -5 } },
        { t: 'damageAreaAtSelf', a: { wh: 'BlimpHE', dmg: 5 } },
        { t: 'damageAreaAtSelf', a: { wh: 'HE', dmg: 5 } },
        { t: 'logSelfType', a: {} }
      ]
    }
  ]);

  // Every event at once, so the whole class shape is compiled in one file.
  add('all_events', '五个事件全用上', [
    { id: 'OnUpdate', conds: [{ t: 'chance', a: { n: 1 } }], acts: [{ t: 'log', a: { level: 'info', msg: '帧' } }] },
    { id: 'OnFire', conds: [], acts: [{ t: 'rememberTarget', a: {} }, { t: 'log', a: { level: 'info', msg: '开火' } }] },
    { id: 'OnReceiveDamage', conds: [], acts: [{ t: 'log', a: { level: 'warn', msg: '受伤' } }] },
    { id: 'OnPut', conds: [], acts: [{ t: 'logSelf', a: {} }] },
    { id: 'OnRemove', conds: [], acts: [{ t: 'log', a: { level: 'error', msg: '被移除' } }] }
  ]);

  // Type filters, including the array form.
  add('type_filters', '类型判断（单个 + 列表）', [
    {
      id: 'OnUpdate',
      conds: [{ t: 'selfTypeIs', a: { id: 'MTNK' } }],
      acts: [{ t: 'log', a: { level: 'info', msg: '我是灰熊' } }]
    },
    {
      id: 'OnPut',
      conds: [{ t: 'selfTypeIn', a: { ids: 'MTNK, GRIZZLY ,, BADGER' } }],
      acts: [{ t: 'log', a: { level: 'info', msg: '在列表里' } }]
    }
  ]);

  // Health both ways, plus clearing the remembered target.
  add('health_and_clear', '加血/扣血 + 忘记目标', [
    {
      id: 'OnUpdate',
      conds: [{ t: 'hpAbove', a: { n: 80 } }],
      acts: [
        { t: 'changeHealth', a: { delta: -20 } },
        { t: 'clearRemembered', a: {} },
        { t: 'clearRemembered', a: {} }
      ]
    }
  ]);

  // Text escaping: quotes, backslashes and braces in user text must survive.
  add('text_escaping', '日志文本里的引号/反斜杠/花括号', [
    {
      id: 'OnUpdate',
      conds: [],
      acts: [
        { t: 'log', a: { level: 'info', msg: '他说"你好" \\ 反斜杠 {0} 花括号' } },
        { t: 'log', a: { level: 'warn', msg: '路径 C:\\Games\\RA2\\KarstPatcher' } },
        { t: 'log', a: { level: 'error', msg: '换行\n也要转义' } }
      ]
    }
  ]);

  // Non-ASCII text (CJK) in the message body.
  add('cjk_text', '日志里的中文', [
    { id: 'OnUpdate', conds: [], acts: [{ t: 'log', a: { level: 'info', msg: '脚本中文测试：坦克已就绪' } }] }
  ]);

  // numeric edge values
  add('numeric_edges', '数值边界（0 / 大数 / 负数）', [
    {
      id: 'OnUpdate',
      conds: [
        { t: 'everyNFrames', a: { n: '1' } },
        { t: 'chance', a: { n: '100' } },
        { t: 'hpBelow', a: { n: '0' } }
      ],
      acts: [
        { t: 'changeHealth', a: { delta: 0 } },
        { t: 'changeHealth', a: { delta: -99999 } },
        { t: 'damageAreaAtSelf', a: { wh: 'BlimpHE', dmg: 999999 } }
      ]
    }
  ]);

  return cases;
}

// --- 3. metadata round trip ----------------------------------------------

function testRoundTrip(cases) {
  section('元数据往返（生成 -> 解析 -> 再生成 必须完全一致）');

  cases.forEach(c => {
    let text1;
    try {
      text1 = CODEGEN.generate(c.doc);
    } catch (e) {
      bad(c.name + '：生成抛异常 ' + e.message);
      return;
    }

    const parsed = META.decode(text1);
    if (!parsed) {
      bad(c.name + '：生成的代码里解析不出元数据');
      return;
    }

    const text2 = CODEGEN.generate(parsed);
    if (text1 !== text2) {
      bad(c.name + '：往返后代码变了（不可幂等）', firstDiff(text1, text2));
      return;
    }

    // The decoded doc must still describe the same blocks.
    const before = describe(c.doc);
    const after = describe(parsed);
    if (before !== after) {
      bad(c.name + '：往返后积木结构变了', 'before: ' + before + '\nafter:  ' + after);
      return;
    }
  });

  if (failures === 0) { ok('全部 ' + cases.length + ' 个用例往返一致'); }
}

function describe(doc) {
  return (doc.events || []).map(ev =>
    ev.id + '[' +
    (ev.conds || []).map(x => x.t).join(',') + '|' +
    (ev.acts || []).map(x => x.t).join(',') + ']'
  ).join(' ');
}

function firstDiff(a, b) {
  const la = a.split('\n'), lb = b.split('\n');
  for (let i = 0; i < Math.max(la.length, lb.length); i++) {
    if (la[i] !== lb[i]) {
      return 'line ' + (i + 1) + ':\n  A: ' + JSON.stringify(la[i]) + '\n  B: ' + JSON.stringify(lb[i]);
    }
  }
  return '';
}

// --- 4. validation behaviour ---------------------------------------------

function testValidation() {
  section('校验器行为');

  // (a) a block needing pTarget placed in OnUpdate must be an ERROR.
  {
    const doc = withIds(emptyDoc([{
      id: 'OnUpdate',
      conds: [],
      acts: [{ t: 'rememberTarget', a: {} }]
    }]));
    const r = VALIDATE.validate(doc);
    if (!r.ok && r.errors.length > 0) {
      ok('在"每帧"里放需要目标的积木 -> 报错（正确）');
    } else {
      bad('在"每帧"里放需要目标的积木 -> 没有报错', JSON.stringify(r));
    }
  }

  // (b) modifyDamage outside OnReceiveDamage must be an ERROR.
  {
    const doc = withIds(emptyDoc([{
      id: 'OnUpdate',
      conds: [],
      acts: [{ t: 'modifyDamage', a: { mode: 'mul', v: 50 } }]
    }]));
    const r = VALIDATE.validate(doc);
    if (!r.ok) {
      ok('把"修改伤害"放到非受伤事件 -> 报错（正确）');
    } else {
      bad('把"修改伤害"放到非受伤事件 -> 没有报错', JSON.stringify(r));
    }
  }

  // (c) a good document must be clean.
  {
    const doc = withIds(emptyDoc([
      { id: 'OnFire', conds: [], acts: [{ t: 'rememberTarget', a: {} }] },
      { id: 'OnUpdate', conds: [{ t: 'hasRemembered', a: {} }], acts: [{ t: 'damageRemembered', a: { wh: 'BlimpHE', dmg: 5 } }] }
    ]));
    const r = VALIDATE.validate(doc);
    if (r.ok && r.errors.length === 0) {
      ok('正常的积木组合没有报错');
    } else {
      bad('正常的积木组合被误报', JSON.stringify(r.errors));
    }
  }

  // (d) bad class names must be caught.
  [['', '空类名'], ['1Bad', '数字开头'], ['class', 'C# 关键字'], ['有中文', '非 ASCII']].forEach(([name, why]) => {
    const doc = withIds(emptyDoc([{ id: 'OnUpdate', conds: [], acts: [{ t: 'log', a: { level: 'info', msg: 'x' } }] }]));
    doc.cls = name;
    const r = VALIDATE.validate(doc);
    if (!r.ok) {
      ok('类名校验：' + why + ' -> 报错（正确）');
    } else {
      bad('类名校验：' + why + ' -> 没有报错');
    }
  });

  // (e) a game name that is not in the rules must be a WARNING (compiles, but
  //     silently does nothing at runtime) - not an error.
  {
    const doc = withIds(emptyDoc([{
      id: 'OnUpdate',
      conds: [],
      acts: [{ t: 'damageAreaAtSelf', a: { wh: 'NoSuchWarheadXyz', dmg: 10 } }]
    }]));
    const fakeData = { warheads: ['BlimpHE', 'HE'] };
    const r = VALIDATE.validate(doc, fakeData);
    if (r.ok && r.warnings.length > 0) {
      ok('不存在的弹头名 -> 警告而不是错误（正确）');
    } else {
      bad('不存在的弹头名 -> 行为不符预期', JSON.stringify(r));
    }
  }

  // (f) duplicate event -> error.
  {
    const doc = withIds(emptyDoc([
      { id: 'OnUpdate', conds: [], acts: [{ t: 'log', a: { level: 'info', msg: 'a' } }] },
      { id: 'OnUpdate', conds: [], acts: [{ t: 'log', a: { level: 'info', msg: 'b' } }] }
    ]));
    const r = VALIDATE.validate(doc);
    if (!r.ok) {
      ok('同一事件出现两次 -> 报错（正确）');
    } else {
      bad('同一事件出现两次 -> 没有报错');
    }
  }

  // (g) out-of-range numbers -> error.
  {
    const doc = withIds(emptyDoc([{
      id: 'OnUpdate',
      conds: [{ t: 'chance', a: { n: 150 } }],
      acts: [{ t: 'log', a: { level: 'info', msg: 'x' } }]
    }]));
    const r = VALIDATE.validate(doc);
    if (!r.ok) {
      ok('概率 150% -> 报错（正确）');
    } else {
      bad('概率 150% -> 没有报错');
    }
  }
}

// --- 5. write fixtures + compile -----------------------------------------

function writeFixtures(cases) {
  section('写出测试夹具');

  fs.mkdirSync(CASES, { recursive: true });
  // Clear stale fixtures so a removed block cannot keep "passing".
  fs.readdirSync(CASES).filter(f => /\.cs$/.test(f)).forEach(f => {
    fs.unlinkSync(path.join(CASES, f));
  });

  const index = [];
  cases.forEach(c => {
    // Each fixture gets a unique class name: two files with the same class are
    // still compiled separately, but unique names make the output readable.
    const doc = JSON.parse(JSON.stringify(c.doc));
    doc.cls = 'Studio_' + c.name.replace(/[^A-Za-z0-9_]/g, '_');
    let text;
    try {
      text = CODEGEN.generate(doc);
    } catch (e) {
      bad(c.name + '：生成失败 ' + e.message);
      return;
    }
    fs.writeFileSync(path.join(CASES, c.name + '.cs'), text, 'utf8');
    index.push({ file: c.name + '.cs', why: c.why });
  });

  fs.writeFileSync(
    path.join(CASES, 'index.json'),
    JSON.stringify(index, null, 2),
    'utf8'
  );

  ok('写出 ' + index.length + ' 个 .cs 到 tests\\cases\\');
}

function runCompiler() {
  section('离线编译全部夹具（与游戏内同一套引用与参数）');

  const verify = path.join(ROOT, 'verify_compile.ps1');
  if (!fs.existsSync(verify)) {
    bad('找不到 verify_compile.ps1');
    return;
  }

  const args = [
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', verify,
    '-PathDir', CASES
  ];

  let out = '';
  let code = 0;
  try {
    const r = cp.spawnSync('powershell', args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
    if (r.error) { throw r.error; }
    out = (r.stdout || '') + (r.stderr || '');
    code = r.status;
  } catch (e) {
    bad('无法调用 PowerShell 编译器：' + e.message,
      '（若是沙箱阻止子进程，请单独运行 verify_compile.ps1 -PathDir tests\\cases）');
    return;
  }

  const lines = out.split(/\r?\n/);
  if (code === 0) {
    const summary = lines.find(l => /^checked /.test(l)) || 'compiled';
    ok('全部夹具编译通过：' + summary.trim());
  } else {
    // Show the failing files and their CS errors; the compiler banner and the
    // per-file [OK] lines are noise here.
    const relevant = lines.filter(l => /\[FAIL\]|error CS|failed:|^ {2}\S/.test(l));
    bad('有夹具编译失败', (relevant.length ? relevant : lines.slice(-40)).join('\n'));
  }
}

// --- main ----------------------------------------------------------------

function main() {
  console.log('Karst script studio - test suite');
  console.log('blocks: ' + Object.keys(REG.EVENTS).length + ' events, '
    + Object.keys(REG.CONDITIONS).length + ' conditions, '
    + Object.keys(REG.ACTIONS).length + ' actions');

  const single = buildSingleBlockCases();
  const combos = buildComboCases();
  const all = single.concat(combos);

  console.log('fixtures planned: ' + all.length
    + ' (' + single.length + ' single-block, ' + combos.length + ' combination)');

  testRoundTrip(all);
  testValidation();
  writeFixtures(all);

  if (wantCompile) {
    runCompiler();
  } else {
    section('跳过编译');
    console.log('  加上 --compile 才会调用 verify_compile.ps1');
  }

  section('结果');
  console.log('  checks: ' + checks + ', failures: ' + failures);
  if (failures > 0) {
    console.log('  FAILED');
    process.exit(1);
  }
  console.log('  PASSED');
}

main();
