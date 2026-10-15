/* JavaScript evaluated in Home's runtime right before its bundle (see
 * home_js.cpp). `__aniketosConfig` is defined by the native side just ahead
 * of it: {space, caption, label, limit, icon, presence}; space is empty in
 * an app other than Home, where only the presence dot is touched.
 *
 * Home is a webpack bundle whose screens are driven by data: the tab bar is
 * `spaceIds.map(...)`, a tab's caption is `textBySpaceId[id]`, and each
 * space's tile row is its own query to the app database, made with the space
 * id. Adding a space is therefore a matter of extending those tables, which
 * this script does as each module that owns one is initialised. Modules are
 * recognised by what they export, never by number or position, so a new
 * firmware's bundle is handled the same way as long as it keeps those names.
 *
 * Kept to plain ES5. */
R"JS((function () {
  var cfg = globalThis.__aniketosConfig;
  if (!cfg || globalThis.__aniketos) return;
  var A = globalThis.__aniketos = { patched: {}, errors: 0 };
  var SPACE = cfg.space;
  function log(message) {
    try {
      console.log(message.charAt(0) === ':' ? 'ANIKETOS' + message
                                           : 'ANIKETOS ' + message);
    } catch (e) {}
  }
  function once(name, id) {
    if (A.patched[name]) return false;
    A.patched[name] = id;
    log('patched ' + name + ' in module ' + id);
    return true;
  }

  /* Our spaces after Games and Media: Emulators, and Favorites while it
     holds a game. */
  var EXTRA = [];
  if (SPACE)
    EXTRA.push({ id: SPACE, caption: cfg.caption, label: cfg.label,
                 limit: cfg.limit });
  if (SPACE && cfg.favorites)
    EXTRA.push({ id: 'favorites', caption: cfg.favorites,
                 label: cfg.favoritesLabel, limit: cfg.favoritesLimit });
  function extraSpace(name) {
    for (var i = 0; i < EXTRA.length; i++)
      if (EXTRA[i].id === name) return EXTRA[i];
    return null;
  }

  /* The app database knows two spaces. Ours ask for the Games space with a
     content limit the native side recognises, and the native side swaps that
     one query's filter for their title ids. */
  function wrapAccessor(accessor, id) {
    var original = accessor.setSearchCondition;
    if (typeof original !== 'function' || original.__aniketos) return;
    var wrapped = function (ctxId, condition) {
      var extra = condition && extraSpace(condition.filterTypeDispLocationSpace);
      if (extra) {
        var copy = {};
        for (var key in condition) copy[key] = condition[key];
        copy.filterTypeDispLocationSpace = 'game';
        copy.contentLimit = extra.limit;
        arguments[1] = copy;
      }
      return original.apply(this, arguments);
    };
    wrapped.__aniketos = true;
    try {
      accessor.setSearchCondition = wrapped;
      once('AppBrowseItemAccessor.setSearchCondition', id);
    } catch (e) { log('accessor not writable: ' + e); }
  }

  function patchObject(o, id) {
    if (!EXTRA.length || !o || (typeof o !== 'object' && typeof o !== 'function'))
      return;
    var ids = o.spaceIds;
    if (Array.isArray(ids) && ids.indexOf('game') >= 0) A.spaceIds = ids;
    var text = o.textBySpaceId;
    var labels = o.accessibilityLabelsBySpaceId;
    for (var i = 0; i < EXTRA.length; i++) {
      var extra = EXTRA[i];
      if (Array.isArray(ids) && ids.indexOf('game') >= 0 &&
          ids.indexOf(extra.id) < 0) {
        ids.push(extra.id);
        once('spaceIds', id);
      }
      if (text && typeof text === 'object' && !(extra.id in text)) {
        text[extra.id] = extra.caption;
        once('textBySpaceId', id);
      }
      if (labels && typeof labels === 'object' && !(extra.id in labels)) {
        labels[extra.id] = extra.label;
        once('accessibilityLabelsBySpaceId', id);
      }
    }
    var accessor = o.AppBrowseItemAccessor;
    if (accessor && typeof accessor === 'object') wrapAccessor(accessor, id);
  }

  /* The top bar's system icons (Search, Settings, Profile) are components
     exported by one module and shown in a fixed order written into the bar
     itself. Ours goes right after Search (between it and Settings): Search
     is replaced by a component that draws the original and then our button. The button is the same
     component Search uses, taken from the element Search renders, with our
     picture and press handler; the press is passed to the native side, which
     opens the menu. Drawn only while cfg.icon is set: the native side clears
     it once the bundle is in, if its byte patch added the icon already. */
  function findReact(o) {
    if (!A.React && o && typeof o.createElement === 'function' &&
        typeof o.memo === 'function' && o.Fragment) A.React = o;
  }
  function patchIcons(o, id) {
    if (!cfg.icon || A.patched.icon || !o) return;
    var Search = o.Search;
    if (!Search || !o.Settings || !o.Profile) return;
    var inner = typeof Search === 'function' ? Search : Search.type;
    if (typeof inner !== 'function') return;
    var press = function () { log(':menu'); };
    var Both = function (props) {
      var search = inner(props || {});
      var React = A.React;
      if (!cfg.icon || !React || !search || !search.type) return search;
      var own = {};
      for (var key in search.props) own[key] = search.props[key];
      own.iconId = { uri: cfg.icon };
      own.onPress = press;
      own.title = '';
      own.key = 'aniketos';
      return React.createElement(React.Fragment, null, search,
          React.createElement(search.type, own));
    };
    var replacement = A.React ? A.React.memo(Both) : Both;
    try {
      o.Search = replacement;
    } catch (e) {}
    if (o.Search !== replacement) {
      try {
        Object.defineProperty(o, 'Search', {
          enumerable: true, configurable: true,
          get: function () { return replacement; }
        });
      } catch (e) { log('Search not replaceable: ' + e); return; }
    }
    if (o.Search === replacement) once('icon', id);
  }

  /* The dot on an avatar (online, busy, appearing offline) is looked up by
     presence name in one table, each entry naming the dot's picture. With
     cfg.presence set, the names an avatar asks for when its owner is not
     online get that entry instead, so the dot shows as if they were. */
  function patchLamps(o, id) {
    var table = o && o.default;
    if (A.patched.lamps || !table || typeof table !== 'object' ||
        !table.online || !table.busy || !table.appearOffline ||
        !('unavailable' in table) || !table.online.iconId) return;
    if (typeof Proxy !== 'function') { log('lamps: no Proxy'); return; }
    var view = new Proxy(table, {
      get: function (target, key) {
        var forced = cfg.presence && target[cfg.presence];
        if (forced && (key === 'none' || key === 'unavailable' ||
                       key === 'appearOffline'))
          return forced;
        return target[key];
      }
    });
    try { o.default = view; } catch (e) {}
    if (o.default !== view) {
      try {
        Object.defineProperty(o, 'default', {
          enumerable: true, configurable: true,
          get: function () { return view; }
        });
      } catch (e) { log('lamps not replaceable: ' + e); return; }
    }
    if (o.default === view) once('lamps', id);
  }

  /* Game folders in the Games row (home_folders.cpp). The row's titles come
     from one store of Home's: fetchTitleByUserId(user, limit) gives a list
     per space, and the Games list gets a tile for each folder after the
     system's own tiles at its head (the native side's database query has
     left the folders' titles out of it). Below a folder's tile, as below any
     tile, Home shows a hub: for a folder, the Game Library with the folder
     named in its parameters, which shows the folder's titles alone
     (library_inject_js.inc). Pressing the tile goes down to it, as Down
     does. The folders are read through Home's registry module, which the
     native side answers for our key; when they change, the row is fetched
     again by calling the listeners Home gave the store, which is what Home
     does itself when its apps change. */
  var F = { on: !!cfg.folders, state: null, rev: -1, listeners: [],
            nm: null, asking: false, hub: null,
            /* the Favorites tab's row is fetched again on changes too */
            watch: !!cfg.folders || !!cfg.favorites };
  function folderState() {
    var nm = F.nm;
    if (!F.watch || !nm || typeof nm.Get !== 'function')
      return Promise.resolve(null);
    var asked;
    try { asked = nm.Get({ keys: ['aniketos.home_folders'] }); }
    catch (e) { return Promise.resolve(null); }
    return Promise.resolve(asked).then(function (answer) {
      if (typeof answer !== 'string') return null;
      var state = JSON.parse(answer);
      return state && Array.isArray(state.folders) ? state : null;
    }, function () { return null; });
  }
  function folderOf(titleId) {
    var folders = F.state ? F.state.folders : [];
    for (var i = 0; i < folders.length; i++)
      if (folders[i].tile === titleId) return folders[i];
    return null;
  }
  function systemTile(item) {
    return !!item && typeof item.titleId === 'string' &&
           item.titleId.indexOf('NPXS') === 0;
  }
  /* A folder's tile, in the shape of the row's own entries. */
  function folderTile(folder, template, icon) {
    var tile = {};
    for (var key in template) tile[key] = template[key];
    var info = {};
    var stock = template.conceptInfo || {};
    for (var name in stock) info[name] = null;
    info.iconPathWithTs = icon;
    tile.conceptInfo = info;
    tile.bgMatteType = 'None';
    tile.conceptId = '0';
    tile.localConceptId = null;
    tile.deeplinkUri = 'OnionHEN?Folder=' + folder.id;
    tile.hubAppUri = 'pslibraryhub:main?space=game&aniketosFolder=' +
                     folder.id;
    tile.icon0PathWithTs = icon;
    tile.pic0PathWithTs = null;
    tile.sndPathWithTs = null;
    tile.pinnedStatus = null;
    tile.titleId = folder.tile;
    tile.titleName = folder.name;
    return tile;
  }
  function applyFolders(lists, limit) {
    var state = F.state;
    if (!state || !state.folders.length || !Array.isArray(lists)) return;
    var at = A.spaceIds ? A.spaceIds.indexOf('game') : 0;
    var list = lists[at < 0 ? 0 : at];
    var items = list && list.items;
    if (!Array.isArray(items)) return;
    /* an entry of the row's own to copy the shape of */
    var template = null;
    for (var i = 0; i < items.length && !template; i++)
      if (!systemTile(items[i])) template = items[i];
    template = template || items[0] || { conceptInfo: {} };
    var max = typeof limit === 'number' && limit > 0 ? limit : 0;
    var head = 0;
    while (head < items.length && systemTile(items[head])) head++;
    var tiles = [];
    for (var k = 0; k < state.folders.length; k++)
      tiles.push(folderTile(state.folders[k], template, state.icon));
    items.splice.apply(items, [head, 0].concat(tiles));
    /* Home shows `limit` entries: games at the end make way for the
       folders, the system's tiles at the end (Game Library) stay. */
    var tail = 0;
    while (tail < items.length &&
           systemTile(items[items.length - 1 - tail])) tail++;
    while (max && items.length > max &&
           items.length - tail > head + tiles.length)
      items.splice(items.length - tail - 1, 1);
  }
  function refreshRow() {
    for (var i = 0; i < F.listeners.length; i++) {
      try { F.listeners[i](); } catch (e) { log('folders: refresh ' + e); }
    }
  }
  /* Changes made on the native side (a game filed, a folder renamed...):
     the row is fetched again when the folders' revision moves on. */
  function watchFolders() {
    setInterval(function () {
      if (F.asking || !F.listeners.length) return;
      F.asking = true;
      folderState().then(function (state) {
        F.asking = false;
        if (state && state.rev !== F.rev) refreshRow();
      }, function () { F.asking = false; });
    }, 2000);
  }
  function patchTitles(o, id) {
    var store = o && o.default;
    if (!F.watch || A.patched.titles || !store ||
        typeof store.fetchTitleByUserId !== 'function' ||
        typeof store.updateLastAccessTimeUser !== 'function' ||
        typeof store.addListener !== 'function') return;
    var fetch = store.fetchTitleByUserId;
    store.fetchTitleByUserId = function (user, limit) {
      var self = this, args = arguments;
      return folderState().then(function (state) {
        F.state = state;
        F.rev = state ? state.rev : -1;
        return fetch.apply(self, args);
      }, function () {
        return fetch.apply(self, args);
      }).then(function (lists) {
        try { applyFolders(lists, limit); }
        catch (e) { A.errors++; log('folders: row ' + e); }
        return lists;
      });
    };
    /* every tile press ends here; a folder's tile has nothing to launch */
    var touch = store.updateLastAccessTimeUser;
    store.updateLastAccessTimeUser = function (titleId) {
      try {
        if (folderOf(titleId)) {
          var hub = F.hub;
          if (hub && typeof hub.onDownKeyDown === 'function')
            hub.onDownKeyDown();
          return;
        }
      } catch (e) { log('folders: press ' + e); }
      return touch.apply(this, arguments);
    };
    var listen = store.addListener;
    store.addListener = function (handler) {
      if (typeof handler === 'function') F.listeners.push(handler);
      return listen.apply(this, arguments);
    };
    var unlisten = store.removeListener;
    if (typeof unlisten === 'function')
      store.removeListener = function (handler) {
        var at = F.listeners.indexOf(handler);
        if (at >= 0) F.listeners.splice(at, 1);
        return unlisten.apply(this, arguments);
      };
    once('titles', id);
    watchFolders();
  }
  /* The hub viewer of the row: what Down does (onDownKeyDown), so that a
     folder's tile, pressed, goes down to its hub too. */
  function patchHubViewer(o, id) {
    var use = o && o.useHubViewer;
    if (A.patched.hubViewer || typeof use !== 'function') return;
    var wrapped = function () {
      /* the space of the focused tile, for L1/R1 (patchSpaceFocus) */
      var options = arguments[0];
      if (options && typeof options.onChange === 'function') {
        var change = options.onChange;
        if (!change.__aniketos) {
          if (A.changeOf !== change) {
            A.changeOf = change;
            A.change = function (experience) {
              if (experience && typeof experience.spaceId === 'string')
                A.space = experience.spaceId;
              return change.apply(this, arguments);
            };
            A.change.__aniketos = true;
          }
          options.onChange = A.change;
        }
      }
      var viewer = use.apply(this, arguments);
      if (viewer && typeof viewer === 'object') F.hub = viewer;
      return viewer;
    };
    try { o.useHubViewer = wrapped; } catch (e) {}
    if (o.useHubViewer === wrapped) once('hubViewer', id);
  }
  /* L1 and R1 move between the spaces of the tab bar. Home's own handlers
     know two (R1 from Games to Media, L1 back); with ours they go on through
     our spaces (Media, Emulators, Favorites) and back, with the same sound
     and the same move. */
  function neighbour(step) {
    var ids = A.spaceIds;
    var at = Array.isArray(ids) ? ids.indexOf(A.space) : -1;
    /* Games <-> Media stays Home's own */
    if (at < 0 || (step > 0 ? at < 1 : at < 2)) return null;
    var next = ids[at + step];
    return typeof next === 'string' ? next : null;
  }
  function patchSpaceFocus(o, id) {
    var use = o && o.useSpaceFocus;
    if (!SPACE || A.patched.spaceFocus || typeof use !== 'function') return;
    var cache = { right: null, left: null, go: null, value: null };
    var move = function (go, space) {
      try {
        var sound = A.rn && A.rn.SystemSoundPS;
        if (sound && typeof sound.playByID === 'function')
          sound.playByID('psfx_change_space');
      } catch (e) {}
      A.space = space;
      go(space);
      return false;
    };
    var wrapped = function () {
      var focus = use.apply(this, arguments);
      try {
        if (!focus || typeof focus.onRKeyDown !== 'function' ||
            typeof focus.onLKeyDown !== 'function' ||
            typeof focus.focusSpaceDefaultPosition !== 'function')
          return focus;
        var right = focus.onRKeyDown, left = focus.onLKeyDown;
        var go = focus.focusSpaceDefaultPosition;
        /* Home's handlers keep their identity across renders; so do ours */
        if (cache.right !== right || cache.left !== left || cache.go !== go) {
          cache.right = right;
          cache.left = left;
          cache.go = go;
          cache.value = {
            right: function (event) {
              var next = event && event.keyEventType === 'Down'
                             ? neighbour(1) : null;
              if (next) return move(go, next);
              return right.apply(this, arguments);
            },
            left: function (event) {
              var next = event && event.keyEventType === 'Down'
                             ? neighbour(-1) : null;
              if (next) return move(go, next);
              return left.apply(this, arguments);
            }
          };
        }
        var own = {};
        for (var key in focus) own[key] = focus[key];
        own.onRKeyDown = cache.value.right;
        own.onLKeyDown = cache.value.left;
        return own;
      } catch (e) { log('spaces: ' + e); }
      return focus;
    };
    try { o.useSpaceFocus = wrapped; } catch (e) {}
    if (o.useSpaceFocus === wrapped) once('spaceFocus', id);
  }
  /* React Native's own module: the native modules. */
  function findNative(o) {
    if (F.nm || !o || typeof o !== 'object' || !('AppRegistry' in o)) return;
    A.rn = o;
    try {
      var modules = o.NativeModules;
      if (modules && modules.VshNMRegistryModule)
        F.nm = modules.VshNMRegistryModule;
    } catch (e) {}
  }

  /* Options on a folder's tile: the folder's own menu (rename, delete),
     opened by the native side, instead of the system's menu for a title.
     Home makes the focused tile's element with its key handlers; for a
     folder's tile the Options one is swapped as the element is made. */
  var kFocusedTile = 'focused-item-';
  function element(type, props) {
    if (!props || typeof props !== 'object' ||
        typeof props.onOptionsKeyDown !== 'function') return;
    var testID = props.testID;
    if (typeof testID !== 'string' || testID.indexOf(kFocusedTile) !== 0)
      return;
    var folder = folderOf(testID.slice(kFocusedTile.length));
    if (!folder || props.onOptionsKeyDown.__aniketos) return;
    var id = folder.id;
    var options = function () {
      log(':folder|options|' + id);
      return true;
    };
    options.__aniketos = true;
    props.onOptionsKeyDown = options;
  }
  function wrapElements(o) {
    if (!F.on) return;
    ['jsx', 'jsxs', 'createElement'].forEach(function (n) {
      var make = o[n];
      if (typeof make !== 'function' || make.__aniketos) return;
      var wrapped = function (type, props) {
        try { element(type, props); }
        catch (e) { log('folders: element ' + e); }
        return make.apply(this, arguments);
      };
      wrapped.__aniketos = true;
      try { o[n] = wrapped; } catch (e) {}
    });
  }

  function patchModule(module, id) {
    try {
      var exports = module && module.exports;
      findReact(exports);
      if (exports && exports.default) findReact(exports.default);
      findNative(exports);
      if (exports && ((typeof exports.jsx === 'function' &&
                       typeof exports.jsxs === 'function') ||
                      (typeof exports.createElement === 'function' &&
                       typeof exports.cloneElement === 'function')))
        wrapElements(exports);
      patchTitles(exports, id);
      patchHubViewer(exports, id);
      patchSpaceFocus(exports, id);
      patchIcons(exports, id);
      patchLamps(exports, id);
      patchObject(exports, id);
      if (exports && exports.default) patchObject(exports.default, id);
    } catch (e) {
      A.errors++;
      log('error in module ' + id + ': ' + e);
    }
  }

  /* webpack's require is private to the bundle; the one place it shows is
     the call that runs a module: factory.call(exports, module, exports,
     require). The first such call hands over require, with its table of
     module factories (require.m), and the trap is taken away again. */
  var originalCall = Function.prototype.call;
  var hooked = false;
  function hookRequire(require) {
    var factories = require.m;
    var count = 0;
    var wrap = function (factory, id) {
      return function (module, exports, req) {
        var result = factory.call(this, module, exports, req);
        patchModule(module, id);
        return result;
      };
    };
    for (var id in factories) {
      if (typeof factories[id] === 'function') {
        factories[id] = wrap(factories[id], id);
        count++;
      }
    }
    log('webpack require captured, ' + count + ' modules');
  }
  Function.prototype.call = function (thisArg, module, exports, require) {
    if (!hooked && arguments.length === 4 && typeof require === 'function' &&
        module && typeof module === 'object' && module.exports === exports &&
        exports === thisArg && require.m && require.c) {
      hooked = true;
      Function.prototype.call = originalCall;
      var entry = this;
      try { hookRequire(require); } catch (e) { log('hook failed: ' + e); }
      var result = originalCall.call(entry, thisArg, module, exports, require);
      patchModule(module, module.i);
      return result;
    }
    return originalCall.apply(this, arguments);
  };

  /* Home up and drawing: the native side stops treating this start as
     unconfirmed. */
  setTimeout(function () {
    log('alive patched=' + JSON.stringify(A.patched) + ' errors=' + A.errors +
        ' hooked=' + hooked);
  }, 8000);
  log('installed for space ' + SPACE);
})();)JS"
