/* x5Guard PlayStation Network Blocker, React Native side: evaluated in the
 * apps that can start a PSN sign-in or a system update, right before their
 * bundle (script_injector.cpp, X5Guard::PsnShield). The system's own texts
 * stay; only the actions are stopped:
 *
 * - The sign-in launchers, the same two in every app, do nothing:
 *   NativeModules.VshNMPsnShellUISigninUtility.Open (answers 'cancel') and
 *   any psaccounts:signin link through VshNMApplicationLinkingPSModule.
 * - Every "update now" opens the system modal dialog's SystemUpdaterDialog,
 *   by a pssmdlg: link or VshNMSystemModalDialogModule.Open: both do
 *   nothing, nor do the links to the update settings. An app started for a
 *   sign-in or for that dialog anyway (by native code) closes at once, and
 *   inside the dialog app the updater module never answers.
 * - Settings: its update check finds nothing, System Feature Updates
 *   (newer rnps-* apps from Sony) never runs, and the automatic download
 *   and install of system software cannot be switched on. Control Center's
 *   sign-in cards lose their Sign In button.
 * - The notification overlay drops the system toasts that invite a sign-in
 *   or a system update, by use case id (game patch, controller firmware and
 *   server status toasts stay).
 * - Each blocked action is reported to x5Guard (banner, count, log) as
 *   "ANIKETOS:x5guard|<event>|<detail>" (guard.cpp has the event catalog).
 *
 * Modules are recognised by what they export, never by number. Both
 * module systems are hooked (the shared base_dll's and the app's, which in
 * the store has no factory table): the Function.prototype.call hook stays
 * for the app's first seconds, then gives the original back.
 *
 * Kept to plain ES5, ASCII only. */
