/* ===========================================================================
 *  Karst script studio - code generator
 * ===========================================================================
 *  doc (the block document)  ->  C# source text.
 *
 *  Pure function, no DOM: the page calls it on every edit, and Node calls it to
 *  produce the compile-check fixtures. If the two ever disagreed, the page
 *  could show green while the test compiled something else - so there is
 *  exactly one implementation, and this is it.
 *
 *  Shape of the generated method bodies:
 *
 *      Pointer<TechnoClass> self = Owner.OwnerObject;      // only if used
 *      if (cond1)                                          // nested, NOT &&
 *      {
 *          if (cond2)
 *          {
 *              ...actions...
 *          }
 *      }
 *
 *  Conditions are nested rather than joined with && on purpose: a condition may
 *  declare an `out` variable (targetIsTechno does), and such a variable is only
 *  in scope inside the if-statement that declares it. An && chain would produce
 *  code that reads an undeclared name - i.e. a compile error.
 * ======================================================================== */
(function (root, factory) {
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = factory(require('./registry.js'), require('./metadata.js'));
  } else {
    root.KarstCodegen = factory(root.KarstRegistry, root.KarstMetadata);
  }
})(typeof globalThis !== 'undefined' ? globalThis : this, function (REG, META) {
  'use strict';

  var INDENT = '    ';

  function indent(n) {
    var s = '';
    for (var i = 0; i < n; i++) { s += INDENT; }
    return s;
  }

  // C# class names: letters/digits/underscore, must not start with a digit.
  function sanitizeClassName(name) {
    var s = String(name === undefined || name === null ? '' : name).replace(/[^A-Za-z0-9_]/g, '');
    if (!s || /^[0-9]/.test(s)) { s = 'MyScript'; }
    return s;
  }

  // The per-event emitter context handed to block emitters. It resolves local
  // variable names and records what the method body needs.
  //
  // `id` is the CURRENT BLOCK's id, not the event's. Generated locals are named
  // after it (`wh_b3`, `pos_b7`) so that two copies of the same block in one
  // event do not collide - naming them after the event instead produced
  // "已在此范围定义了名为 hp_e1 的局部变量" as soon as a user added the same
  // block twice.
  function makeContext(eventId) {
    return {
      eventId: eventId,
      id: 'x',
      _provided: {},
      _usedFields: {},
      _needsSelf: false,

      // Called before a condition's expr() so that (a) the out-variable it
      // declares is registered for later blocks, and (b) local names carry this
      // block's id.
      beginBlock: function (block, index) {
        this.id = REG.safeId(block.i || ('c' + (index === undefined ? 0 : index)));
        var def = REG.CONDITIONS[block.t];
        if (!def) { return; }
        if (def.needs && def.needs.indexOf('self') >= 0) { this._needsSelf = true; }
        if (def.needs) {
          for (var i = 0; i < def.needs.length; i++) {
            if (REG.FIELDS[def.needs[i]]) { this._usedFields[def.needs[i]] = true; }
          }
        }
        if (def.provides) {
          for (var j = 0; j < def.provides.length; j++) {
            this._provided[def.provides[j]] = def.provides[j] + '_' + this.id;
          }
        }
      },

      beginAction: function (block, index) {
        this.id = REG.safeId(block.i || ('a' + (index === undefined ? 0 : index)));
        var def = REG.ACTIONS[block.t];
        if (!def) { return; }
        if (def.needs && def.needs.indexOf('self') >= 0) { this._needsSelf = true; }
        if (def.needs) {
          for (var i = 0; i < def.needs.length; i++) {
            if (REG.FIELDS[def.needs[i]]) { this._usedFields[def.needs[i]] = true; }
          }
        }
      },

      varName: function (name) {
        return this._provided[name] || (name + '_' + this.id);
      },
      hasVar: function (name) {
        return !!this._provided[name];
      },
      usesFields: function () { return this._usedFields; },
      needsSelf: function () { return this._needsSelf; }
    };
  }

  // --- one method body -----------------------------------------------------
  function renderEventBody(ev, ctx) {
    var conds = ev.conds || [];
    var acts = ev.acts || [];

    // PRE-PASS. The declarations at the top of the method (`self`, ...) depend
    // on what the blocks below use, but the blocks are only asked for their code
    // in the second pass. Generating in a single pass therefore wrote bodies
    // that used `self` without declaring it - error CS0103 in every fixture that
    // used a self-reading block. Ask first, write second.
    for (var s1 = 0; s1 < conds.length; s1++) { ctx.beginBlock(conds[s1], s1); }
    for (var s2 = 0; s2 < acts.length; s2++) { ctx.beginAction(acts[s2], s2); }

    var out = [];

    if (ctx.needsSelf()) {
      out.push('Pointer<TechnoClass> self = Owner.OwnerObject;');
      out.push('');
    }

    var depth = 0;

    for (var i = 0; i < conds.length; i++) {
      var c = conds[i];
      var cdef = REG.CONDITIONS[c.t];
      // Re-register in order: a condition's `provides` must only become visible
      // to the blocks that come after it.
      ctx.beginBlock(c, i);
      var expr = cdef ? cdef.expr(c.a || {}, ctx) : 'true /* 未知条件积木 */';
      out.push(indent(depth) + 'if (' + expr + ')');
      out.push(indent(depth) + '{');
      depth++;
    }

    if (acts.length > 0) {
      for (var k = 0; k < acts.length; k++) {
        var a = acts[k];
        var adef = REG.ACTIONS[a.t];
        ctx.beginAction(a, k);
        if (!adef) {
          out.push(indent(depth) + '// 未知动作积木：' + a.t);
          continue;
        }
        var code = adef.emit(a.a || {}, ctx) || [];
        for (var m = 0; m < code.length; m++) {
          var line = code[m];
          if (line === '') {
            out.push('');
          } else if (/^\s*\/\//.test(line)) {
            // comment lines follow the block's own relative indent
            out.push(indent(depth) + line.replace(/^\s+/, ''));
          } else {
            out.push(indent(depth) + line);
          }
        }
      }
    } else {
      out.push(indent(depth) + '// 这个事件还没有动作积木。');
      out.push(indent(depth) + '// （在左边"动作"里挑一个加上去，右边的代码会立刻出现。）');
    }

    for (var j = conds.length - 1; j >= 0; j--) {
      depth--;
      out.push(indent(depth) + '}');
    }

    // Blocks may start their snippet with a blank line for breathing room in the
    // middle of a body, but that must not leave blank runs or a blank first line.
    var cleaned = [];
    for (var q = 0; q < out.length; q++) {
      if (out[q] === '' && (cleaned.length === 0 || cleaned[cleaned.length - 1] === '')) { continue; }
      cleaned.push(out[q]);
    }
    while (cleaned.length > 0 && cleaned[cleaned.length - 1] === '') { cleaned.pop(); }

    return cleaned;
  }

  // --- whole file ----------------------------------------------------------
  function generate(doc) {
    doc = doc || {};
    var cls = sanitizeClassName(doc.cls);
    var events = doc.events || [];

    var lines = [];

    // Header. The metadata line is commented out so the script still compiles;
    // it is what lets the page rebuild the block view when the file is reopened.
    lines.push('// 由 Karst 脚本工作室生成 —— 用积木搭出来的脚本。');
    lines.push('// 放到 <游戏目录>\\karstpatcher\\Scripts\\ 下即可，Karst 会自动编译。');
    lines.push('//');
    lines.push('// 下面这行是积木数据，用来"再次编辑"时还原积木视图。');
    lines.push('// 删掉它脚本照样能编译，但页面就只能当普通代码编辑器用了：');
    lines.push(META.encode(doc));
    lines.push('');

    lines.push('using System;');
    lines.push('using KarstPatcher;            // Logger');
    lines.push('using Extension.Ext;             // TechnoExt');
    lines.push('using Extension.Script;          // TechnoScriptable');
    lines.push('using Extension.Utilities;       // ExtensionReference（记住别的对象时要用）');
    lines.push('using PatcherYRpp;               // Pointer<T> / CoordStruct 等');
    lines.push('using PatcherYRpp.Utilities;     // MathEx（随机数等）');
    lines.push('');
    lines.push('namespace Scripts');
    lines.push('{');
    lines.push(INDENT + 'public class ' + cls + ' : TechnoScriptable');
    lines.push(INDENT + '{');
    lines.push(INDENT + INDENT + 'public ' + cls + '(TechnoExt owner) : base(owner) { }');

    // --- methods (emitted first so we learn which state fields are needed) --
    var methodBlocks = [];
    var usedFields = {};

    for (var e = 0; e < events.length; e++) {
      var ev = events[e];
      var edef = REG.EVENTS[ev.id];
      if (!edef) { continue; }

      var ctx = makeContext(ev.id);
      var body = renderEventBody(ev, ctx);

      var f = ctx.usesFields();
      for (var key in f) {
        if (Object.prototype.hasOwnProperty.call(f, key)) { usedFields[key] = true; }
      }

      methodBlocks.push({ def: edef, body: body });
    }

    // --- state fields -------------------------------------------------------
    var fieldLines = [];
    for (var fname in usedFields) {
      if (!Object.prototype.hasOwnProperty.call(usedFields, fname)) { continue; }
      var fdef = REG.FIELDS[fname];
      if (!fdef) { continue; }
      if (fdef.comment) { fieldLines.push(INDENT + INDENT + fdef.comment); }
      fieldLines.push(INDENT + INDENT + fdef.decl);
    }

    if (fieldLines.length > 0) {
      lines.push('');
      fieldLines.forEach(function (l) { lines.push(l); });
    }

    // --- method bodies ------------------------------------------------------
    if (methodBlocks.length === 0) {
      lines.push('');
      lines.push(INDENT + INDENT + '// 还没有事件积木：先在左边"事件"里挑一个（比如"每帧 OnUpdate"）。');
    }

    methodBlocks.forEach(function (mb) {
      lines.push('');
      var sigLines = mb.def.sig.split('\n');
      lines.push(INDENT + INDENT + sigLines[0]);
      for (var i = 1; i < sigLines.length; i++) {
        lines.push(INDENT + INDENT + sigLines[i]);
      }
      lines.push(INDENT + INDENT + '{');
      mb.body.forEach(function (l) {
        lines.push(l === '' ? '' : INDENT + INDENT + INDENT + l);
      });
      lines.push(INDENT + INDENT + '}');
    });

    lines.push(INDENT + '}');
    lines.push('}');
    lines.push('');

    return lines.join('\n');
  }

  // The rulesmd.ini line the user has to add, or null when no unit is chosen.
  function bindingHint(doc) {
    var cls = sanitizeClassName(doc && doc.cls);
    var unit = String((doc && doc.binding) || '').trim();
    if (!unit) { return null; }
    if (unit.charAt(0) !== '[') { unit = '[' + unit + ']'; }
    if (unit.charAt(unit.length - 1) !== ']') { unit = unit + ']'; }
    return unit + '\nScripts=' + cls;
  }

  return {
    generate: generate,
    bindingHint: bindingHint,
    sanitizeClassName: sanitizeClassName
  };
});
