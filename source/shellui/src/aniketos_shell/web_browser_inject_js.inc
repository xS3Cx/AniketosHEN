/* JavaScript evaluated in the action cards' app (rnps-action-cards) right
 * before its bundle (see home_js.cpp). `__aniketosWeb` is defined by the
 * native side just ahead of it (WebTiles::ScriptConfig).
 *
 * The web browser is one of these cards. Its card keeps the page's size in its
 * state and sends it to the Control Center, which owns the card's place on
 * screen; the card draws its title bar only while state.hideHeader is false.
 * So full screen is the card's own `resize` to the whole screen with that flag
 * set (the Control Center's size limit is lifted by Home's script, which runs
 * there too), the key guide hidden with the card's own prop, and the panel
 * behind the card and the card's round corners taken away.
 *
 * Everything is found by what a module exports and what an instance has,
 * never by number; a firmware that lacks a shape just leaves that part out.
 * Exports are read as data properties only: re-exporting modules have getters
 * that would load what they point at. Kept to plain ES5. */
R"JS((function () {
  var cfg = globalThis.__aniketosWeb;
  if (!cfg || globalThis.__aniketosWebOn) return;
  globalThis.__aniketosWebOn = true;
  var B = { React: null, card: null, root: null, tiles: cfg.tiles, seen: {} };
  /* the panel behind the card, gone but for its place in the layout */
  var NO_PANEL = { position: 'absolute', left: 0, top: 0, right: 0, bottom: 0,
                   opacity: 0 };
  function log(message) {
    try { console.log('ANIKETOS web ' + message); } catch (e) {}
  }
  function once(name) {
    if (B.seen[name]) return false;
    B.seen[name] = true;
    log('patched ' + name);
    return true;
  }
  function own(o, name) {
    var d = Object.getOwnPropertyDescriptor(o, name);
    return d ? d.value : undefined;
  }
  function isObject(o) {
    return !!o && (typeof o === 'object' || typeof o === 'function');
  }

  /* ---- full screen --------------------------------------------------- */
  function isFull() {
    return !!(B.card && B.card.__aniketos.full);
  }
  function redrawRoot() {
    try { if (B.root) B.root.forceUpdate(); }
    catch (e) { log('root: ' + e); }
  }
  function guide(card, shown) {
    var call = shown ? card.props.showKeyGuide : card.props.hideKeyGuide;
    if (typeof call === 'function') call();
  }
  /* The title bar, and the web view's clipping to the card's round corners. */
  function dress(card, on) {
    var props = {}, now = card.state.webviewProps;
    for (var key in now) props[key] = now[key];
    props.ignoreBorderRadius = on;
    card.setState({ hideHeader: on ? true : card.__aniketos.header,
                    webviewProps: props });
  }
  function enter(card) {
    var s = card.__aniketos;
    s.resume = false;
    s.full = true;
    if (!s.size) s.size = card.state.cardSize;
    dress(card, true);
    card.resize({ width: 1920, height: 1080 });
    guide(card, false);
    redrawRoot();
  }
  function leave(card) {
    var s = card.__aniketos, size = s.size;
    s.full = false;
    s.size = null;
    dress(card, false);
    card.resize(size);
    guide(card, true);
    redrawRoot();
  }
  function onR2(event) {
    try {
      if (!event || event.keyEventType !== 'Down' || event.repeat) return;
      if (B.card) (isFull() ? leave : enter)(B.card);
    } catch (e) { log('R2: ' + e); }
  }
  /* The View around the web view takes the card's keys. */
  function patchJsx(o) {
    var jsxs = own(o, 'jsxs');
    if (B.seen.jsx || typeof jsxs !== 'function' ||
        typeof own(o, 'jsx') !== 'function')
      return;
    var wrapped = function (type, props) {
      try {
        if (props && props.onSquareKeyUp && props.onTriangleKeyDown &&
            props.onL3KeyDown) {
          var mine = {};
          for (var key in props) mine[key] = props[key];
          mine.onR2KeyDown = onR2;
          mine.ignoreBorderRadius = isFull();
          arguments[1] = mine;
        }
      } catch (e) { log('keys: ' + e); }
      return jsxs.apply(this, arguments);
    };
    try { o.jsxs = wrapped; } catch (e) {}
    if (own(o, 'jsxs') !== wrapped) {
      try {
        Object.defineProperty(o, 'jsxs', { enumerable: true, configurable: true,
                                           writable: true, value: wrapped });
      } catch (e) { log('jsxs not replaceable: ' + e); return; }
    }
    once('jsx');
  }
  /* The browser's key guide: `show` hands the card's keys to updateShortcut.
     Other modules have a show and a hide too; they call it with other
     arguments and go straight through. */
  function patchKeyGuide(o) {
    var d = own(o, 'default');
    if (!isObject(d)) return;
    var show = own(d, 'show');
    if (typeof show !== 'function' || show.__aniketos ||
        typeof own(d, 'hide') !== 'function')
      return;
    var wrapped = function (options) {
      try {
        if (options && typeof options.updateShortcut === 'function' &&
            options.cardSize) {
          var update = options.updateShortcut, mine = {};
          for (var key in options) mine[key] = options[key];
          mine.updateShortcut = function (keys) {
            var all = keys || {};
            all.r2 = { label: cfg.fullscreen, value: 'ANIKETOS_FULLSCREEN' };
            return update.call(this, all);
          };
          arguments[0] = mine;
          once('guide');
        }
      } catch (e) { log('guide: ' + e); }
      return show.apply(this, arguments);
    };
    wrapped.__aniketos = true;
    try { d.show = wrapped; } catch (e) { log('guide: ' + e); }
  }
  function adopt(card) {
    if (typeof card.renderAddressBar !== 'function' ||
        typeof card.toggleCardSize !== 'function' || !card.state ||
        !('hideHeader' in card.state))
      return;
    /* the marker a Home tile puts in the browser's parameters */
    var tile = !!(card.state.webviewProps &&
                  card.state.webviewProps.aniketosFull);
    card.__aniketos = { full: false, resume: false, size: null,
                        header: tile ? false : card.state.hideHeader };
    B.card = card;
    /* Square changes the card's size: not at full screen */
    var toggle = card.toggleCardSize;
    card.toggleCardSize = function () {
      if (!isFull()) return toggle.apply(this, arguments);
    };
    once('card');
    if (tile) enter(card);
  }
  /* The card drops to its small size when it stops being the selected one
     (the Control Center opened over it); on coming back it is full again. */
  function followMode(card, before) {
    var s = card.__aniketos, mode = card.props.mode;
    if (!s || !before || mode === before.mode) return;
    if (s.full && before.mode === 'selected') {
      s.full = false;
      s.resume = true;
      guide(card, true);
      redrawRoot();
    } else if (s.resume && mode === 'selected') {
      enter(card);
    }
  }
  function patchCard(o) {
    var Card = own(o, 'default');
    if (typeof own(o, 'cardMenuHandler') !== 'function' ||
        typeof Card !== 'function' || !Card.prototype)
      return;
    var proto = Card.prototype;
    var mount = proto.componentDidMount, update = proto.componentDidUpdate;
    var unmount = proto.componentWillUnmount;
    if (proto.__aniketos || typeof proto.render !== 'function' ||
        typeof mount !== 'function' || typeof update !== 'function')
      return;
    proto.__aniketos = true;
    proto.componentDidMount = function () {
      var result = mount.apply(this, arguments);
      try { adopt(this); } catch (e) { log('mount: ' + e); }
      return result;
    };
    proto.componentDidUpdate = function (props) {
      var result = update.apply(this, arguments);
      try { followMode(this, props); } catch (e) { log('update: ' + e); }
      return result;
    };
    proto.componentWillUnmount = function () {
      try {
        if (this.__aniketos && this.__aniketos.full) guide(this, true);
        if (B.card === this) B.card = null;
      } catch (e) { log('unmount: ' + e); }
      if (typeof unmount === 'function') return unmount.apply(this, arguments);
    };
  }
  /* The root every card is drawn in picks the panel behind the card. */
  function patchRoot(o) {
    var Root = own(o, 'AppModuleCardRoot');
    var proto = typeof Root === 'function' ? Root.prototype : null;
    var stock = proto && proto.getBackgroundStyle;
    if (typeof stock !== 'function' || stock.__aniketos) return;
    var wrapped = function () {
      var style = stock.apply(this, arguments);
      try {
        if (this.props && this.props.actionCardType === 'ActionCardWebBrowser') {
          B.root = this;
          if (isFull()) return { lightMat: undefined, bgStyle: NO_PANEL };
        }
      } catch (e) { log('panel: ' + e); }
      return style;
    };
    wrapped.__aniketos = true;
    proto.getBackgroundStyle = wrapped;
    once('root');
  }

  /* ---- the Options menu's row ---------------------------------------- */
  /* A tile stays itself whatever page it went on to. */
  function pageUrl(card) {
    var props = card.state.webviewProps;
    return props && typeof props.aniketosFull === 'string'
               ? props.aniketosFull : card.state.url;
  }
  function send(op, card) {
    try {
      console.log('ANIKETOS:web|' + JSON.stringify({
        op: op, url: pageUrl(card), title: card.state.title || '' }));
    } catch (e) { log('send: ' + e); }
  }
  function addRow(list, card) {
    var url = card && card.state && pageUrl(card);
    if (!B.React || !Array.isArray(list) || typeof url !== 'string' ||
        !/^https?:\/\//.test(url))
      return;
    var template = null;
    for (var i = 0; i < list.length && !template; i++) {
      var item = list[i];
      if (item && item.type && item.props &&
          typeof item.props.onPress === 'function')
        template = item;
    }
    if (!template) return;
    var on = B.tiles.indexOf(url) >= 0, props = {};
    for (var name in template.props) props[name] = template.props[name];
    delete props.children;
    delete props.soundEffectsEnabled;
    props.disabled = false;
    props.key = 'aniketos-home';
    props.testID = 'aniketos-home';
    props.label = on ? cfg.removeHome : cfg.addHome;
    props.onPress = function () {
      try {
        var at = B.tiles.indexOf(url);
        if (at >= 0) B.tiles.splice(at, 1);
        else B.tiles.push(url);
        send(at >= 0 ? 'remove' : 'add', card);
        card.setState({ updateOptionMenu: true });
      } catch (e) { log('menu: ' + e); }
    };
    list.push(B.React.createElement(template.type, props));
  }
  function patchMenu(o) {
    var d = own(o, 'default');
    if (!Array.isArray(own(o, 'ZOOM_RATIO_ARRAY')) || !isObject(d)) return;
    var make = own(d, 'getCardMenu');
    if (typeof make !== 'function' || make.__aniketos) return;
    var wrapped = function (props) {
      var list = make.apply(this, arguments);
      try { addRow(list, props && props.component); }
      catch (e) { log('menu: ' + e); }
      return list;
    };
    wrapped.__aniketos = true;
    try { d.getCardMenu = wrapped; once('menu'); }
    catch (e) { log('menu: ' + e); }
  }

  /* ---- modules ------------------------------------------------------- */
  function scan(module) {
    var o = module && module.exports;
    if (!isObject(o)) return;
    try {
      if (!B.React && typeof own(o, 'createElement') === 'function' &&
          typeof own(o, 'cloneElement') === 'function')
        B.React = o;
      patchJsx(o);
      patchKeyGuide(o);
      patchCard(o);
      patchRoot(o);
      patchMenu(o);
    } catch (e) { log('module: ' + e); }
  }
  var originalCall = Function.prototype.call;
  var hooked = false;
  function hookRequire(require) {
    var factories = require.m;
    var wrap = function (factory) {
      return function (module, exports, req) {
        var result = factory.call(this, module, exports, req);
        scan(module);
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
      scan(module);
      return result;
    }
    return originalCall.apply(this, arguments);
  };

  /* The app up and drawing: the native side stops counting this start. */
  setTimeout(function () {
    log('alive patched=' + JSON.stringify(B.seen) + ' hooked=' + hooked);
  }, 8000);
  log('installed');
})();)JS"