R"JS((function () {
  if (!globalThis.__x5guardPsn || globalThis.__x5guardPsnOn) return;
  globalThis.__x5guardPsnOn = true;
  var cfg = globalThis.__x5guardPsn;
  var A = { rn: null, patched: false };
  function log(message) {
    try { console.log('ANIKETOS x5guard psn ' + message); } catch (e) {}
  }
  function report(event, detail) {
    try { console.log('ANIKETOS:x5guard|' + event + '|' + detail); } catch (e) {}
  }
  function own(o, name) {
    try {
      var d = Object.getOwnPropertyDescriptor(o, name);
      return d && 'value' in d ? d.value : undefined;
    } catch (e) { return undefined; }
  }

  /* ---- the sign-in and update launchers ------------------------------ */
  /* {event, detail} for a link to block, else null */
  function blockedUrl(arg) {
    var url = arg && typeof arg === 'object' ? arg.url || arg.uri : arg;
    if (typeof url !== 'string') return null;
    if (url.indexOf('psaccounts:signin') === 0)
      return { event: 'signin.link', detail: 'psaccounts:signin' };
    /* the accounts app opened on a PSN step: links from other apps to
       account linking, sign-up, admin sign-in */
    var entry = signinEntry(url);
    if (entry && (url.indexOf('psaccounts:') === 0 ||
                  url.indexOf('pssettings:accounts') === 0))
      return { event: 'signin.link', detail: url.split(/[?:]/)[0] + ':' + entry };
    if (url.indexOf('pssmdlg:') === 0 && url.indexOf('SystemUpdater') > 0)
      return { event: 'update.link', detail: 'pssmdlg:SystemUpdaterDialog' };
    if (url.indexOf('pssettings:') === 0) {
      if (url.indexOf('system_software_update_settings') > 0)
        return { event: 'update.link',
                 detail: 'pssettings:system_software_update_settings' };
      if (url.indexOf('entry=update') > 0)
        return { event: 'update.link', detail: 'pssettings:entry=update' };
    }
    return null;
  }
  /* The accounts app's entries (query entry=, or the psaccounts path) that
     sign in to PSN. Local-user flows (login, logindialog, switchuser,
     passcode, logout, initial setup, account creation of a local user)
     are left alone. */
  var SIGNIN_ENTRIES = ['signin', 'signindialog', 'signinutility', 'signup',
                        'quicksignup', 'sbadminsignin', 'accountlink'];
  function signinEntry(url) {
    if (typeof url !== 'string') return null;
    var m = /[?&]entry=([^&#]*)/.exec(url);
    var entry = m ? m[1]
              : url.indexOf('psaccounts:') === 0
                  ? url.substring(11).split(/[?#\/]/)[0] : '';
    return SIGNIN_ENTRIES.indexOf(entry) >= 0 ? entry : null;
  }
  /* The accounts app started for a PSN sign-in (a game's sign-in dialog, a
     Sign In button anywhere): closed at once the way its own Back closes
     it, so whoever opened it gets "cancelled" and never waits (a game
     handles that result as the user pressing Circle). */
  function cancelSigninFlow(rn, url) {
    var entry = signinEntry(url);
    if (!entry) return false;
    var scheme = url.split(':')[0];
    var exitParam = { entry: entry, result: 'cancel', retCode: 0 };
    if (entry === 'signindialog') exitParam.backToGame = true;
    var nm = rn.NativeModules, closed = false;
    var exit = function () {
      if (closed) return;
      closed = true;
      try {
        nm.VshNMApplicationExitHandlerModule.ExitApp(JSON.stringify(exitParam));
      } catch (e) { log('exit: ' + e); }
    };
    report(entry === 'signindialog' ? 'signin.dialog' : 'signin.app', entry);
    var lm = nm && nm.VshNMLoginMgrShellUI;
    var notify = scheme === 'pssettings' ? 'NotifyInstanceClosedForShellApp'
                                         : 'NotifyInstanceClosed';
    if (scheme === 'cdlg' || scheme === 'psfm' || !lm ||
        typeof lm[notify] !== 'function') {
      setTimeout(exit, 0);
      return true;
    }
    setTimeout(function () {
      try {
        Promise.resolve(lm[notify]({ instanceClosedType: 'instanceCanceled',
                                     launchedUrl: url, exitParam: exitParam }))
            .then(exit, exit);
      } catch (e) { exit(); }
    }, 0);
    setTimeout(exit, 2000); /* never leave the caller waiting */
    return true;
  }
  /* The accounts app's own sign-in calls (every route: its screens, QR and
     PS App sign-in, account linking): refused as "cannot connect to the
     server" (0x8041013D, an error with no Sign In button); never a promise
     left pending, some run under a spinner that cannot be cancelled. */
  var LOGIN_CALLS = ['Authenticate', 'Signin', 'BindAccount',
                     'CreateAuthenticationTicket', 'GetEasySigninCode',
                     'VerifyEasySignin', 'PfAuth'];
  function refuseLogin(lm) {
    if (!lm || lm.__x5guard) return;
    LOGIN_CALLS.forEach(function (name) {
      if (typeof lm[name] !== 'function') return;
      var refused = function () {
        report('signin.native', 'LoginMgr.' + name);
        var error = new Error('PlayStation Network blocked by x5Guard');
        error.code = '0x8041013D';
        return Promise.reject(error);
      };
      try { lm[name] = refused; } catch (e) {}
    });
    lm.__x5guard = true;
  }
  function exitApp(rn) {
    try {
      var handler = rn.NativeModules.VshNMApplicationExitHandlerModule;
      /* one argument, as the app's own ExitHandler.exitApp(x) passes it: the
         bridge refuses the call without it and the app stays open */
      if (handler && typeof handler.ExitApp === 'function') handler.ExitApp(null);
      else rn.ExitHandler.exitApp();
    } catch (e) { log('exit: ' + e); }
  }
  /* An app started for a sign-in or an update (the system modal dialog's
     updater, a psaccounts:signin opened by native code): closed at once. */
  function startedFor(rn, linking) {
    var url = null;
    try {
      url = linking && (linking.initialURL ||
            (typeof linking.getConstants === 'function' &&
             linking.getConstants().initialURL));
    } catch (e) {}
    if (cancelSigninFlow(rn, url)) return;
    var blocked = blockedUrl(url);
    if (!blocked) return;
    if (blocked.event === 'signin.link')
      report('signin.app', 'initialURL ' + blocked.detail);
    else if (url.indexOf('dialogName=SystemUpdaterDialog') >= 0)
      report('update.app', 'initialURL ' + blocked.detail);
    else
      return;
    setTimeout(function () { exitApp(rn); }, 0);
  }
  function blockedDialog(arg) {
    var name = arg && typeof arg === 'object' ? arg.dialogName : null;
    return typeof name === 'string' && name.indexOf('SystemUpdater') === 0
        ? { event: 'update.dialog', detail: name } : null;
  }
  function guard(target, name, blocked) {
    try {
      var original = target && target[name];
      if (typeof original !== 'function' || original.__x5guard) return;
      var wrapped = function () {
        var hit = blocked(arguments[0]);
        if (hit) {
          report(hit.event, hit.detail);
          return Promise.resolve(false);
        }
        return original.apply(this, arguments);
      };
      wrapped.__x5guard = true;
      target[name] = wrapped;
      if (target[name] !== wrapped) log(name + ' could not be replaced');
    } catch (e) { log(name + ': ' + e); }
  }
  function patchNative(rn) {
    var nm = rn.NativeModules;
    var signin = nm && nm.VshNMPsnShellUISigninUtility;
    if (signin && typeof signin.Open === 'function' &&
        !signin.Open.__x5guard) {
      var open = function () {
        report('signin.utility', 'SigninUtility.Open');
        return Promise.resolve({ result: 'cancel' });
      };
      open.__x5guard = true;
      try { signin.Open = open; } catch (e) {}
      if (signin.Open !== open) log('SigninUtility.Open could not be replaced');
    }
    var linking = nm && nm.VshNMApplicationLinkingPSModule;
    guard(linking, 'OpenURL', blockedUrl);
    guard(linking, 'OpenURLArg', blockedUrl);
    guard(linking, 'OpenTask', blockedUrl);
    guard(rn.Linking, 'openURL', blockedUrl);
    guard(nm && nm.VshNMSystemModalDialogModule, 'Open', blockedDialog);
    guard(nm && nm.VshNMLoginMgrShellUI, 'OpenSystemModalDialog', blockedDialog);
    refuseLogin(nm && nm.VshNMLoginMgr);
    stopUpdater(rn, nm && nm.VshNMSystemUpdaterModule);
    stopFeatureUpdates(nm && nm.VshNMSettingsSystem);
    startedFor(rn, linking);
  }
  /* Settings > System Software > System Feature Updates downloads newer
     rnps-* apps from Sony: nothing is ever found. */
  function stopFeatureUpdates(system) {
    if (!system || system.__x5guard) return;
    ['CheckRnpsApps', 'UpdateRnpsApps'].forEach(function (name) {
      if (typeof system[name] === 'function')
        system[name] = function () {
          report('update.feature', name);
          return Promise.resolve('notFound');
        };
    });
    system.__x5guard = true;
  }
  /* The update dialog itself (system modal dialog app): no call of its
     updater module is ever answered, and the app is closed. */
  function stopUpdater(rn, updater) {
    if (!updater || updater.__x5guard) return;
    var closing = false;
    var gaveUp = false;
    var waiting = [];
    /* Should the app still be open after a while, the calls fail: the
       dialog then shows its own error with a way out, never "Please wait"
       for ever. */
    var giveUp = function () {
      var list = waiting;
      waiting = [];
      gaveUp = true;
      for (var i = 0; i < list.length; i++)
        try { list[i](new Error('System update blocked by x5Guard')); }
        catch (e) {}
    };
    var stop = function (name) {
      return function () {
        if (!closing) {
          closing = true;
          report('update.process', 'SystemUpdater.' + name);
          setTimeout(function () { exitApp(rn); }, 0);
          setTimeout(giveUp, 3000);
        }
        return new Promise(function (resolve, reject) {
          if (gaveUp) reject(new Error('System update blocked by x5Guard'));
          else waiting.push(reject);
        });
      };
    };
    for (var name in updater)
      if (typeof updater[name] === 'function')
        try { updater[name] = stop(name); } catch (e) {}
    updater.__x5guard = true;
  }

  /* ---- the notification overlay's sign-in and update toasts ---------- */
  /* System software only: ready to install (NUC249, NUC333), update
     complete (NUC448), games closed by a rest mode update (NUC454), update
     to use network features (NUC294), any system software package download
     ('Pup'); sign-in: signed out (NUC87), sign in to use network features
     (NUC130). */
  var DROPPED = ['NUC249', 'NUC333', 'NUC448', 'NUC454', 'NUC294', 'Pup',
                 'NUC87', 'NUC130'];
  function useCase(type, data) {
    if (!data) return null;
    if (type === 'sendById') {
      /* the download toasts of a system software package, whatever id */
      try {
        if (typeof data.params === 'string' &&
            JSON.parse(data.params).contentType === 'Pup')
          return 'Pup';
      } catch (e) {}
      return data.useCaseId;
    }
    if (type === 'send' && typeof data.payload === 'string') {
      try { return JSON.parse(data.payload).useCaseId; } catch (e) {}
    }
    return null;
  }
  /* one report per toast, however many listeners see it */
  var lastDrop = { id: null, at: 0 };
  function dropped(id) {
    var now = Date.now();
    if (id !== lastDrop.id || now - lastDrop.at > 1000)
      report('notify.drop', id);
    lastDrop = { id: id, at: now };
  }
  function patchDispatcher(o) {
    if (!('Dispatcher' in o) || !('LayerMediator' in o) ||
        !('FocusModeSupport' in o))
      return;
    var dispatcher = o.Dispatcher;
    var add = dispatcher && dispatcher.addEventListener;
    if (typeof add !== 'function' || add.__x5guard) return;
    var wrapped = function (name, listener) {
      var args = Array.prototype.slice.apply(arguments);
      if (name === 'notification' && typeof listener === 'function') {
        args[1] = function (type, data) {
          var id = useCase(type, data);
          if (DROPPED.indexOf(id) >= 0) {
            dropped(id);
            return undefined;
          }
          return listener.apply(this, arguments);
        };
      }
      return add.apply(this, args);
    };
    wrapped.__x5guard = true;
    try { dispatcher.addEventListener = wrapped; } catch (e) {}
    log(dispatcher.addEventListener === wrapped ? 'notifications watched'
                                                : 'notifications not watched');
  }
  function patchReactNative(rn) {
    if (A.patched) return;
    A.patched = true;
    try { patchNative(rn); } catch (e) { log('native: ' + e); }
    log('installed');
  }

  /* ---- Settings' update check, Control Center's sign-in cards -------- */
  /* Settings' own checker (hasUpdate, doCheckSystem, checkIsTestPeriod):
     never an update, never a query to the update server. */
  var checkReported = false;
  function patchUpdateChecker(o) {
    var has = own(o, 'hasUpdate');
    if (typeof has !== 'function' || has.__x5guard ||
        typeof own(o, 'doCheckSystem') !== 'function' ||
        typeof own(o, 'checkIsTestPeriod') !== 'function')
      return;
    var never = function () {
      if (!checkReported) {
        checkReported = true;
        report('update.check', 'UpdateChecker.hasUpdate');
      }
      return Promise.resolve(false);
    };
    never.__x5guard = true;
    try { o.hasUpdate = never; } catch (e) {}
    log(o.hasUpdate === never ? 'update check off' : 'update check kept');
  }
  /* Settings' registry writes (setRegistry/saveRegistry of the module that
     also has getRegistry and fetchRegistry; entries {key|registryKey,
     value}): the automatic download and install of system software, and of
     game and app updates (both fetched from PSN), stay off. Switching them
     on, by their switches or a rest-mode preset, writes off instead. */
  var AUTO = ['isSystemUpdateAutoDownload', 'isSystemUpdateAutoInstall',
              'isPatchAutoDownload', 'isPatchAutoInstall'];
  function keepAutoOff(list) {
    for (var i = 0; list && i < list.length; i++) {
      var entry = list[i];
      var key = entry && (entry.key || entry.registryKey);
      if (AUTO.indexOf(key) >= 0 && entry.value) {
        entry.value = false;
        report('update.auto', key);
      }
    }
  }
  function patchRegistry(o) {
    if (typeof own(o, 'getRegistry') !== 'function' ||
        typeof own(o, 'fetchRegistry') !== 'function')
      return;
    ['setRegistry', 'saveRegistry'].forEach(function (name) {
      var write = own(o, name);
      if (typeof write !== 'function' || write.__x5guard) return;
      var wrapped = function () {
        for (var i = 0; i < arguments.length; i++)
          if (Array.isArray(arguments[i])) keepAutoOff(arguments[i]);
        return write.apply(this, arguments);
      };
      wrapped.__x5guard = true;
      try { o[name] = wrapped; } catch (e) {}
    });
    log('automatic updates kept off');
  }
  /* The cards' ConnectionStatus draws its button only while its label is
     set: without one the not-signed-in card looks like the maintenance
     card, texts only. */
  function patchConnectionStatus(o) {
    var Card = own(o, 'ConnectionStatus');
    var proto = typeof Card === 'function' && Card.prototype;
    var render = proto && proto.render;
    if (typeof render !== 'function' || render.__x5guard) return;
    var wrapped = function () {
      var card = this.defaultValue && this.defaultValue.PSN_NOT_SIGNED_IN;
      if (card) card.ctaButtonLabel = null;
      return render.apply(this, arguments);
    };
    wrapped.__x5guard = true;
    proto.render = wrapped;
    log('sign-in cards without button');
  }

  /* ---- the guard banner, in the notification overlay ------------------ */
  /* The overlay draws a banner's text in fixed white. Ours starts with the
     guard's prefix (warning sign U+26A0 U+FE0E + "x5Guard(TM)", see
     BannerPrefix in guard_banner.cpp): the sign keeps its own colour (an
     emoji, yellow, which no text colour changes) and the name becomes a
     nested Text in that same yellow; the rest stays as it was. Only in the
     overlay (cfg.overlay), only for a string starting with the prefix. */
  var kSign = '\u26A0\uFE0E ';
  var kName = 'x5Guard\u2122';
  var kPrefix = kSign + kName;
  var kColor = '#FFC83D'; /* the warning emoji's yellow */
  function paintFactory(target, name) {
    var make = own(target, name);
    if (typeof make !== 'function' || make.__x5guard) return;
    var classic = name === 'createElement';
    var wrapped = function (type, props) {
      var text = classic ? arguments[2] : props && props.children;
      if (typeof text !== 'string' || text.indexOf(kPrefix) !== 0 ||
          (classic && arguments.length !== 3))
        return make.apply(this, arguments);
      try {
        var rest = text.substring(kPrefix.length);
        var args = Array.prototype.slice.call(arguments);
        if (classic) {
          args[2] = kSign;
          args.push(make(type, { style: { color: kColor } }, kName), rest);
        } else {
          var copy = {};
          for (var key in props) copy[key] = props[key];
          copy.children = [kSign, make(type, { style: { color: kColor },
                                               children: kName }, 'x5'),
                           rest];
          args[1] = copy;
        }
        return make.apply(this, args);
      } catch (e) { log('banner paint: ' + e); }
      return make.apply(this, arguments);
    };
    wrapped.__x5guard = true;
    try { target[name] = wrapped; } catch (e) {}
  }
  function patchBannerPaint(o) {
    if (!cfg.overlay) return;
    if (typeof own(o, 'jsx') === 'function' &&
        typeof own(o, 'jsxs') === 'function') {
      paintFactory(o, 'jsx');
      paintFactory(o, 'jsxs');
    }
    if (typeof own(o, 'createElement') === 'function' &&
        typeof own(o, 'memo') === 'function')
      paintFactory(o, 'createElement');
  }

  /* ---- the modules, by shape ----------------------------------------- */
  function scan(o) {
    if (!o || (typeof o !== 'object' && typeof o !== 'function')) return;
    try {
      if (!A.rn && 'AppRegistry' in o && 'NativeModules' in o &&
          'I18nPS' in o) {
        A.rn = o;
        patchReactNative(o);
      }
      patchDispatcher(o);
      patchUpdateChecker(o);
      patchRegistry(o);
      patchConnectionStatus(o);
      patchBannerPaint(o);
    } catch (e) {}
  }
  var originalCall = Function.prototype.call;
  var runtimes = [];
  var releasing = false;
  function hookRequire(require) {
    var factories = require.m;
    var wrap = function (factory) {
      return function (module, exports, req) {
        var result = factory.apply(this, arguments);
        scan(module && module.exports);
        return result;
      };
    };
    for (var id in factories)
      if (typeof factories[id] === 'function')
        factories[id] = wrap(factories[id]);
  }
  function release() {
    if (Function.prototype.call === hook) Function.prototype.call = originalCall;
  }
  /* A module factory run by a webpack require: factory.call(exports,
     module, exports, require[, factory]). A require with its factory table
     (require.m) has every factory wrapped once; a bare one (the store's)
     has each module looked at here as it is made, while the hook is in. */
  function hook(thisArg, module, exports, require) {
    if (arguments.length >= 4 && typeof require === 'function' && module &&
        typeof module === 'object' && module.exports === exports &&
        exports === thisArg) {
      var table = require.m && require.c ? require.m : null;
      if (!table || runtimes.indexOf(table) < 0) {
        if (table) {
          runtimes.push(table);
          try { hookRequire(require); } catch (e) { log('hook failed: ' + e); }
        }
        var result = originalCall.apply(this, arguments);
        scan(module.exports);
        return result;
      }
    }
    if (!releasing && typeof setTimeout === 'function') {
      releasing = true;
      setTimeout(release, 15000);
    }
    return originalCall.apply(this, arguments);
  }
  Function.prototype.call = hook;
})();)JS"
