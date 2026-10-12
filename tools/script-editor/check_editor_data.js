// Checks the JSON the editor page generates.
//
// The page's value is the JSON it produces, and that JSON has to satisfy the
// same rules the C++ loader applies. This mirrors the page's build() logic in
// plain JS so it can be run headlessly: a form can look right and still emit a
// script the loader refuses, which is exactly the failure worth catching before
// a user hits it.
//
// Usage:  node tools\script-editor\check_editor_data.js <path-to-script-editor.html>
//
// It also verifies the page's embedded data is present and sane (the building
// list must contain the buildings the examples use).

const fs = require('fs');

const fail = [];
function check(what, ok) {
  console.log('  ' + what.padEnd(58) + (ok ? 'ok' : 'FAILED'));
  if (!ok) fail.push(what);
}

// --- the page's build() logic, mirrored ------------------------------------
function build(scripts) {
  const out = [];
  for (const s of scripts) {
    const id = (s.buildingFree || s.building || '').trim();
    if (!id) continue;

    const o = { type: 'appearance', building: id };
    if (s.name) o.name = s.name;

    if (s.mode === 'byCountry') {
      const map = {};
      for (const p of (s.byCountry || [])) {
        if (p.country && p.section) map[p.country] = p.section;
      }
      if (Object.keys(map).length === 0) continue;
      o.byCountry = map;
    } else {
      const look = (s.lookFree || s.look || '').trim();
      if (!look) continue;
      o.look = look;
      if (s.footprint === 'self') o.foundationFrom = 'self';
    }
    out.push(o);
  }
  return out;
}

// --- the loader's rules, mirrored ------------------------------------------
// Mirrors ScriptLoader::AddAppearanceRule / ValidateScript: a script needs a
// building, and exactly one of look / byCountry; "self" becomes KeepFoundation.
function validate(doc) {
  const errs = [];
  if (!Array.isArray(doc.scripts)) { errs.push('scripts must be an array'); return errs; }

  for (const s of doc.scripts) {
    const where = '[' + (s.building || '?') + ']';
    if (!s.building) { errs.push(where + ' missing building'); continue; }

    const hasLook = typeof s.look === 'string' && s.look.trim() !== '';
    const hasByCountry = s.byCountry && typeof s.byCountry === 'object' &&
      Object.keys(s.byCountry).length > 0;

    if (!hasLook && !hasByCountry) {
      errs.push(where + ' needs look or byCountry');
      continue;
    }

    if (hasByCountry) {
      for (const [country, section] of Object.entries(s.byCountry)) {
        if (!country || !section || String(section).trim() === '')
          errs.push(where + ' has an empty country entry');
        if (String(section).toLowerCase() === 'self')
          errs.push(where + ' "self" is not valid inside byCountry');
      }
    }

    if (hasLook) {
      const f = s.foundationFrom;
      if (f !== undefined && f !== 'self' && String(f).trim() === '')
        errs.push(where + ' foundationFrom is present but empty');
    }
  }
  return errs;
}

// --- run -------------------------------------------------------------------
const pagePath = process.argv[2];
if (!pagePath) {
  console.error('usage: node check_editor_data.js <script-editor.html>');
  process.exit(2);
}

const html = fs.readFileSync(pagePath, 'utf8');

console.log('editor page:\n');
check('page is not empty', html.length > 1000);
check('no unreplaced data placeholder',
  !html.includes('/*__BUILDINGS__*/') && !html.includes('/*__ARTS__*/'));

const b = html.match(/const BUILDINGS = \[(.*?)\];/s);
const a = html.match(/const ARTS = \[(.*?)\];/s);
check('building list embedded', !!b);
check('ART section list embedded', !!a);

if (b && a) {
  const buildings = JSON.parse('[' + b[1] + ']');
  const arts = JSON.parse('[' + a[1] + ']');

  console.log('\nembedded data:\n');
  check('building list is substantial (>300)', buildings.length > 300);
  check('ART list is substantial (>800)', arts.length > 800);
  // The entries the shipped example uses must be offerable in the form.
  for (const id of ['GAPILE', 'GAPOWR', 'GACNST']) {
    check('  building list contains ' + id, buildings.includes(id));
  }
  for (const sec of ['YABRCK', 'NAHAND', 'YAPOWR', 'NAPOWR']) {
    check('  ART list contains ' + sec, arts.includes(sec));
  }
}

console.log('\ngenerated JSON:\n');

// A deliberately wide set: the two shapes, the self option, and an entry with
// free-text ids (the form allows typing one that is not in the lists).
const cases = [
  {
    name: 'a single look',
    scripts: [{ mode: 'look', building: 'GAPILE', look: 'YABRCK', footprint: 'follow' }]
  },
  {
    name: 'look with the footprint kept',
    scripts: [{ mode: 'look', building: 'GAPILE', look: 'YABRCK', footprint: 'self' }]
  },
  {
    name: 'per-country',
    scripts: [{
      mode: 'byCountry', building: 'GAPOWR',
      byCountry: [{ country: 'Americans', section: 'YAPOWR' },
                  { country: 'Alliance', section: 'NAPOWR' }]
    }]
  },
  {
    name: 'free-text building and section',
    scripts: [{ mode: 'look', buildingFree: 'GAWEAT', lookFree: 'GAWETH', footprint: 'follow' }]
  },
  {
    name: 'a half-filled script is dropped',
    scripts: [{ mode: 'look', building: 'GAPILE', look: '' }]
  },
  {
    name: 'a half-filled country is dropped',
    scripts: [{
      mode: 'byCountry', building: 'GAPOWR',
      byCountry: [{ country: 'Americans', section: '' }]
    }]
  }
];

for (const c of cases) {
  const doc = { scripts: build(c.scripts) };
  const text = JSON.stringify(doc, null, 2);
  const errs = validate(doc);

  check(c.name + ': emits valid JSON', (() => {
    try { JSON.parse(text); return true; } catch (e) { return false; }
  })());

  const expectEmpty = c.name.startsWith('a half-filled');
  if (expectEmpty) {
    check('  ' + c.name + ': emits nothing', doc.scripts.length === 0);
  }

  check('  passes the loader rules', errs.length === 0);
  if (errs.length) console.log('       ' + errs.join('; '));
}

console.log('\n' + (fail.length === 0
  ? 'ALL CHECKS PASSED'
  : 'CHECKS FAILED (' + fail.length + ')'));

process.exit(fail.length === 0 ? 0 : 1);
