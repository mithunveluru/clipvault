import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const BUS_NAME = 'io.github.mithunveluru.ClipVault.Focus';
const OBJECT_PATH = '/io/github/mithunveluru/ClipVault/Focus';
const CLIENT_NAME = 'clipvault';
const ACCESS_DENIED = 'org.freedesktop.DBus.Error.AccessDenied';

const INTERFACE_XML = `
<node>
  <interface name="io.github.mithunveluru.ClipVault.Focus">
    <method name="Subscribe"/>
    <method name="GetFocus">
      <arg type="s" direction="out" name="appId"/>
      <arg type="s" direction="out" name="wmClass"/>
      <arg type="s" direction="out" name="title"/>
      <arg type="x" direction="out" name="pid"/>
      <arg type="t" direction="out" name="windowId"/>
    </method>
    <method name="Activate">
      <arg type="t" direction="in" name="windowId"/>
      <arg type="b" direction="out" name="activated"/>
    </method>
    <signal name="FocusChanged">
      <arg type="s" name="appId"/>
      <arg type="s" name="wmClass"/>
      <arg type="s" name="title"/>
      <arg type="x" name="pid"/>
      <arg type="t" name="windowId"/>
    </signal>
  </interface>
</node>`;

// titles only ever travel over the session bus of this login to clipvault and are never logged or stored here
class FocusService {
    constructor() {
        this._window = null;
        this._titleId = 0;
        // unique bus name of each subscriber mapped to the watch that forgets it
        this._subscribers = new Map();
        this._bus = Gio.DBus.session;
        this._dbus = Gio.DBusExportedObject.wrapJSObject(INTERFACE_XML, this);
        this._dbus.export(this._bus, OBJECT_PATH);
        this._nameId = Gio.bus_own_name_on_connection(
            this._bus, BUS_NAME, Gio.BusNameOwnerFlags.NONE, null, null);
        this._focusId = global.display.connect('notify::focus-window', () => this._onFocus());
        this._watch(global.display.focus_window);
    }

    destroy() {
        global.display.disconnect(this._focusId);
        this._watch(null);
        for (const id of this._subscribers.values())
            Gio.bus_unwatch_name(id);
        this._subscribers.clear();
        Gio.bus_unown_name(this._nameId);
        this._dbus.unexport();
        this._dbus = null;
    }

    // a same user check that the caller runs a binary named clipvault and not a sandbox boundary
    _whenClipVault(invocation, action) {
        const sender = invocation.get_sender();
        this._bus.call('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
            'GetConnectionUnixProcessID', new GLib.Variant('(s)', [sender]), new GLib.VariantType('(u)'),
            Gio.DBusCallFlags.NONE, -1, null, (bus, result) => {
                let allowed = false;
                try {
                    const [pid] = bus.call_finish(result).deepUnpack();
                    const exe = GLib.file_read_link(`/proc/${pid}/exe`);
                    allowed = GLib.path_get_basename(exe) === CLIENT_NAME;
                } catch {
                    allowed = false;
                }
                if (!allowed) {
                    invocation.return_dbus_error(ACCESS_DENIED, 'only clipvault may use this interface');
                    return;
                }
                action(sender);
            });
    }

    SubscribeAsync(_params, invocation) {
        this._whenClipVault(invocation, sender => {
            if (!this._subscribers.has(sender)) {
                const id = Gio.bus_watch_name_on_connection(this._bus, sender,
                    Gio.BusNameWatcherFlags.NONE, null, () => this._forget(sender));
                this._subscribers.set(sender, id);
            }
            invocation.return_value(null);
        });
    }

    GetFocusAsync(_params, invocation) {
        this._whenClipVault(invocation, () => {
            invocation.return_value(
                new GLib.Variant('(sssxt)', this._describe(global.display.focus_window)));
        });
    }

    ActivateAsync([windowId], invocation) {
        this._whenClipVault(invocation, () => {
            const window = global.get_window_actors()
                .map(actor => actor.get_meta_window())
                .find(candidate => candidate && Number(candidate.get_id()) === Number(windowId));
            if (window)
                Main.activateWindow(window);
            invocation.return_value(new GLib.Variant('(b)', [Boolean(window)]));
        });
    }

    _forget(name) {
        const id = this._subscribers.get(name);
        if (id === undefined)
            return;
        Gio.bus_unwatch_name(id);
        this._subscribers.delete(name);
    }

    _describe(window) {
        if (!window)
            return ['', '', '', 0, 0];
        const app = Shell.WindowTracker.get_default().get_window_app(window);
        // a window without a desktop file gets a made up id that names nothing
        const appId = app && !app.is_window_backed() ? app.get_id().replace(/\.desktop$/, '') : '';
        return [appId, window.get_wm_class() ?? '', window.get_title() ?? '',
            Math.max(window.get_pid(), 0), Number(window.get_id())];
    }

    // editors put the open file in the title so a rename of the focused window counts as a change
    _watch(window) {
        if (this._window && this._titleId)
            this._window.disconnect(this._titleId);
        this._window = window;
        this._titleId = window ? window.connect('notify::title', () => this._emit()) : 0;
    }

    _onFocus() {
        this._watch(global.display.focus_window);
        this._emit();
    }

    // sent only to subscribers so other clients on the bus never see titles
    _emit() {
        if (!this._dbus || this._subscribers.size === 0)
            return;
        const values = new GLib.Variant('(sssxt)', this._describe(this._window));
        for (const name of this._subscribers.keys())
            this._bus.emit_signal(name, OBJECT_PATH, BUS_NAME, 'FocusChanged', values);
    }
}

export default class ClipVaultFocusExtension extends Extension {
    enable() {
        this._service = new FocusService();
    }

    disable() {
        this._service?.destroy();
        this._service = null;
    }
}
