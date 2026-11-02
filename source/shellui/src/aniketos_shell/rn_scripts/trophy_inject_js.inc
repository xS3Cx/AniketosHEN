/* JavaScript evaluated in the Trophies app (rnps-trophy) right before its
 * bundle (see script_injector.cpp). `__aniketosTrophy` is defined by the native
 * side just ahead of it and holds the texts (TrophyTools::ScriptConfig).
 *
 * The app's screens build their options menu with a function they keep as
 * `makeOptionsMenuItems` (the trophy list sets it on itself, the title list
 * makes it with `getMakeOptionsMenuItems` and keeps it in its state). Both
 * names are watched on Object.prototype: when a screen stores one, the
 * stored function is wrapped so the menu it returns gets our entries too,
 * made from the menu's own entries (same component, same look). Modules are
 * recognised by what they export, never by number, as in Home's script.
 *
 * The "all trophies" choices are confirmed with the app's own Alert. Unlock
 * and Lock go to the native side as console lines
 * ("ANIKETOS:trophy|op|type|id|user|trophy"), which makes the change with
 * the system's trophy calls; the app hears of it through its usual title
 * update and redraws.
 *
 * Kept to plain ES5. */
R"JS((function () {
  var cfg = globalThis.__aniketosTrophy;
  if (!cfg || globalThis.__aniketosTrophyOn) return;
  globalThis.__aniketosTrophyOn = true;
  var A = { React: null, rn: null, accessor: null };
  function log(message) {
    try { console.log('ANIKETOS trophy ' + message); } catch (e) {}
  }
  function send(parts) {
    try { console.log('ANIKETOS:trophy|' + parts.join('|')); } catch (e) {}
  }

  /* ---- the modules we need, by shape --------------------------------- */
  function scan(o) {
    if (!o || (typeof o !== 'object' && typeof o !== 'function')) return;
    try {
      if (!A.React && typeof o.createElement === 'function' &&
          typeof o.cloneElement === 'function' && o.Fragment) A.React = o;
      if (!A.rn && 'AppRegistry' in o && 'Alert' in o) A.rn = o;
      if (!A.accessor && o.TrophyAccessor &&
          typeof o.TrophyAccessor.toTitleTypeAndInternalId === 'function')
        A.accessor = o.TrophyAccessor;
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

  /* ---- helpers ------------------------------------------------------- */
  function param(screen, name) {
    try {
      var nav = screen && screen.props && screen.props.navigation;
      return nav && typeof nav.getParam === 'function'
          ? nav.getParam(name, null) : null;
    } catch (e) { return null; }
  }
  /* The app's title id -> {type: 'trophy1'|'trophy2', id: 'NPWR12345_00'} */
  function target(titleId) {
    if (!titleId || !A.accessor) return null;
    try {
      var parsed = A.accessor.toTitleTypeAndInternalId(titleId);
      var type = parsed && parsed.titleType;
      var id = parsed && parsed.internalId && parsed.internalId.commId;
      if ((type === 'trophy1' || type === 'trophy2') && id)
        return { type: type, id: String(id) };
    } catch (e) { log('title id ' + titleId + ': ' + e); }
    return null;
  }
  function alert(title, message, buttons) {
    try { A.rn.Alert.alert(title, message, buttons); }
    catch (e) { log('alert: ' + e); }
  }
  function confirm(title, message, yes, action) {
    alert(title, message, [{ text: cfg.cancel, style: 'cancel' },
                           { text: yes, onPress: action }]);
  }
  function refreshLater(screen) {
    setTimeout(function () {
      try {
        if (typeof screen.refreshScreen === 'function') screen.refreshScreen();
        else if (typeof screen.onReload === 'function') screen.onReload();
      } catch (e) { log('refresh: ' + e); }
    }, 2500);
  }

  /* ---- the entries --------------------------------------------------- */
  function entry(template, key, label, onPress) {
    var props = {};
    for (var name in template.props) props[name] = template.props[name];
    delete props.children;
    delete props.trophyInfo;
    props.label = label;
    props.onPress = onPress;
    props.testID = 'aniketos-' + key;
    props.showSeparator = false;
    props.key = 'aniketos_' + key;
    return A.React.createElement(template.type, props);
  }
  function trophyEntries(screen, trophy, list, template) {
    var user = param(screen, 'userId') || 0;
    var where = target(param(screen, 'titleId'));
    /* the platinum follows the others; the system does not set it alone */
    if (!where || trophy.grade === 'platinum') return;
    var op = trophy.unlocked ? 'lock' : 'unlock';
    list.push(entry(template, op, trophy.unlocked ? cfg.lock : cfg.unlock,
                    function () {
      send([op, where.type, where.id, user, trophy.trophyId]);
      refreshLater(screen);
    }));
  }
  function titleEntries(screen, title, list, template) {
    var user = param(screen, 'userId') || 0;
    var where = target(title.titleId);
    if (!where) return;
    var name = title.name ? String(title.name) : where.id;
    list.push(entry(template, 'unlock_all', cfg.unlockAll, function () {
      confirm(cfg.unlockAllTitle, name + '\n\n' + cfg.syncNote, cfg.unlockYes,
              function () {
        send(['unlockall', where.type, where.id, user, -1]);
        refreshLater(screen);
      });
    }));
    list.push(entry(template, 'lock_all', cfg.lockAll, function () {
      confirm(cfg.lockAllTitle, name, cfg.lockYes, function () {
        send(['lockall', where.type, where.id, user, -1]);
        refreshLater(screen);
      });
    }));
  }
  function addEntries(screen, menu, args) {
    if (!A.React || !menu || !screen || !screen.props) return;
    var lists = [];
    if (Array.isArray(menu.contextItems)) lists.push(menu.contextItems);
    if (Array.isArray(menu.globalItems)) lists.push(menu.globalItems);
    var template = null;
    for (var i = 0; i < lists.length && !template; i++)
      for (var j = 0; j < lists[i].length && !template; j++) {
        var element = lists[i][j];
        if (element && element.type && element.props &&
            typeof element.props.onPress === 'function') template = element;
      }
    if (!template) return;
    var list = lists[0];
    var focused = typeof args[0] === 'function' ? args[0]() : args[0];
    if (!focused || typeof focused !== 'object') return;
    if (typeof screen.unlock === 'function' && focused.trophyId !== undefined)
      trophyEntries(screen, focused, list, template);
    else if (typeof screen.unlockAll === 'function' && focused.titleId)
      titleEntries(screen, focused, list, template);
  }

  /* ---- the screens' menu builders ------------------------------------ */
  function wrapMake(make, owner) {
    if (typeof make !== 'function' || make.__aniketos) return make;
    var wrapped = function () {
      var menu = make.apply(this, arguments);
      try {
        addEntries(owner && owner.props ? owner : this, menu, arguments);
      } catch (e) { log('menu: ' + e); }
      return menu;
    };
    wrapped.__aniketos = true;
    return wrapped;
  }
  function wrapFactory(factory, owner) {
    if (typeof factory !== 'function' || factory.__aniketos) return factory;
    var wrapped = function () {
      return wrapMake(factory.apply(this, arguments),
                      this && this.props ? this : owner);
    };
    wrapped.__aniketos = true;
    return wrapped;
  }
  function watch(name, wrap) {
    try {
      Object.defineProperty(Object.prototype, name, {
        configurable: true, enumerable: false,
        get: function () { return undefined; },
        set: function (value) {
          try {
            Object.defineProperty(this, name, {
              value: wrap(value, this), writable: true, enumerable: true,
              configurable: true });
          } catch (e) { log('store ' + name + ': ' + e); }
        }
      });
    } catch (e) { log('watch ' + name + ': ' + e); }
  }
  watch('makeOptionsMenuItems', wrapMake);
  watch('getMakeOptionsMenuItems', wrapFactory);
  log('installed');
})();)JS"
