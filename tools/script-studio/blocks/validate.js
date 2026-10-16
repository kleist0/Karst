/* ===========================================================================
 *  Karst script studio - document validation
 * ===========================================================================
 *  Runs before the code is shown, so the page can say "this will not compile"
 *  where the user is looking, instead of leaving a CS error for karstpatcher.log.
 *
 *  Two kinds of finding:
 *    error   - the generated C# would not compile, or would reference a name
 *              that does not exist (e.g. an event variable the event lacks).
 *    warning - it compiles, but will probably not do what the user expects
 *              (a name that is not in the game's rules, an empty event, ...).
 *
 *  The needs/provides check is the important one: "打击记住的目标" only makes
 *  sense after "记住目标", and using a pTarget-based block in OnUpdate is a
 *  compile error. Catching that here is what keeps the promise that anything the
 *  page shows as OK actually compiles.
 * ======================================================================== */
(function (root, factory) {
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = factory(require('./registry.js'));
  } else {
    root.KarstValidate = factory(root.KarstRegistry);
  }
})(typeof globalThis !== 'undefined' ? globalThis : this, function (REG) {
  'use strict';

  // C# keywords that cannot be a class name.
  var CS_KEYWORDS = ('abstract as base bool break byte case catch char checked class const continue decimal ' +
    'default delegate do double else enum event explicit extern false finally fixed float for foreach goto if ' +
    'implicit in int interface internal is lock long namespace new null object operator out override params ' +
    'private protected public readonly ref return sbyte sealed short sizeof stackalloc static string struct ' +
    'switch this throw true try typeof uint ulong unchecked unsafe ushort using virtual void volatile while')
    .split(' ');

  function validate(doc, gameData) {
    var errors = [];
    var warnings = [];
    doc = doc || {};

    // --- class name --------------------------------------------------------
    var cls = String(doc.cls === undefined || doc.cls === null ? '' : doc.cls).trim();
    if (!cls) {
      errors.push('类名不能为空 —— 它就是 rulesmd.ini 里 Scripts= 要填的值。');
    } else {
      if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(cls)) {
        errors.push('类名 "' + cls + '" 不是合法的 C# 类名（只能用字母、数字、下划线，且不能以数字开头）。');
      } else if (CS_KEYWORDS.indexOf(cls) >= 0) {
        errors.push('类名 "' + cls + '" 是 C# 关键字，换一个。');
      }
    }

    var events = doc.events || [];
    if (events.length === 0) {
      warnings.push('还没有事件积木，生成的类里没有任何事件，挂上去也不会有反应。');
    }

    var seenEvents = {};
    var anyAction = false;

    events.forEach(function (ev, ei) {
      var edef = REG.EVENTS[ev.id];
      var where = '事件 ' + (ei + 1);
      if (!edef) {
        errors.push(where + '：未知事件 "' + ev.id + '"。');
        return;
      }
      where = '「' + edef.label + '」';

      if (seenEvents[ev.id]) {
        errors.push(where + '：同一个事件出现了两次，C# 不允许重复定义，请合并成一个。');
      }
      seenEvents[ev.id] = true;

      // Variables available to blocks in this event: the event's own params,
      // plus `self` (always available) plus each condition's `provides`.
      var available = { self: true };
      for (var p in edef.params) {
        if (Object.prototype.hasOwnProperty.call(edef.params, p)) { available[p] = true; }
      }
      for (var f in REG.FIELDS) {
        if (Object.prototype.hasOwnProperty.call(REG.FIELDS, f)) { available[f] = true; }
      }

      (ev.conds || []).forEach(function (c) {
        var def = REG.CONDITIONS[c.t];
        if (!def) {
          errors.push(where + '：未知条件积木 "' + c.t + '"。');
          return;
        }
        checkBlock(def, c, where, '条件', available, ev.id, errors, warnings, gameData);
        (def.provides || []).forEach(function (name) { available[name] = true; });
      });

      (ev.acts || []).forEach(function (a) {
        var def = REG.ACTIONS[a.t];
        if (!def) {
          errors.push(where + '：未知动作积木 "' + a.t + '"。');
          return;
        }
        anyAction = true;
        checkBlock(def, a, where, '动作', available, ev.id, errors, warnings, gameData);
      });

      if ((ev.acts || []).length === 0) {
        warnings.push(where + '：还没有动作积木，这个方法体是空的。');
      }
    });

    if (events.length > 0 && !anyAction) {
      warnings.push('所有事件都还没有动作积木。');
    }

    return {
      errors: errors,
      warnings: warnings,
      ok: errors.length === 0
    };
  }

  // `eventId` is the event's key (e.g. 'OnFire'). It is passed as a string, not
  // as the EVENTS entry: the entries are keyed by id and carry no `id` field of
  // their own - reading `edef.id` silently yielded undefined and made every
  // event-restricted block look misplaced.
  function checkBlock(def, block, where, kind, available, eventId, errors, warnings, gameData) {
    // Event restriction (e.g. pDamage only exists in OnReceiveDamage).
    if (def.events && def.events.indexOf(eventId) < 0) {
      var names = def.events.map(function (id) {
        return REG.EVENTS[id] ? REG.EVENTS[id].label : id;
      }).join('、');
      errors.push(where + '：' + kind + '「' + def.label + '」只能用在：' + names + '。'
        + '（它需要那个事件才有的变量，放这里会编译不过。）');
    }

    // Required variables.
    (def.needs || []).forEach(function (need) {
      if (!available[need]) {
        errors.push(where + '：' + kind + '「' + def.label + '」需要先有 "'
          + friendlyVar(need) + '"，请在这个块之前加上提供它的条件积木。');
      }
    });

    // Arguments.
    (def.args || []).forEach(function (arg) {
      var v = block.a ? block.a[arg.k] : undefined;
      if (arg.type === 'int') {
        if (v === undefined || v === '' || v === null) {
          warnings.push(where + '：' + kind + '「' + def.label + '」的「' + arg.label + '」没填，按 '
            + arg.def + ' 处理。');
          return;
        }
        var n = parseInt(v, 10);
        if (isNaN(n)) {
          errors.push(where + '：' + kind + '「' + def.label + '」的「' + arg.label + '」必须是整数。');
          return;
        }
        if (arg.min !== undefined && n < arg.min) {
          errors.push(where + '：' + kind + '「' + def.label + '」的「' + arg.label + '」不能小于 ' + arg.min + '。');
        }
        if (arg.max !== undefined && n > arg.max) {
          errors.push(where + '：' + kind + '「' + def.label + '」的「' + arg.label + '」不能大于 ' + arg.max + '。');
        }
      } else if (arg.type === 'game') {
        var name = String(v === undefined || v === null ? '' : v).trim();
        if (!name) {
          warnings.push(where + '：' + kind + '「' + def.label + '」的「' + arg.label + '」没填，'
            + '运行时会找不到任何东西（脚本不报错，但也没效果）。');
        } else if (gameData && arg.list && Array.isArray(gameData[arg.list])) {
          if (!containsIgnoreCase(gameData[arg.list], name)) {
            warnings.push(where + '：' + kind + '「' + def.label + '」的「' + arg.label + '」= "'
              + name + '" 不在本机 rules/art 文件里。写错的话运行时会静默失效。');
          }
        }
      }
    });
  }

  function friendlyVar(name) {
    if (name === 'pTarget') { return '目标（开火事件自带）'; }
    if (name === 'pDamage') { return '本次伤害（受伤事件自带）'; }
    if (name === 'targetTechno') { return '一个"目标是单位/建筑"条件'; }
    if (name === 'rememberedTarget') { return '记住的目标（用"记住目标"动作）'; }
    return name;
  }

  function containsIgnoreCase(list, value) {
    for (var i = 0; i < list.length; i++) {
      if (String(list[i]).toLowerCase() === value.toLowerCase()) { return true; }
    }
    return false;
  }

  return { validate: validate, CS_KEYWORDS: CS_KEYWORDS };
});
