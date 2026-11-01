import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const BUS_NAME = 'io.github.mithunveluru.ClipVault.Focus';
const OBJECT_PATH = '/io/github/mithunveluru/ClipVault/Focus';

const INTERFACE_XML = `
<node>
  <interface name="io.github.mithunveluru.ClipVault.Focus">
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

// titles only ever travel over the session bus of this login and are never logged or stored here
class FocusService {
    constructor() {
        this._window = null;
        this._titleId = 0;
        this._dbus = Gio.DBusExportedObject.wrapJSObject(INTERFACE_XML, this);
        this._dbus.export(Gio.DBus.session, OBJECT_PATH);
        this._nameId = Gio.bus_own_name_on_connection(
            Gio.DBus.session, BUS_NAME, Gio.BusNameOwnerFlags.NONE, null, null);
        this._focusId = global.display.connect('notify::focus-window', () => this._onFocus());
        this._onFocus();
    }

    destroy() {
        global.display.disconnect(this._focusId);
        this._watch(null);
        Gio.bus_unown_name(this._nameId);
        this._dbus.unexport();
        this._dbus = null;
    }

    GetFocus() {
        return this._describe(global.display.focus_window);
    }

    Activate(windowId) {
        const window = global.get_window_actors()
            .map(actor => actor.get_meta_window())
            .find(candidate => candidate && Number(candidate.get_id()) === Number(windowId));
        if (!window)
            return false;
        Main.activateWindow(window);
        return true;
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

    _emit() {
        this._dbus?.emit_signal('FocusChanged',
            new GLib.Variant('(sssxt)', this._describe(this._window)));
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
