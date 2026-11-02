/* JavaScript evaluated in the Information screen (rnps-content-information,
 * Options > Information of a game or app) right before its bundle (see
 * script_injector.cpp). `__aniketosInfo` is defined by the native side just ahead
 * of it and holds the labels and the folders dumped games are mounted from
 * (ContentInfo::ScriptConfig).
 *
 * The screen's ContentInfo component rebuilds `this.infoList`, its rows
 * ({testID, firstLineLabel, firstLineValue}), on every render and maps it
 * to list items. `infoList` is watched on Object.prototype: every list the
 * component stores gets our rows appended right before it is mapped, made
 * from the list's first row by cloning its label and value elements (same
 * Text, same look). The values come from the app database through the
 * module the screen reads it with itself (AppInfoOperation.getAppInfo), for
 * the title in the URL the screen was opened with (LinkingPS.initialURL);
 * the component is drawn again once they arrive. Nothing is written.
 * Modules are recognised by what they export, never by number, as in
 * Home's script.
 *
 * Kept to plain ES5. */
R"JS((function () {
  var cfg = globalThis.__aniketosInfo;
  if (!cfg || globalThis.__aniketosInfoOn) return;
  globalThis.__aniketosInfoOn = true;
  var A = { React: null, ops: null, linking: null };
  var S = { titleId: null, asked: false, rows: null, owners: [],
            warned: false };
  /* The app database keys behind the rows (appinfo.db, tbl_appinfo). */
  var KEYS = ['CONTENT_ID', '_ps_platform', 'VERSION', 'SYSTEM_VER',
              'SYSTEM_VER_PPR', 'SDK_VERSION', '_org_path', '_metadata_path',
              '#_install_time', '#_last_access_time', '#_booted'];
  function log(message) {
    try { console.log('ANIKETOS info ' + message); } catch (e) {}
  }

  /* ---- the modules we need, by shape --------------------------------- */
  /* A data property only: the modules that re-export others do it with
     getters that load the module on first read. */
  function own(o, name) {
    try {
      var d = Object.getOwnPropertyDescriptor(o, name);
      return d && 'value' in d ? d.value : undefined;
    } catch (e) { return undefined; }
  }
  function scan(o) {
    if (!o || (typeof o !== 'object' && typeof o !== 'function')) return;
    try {
      if (!A.React && typeof own(o, 'createElement') === 'function' &&
          typeof own(o, 'cloneElement') === 'function' && own(o, 'Fragment'))
        A.React = o;
      var ops = own(o, 'AppInfoOperation');
      if (!A.ops && ops && typeof ops.getAppInfo === 'function' &&
          typeof ops.createAdapter === 'function' &&
          typeof ops.deleteAdapter === 'function')
        A.ops = ops;
      if (!A.linking && typeof o.getInitialURL === 'function' &&
          typeof o.openURLArg === 'function' &&
          typeof o.openTask === 'function')
        A.linking = o;
    } catch (e) {}
  }
  var originalCall = Function.prototype.call;
  var hooked = false;
  function hookRequire(require) {
    var factories = require.m;
    var wrap = function (factory) {
      return function (module, exports, req) {
        var result = factory.call(this, module, exports, req);
        scan(module && module.exports);
        if (module && module.exports) scan(module.exports['default']);
        return result;
      };
    };
    for (var id in factories)
      if (typeof factories[id] === 'function')
        factories[id] = wrap(factories[id]);
  }
  Function.prototype.call = function (thisArg, module, exports, require) {
    if (!hooked && arguments.length === 4 && typeof require === 'function' &&
        module && typeof module === 'object' && module.exports === exports &&
        exports === thisArg && require.m && require.c) {
      hooked = true;
      Function.prototype.call = originalCall;
      try { hookRequire(require); } catch (e) { log('hook failed: ' + e); }
      var result = originalCall.call(this, thisArg, module, exports, require);
      scan(module.exports);
      return result;
    }
    return originalCall.apply(this, arguments);
  };

  /* ---- the values ---------------------------------------------------- */
  function titleId() {
    if (S.titleId) return S.titleId;
    var url = '';
    try { url = String(A.linking.initialURL || ''); } catch (e) {}
    var m = /[?&]titleId=([A-Za-z0-9]{9})(?:&|$)/.exec(url);
    if (m) S.titleId = m[1];
    else if (!S.warned) { S.warned = true; log('no title in ' + url); }
    return S.titleId;
  }
  function text(value) {
    return value === null || value === undefined ? '' : String(value);
  }
  function digits(n) {
    return (n >> 4) < 10 && (n & 15) < 10 ? (n >> 4) * 10 + (n & 15) : -1;
  }
  /* System software as the system writes it: major and minor in BCD. */
  function release(major, minor) {
    var a = digits(major), b = digits(minor);
    if (a < 0 || b < 0 || (a === 0 && b === 0)) return '';
    return a + '.' + (b < 10 ? '0' + b : b);
  }
  /* PS5: 0xMMmm000000000000, as a number or as such a string */
  function release64(value) {
    var t = text(value);
    if (/^0x[0-9a-fA-F]{16}$/.test(t))
      return release(parseInt(t.substr(2, 2), 16), parseInt(t.substr(4, 2), 16));
    var n = Number(t);
    if (!(n > 0)) return '';
    var top = Math.floor(n / 281474976710656); /* 2^48 */
    return release(Math.floor(top / 256) % 256, top % 256);
  }
  /* PS4: 0xMMmm0000 */
  function release32(value) {
    var n = Number(text(value));
    if (!(n > 0) || n > 4294967295) return '';
    return release(Math.floor(n / 16777216) % 256, Math.floor(n / 65536) % 256);
  }
  /* "2026-09-30 08:15:40.000", kept in UTC: milliseconds, or NaN */
  function stamp(value) {
    var m = /^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d)(?::(\d\d))?/.exec(text(value));
    return m ? Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +(m[6] || 0))
             : NaN;
  }
  /* in local time, as 2026-09-30 10:15 */
  function date(value) {
    var t = stamp(value);
    if (isNaN(t)) return '';
    var d = new Date(t);
    var two = function (n) { return n < 10 ? '0' + n : String(n); };
    return d.getFullYear() + '-' + two(d.getMonth() + 1) + '-' +
        two(d.getDate()) + ' ' + two(d.getHours()) + ':' + two(d.getMinutes());
  }
  function region(contentId) {
    var m = /^([UEJH])P\d{4}-/.exec(contentId);
    return m && cfg.regions[m[1]]
        ? cfg.regions[m[1]] + ' (' + contentId.substr(0, 2) + ')' : '';
  }
  function rowsOf(item, id) {
    var rows = [];
    var add = function (key, value) {
      if (value && cfg.labels[key])
        rows.push({ key: key, label: cfg.labels[key], value: value });
    };
    var contentId = text(item.CONTENT_ID);
    var platform = text(item._ps_platform);
    var ps4 = platform === '1';
    add('titleId', id);
    add('contentId', contentId);
    add('region', region(contentId));
    add('platform', ps4 ? 'PS4' : platform === '0' ? 'PS5' : '');
    if (ps4) add('masterVersion', text(item.VERSION));
    add('systemVersion', ps4 ? release32(item.SYSTEM_VER)
                             : release64(item.SYSTEM_VER_PPR));
    add('sdkVersion', release64(item.SDK_VERSION));
    add('path', text(item._org_path));
    add('mountedFrom', text(cfg.links && cfg.links[id]));
    add('metadataPath', text(item._metadata_path));
    add('installed', date(item['#_install_time']));
    /* Set when the title is started, and once at install, a moment after
       the install time, which is not a use. PS4 titles also say whether
       they were ever started. */
    if (text(item['#_booted']) === '1' ||
        stamp(item['#_last_access_time']) - stamp(item['#_install_time']) >
            60000)
      add('lastUsed', date(item['#_last_access_time']));
    return rows;
  }
  function redraw() {
    for (var i = 0; i < S.owners.length; i++) {
      try { S.owners[i].forceUpdate(); } catch (e) { log('redraw: ' + e); }
    }
  }
  function ask(id) {
    if (S.asked) return;
    S.asked = true;
    var ctx = null;
    try { ctx = A.ops.createAdapter({}).ctxId; } catch (e) { log('adapter: ' + e); }
    if (typeof ctx !== 'number') { log('no adapter'); return; }
    var done = function () {
      try { A.ops.deleteAdapter(ctx); } catch (e) {}
    };
    var asked;
    try { asked = A.ops.getAppInfo(ctx, id, KEYS); }
    catch (e) { done(); log('query: ' + e); return; }
    asked.then(function (result) {
      done();
      S.rows = rowsOf((result && result.item) || {}, id);
      log(id + ': ' + S.rows.length + ' rows');
      redraw();
    }, function (e) {
      done();
      log('query failed: ' + e);
    });
  }

  /* ---- the rows ------------------------------------------------------ */
  function append(list, owner) {
    var props = owner && owner.props;
    if (!props || list.__aniketosRows || props.isPsNow ||
        !props.isContentDataReady) return;
    if (!A.React || !A.ops || !A.linking) {
      if (!S.warned) {
        S.warned = true;
        log('modules missing react=' + !!A.React + ' ops=' + !!A.ops +
            ' linking=' + !!A.linking);
      }
      return;
    }
    var id = titleId();
    if (!id) return;
    ask(id);
    var template = list[0];
    if (!S.rows || !S.rows.length || !template ||
        !template.firstLineLabel || !template.firstLineValue) return;
    Object.defineProperty(list, '__aniketosRows', { value: true });
    for (var i = 0; i < S.rows.length; i++) {
      var row = S.rows[i];
      list.push({
        testID: 'aniketos-info-' + row.key,
        firstLineLabel: A.React.cloneElement(template.firstLineLabel,
                                             { children: row.label }),
        firstLineValue: A.React.cloneElement(template.firstLineValue,
                                             { children: row.value })
      });
    }
  }
  function attach(list, owner) {
    if (!Array.isArray(list) || list.__aniketos) return list;
    try {
      Object.defineProperty(list, '__aniketos', { value: true });
      Object.defineProperty(list, 'map', {
        configurable: true, enumerable: false, writable: true,
        value: function () {
          try { append(list, owner); } catch (e) { log('rows: ' + e); }
          return Array.prototype.map.apply(list, arguments);
        }
      });
    } catch (e) { log('list: ' + e); }
    return list;
  }
  try {
    Object.defineProperty(Object.prototype, 'infoList', {
      configurable: true, enumerable: false,
      get: function () { return undefined; },
      set: function (value) {
        var owner = this;
        var current = attach(value, owner);
        if (S.owners.indexOf(owner) < 0) S.owners.push(owner);
        try {
          Object.defineProperty(owner, 'infoList', {
            configurable: true, enumerable: true,
            get: function () { return current; },
            set: function (next) { current = attach(next, owner); }
          });
        } catch (e) { log('store infoList: ' + e); }
      }
    });
  } catch (e) { log('watch infoList: ' + e); }
  log('installed');
})();)JS"
