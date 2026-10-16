/* ===========================================================================
 *  Karst script studio - demo script generator
 * ===========================================================================
 *      node tools\script-studio\tests\make_demo.js
 *      node tools\script-studio\tests\make_demo.js --deploy "E:\_game\RA2\yr_karst_test\karstpatcher\Scripts"
 *
 *  Writes demo\*.cs - three scripts built the way the page builds them, through
 *  the same generator. They exist for two reasons:
 *
 *    1. as the examples a new user can copy, and as the proof that the block
 *       library covers real scripting tasks end to end;
 *    2. as the payload for the in-game acceptance test: drop them into
 *       karstpatcher\Scripts, start the game, and karstpatcher.log must say
 *       "compile file succeed!" for each - which is the only evidence that
 *       matters, since that is the compiler the user will actually run.
 *
 *  The .cs files are generated, never hand-edited: if the block library changes,
 *  re-running this keeps the demos honest.
 * ======================================================================== */

'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const OUT = path.join(ROOT, 'demo');

const META = require(path.join(ROOT, 'blocks', 'metadata.js'));
const CODEGEN = require(path.join(ROOT, 'blocks', 'codegen.js'));
const VALIDATE = require(path.join(ROOT, 'blocks', 'validate.js'));

const DEMOS = [
  {
    file: 'StudioDemoLog.cs',
    why: '入门：每 60 帧在日志里打一次自己的坐标和血量',
    doc: {
      v: 1, cls: 'StudioDemoLog', base: 'techno', binding: 'MTNK',
      events: [{
        id: 'OnUpdate',
        conds: [{ t: 'everyNFrames', a: { n: 60 }, i: 'b1' }],
        acts: [
          { t: 'log', a: { level: 'info', msg: 'StudioDemoLog 还活着' }, i: 'b2' },
          { t: 'logSelf', a: {}, i: 'b3' }
        ]
      }]
    }
  },
  {
    file: 'StudioDemoMissileRain.cs',
    why: '进阶：开火时记住目标，之后每 15 帧对目标砸一发范围伤害（等价于手写的 MissileRain）',
    doc: {
      v: 1, cls: 'StudioDemoMissileRain', base: 'techno', binding: 'MTNK',
      events: [
        {
          id: 'OnFire',
          conds: [],
          acts: [
            { t: 'rememberTarget', a: {}, i: 'b1' },
            { t: 'log', a: { level: 'info', msg: '锁定目标' }, i: 'b2' }
          ]
        },
        {
          id: 'OnUpdate',
          conds: [
            { t: 'hasRemembered', a: {}, i: 'b3' },
            { t: 'everyNFrames', a: { n: 15 }, i: 'b4' }
          ],
          acts: [
            { t: 'damageRemembered', a: { wh: 'BlimpHE', dmg: 20 }, i: 'b5' },
            { t: 'logRememberedPos', a: {}, i: 'b6' }
          ]
        }
      ]
    }
  },
  {
    file: 'StudioDemoArmor.cs',
    why: '条件组合：受伤减半；血量低于 30% 时每 30 帧有 25% 概率在自己脚下引爆',
    doc: {
      v: 1, cls: 'StudioDemoArmor', base: 'techno', binding: 'MTNK',
      events: [
        {
          id: 'OnReceiveDamage',
          conds: [{ t: 'damageOver', a: { n: 20 }, i: 'b1' }],
          acts: [
            { t: 'modifyDamage', a: { mode: 'mul', v: 50 }, i: 'b2' },
            { t: 'log', a: { level: 'warn', msg: '伤害减半' }, i: 'b3' }
          ]
        },
        {
          id: 'OnUpdate',
          conds: [
            { t: 'hpBelow', a: { n: 30 }, i: 'b4' },
            { t: 'everyNFrames', a: { n: 30 }, i: 'b5' },
            { t: 'chance', a: { n: 25 }, i: 'b6' }
          ],
          acts: [
            { t: 'damageAreaAtSelf', a: { wh: 'BlimpHE', dmg: 150 }, i: 'b7' },
            { t: 'log', a: { level: 'error', msg: '重伤自爆！' }, i: 'b8' }
          ]
        }
      ]
    }
  }
];

function main() {
  fs.mkdirSync(OUT, { recursive: true });

  const deployIdx = process.argv.indexOf('--deploy');
  const deployDir = deployIdx >= 0 ? process.argv[deployIdx + 1] : null;

  let failures = 0;

  // Validate before writing: a demo that the studio itself would flag as broken
  // must not be shipped as an example.
  DEMOS.forEach(d => {
    const r = VALIDATE.validate(d.doc);
    if (!r.ok) {
      console.log('[FAIL] ' + d.file + ' 校验不过：' + r.errors.join('；'));
      failures++;
    } else if (r.warnings.length) {
      console.log('[warn] ' + d.file + '：' + r.warnings.join('；'));
    }
  });

  DEMOS.forEach(d => {
    META.assignIds(d.doc);
    const code = CODEGEN.generate(d.doc);
    fs.writeFileSync(path.join(OUT, d.file), code, 'utf8');
    const lines = code.split('\n').length;
    console.log('wrote demo\\' + d.file + '  (' + lines + ' 行) - ' + d.why);

    // The metadata must survive, or reopening the demo in the studio would drop
    // to code view - the exact failure this metadata line exists to prevent.
    const back = META.decode(code);
    if (!back || back.cls !== d.doc.cls) {
      console.log('  [FAIL] 元数据往返失败');
      failures++;
    }
  });

  // An index the acceptance step can read.
  fs.writeFileSync(path.join(OUT, 'index.json'), JSON.stringify(
    DEMOS.map(d => ({ file: d.file, cls: d.doc.cls, binding: d.doc.binding, why: d.why })), null, 2), 'utf8');

  if (deployDir) {
    if (!fs.existsSync(deployDir)) {
      console.log('[FAIL] 目标目录不存在：' + deployDir);
      process.exit(1);
    }
    DEMOS.forEach(d => {
      const dst = path.join(deployDir, d.file);
      fs.copyFileSync(path.join(OUT, d.file), dst);
      console.log('deployed -> ' + dst);
    });
  }

  console.log('');
  if (failures) { console.log('FAILED (' + failures + ')'); process.exit(1); }
  console.log('OK: ' + DEMOS.length + ' 个演示脚本已生成到 demo\\');
  console.log('下一步（可选）：');
  console.log('  node tools\\script-studio\\tests\\make_demo.js --deploy "<游戏目录>\\karstpatcher\\Scripts"');
  console.log('  powershell -ExecutionPolicy Bypass -File tools\\script-studio\\verify_compile.ps1 -PathDir tools\\script-studio\\demo');
}

main();
