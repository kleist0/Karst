/* ===========================================================================
 *  Karst script studio - block metadata
 * ===========================================================================
 *  The block document travels INSIDE the .cs file, as one commented JSON line:
 *
 *      // @karst-studio {"v":1,"cls":"MyScript","binding":"[MTNK]","events":[...]}
 *
 *  WHY IT HAS TO BE IN THE FILE
 *
 *  The page cannot otherwise know how a script was built. It only ever sees
 *  files the user picks; there is no directory listing it can cross-reference,
 *  and even if there were, "which events did I tick last time" is not derivable
 *  from generated code. Without this line, reopening an old script degrades to
 *  "edit a wall of text" - a text editor with extra steps.
 *
 *  Design constraints, all of them load-bearing:
 *    * the line must be a `//` comment, or the script would not compile;
 *    * it must be ONE line, because a block comment spanning lines could be
 *      broken by user edits and JSON cannot contain raw newlines anyway;
 *    * decoding must never throw - a file edited by hand, or written by an
 *      older version, falls back to plain code view instead of an error;
 *    * unknown fields survive a round trip (normalize keeps them), so opening
 *      and re-saving a file does not silently drop data written by a newer
 *      build.
 * ======================================================================== */
(function (root, factory) {
  var api = factory();
  if (typeof module !== 'undefined' && module.exports) { module.exports = api; }
  else { root.KarstMetadata = api; }
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  var PREFIX = '// @karst-studio ';
  var VERSION = 1;

  function encode(doc) {
    return PREFIX + JSON.stringify(doc);
  }

  // True when this source looks like it came out of the studio, whatever the
  // metadata line's state. The generator always writes this first line.
  function looksGenerated(source) {
    return /^\s*\/\/\s*由 Karst 脚本工作室生成/m.test(String(source || ''));
  }

  function decode(source) {
    var text = String(source || '');
    var re = /^[ \t]*\/\/[ \t]*@karst-studio[ \t]+(\{.*\})[ \t]*$/m;
    var m = re.exec(text);
    if (!m) { return null; }
    var doc;
    try {
      doc = JSON.parse(m[1]);
    } catch (e) {
      // Hand-edited JSON (a stray quote, a truncated line): fall back to code
      // view rather than refusing to open the file.
      return null;
    }
    if (!doc || typeof doc !== 'object') { return null; }
    return normalize(doc);
  }

  // Fills in everything the rest of the code assumes exists, without discarding
  // what it does not recognise.
  function normalize(doc) {
    var out = {};
    for (var k in doc) {
      if (Object.prototype.hasOwnProperty.call(doc, k)) { out[k] = doc[k]; }
    }
    out.v = out.v || VERSION;
    out.cls = typeof out.cls === 'string' && out.cls ? out.cls : 'MyScript';
    out.base = out.base || 'techno';
    out.binding = typeof out.binding === 'string' ? out.binding : '';
    if (!Array.isArray(out.events)) { out.events = []; }

    out.events = out.events.map(function (ev) {
      ev = ev || {};
      var e = {};
      for (var k in ev) {
        if (Object.prototype.hasOwnProperty.call(ev, k)) { e[k] = ev[k]; }
      }
      e.id = e.id || 'OnUpdate';
      e.conds = Array.isArray(e.conds) ? e.conds.map(normalizeBlock) : [];
      e.acts = Array.isArray(e.acts) ? e.acts.map(normalizeBlock) : [];
      return e;
    });

    return out;
  }

  function normalizeBlock(b) {
    b = b || {};
    var out = {};
    for (var k in b) {
      if (Object.prototype.hasOwnProperty.call(b, k)) { out[k] = b[k]; }
    }
    out.t = out.t || '';
    out.a = out.a && typeof out.a === 'object' ? out.a : {};
    return out;
  }

  // Block instance ids must be unique and stable: the generated local variable
  // names are derived from them (`wh_b3`), and the id is what makes reopening a
  // file reproduce the same code.
  //
  // Ids already present are kept, and new ones skip over them. Starting the
  // counter at 1 without checking would hand a freshly added block an id that a
  // surviving block already holds - two blocks would then generate the same
  // local name and the file would stop compiling.
  function assignIds(doc, prefix) {
    var p = prefix || 'b';
    var used = {};
    (doc.events || []).forEach(function (ev) {
      (ev.conds || []).forEach(function (c) { if (c.i) { used[c.i] = true; } });
      (ev.acts || []).forEach(function (a) { if (a.i) { used[a.i] = true; } });
    });

    var n = 1;
    function nextId() {
      var id;
      do { id = p + (n++); } while (used[id]);
      used[id] = true;
      return id;
    }

    (doc.events || []).forEach(function (ev) {
      (ev.conds || []).forEach(function (c) { if (!c.i) { c.i = nextId(); } });
      (ev.acts || []).forEach(function (a) { if (!a.i) { a.i = nextId(); } });
    });
    return doc;
  }

  return {
    PREFIX: PREFIX,
    VERSION: VERSION,
    encode: encode,
    decode: decode,
    normalize: normalize,
    assignIds: assignIds,
    looksGenerated: looksGenerated
  };
});
