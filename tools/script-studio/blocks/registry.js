/* ===========================================================================
 *  Karst script studio - block registry
 * ===========================================================================
 *  This file is the single source of truth for the block library. It is used
 *  by the page (inlined by build_studio.ps1) AND by tests\build_cases.js under
 *  Node, which is what makes "every block compiles" a checkable claim: the
 *  test generates one .cs per block through this very code path and compiles
 *  them all with verify_compile.ps1.
 *
 *  A block is data plus two optional code emitters:
 *      expr(args, ctx)  -> a C# *boolean expression*      (conditions)
 *      emit(args, ctx)  -> an array of C# *statement lines* (actions)
 *
 *  Every snippet below was written against the verified API inventory
 *  (Karst\docs\脚本API参考.md) and is compiled by tests\build_cases.js.
 *  Being careful here is not pedantry: the failure mode of a wrong snippet is
 *  an `error CS...` in the user's karstpatcher.log, long after the page said "ok".
 *
 *  Design rule taken from the inventory - never generate a bare `.Ref` on a
 *  pointer that can be null. A null `Ref` is an AccessViolationException, which
 *  the CLR does not let anyone catch: the game exits. So every looked-up
 *  pointer (ABSTRACTTYPE_ARRAY.Find, ExtMap.Find, CastTo*) is null-checked in
 *  the generated code, even when that costs an extra line.
 * ======================================================================== */
