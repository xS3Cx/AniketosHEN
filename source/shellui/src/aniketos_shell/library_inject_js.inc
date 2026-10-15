/* JavaScript evaluated in the Game Library app (rnps-library) right before
 * its bundle (see home_js.cpp). `__aniketosLibrary` is defined by the native
 * side just ahead of it: {marker, emptyTitle, emptyText}.
 *
 * Home shows the Game Library below a tile as a hub: an instance of this app
 * of its own per tile, told by Home what to show through the hub's query
 * parameters. A game folder's tile on Home (home_inject_js.inc) asks for the
 * library with one parameter more, aniketosFolder=<id>. The hub SDK in this
 * app serialises the parameters it is handed (JSON.stringify), which is
 * where this script reads the folder. The app's lists of titles are then
 * marked for the native side's database query (home_folders.cpp), which
 * keeps only the folder's titles in them: content limit `marker` + id; and
 * the screen is cut down to those titles (shape()). In any other instance
 * of the app nothing changes.
 *
 * Kept to plain ES5. */
R"JS((function () {
  var cfg = globalThis.__aniketosLibrary;
  if (!cfg || globalThis.__aniketosLibraryOn) return;
  globalThis.__aniketosLibraryOn = true;
  var L = { folder: 0 };
  function log(message) {
    try { console.log('ANIKETOS library ' + message); } catch (e) {}
  }

  /* The hub's parameters on their way through the SDK. */
  var stringify = JSON.stringify;
  JSON.stringify = function (value) {
    try {
      if (!L.folder && value && typeof value === 'object' &&
          Object.prototype.hasOwnProperty.call(value, 'aniketosFolder')) {
        var id = parseInt(value.aniketosFolder, 10);
        if (id > 0) {
          L.folder = id;
          log('folder ' + id);
        }
      }
    } catch (e) {}
    return stringify.apply(this, arguments);
  };

  function wrapAccessor(accessor) {
    var original = accessor.setSearchCondition;
    if (typeof original !== 'function' || original.__aniketos) return;
    var wrapped = function (ctxId, condition) {
      try {
        if (L.folder && condition && typeof condition === 'object' &&
            condition.filterTypeDispLocationSpace) {
          var copy = {};
          for (var key in condition) copy[key] = condition[key];
          copy.contentLimit = cfg.marker + L.folder;
          arguments[1] = copy;
        }
      } catch (e) { log('condition: ' + e); }
      return original.apply(this, arguments);
    };
    wrapped.__aniketos = true;
    try { accessor.setSearchCondition = wrapped; } catch (e) {
      log('accessor not writable: ' + e);
    }
  }
  /* What a folder's hub leaves out of the library's screen. Every element
     is made through React (jsx, jsxs, createElement); in a folder's hub:
       - the tabs (Installed, Your Collection) give way to the Installed
         panel alone: the folder's games, without the tab list;
       - the empty screen says the folder is empty, without its button to
         the Store. */
  function shape(props) {
    if (!L.folder || !props || typeof props !== 'object') return null;
    if (props.testID === 'hubsdk-TabViewPS' && Array.isArray(props.tabData) &&
        props.tabData.length > 1) {
      var first = props.tabData[0] && props.tabData[0].tabId;
      var panel = first && props.panelMap ? props.panelMap[first] : null;
      if (panel && typeof panel === 'object' && panel.$$typeof) return panel;
      props.tabData = [props.tabData[0]];
    }
    if ('showMainButton' in props && 'headerText' in props) {
      props.showMainButton = false;
      if (cfg.emptyTitle) props.headerText = cfg.emptyTitle;
      if (cfg.emptyText) props.mainText = cfg.emptyText;
    }
    return null;
  }
  function wrapReact(o) {
    ['jsx', 'jsxs', 'createElement'].forEach(function (name) {
      var make = o[name];
      if (typeof make !== 'function' || make.__aniketos) return;
      var wrapped = function (type, props) {
        if (L.folder) {
          try {
            var instead = shape(props);
            if (instead) return instead;
          } catch (e) { log('element: ' + e); }
        }
        return make.apply(this, arguments);
      };
      wrapped.__aniketos = true;
      try { o[name] = wrapped; } catch (e) {}
    });
  }
  function scan(o) {
    if (!o || (typeof o !== 'object' && typeof o !== 'function')) return;
    try {
      var accessor = o.AppBrowseItemAccessor;
      if (accessor && typeof accessor === 'object') wrapAccessor(accessor);
      if ((typeof o.jsx === 'function' && typeof o.jsxs === 'function') ||
          (typeof o.createElement === 'function' &&
           typeof o.cloneElement === 'function'))
        wrapReact(o);
    } catch (e) {}
  }

  /* webpack's require, as in Home's script: the first module call hands it
     over, with its table of module factories. */
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
})();)JS"
