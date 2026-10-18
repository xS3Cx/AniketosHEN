/* JavaScript evaluated in Home's runtime right before its bundle (see
 * home_js.cpp). `__aniketosConfig` is defined by the native side just ahead
 * of it: {app, space, emulators, caption, label, limit, icon, presence, clock,
 * and the bookmark's}; app is 'home' or 'control-center', space is empty in
 * the Control Center, where only the presence dot and the clock are touched,
 * and emulators is false when the Emulators tab is switched off.
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

  /* Our spaces after Games and Media: Emulators unless switched off, and
     the user's bookmark while it holds a game. */
  var EXTRA = [];
  if (SPACE && cfg.emulators)
    EXTRA.push({ id: SPACE, caption: cfg.caption, label: cfg.label,
                 limit: cfg.limit });
  if (SPACE && cfg.bookmark)
    EXTRA.push({ id: 'bookmark', caption: cfg.bookmark,
                 label: cfg.bookmarkLabel, limit: cfg.bookmarkLimit });
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

  /* Games and Media under the captions the native text hook gives them (the
     user's name, the glyph in front): React lays the bar out for the text a
     tab shows, so the tabs are where they belong from the first frame
     instead of being moved after each of React's layouts. */
  function captionStockTabs(text, id) {
    var stock = cfg.stockCaptions;
    if (!SPACE || !stock || !text || typeof text !== 'object') return;
    for (var key in stock) {
      var name = stock[key][0], glyph = stock[key][1];
      if (!name && !glyph) continue;
      /* Home keeps a getter there that gives a text element the system
         translates (RCTVirtualTextObject, msgid_games / msgid_media). The
         user's name replaces it; the glyph goes in front of it, the
         translation left to the system as before. */
      var d = Object.getOwnPropertyDescriptor(text, key);
      if (!d || d.__aniketos) continue;
      var original = d.get ? d.get : (function (v) {
        return function () { return v; };
      })(d.value);
      if (!d.configurable) {
        log('caption of ' + key + ' is fixed; left as it is');
        continue;
      }
      try {
        var getter = (function (get, name, glyph) {
          return function () {
            return name ? name : [glyph + ' ', get.call(this)];
          };
        })(original, name, glyph);
        getter.__aniketos = true;
        Object.defineProperty(text, key, {
          configurable: true, enumerable: d.enumerable, get: getter
        });
        once('stockCaption.' + key, id);
      } catch (e) { log('caption of ' + key + ' not replaced: ' + e); }
    }
  }

  function patchObject(o, id) {
    if (!o || (typeof o !== 'object' && typeof o !== 'function'))
      return;
    captionStockTabs(o.textBySpaceId, id);
    if (!EXTRA.length)
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

  /* The bookmark tab's row is the native side's query, which a change of
     the bookmark (a game added from the Options menu, one taken out) does
     not make Home ask again. Home's store of titles is told to fetch every
     row again, as Home does itself when its apps change: by calling the
     listeners Home gave it. When to: the bookmark's revision, read through
     Home's registry module, which the native side answers for our key. */
  var F = { watch: !!cfg.bookmarkWatch, rev: -1, listeners: [], nm: null,
            asking: false };
  function bookmarkRevision() {
    var nm = F.nm;
    if (!nm || typeof nm.Get !== 'function') return Promise.resolve(null);
    var asked;
    try { asked = nm.Get({ keys: ['aniketos.home_bookmark'] }); }
    catch (e) { return Promise.resolve(null); }
    return Promise.resolve(asked).then(function (answer) {
      if (typeof answer !== 'string') return null;
      var state = JSON.parse(answer);
      return state && typeof state.rev === 'number' ? state.rev : null;
    }, function () { return null; });
  }
  function refreshRows() {
    for (var i = 0; i < F.listeners.length; i++) {
      try { F.listeners[i](); } catch (e) { log('bookmark: refresh ' + e); }
    }
  }
  function watchBookmark() {
    setInterval(function () {
      if (F.asking || !F.listeners.length) return;
      F.asking = true;
      bookmarkRevision().then(function (rev) {
        F.asking = false;
        if (rev === null) return;
        if (F.rev >= 0 && rev !== F.rev) refreshRows();
        F.rev = rev;
      }, function () { F.asking = false; });
    }, 2000);
  }
  function patchTitles(o, id) {
    var store = o && o.default;
    if (!F.watch || A.patched.titles || !store ||
        typeof store.fetchTitleByUserId !== 'function' ||
        typeof store.addListener !== 'function') return;
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
    watchBookmark();
  }
  /* The hub viewer of the row hears of every tile that gets the focus:
     its space is the one in front, for L1/R1 (patchSpaceFocus). */
  function patchHubViewer(o, id) {
    var use = o && o.useHubViewer;
    if (A.patched.hubViewer || typeof use !== 'function') return;
    var wrapped = function () {
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
      return use.apply(this, arguments);
    };
    try { o.useHubViewer = wrapped; } catch (e) {}
    if (o.useHubViewer === wrapped) once('hubViewer', id);
  }

  /* L1 and R1 move between the spaces of the tab bar. Home's own handlers
     know two (R1 from Games to Media, L1 back); with ours they go on through
     our spaces (Media, Emulators, the bookmark) and back, with the same sound
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

  /* The custom clock, drawn by the app's own clock (Home and the Control
     Center have the same component). Sony's clock (the module exporting
     `Clock`) is a View holding one Text whose text is the time, from a
     timer that sets it again only when the minute changes. Ours draws
     that same element, with the same View, Text, style and timing, and with
     the user's settings: seconds, a 12- or 24-hour form, the colour and size,
     and the date as a second Text inside the same View. Being part of the
     clock, the date is laid out, moved, faded and hidden with it, and the
     clock's width comes from React's layout of the text it really shows.
     The system's time format is Sony's own (the time cache's toTime). The
     settings are read through the app's registry module (aniketos.clock),
     which the native side answers. */
  var CK = { cfg: cfg.clock || null, rev: 0, raw: '', cache: null,
             component: null, asking: false, told: false, failed: false };
  function pad(n) { return n < 10 ? '0' + n : '' + n; }
  function clockTime(now, stock) {
    var c = CK.cfg, h = now.getHours(), mm = pad(now.getMinutes());
    var ss = pad(now.getSeconds());
    if (c.hour === 1)
      return pad(h) + ':' + mm + (c.seconds ? ':' + ss : '');
    if (c.hour === 2)
      return ((h % 12) || 12) + ':' + mm + (c.seconds ? ':' + ss : '') +
             ' ' + (h < 12 ? c.am : c.pm);
    var text = stock;
    try {
      if (CK.cache) text = CK.cache.toTime(now);
    } catch (e) {}
    if (typeof text !== 'string') return stock;
    /* the seconds after the minutes, with the language's separator */
    return c.seconds
               ? text.replace(/(\d)([:.])(\d\d)(?!\d)/, '$1$2$3$2' + ss)
               : text;
  }
  function clockDate(now) {
    var c = CK.cfg, parts = [];
    var day = pad(now.getDate());
    var month = c.months ? c.months[now.getMonth()]
                         : pad(now.getMonth() + 1);
    var year = c.year ? '' + now.getFullYear() : '';
    if (c.order === 2) parts = [month, day, year];
    else if (c.order === 3) parts = [year, month, day];
    else parts = [day, month, year];
    var text = '';
    for (var i = 0; i < parts.length; i++)
      if (parts[i]) text += (text ? c.sep : '') + parts[i];
    if (c.weekdays) text = c.weekdays[now.getDay()] + ', ' + text;
    return text;
  }
  /* Text takes only the sizes of React Native's FontSizePS table (any other
     is a fatal error); the size asked for is taken to the nearest of them.
     0 when there is no table: the Text keeps the size it has. */
  function fontSize(px) {
    if (!CK.sizes) {
      CK.sizes = [];
      var table = A.rn && A.rn.FontSizePS;
      for (var name in table)
        if (typeof table[name] === 'number' && table[name] > 0 &&
            name.indexOf('5X') < 0)
          CK.sizes.push(table[name]);
      log('clock: font sizes ' + JSON.stringify(CK.sizes));
    }
    var best = 0;
    for (var i = 0; i < CK.sizes.length; i++)
      if (!best || Math.abs(CK.sizes[i] - px) < Math.abs(best - px))
        best = CK.sizes[i];
    return best;
  }
  /* Draws Sony's clock instead of ours if ours fails while React draws it */
  function makeBoundary(React) {
    function Boundary(props) {
      React.Component.call(this, props);
      this.state = { failed: false };
    }
    Boundary.prototype = Object.create(React.Component.prototype);
    Boundary.prototype.constructor = Boundary;
    Boundary.getDerivedStateFromError = function () { return { failed: true }; };
    Boundary.prototype.componentDidCatch = function (e) { clockFailed(e); };
    Boundary.prototype.render = function () {
      return this.state.failed ? this.props.fallback : this.props.children;
    };
    return Boundary;
  }
  /* Sony's clock element with our text in its Text and the date under it */
  function dressClock(element) {
    var React = A.React, c = CK.cfg;
    var props = element && element.props;
    var text = props && props.children;
    if (!React || !text || !text.props || typeof props.testID !== 'string' ||
        text.props.testID !== props.testID + '-text' ||
        typeof text.props.children !== 'string')
      return element;
    if (!CK.told) {
      CK.told = true;
      log('clock: drawn in Sony\'s clock (' + props.testID + ')');
    }
    if (!c || !c.on) return element;
    var now = new Date(), key;
    var own = {};
    for (key in text.props) own[key] = text.props[key];
    var look = { color: c.tc };
    var size = c.ts > 0 ? fontSize(c.ts) : 0;
    if (size) look.fontSize = size;
    own.style = [text.props.style, look];
    own.children = clockTime(now, text.props.children);
    own.key = 'time';
    /* the native label hooks leave a clock with this ending alone
       (shell_clock.cpp, kScriptClockMark) */
    own.testID = text.props.testID + '.aniketos';
    var children = [React.createElement(text.type, own)];
    if (c.date) {
      var date = { position: 'absolute', top: '100%', left: -300, right: -300,
                   marginTop: c.gap, textAlign: 'center', color: c.dc };
      size = fontSize(c.ds);
      if (size) date.fontSize = size;
      children.push(React.createElement(text.type, {
        key: 'date', testID: props.testID + '-date', numberOfLines: 1,
        style: date, children: clockDate(now)
      }));
    }
    var view = {};
    for (key in props) view[key] = props[key];
    view.children = children;
    if (!CK.Boundary) CK.Boundary = makeBoundary(React);
    return React.createElement(CK.Boundary, { fallback: element },
                               React.createElement(element.type, view));
  }
  function clockKey() {
    var c = CK.cfg, now = new Date();
    if (!c || !c.on) return CK.rev;
    return CK.rev + '|' + clockTime(now, '') + '|' +
           (c.date ? clockDate(now) : '');
  }
  /* A failure of ours leaves Sony's clock as it is, for good */
  function clockFailed(e) {
    if (!CK.failed) log('clock: ' + e);
    CK.failed = true;
  }
  function makeClock(inner) {
    var Clock = function (props) {
      var React = A.React;
      var element = inner(props); /* Sony's hooks, then ours */
      var shown = React.useState(null);
      React.useEffect(function () {
        var timer = null;
        var step = function () {
          if (CK.failed) return;
          try {
            shown[1](clockKey()); /* drawn again only when it changed */
          } catch (e) { clockFailed(e); return; }
          timer = setTimeout(step, 1000 - Date.now() % 1000);
        };
        timer = setTimeout(step, 1000 - Date.now() % 1000);
        return function () { clearTimeout(timer); };
      }, []);
      if (CK.failed) return element;
      try {
        return dressClock(element);
      } catch (e) { clockFailed(e); }
      return element;
    };
    return A.React.memo(Clock);
  }
  function watchClock() {
    setInterval(function () {
      var nm = F.nm;
      if (CK.asking || !nm || typeof nm.Get !== 'function') return;
      CK.asking = true;
      var asked;
      try { asked = nm.Get({ keys: ['aniketos.clock'] }); }
      catch (e) { CK.asking = false; return; }
      Promise.resolve(asked).then(function (answer) {
        CK.asking = false;
        if (typeof answer !== 'string' || answer === CK.raw) return;
        try {
          var next = JSON.parse(answer);
          if (next && typeof next === 'object') {
            CK.raw = answer;
            CK.cfg = next;
            CK.rev++;
          }
        } catch (e) { log('clock: ' + e); }
      }, function () { CK.asking = false; });
    }, 2000);
  }
  /* The time cache (toTime) the clock formats the system's time with. */
  function patchClockCache(o) {
    if (!CK.cache && o && typeof o.toTime === 'function' &&
        typeof o.onCacheUpdate === 'function' &&
        typeof o.updateCache === 'function')
      CK.cache = o;
  }
  /* The module's `Clock` is a getter that loads the clock when first asked
     for; the module's exports are put behind an object whose `Clock` is ours,
     everything else read through to the original. */
  function patchClock(module, id) {
    var exports = module && module.exports;
    if (!CK.cfg || A.patched.clock || !exports ||
        typeof exports !== 'object' || !A.React ||
        typeof A.React.useState !== 'function')
      return;
    var d = Object.getOwnPropertyDescriptor(exports, 'Clock');
    if (!d || !(d.get || d.value)) return;
    var view = Object.create(exports);
    Object.defineProperty(view, 'Clock', {
      enumerable: true, configurable: true,
      get: function () {
        if (CK.component) return CK.component;
        var stock = d.get ? d.get.call(exports) : d.value;
        var inner = stock && typeof stock === 'object' &&
                    typeof stock.type === 'function' && !stock.compare
                        ? stock.type
                        : stock;
        if (typeof inner !== 'function') return stock;
        CK.component = makeClock(inner);
        return CK.component;
      }
    });
    module.exports = view;
    once('clock', id);
    watchClock();
  }

  /* An error React Native reports (fatal ones restart the app) is written to
     our log too, before React Native's own handler gets it. ErrorUtils comes
     with the bundle, so this is tried as modules come in. */
  function watchErrors() {
    var utils = globalThis.ErrorUtils;
    if (A.errorsWatched || !utils ||
        typeof utils.setGlobalHandler !== 'function')
      return;
    A.errorsWatched = true;
    var wrap = function (handler) {
      if (typeof handler !== 'function' || handler.__aniketos) return handler;
      var wrapped = function (error, fatal) {
        try {
          log((fatal ? 'fatal error: ' : 'error: ') +
              (error && error.message ? error.message : String(error)) +
              ' | ' + String(error && error.stack).slice(0, 240));
        } catch (e) {}
        return handler.apply(this, arguments);
      };
      wrapped.__aniketos = true;
      return wrapped;
    };
    var set = utils.setGlobalHandler;
    utils.setGlobalHandler = function (handler) {
      return set.call(this, wrap(handler));
    };
    try {
      if (typeof utils.getGlobalHandler === 'function')
        set.call(utils, wrap(utils.getGlobalHandler()));
    } catch (e) {}
  }

  function patchModule(module, id) {
    try {
      watchErrors();
      var exports = module && module.exports;
      findReact(exports);
      if (exports && exports.default) findReact(exports.default);
      findNative(exports);
      if (exports) patchClockCache(exports.default);
      patchClock(module, id);
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
    log('alive app=' + cfg.app + ' patched=' + JSON.stringify(A.patched) +
        ' errors=' + A.errors +
        ' hooked=' + hooked + ' spaces=' + JSON.stringify(A.spaceIds || null));
  }, 8000);
  log('installed for space ' + SPACE);
})();)JS"