(function (root, factory) {
  var api = factory();
  if (typeof module !== 'undefined' && module.exports) { module.exports = api; }
  else { root.KarstRegistry = api; }
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  var VERSION = 1;

  // --- helpers -------------------------------------------------------------

  // C# string literal (regular, not verbatim: escapes are predictable).
  function csStr(s) {
    return '"' + String(s === undefined || s === null ? '' : s)
      .replace(/\\/g, '\\\\')
      .replace(/"/g, '\\"')
      .replace(/\r\n|\r|\n/g, '\\n') + '"';
  }

  function csInt(v, def) {
    var n = parseInt(v, 10);
    if (isNaN(n)) { n = def; }
    return n;
  }

  // A C# identifier safe to use as a local variable suffix.
  function safeId(id) {
    var s = String(id || 'x').replace(/[^A-Za-z0-9_]/g, '');
    if (!s || /^[0-9]/.test(s)) { s = 'b' + s; }
    return s;
  }

  // =========================================================================
  //  Events
  // =========================================================================
  //  Signatures are copied from Extension\Script\Scriptable.cs. OnUpdate is NOT
  //  declared there - it comes from Component.OnUpdate() - but it is virtual,
  //  so `override` is correct for it too.
  //
  //  `params` lists the variables the event hands to the block bodies.
  // =========================================================================
  var EVENTS = {
    OnUpdate: {
      label: '每帧 OnUpdate',
      short: '每帧',
      hint: '每一帧都会跑。做持续效果、计时、追踪目标时用这个。',
      sig: 'public override void OnUpdate()',
      params: {},
      note: null
    },
    OnFire: {
      label: '开火时 OnFire',
      short: '开火',
      hint: '这个单位开火时跑一次。pTarget 是开火目标。',
      sig: 'public override void OnFire(Pointer<AbstractClass> pTarget, int weaponIndex)',
      params: { pTarget: 'Pointer<AbstractClass>', weaponIndex: 'int' },
      note: '⚠️ 不要在这个事件里再让单位开火（会无限递归）。'
    },
    OnReceiveDamage: {
      label: '受伤时 OnReceiveDamage',
      short: '受伤',
      hint: '受到伤害时跑。改 pDamage 就能改伤害值（可以免疫或加倍）。',
      sig: 'public override void OnReceiveDamage(Pointer<int> pDamage, int DistanceFromEpicenter,\n'
         + '            Pointer<WarheadTypeClass> pWH, Pointer<ObjectClass> pAttacker,\n'
         + '            bool IgnoreDefenses, bool PreventPassengerEscape, Pointer<HouseClass> pAttackingHouse)',
      params: {
        pDamage: 'Pointer<int>',
        DistanceFromEpicenter: 'int',
        pWH: 'Pointer<WarheadTypeClass>',
        pAttacker: 'Pointer<ObjectClass>',
        IgnoreDefenses: 'bool',
        PreventPassengerEscape: 'bool',
        pAttackingHouse: 'Pointer<HouseClass>'
      },
      note: '⚠️ 这个事件在单位的受伤流程里跑，别在里面做重活。'
    },
    OnPut: {
      label: '出场时 OnPut',
      short: '出场',
      hint: '单位第一次出现在世界上时跑一次（用它做初始化）。',
      sig: 'public override void OnPut(CoordStruct coord, Direction faceDir)',
      params: { coord: 'CoordStruct', faceDir: 'Direction' },
      // Measured: the driver passes default(Direction) here, so faceDir is
      // always north. Saying so prevents "why is my unit always facing up".
      note: '⚠️ faceDir 由驱动传默认值（恒为北），要真实朝向请读 self.Ref.Facing。'
    },
    OnRemove: {
      label: '被移除时 OnRemove',
      short: '移除',
      hint: '单位被移除/摧毁时跑一次。适合打日志收尾。',
      sig: 'public override void OnRemove()',
      params: {},
      note: '⚠️ 此时对象正在销毁，别去读它的引擎字段（可能会崩），写日志最安全。'
    }
  };

  // =========================================================================
  //  Conditions
  // =========================================================================
  //  Each returns a C# boolean expression. `needs` names variables that must be
  //  available (either from the event's params, or `provides`d by an earlier
  //  condition in the same event).
  // =========================================================================
  var CONDITIONS = {
    everyNFrames: {
      label: '每 N 帧',
      group: '时机',
      hint: '每过 N 帧才通过一次（配合"每帧"事件用）。',
      needs: [],
      args: [{ k: 'n', label: '帧数', type: 'int', def: 30, min: 1, max: 100000 }],
      expr: function (a) {
        return '(Game.CurrentFrame % ' + csInt(a.n, 30) + ') == 0';
      }
    },

    chance: {
      label: '概率 N%',
      group: '时机',
      hint: '每次跑到这里都有 N% 的机会通过。',
      needs: [],
      args: [{ k: 'n', label: '概率(%)', type: 'int', def: 10, min: 0, max: 100 }],
      expr: function (a) {
        return 'MathEx.Random.Next(0, 100) < ' + csInt(a.n, 10);
      }
    },

    hpBelow: {
      label: '自己血量低于 N%',
      group: '自己的状态',
      hint: '用当前血量和最大血量的比例判断。',
      needs: ['self'],
      args: [{ k: 'n', label: '百分比', type: 'int', def: 50, min: 0, max: 100 }],
      // Multiplication instead of division: no divide-by-zero when Strength is 0,
      // and no integer truncation surprises.
      expr: function (a) {
        return '(self.Ref.Base.Health * 100) < (' + csInt(a.n, 50) + ' * self.Ref.Type.Ref.Base.Strength)';
      }
    },

    hpAbove: {
      label: '自己血量高于 N%',
      group: '自己的状态',
      hint: '同上，方向相反。',
      needs: ['self'],
      args: [{ k: 'n', label: '百分比', type: 'int', def: 80, min: 0, max: 100 }],
      expr: function (a) {
        return '(self.Ref.Base.Health * 100) > (' + csInt(a.n, 80) + ' * self.Ref.Type.Ref.Base.Strength)';
      }
    },

    selfTypeIs: {
      label: '自己的类型是…',
      group: '自己的状态',
      hint: '只有一个类型 ID 时用；多个类型请用"自己的类型在列表里"。',
      needs: ['self'],
      args: [{ k: 'id', label: '类型 ID', type: 'game', list: 'units', def: 'MTNK' }],
      expr: function (a) {
        return 'self.Ref.Type.Ref.Base.Base.ID == ' + csStr(a.id);
      }
    },

    selfTypeIn: {
      label: '自己的类型在列表里',
      group: '自己的状态',
      hint: '逗号分隔多个类型 ID，例如 MTNK,GRIZZLY。',
      needs: ['self'],
      args: [{ k: 'ids', label: '类型 ID 列表', type: 'text', def: 'MTNK' }],
      // A local array + Array.IndexOf keeps the expression single-line and
      // avoids a long || chain. `System` is already imported.
      uses: ['Array'],
      expr: function (a, ctx) {
        var list = String(a.ids || '').split(',').map(function (s) { return s.trim(); })
          .filter(function (s) { return s.length > 0; });
        if (list.length === 0) { list = ['MTNK']; }
        var arr = 'new string[] { ' + list.map(csStr).join(', ') + ' }';
        return 'Array.IndexOf(' + arr + ', self.Ref.Type.Ref.Base.Base.ID) >= 0';
      }
    },

    // Provides a Pointer<TechnoClass> for the following actions. The out
    // variable is declared by the condition itself, which is why conditions are
    // emitted as nested ifs rather than one && chain.
    targetIsTechno: {
      label: '目标是单位/建筑',
      group: '目标',
      hint: '开火或被打时，判断对方是不是一个单位/建筑（而不是墙、子弹、地形）。',
      needs: ['pTarget'],
      provides: ['targetTechno'],
      events: ['OnFire'],
      expr: function (a, ctx) {
        return 'pTarget.CastToTechno(out Pointer<TechnoClass> ' + ctx.varName('targetTechno') + ')';
      }
    },

    hasRemembered: {
      label: '记住的目标还在',
      group: '目标',
      hint: '之前用"记住目标"存下来的那个目标是否还活着。',
      needs: ['rememberedTarget'],
      expr: function () {
        return '_rememberedTarget.Get() != null';
      }
    },

    damageOver: {
      label: '本次伤害大于 N',
      group: '自己的状态',
      hint: '只用在"受伤时"事件里。',
      needs: ['pDamage'],
      events: ['OnReceiveDamage'],
      args: [{ k: 'n', label: '伤害值', type: 'int', def: 100, min: 0 }],
      expr: function (a) {
        return 'pDamage.Ref > ' + csInt(a.n, 100);
      }
    }
  };

  // =========================================================================
  //  Actions
  // =========================================================================
  //  Each returns an array of C# statement lines with *relative* indentation.
  //  codegen prefixes the enclosing indent to every line, so multi-line bodies
  //  keep their shape.
  // =========================================================================
  var ACTIONS = {
    log: {
      label: '打日志',
      group: '日志与调试',
      hint: '输出到 karstpatcher\\karstpatcher.log，用来确认脚本真的跑起来了。',
      needs: [],
      args: [
        { k: 'level', label: '级别', type: 'enum', def: 'info', opts: [['info', '普通'], ['warn', '警告'], ['error', '错误']] },
        { k: 'msg', label: '内容', type: 'text', def: 'script is running' }
      ],
      emit: function (a) {
        var fn = a.level === 'warn' ? 'LogWarning' : (a.level === 'error' ? 'LogError' : 'Log');
        return ['Logger.' + fn + '(' + csStr(a.msg) + ');'];
      }
    },

    logSelf: {
      label: '记录自己的坐标/血量',
      group: '日志与调试',
      hint: '一次打出坐标和当前血量，排查位置类问题时最有用。',
      needs: ['self'],
      args: [],
      emit: function (a, ctx) {
        var p = 'pos_' + ctx.id;
        return [
          'CoordStruct ' + p + ' = self.Ref.Base.Base.GetCoords();',
          'Logger.Log("坐标 {0},{1},{2}  血量 {3}/{4}", ' + p + '.X, ' + p + '.Y, ' + p + '.Z,',
          '    self.Ref.Base.Health, self.Ref.Type.Ref.Base.Strength);'
        ];
      }
    },

    logSelfType: {
      label: '记录自己的类型 ID',
      group: '日志与调试',
      hint: '确认脚本挂在了哪个单位类型上（rulesmd.ini 的 Scripts= 是否写对）。',
      needs: ['self'],
      args: [],
      emit: function () {
        return ['Logger.Log("我的类型是 {0}", self.Ref.Type.Ref.Base.Base.ID);'];
      }
    },

    changeHealth: {
      label: '修改自己的血量',
      group: '数值',
      hint: '正数加血、负数扣血。会夹在 0 和最大血量之间。'
          + '注意：直接改血量不触发死亡流程，要"打死"请用伤害块。',
      needs: ['self'],
      args: [{ k: 'delta', label: '变化量', type: 'int', def: 100 }],
      emit: function (a, ctx) {
        var v = 'hp_' + ctx.id;
        return [
          'int ' + v + ' = self.Ref.Base.Health + (' + csInt(a.delta, 100) + ');',
          'if (' + v + ' < 0) { ' + v + ' = 0; }',
          'if (' + v + ' > self.Ref.Type.Ref.Base.Strength) { ' + v + ' = self.Ref.Type.Ref.Base.Strength; }',
          'self.Ref.Base.Health = ' + v + ';'
        ];
      }
    },

    modifyDamage: {
      label: '修改即将受到的伤害',
      group: '数值',
      hint: '只用在"受伤时"事件里。可以减免（按百分比）或直接加减。',
      needs: ['pDamage'],
      events: ['OnReceiveDamage'],
      args: [
        {
          k: 'mode', label: '方式', type: 'enum', def: 'mul',
          opts: [['mul', '按百分比（100=不变，0=免疫）'], ['add', '加减固定值']]
        },
        { k: 'v', label: '数值', type: 'int', def: 50 }
      ],
      emit: function (a, ctx) {
        var lines;
        if (a.mode === 'add') {
          lines = ['pDamage.Ref = pDamage.Ref + (' + csInt(a.v, 0) + ');'];
        } else {
          lines = ['pDamage.Ref = (pDamage.Ref * ' + csInt(a.v, 100) + ') / 100;'];
        }
        lines.push('if (pDamage.Ref < 0) { pDamage.Ref = 0; }');
        return lines;
      }
    },

    damageAreaAtSelf: {
      label: '在自己位置造成范围伤害',
      group: '伤害与爆炸',
      hint: '走引擎完整伤害流程（弹头、护甲、友伤都生效），是会正常致死的伤害。',
      needs: ['self'],
      args: [
        { k: 'wh', label: '弹头类型名', type: 'game', list: 'warheads', def: 'BlimpHE' },
        { k: 'dmg', label: '伤害值', type: 'int', def: 100, min: 0 }
      ],
      emit: function (a, ctx) {
        var wh = 'wh_' + ctx.id;
        return [
          '',
          '// 范围伤害：弹头找不到就什么都不做（Find 找不到会返回空指针）',
          'Pointer<WarheadTypeClass> ' + wh + ' = WarheadTypeClass.ABSTRACTTYPE_ARRAY.Find(' + csStr(a.wh) + ');',
          'if (!' + wh + '.IsNull)',
          '{',
          '    MapClass.DamageArea(self.Ref.Base.Base.GetCoords(), ' + csInt(a.dmg, 100) + ', self, ' + wh + ', false, self.Ref.Owner);',
          '}'
        ];
      }
    },

    createBulletToTarget: {
      label: '向目标发射武器',
      group: '伤害与爆炸',
      hint: '复制一门武器的子弹飞向目标。⚠️ 这个 API 在底层失败时无法预防（引擎会崩），'
          + '所以请务必填真实存在的武器名。',
      needs: ['pTarget', 'self'],
      // OnFire only: the snippet uses pTarget directly. Letting it appear in
      // OnUpdate produced "名称 pTarget 不存在" in the fixtures.
      events: ['OnFire'],
      args: [
        { k: 'weapon', label: '武器类型名', type: 'game', list: 'weapons', def: 'RedEye2' },
        { k: 'wh', label: '弹头类型名', type: 'game', list: 'warheads', def: 'BlimpHE' },
        { k: 'dmg', label: '伤害值', type: 'int', def: 100, min: 0 },
        { k: 'speed', label: '速度', type: 'int', def: 100, min: 1 },
        { k: 'bright', label: '可见弹道', type: 'bool', def: true }
      ],
      emit: function (a, ctx) {
        var id = ctx.id;
        var wp = 'wp_' + id, wh = 'wh_' + id, tg = 'tgt_' + id, pb = 'pb_' + id;
        var bright = a.bright === false || a.bright === 'false' ? 'false' : 'true';
        return [
          '',
          '// 造子弹：武器/弹头/弹种三层都要判空，否则 Ref 空指针会直接崩游戏',
          'Pointer<WeaponTypeClass> ' + wp + ' = WeaponTypeClass.ABSTRACTTYPE_ARRAY.Find(' + csStr(a.weapon) + ');',
          'Pointer<WarheadTypeClass> ' + wh + ' = WarheadTypeClass.ABSTRACTTYPE_ARRAY.Find(' + csStr(a.wh) + ');',
          'if (!' + wp + '.IsNull && !' + wh + '.IsNull && !' + wp + '.Ref.Projectile.IsNull)',
          '{',
          '    if (pTarget.CastToTechno(out Pointer<TechnoClass> ' + tg + '))',
          '    {',
          '        Pointer<BulletClass> ' + pb + ' = ' + wp + '.Ref.Projectile.Ref.CreateBullet(',
          '            pTarget, self, ' + csInt(a.dmg, 100) + ', ' + wh + ', ' + csInt(a.speed, 100) + ', ' + bright + ');',
          '        if (!' + pb + '.IsNull)',
          '        {',
          '            ' + pb + '.Ref.Detonate(' + tg + '.Ref.Base.Base.GetCoords());',
          '        }',
          '    }',
          '}'
        ];
      }
    },

    rememberTarget: {
      label: '记住目标',
      group: '目标引用',
      hint: '把开火目标存进 _rememberedTarget，之后"每帧"事件里还能用它。',
      needs: ['pTarget', 'rememberedTarget'],
      events: ['OnFire'],
      args: [],
      emit: function (a, ctx) {
        var tg = 'tgt_' + ctx.id, ex = 'ext_' + ctx.id;
        return [
          '',
          '// 存下目标。ExtensionReference 内部是弱引用，目标死了 Get() 会返回 null',
          'if (pTarget.CastToTechno(out Pointer<TechnoClass> ' + tg + '))',
          '{',
          '    TechnoExt ' + ex + ' = TechnoExt.ExtMap.Find(' + tg + ');',
          '    if (' + ex + ' != null)',
          '    {',
          '        _rememberedTarget.Set(' + ex + ');',
          '    }',
          '}'
        ];
      }
    },

    clearRemembered: {
      label: '忘记目标',
      group: '目标引用',
      hint: '清掉记住的目标。',
      needs: ['rememberedTarget'],
      args: [],
      emit: function () {
        return ['_rememberedTarget.Set(null);'];
      }
    },

    // Action on the remembered target. Needs no event variables at all, so it
    // works from OnUpdate - which is the whole point of remembering a target.
    damageRemembered: {
      label: '打击记住的目标',
      group: '目标引用',
      hint: '对记住的目标造成范围伤害。目标已经不在了就自动跳过。',
      needs: ['self', 'rememberedTarget'],
      args: [
        { k: 'wh', label: '弹头类型名', type: 'game', list: 'warheads', def: 'BlimpHE' },
        { k: 'dmg', label: '伤害值', type: 'int', def: 100, min: 0 }
      ],
      emit: function (a, ctx) {
        var id = ctx.id;
        var ex = 'tgt_' + id, pt = 'ptgt_' + id, wh = 'wh_' + id;
        return [
          '',
          '// 打击记住的目标',
          'TechnoExt ' + ex + ' = _rememberedTarget.Get();',
          'if (' + ex + ' != null)',
          '{',
          '    Pointer<TechnoClass> ' + pt + ' = ' + ex + '.OwnerObject;',
          '    if (!' + pt + '.IsNull)',
          '    {',
          '        Pointer<WarheadTypeClass> ' + wh + ' = WarheadTypeClass.ABSTRACTTYPE_ARRAY.Find(' + csStr(a.wh) + ');',
          '        if (!' + wh + '.IsNull)',
          '        {',
          '            MapClass.DamageArea(' + pt + '.Ref.Base.Base.GetCoords(), ' + csInt(a.dmg, 100) + ', self, ' + wh + ', false, self.Ref.Owner);',
          '        }',
          '    }',
          '}'
        ];
      }
    },

    logRememberedPos: {
      label: '记录记住的目标的坐标',
      group: '目标引用',
      hint: '追踪目标时用它打点，可以看到目标在移动。',
      needs: ['rememberedTarget'],
      args: [],
      emit: function (a, ctx) {
        var id = ctx.id;
        var ex = 'tgt_' + id, pt = 'ptgt_' + id, pos = 'tpos_' + id;
        return [
          '',
          'TechnoExt ' + ex + ' = _rememberedTarget.Get();',
          'if (' + ex + ' != null && !' + ex + '.OwnerObject.IsNull)',
          '{',
          '    CoordStruct ' + pos + ' = ' + ex + '.OwnerObject.Ref.Base.Base.GetCoords();',
          '    Logger.Log("目标坐标 {0},{1},{2}", ' + pos + '.X, ' + pos + '.Y, ' + pos + '.Z);',
          '}'
        ];
      }
    }
  };

  // =========================================================================
  //  State fields
  // =========================================================================
  //  Only one piece of per-script state exists in this version: the remembered
  //  target. Keeping it a single fixed field (instead of one per block) means
  //  "remember here, use there" just works, with nothing for the user to name
  //  or wire up. `needs: ['rememberedTarget']` pulls the declaration in.
  // =========================================================================
  var FIELDS = {
    rememberedTarget: {
      decl: 'private ExtensionReference<TechnoExt> _rememberedTarget = new ExtensionReference<TechnoExt>();',
      comment: '// 记住的目标（"记住目标"积木写它，"打击记住的目标"读它）'
    }
  };

  return {
    VERSION: VERSION,
    EVENTS: EVENTS,
    CONDITIONS: CONDITIONS,
    ACTIONS: ACTIONS,
    FIELDS: FIELDS,
    csStr: csStr,
    csInt: csInt,
    safeId: safeId
  };
});
