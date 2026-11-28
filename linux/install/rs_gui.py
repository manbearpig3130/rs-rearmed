#!/usr/bin/env python3
"""The RS Re-ARMed Installer's window on Linux, after the Mac installer's (tools/installer/RSReARMedInstaller.swift):
the game folder, Settings and switches, Install with its progress, what the user has to do in Steam in a box of
its own, the log, and Refresh Game Files; the settings page with the built-in mods and the options in their
groups, the rest under Advanced.

    rs_install.py installer        opens it (and a start with no setup yet, from the desktop)

GTK 3 through PyGObject, which the desktops' own tools are written with and so is there on most of them; where it
is not, rs_install.py keeps to the terminal, or to zenity's progress window. The work is rs_install.py's own
commands, run as a child that writes progress lines (--progress-lines), as the Mac installer runs its scripts.

For a test with no person at hand: --screenshot FILE draws the window, saves a picture of it and ends; with
--page settings it is the settings page's.
"""
import os
import signal
import subprocess
import sys

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")  # (asked for itself: a newer PyGObject loads Gdk 4 when it is imported first)
from gi.repository import Gdk, GLib, Gtk, Pango  # noqa: E402

import rs_install  # noqa: E402
import rs_settings  # noqa: E402

TITLE = "RS Re-ARMed Installer"
STEAM_PAGE = "https://store.steampowered.com/about/"


def version():
    for p in (os.path.join(rs_install.HERE, "VERSION"), os.path.join(rs_install.HERE, "..", "..", "VERSION")):
        try:
            with open(p) as f:
                return f.read().strip()
        except OSError:
            pass
    return ""


# The look, after the Mac installer's (AppKit's proportions in GTK, with the desktop's own theme and colours): labels
# of a row at a medium weight, hints small and dim, the log a darker inset box of small monospace text with rounded
# corners, and room between groups. The log's colour is worked out from the theme's, light or dark.
CSS = """
.rs-row-label { font-weight: 500; }
.rs-hint { font-size: 0.88em; }
.rs-step { font-weight: 500; }
.rs-log-box { border-radius: 6px; border: 1px solid alpha(@theme_fg_color, 0.12); }
.rs-log-box, .rs-log-box textview, .rs-log-box textview text { background-color: %(log_bg)s; }
.rs-log-box textview text { color: alpha(@theme_fg_color, 0.85); }
.rs-log-box textview { font-family: monospace; font-size: 0.82em; }
.rs-guide { border-radius: 6px; border: 1px solid alpha(@theme_selected_bg_color, 0.55);
            background-color: alpha(@theme_selected_bg_color, 0.10); }
.rs-sheet-title { font-weight: bold; font-size: 1.1em; }
.rs-section { font-weight: bold; }
.rs-buttons { border-top: 1px solid alpha(@theme_fg_color, 0.10); }
.rs-bar trough, .rs-bar progress { min-height: 4px; border-radius: 2px; }
.rs-bar trough { border: none; background-color: alpha(@theme_fg_color, 0.12); }
"""


def apply_style(widget):
    """The style sheet, once, for the screen WIDGET is on."""
    if getattr(apply_style, "done", False):
        return
    apply_style.done = True
    found, bg = widget.get_style_context().lookup_color("theme_bg_color")
    dark = found and (0.2126 * bg.red + 0.7152 * bg.green + 0.0722 * bg.blue) < 0.5
    css = CSS % {"log_bg": "shade(@theme_bg_color, 0.62)" if dark else "@theme_base_color"}
    provider = Gtk.CssProvider()
    provider.load_from_data(css.encode())
    Gtk.StyleContext.add_provider_for_screen(widget.get_screen(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)


def styled(widget, *classes):
    for c in classes:
        widget.get_style_context().add_class(c)
    return widget


def label(text, dim=False, small=False, bold=False, wrap=False, big=False):
    w = Gtk.Label(label=text, xalign=0)
    attrs = Pango.AttrList()
    if bold:
        attrs.insert(Pango.attr_weight_new(Pango.Weight.BOLD))
    if small:
        attrs.insert(Pango.attr_scale_new(0.88))
    if big:
        attrs.insert(Pango.attr_scale_new(1.6))
    w.set_attributes(attrs)
    if dim:
        w.get_style_context().add_class("dim-label")
    if wrap:
        w.set_line_wrap(True)
        w.set_max_width_chars(70)
    return w


def alert(parent, title, text, kind=Gtk.MessageType.ERROR):
    d = Gtk.MessageDialog(transient_for=parent, modal=True, message_type=kind, buttons=Gtk.ButtonsType.OK, text=title)
    d.format_secondary_text(text)
    d.run()
    d.destroy()


# --- Settings and switches ------------------------------------------------------------------------------------

class SettingsPage(Gtk.Dialog):
    """Every control starts at what the installed game has (settings.env and options.env, or the defaults when
    there is neither); Save writes both files whole."""

    def __init__(self, parent, home):
        super().__init__(title="Settings and switches", transient_for=parent, modal=True)
        apply_style(self)
        self.home = home
        self.set_default_size(640, 620)
        self.mods, self.controls = {}, {}
        self.names = Gtk.SizeGroup(mode=Gtk.SizeGroupMode.HORIZONTAL)  # the options' names one width: fields in a column
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        for side in ("start", "end", "top", "bottom"):
            getattr(box, "set_margin_" + side)(20)
        box.pack_start(styled(label("Settings and switches: Rocksmith 2014 for Linux"), "rs-sheet-title"), False, False, 0)
        box.pack_start(label("Set what you want. A change takes effect the next time the game starts, with nothing to "
                             "reinstall. Empty fields and Default choices are the game's own settings.", wrap=True),
                       False, False, 0)
        box.pack_start(label("The defaults are what RS Re-ARMed is tested with. Changing these can have unexpected "
                             "effects, and problems with other values are not supported: Reset to Defaults before "
                             "reporting one.", wrap=True), False, False, 0)

        box.pack_start(self.heading("Built-in mods"), False, False, 0)
        off = rs_settings.read_mods_off(home)
        for mod in rs_settings.MODS:
            check = Gtk.CheckButton(label=mod.title)
            check.set_active(mod.key not in off)
            check.set_tooltip_text("%s in %s" % (mod.key, rs_settings.settings_file(home)))
            self.mods[mod.key] = check
            box.pack_start(self.row(check, rs_settings.detail(mod)), False, False, 0)

        values = rs_settings.read_options(home)
        options = rs_settings.shown()
        for group in rs_settings.GROUPS:
            rows = [o for o in options if o.group == group and not o.advanced]
            if rows:
                box.pack_start(self.heading(group), False, False, 0)
                for o in rows:
                    box.pack_start(self.option_row(o, values), False, False, 0)

        # Advanced: one section, collapsed by default, as the Mac sheet has it
        self.advanced_check = Gtk.Expander()
        self.advanced_check.set_label_widget(styled(label("Advanced"), "rs-section"))
        self.advanced_check.set_margin_top(12)
        self.advanced_check.connect("notify::expanded", lambda w, _: self.advanced.set_visible(w.get_expanded()))
        box.pack_start(self.advanced_check, False, False, 0)
        self.advanced = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        self.advanced.set_no_show_all(True)
        self.advanced.pack_start(label(
            "These turn off fixes or change how the game is run. Leave them alone unless you are tracking down a "
            "problem. The defaults are what RS Re-ARMed is tested with: Reset to Defaults before reporting one.",
            dim=True, small=True, wrap=True), False, False, 0)
        for o in options:
            if o.advanced:
                self.advanced.pack_start(self.option_row(o, values), False, False, 0)
        box.pack_start(self.advanced, False, False, 0)
        if any(o.advanced and o.key in values for o in options):
            self.advanced_check.set_expanded(True)

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroll.add(box)
        self.get_content_area().pack_start(scroll, True, True, 0)
        # The buttons in a strip of their own under the list: Reset to Defaults on the left, Cancel and Save right
        area = styled(self.get_action_area(), "rs-buttons")
        area.set_property("margin", 0)
        for side in ("start", "end"):
            getattr(area, "set_margin_" + side)(20)
        area.set_margin_top(0)
        area.set_margin_bottom(14)
        area.set_border_width(0)
        reset = self.add_button("Reset to Defaults", Gtk.ResponseType.REJECT)
        reset.set_tooltip_text("Every mod on and every other switch the game's own setting (Save keeps it).")
        if isinstance(area, Gtk.ButtonBox):
            area.set_child_secondary(reset, True)
            area.set_layout(Gtk.ButtonBoxStyle.END)
        self.add_button("Cancel", Gtk.ResponseType.CANCEL)
        save = self.add_button("Save", Gtk.ResponseType.OK)
        save.get_style_context().add_class("suggested-action")
        for b in area.get_children():
            b.set_margin_top(12)
            if isinstance(area, Gtk.ButtonBox):
                area.set_child_non_homogeneous(b, True)  # each its own width, as the Mac's
        self.set_default_response(Gtk.ResponseType.OK)
        self.show_all()
        for child in self.advanced.get_children():
            child.show_all()
        self.advanced.set_visible(self.advanced_check.get_expanded())
        save.grab_focus()

    @staticmethod
    def heading(text):
        w = styled(label(text), "rs-section")
        w.set_margin_top(12)
        return w

    @staticmethod
    def row(control, detail, sensitive=True):
        """A control with what it does under it, in smaller, dimmer letters."""
        b = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=1)
        b.pack_start(control, False, False, 0)
        d = styled(label(detail, dim=True, wrap=True), "rs-hint")
        d.set_margin_start(26)
        b.pack_start(d, False, False, 0)
        b.set_sensitive(sensitive)
        return b

    def option_row(self, o, values):
        value = values.get(o.key, "")
        there = o.linux == "yes"
        text = rs_settings.detail(o) + ("" if there else " " + rs_settings.SOON)
        tip = "%s in %s" % (o.key, rs_settings.options_file(self.home))
        kind = o.kind
        if kind[0] == "toggle":
            control = Gtk.CheckButton(label=o.title)
            control.set_active(value == "1" if value else kind[1])
            self.controls[o.key] = control
            control.set_tooltip_text(tip)
            return self.row(control, text, there)
        line = Gtk.Box(spacing=8)
        name = label(o.title)
        name.set_margin_start(26)
        self.names.add_widget(name)
        line.pack_start(name, False, False, 0)
        if kind[0] == "choice":
            control = Gtk.ComboBoxText()
            for title, v in kind[1]:
                control.append(v or "default", title)
            control.set_active_id(value or "default")
            if control.get_active() < 0:
                control.set_active(0)
        else:
            control = Gtk.Entry()
            control.set_text(value)
            control.set_placeholder_text(kind[2])
            control.set_width_chars(max(12, len(kind[2]) + 4))
        control.set_tooltip_text(tip)
        self.controls[o.key] = control
        line.pack_start(control, False, False, 0)
        return self.row(line, text, there)

    def reset(self):
        for check in self.mods.values():
            check.set_active(True)
        for key, control in self.controls.items():
            kind = rs_settings.OPTION[key].kind
            if kind[0] == "toggle":
                control.set_active(kind[1])
            elif kind[0] == "choice":
                control.set_active(0)
            else:
                control.set_text("")

    def read(self):
        """(mods switched off, the options' values that are not the default, the first option whose value is not
        one it takes or None). An option the page does not show keeps the line it had."""
        off = {k for k, check in self.mods.items() if not check.get_active()}
        values = {k: v for k, v in rs_settings.read_options(self.home).items() if k not in self.controls}
        for key, control in self.controls.items():
            o = rs_settings.OPTION[key]
            kind = o.kind
            if kind[0] == "toggle":
                if control.get_active() != kind[1]:
                    values[key] = "1" if control.get_active() else "0"
            elif kind[0] == "choice":
                v = control.get_active_id()
                if v and v != "default":
                    values[key] = v
            else:
                v = control.get_text().strip()
                if v and not rs_settings.valid(o, v):
                    return off, values, o
                if v:
                    values[key] = v
        return off, values, None

    def run_page(self):
        """True when saved."""
        while True:
            answer = self.run()
            if answer == Gtk.ResponseType.REJECT:
                self.reset()
                continue
            if answer != Gtk.ResponseType.OK:
                return False
            off, values, bad = self.read()
            if bad:
                alert(self, "That %s isn't valid" % bad.title.lower(), rs_settings.detail(bad))
                continue
            try:
                rs_settings.save_mods_off(self.home, off)
                rs_settings.save_options(self.home, values)
            except OSError as e:
                alert(self, "The settings could not be saved", str(e))
                continue
            return True


# --- the window -----------------------------------------------------------------------------------------------

GUIDE_BUTTON = {"steam-missing": "Get Steam", "steam-starting": "Open Steam", "sign-in": "Open Steam",
                "wrong-account": "Open Steam", "wrong-account-mac": "Open Steam", "steam-offline": "Open Steam",
                "installing-game": "Open Steam", "download-stalled": "Try Again", "download-failed": "Try Again",
                "paste": "Try Again"}


class Installer(Gtk.Window):
    def __init__(self, home=None, steam=None):
        super().__init__(title="%s v%s" % (TITLE, version()))
        self.home = rs_install.home_folder(home)
        self.steam = steam
        self.child = None        # the setup or refresh that runs
        self.watch = None
        self.what = ""           # "install" or "refresh"
        self.stopped = False
        self.guide_code = ""
        self.result_action = None
        self.log_file = None
        self.set_default_size(720, 640)
        self.set_icon_name("rs-rearmed")
        self.connect("delete-event", self.closing)
        apply_style(self)

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        for side in ("start", "end", "top", "bottom"):
            getattr(box, "set_margin_" + side)(20)
        self.add(box)

        head = Gtk.Box(spacing=10)
        head.pack_start(label(TITLE, bold=True, big=True), False, False, 0)
        v = label("v%s" % version(), dim=True)
        v.set_valign(Gtk.Align.END)
        v.set_margin_bottom(4)
        head.pack_start(v, False, False, 0)
        box.pack_start(head, False, False, 0)
        box.pack_start(styled(label("Sets up Rocksmith 2014 on Linux from your Steam copy of the game.", dim=True), "rs-hint"),
                       False, False, 0)

        folder = styled(label("Mac game folder (leave empty to download it with Steam):"), "rs-row-label")
        folder.set_margin_top(12)
        box.pack_start(folder, False, False, 0)
        line = Gtk.Box(spacing=8)
        self.game_dir = Gtk.Entry()
        self.game_dir.set_placeholder_text("Steam's download folder")
        line.pack_start(self.game_dir, True, True, 0)
        self.browse = Gtk.Button(label="Browse…")
        self.browse.connect("clicked", self.browse_game)
        line.pack_start(self.browse, False, False, 0)
        box.pack_start(line, False, False, 0)

        # A row as the Mac's Renderer row: its label, a short hint, and the button at the right
        line = Gtk.Box(spacing=10)
        line.set_margin_top(8)
        line.pack_start(styled(label("Game options:"), "rs-row-label"), False, False, 0)
        hint = styled(label("Custom songs, the mods, the sound; no reinstall", dim=True), "rs-hint")
        hint.set_ellipsize(Pango.EllipsizeMode.END)
        line.pack_start(hint, True, True, 0)
        self.settings = Gtk.Button(label="Settings and switches…")
        self.settings.connect("clicked", self.show_settings)
        line.pack_end(self.settings, False, False, 0)
        box.pack_start(line, False, False, 0)

        self.dlc = Gtk.CheckButton(label="Download my DLC with Steam")
        self.dlc.set_margin_top(6)
        self.dlc.set_active(True)
        self.dlc.set_tooltip_text("Each DLC the Steam account owns, a few seconds apiece. Refresh Game Files gets the "
                                  "ones bought later.")
        box.pack_start(self.dlc, False, False, 0)
        self.menu = Gtk.CheckButton(label="Add RS Re-ARMed to the applications menu")
        self.menu.set_active(True)
        box.pack_start(self.menu, False, False, 0)

        self.bar = styled(Gtk.ProgressBar(), "rs-bar")
        self.bar.set_margin_top(14)
        box.pack_start(self.bar, False, False, 0)
        line = Gtk.Box(spacing=12)
        line.set_margin_top(4)
        self.step = styled(label("Ready"), "rs-step")
        self.step.set_ellipsize(Pango.EllipsizeMode.END)
        line.pack_start(self.step, True, True, 0)
        self.detail = styled(label("", dim=True), "rs-hint")
        line.pack_end(self.detail, False, False, 0)
        box.pack_start(line, False, False, 0)

        # What the user has to do now, or how the last run ended: between the progress and the log.
        self.guide = styled(Gtk.Frame(), "rs-guide")
        self.guide.set_shadow_type(Gtk.ShadowType.NONE)
        inner = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        for side in ("start", "end", "top", "bottom"):
            getattr(inner, "set_margin_" + side)(10)
        self.guide_title = label("", bold=True)
        self.guide_text = label("", wrap=True)
        self.guide_text.set_selectable(True)
        inner.pack_start(self.guide_title, False, False, 0)
        inner.pack_start(self.guide_text, False, False, 0)
        buttons = Gtk.Box(spacing=8)
        self.guide_action = Gtk.Button(label="")
        self.guide_action.connect("clicked", self.guide_clicked)
        self.guide_second = Gtk.Button(label="Stop")
        self.guide_second.connect("clicked", self.second_clicked)
        buttons.pack_start(self.guide_action, False, False, 0)
        buttons.pack_start(self.guide_second, False, False, 0)
        inner.pack_start(buttons, False, False, 0)
        self.guide.add(inner)
        self.guide.set_no_show_all(True)
        box.pack_start(self.guide, False, False, 4)

        self.log = Gtk.TextView()
        self.log.set_editable(False)
        self.log.set_cursor_visible(False)
        self.log.set_monospace(True)
        self.log.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.log.set_left_margin(10)
        self.log.set_right_margin(10)
        self.log.set_top_margin(8)
        self.log.set_bottom_margin(8)
        scroll = styled(Gtk.ScrolledWindow(), "rs-log-box")
        scroll.set_shadow_type(Gtk.ShadowType.NONE)
        scroll.set_min_content_height(80)
        scroll.add(self.log)
        self.scroll = scroll
        box.pack_start(scroll, True, True, 4)

        line = Gtk.Box(spacing=8)
        line.set_margin_top(6)
        self.show_log = Gtk.Button(label="Show Log File")
        self.show_log.connect("clicked", lambda w: self.reveal(self.log_path()))
        self.refresh = Gtk.Button(label="Refresh Game Files")
        self.refresh.set_tooltip_text("Copy new DLC, custom songs and game updates from Steam into RS Re-ARMed's own "
                                      "copy of the game files")
        self.refresh.connect("clicked", lambda w: self.start("refresh"))
        self.play = Gtk.Button(label="Play")
        self.play.connect("clicked", lambda w: self.start_game())
        self.close_button = Gtk.Button(label="Close")
        self.close_button.connect("clicked", lambda w: self.close())
        self.install = Gtk.Button(label="Install")
        self.install.get_style_context().add_class("suggested-action")
        self.install.connect("clicked", lambda w: self.start("install"))
        line.pack_start(self.show_log, False, False, 0)
        line.pack_start(self.refresh, False, False, 0)
        line.pack_end(self.install, False, False, 0)
        line.pack_end(self.close_button, False, False, 0)
        line.pack_end(self.play, False, False, 0)
        box.pack_start(line, False, False, 0)

        for w in (self.show_log, self.refresh, self.play):
            w.set_no_show_all(True)  # there when there is something for them (update_buttons)
        self.set_log(self.what_happens())
        self.show_all()
        self.update_buttons()
        # the first button with something to do has the focus, not the folder field (whose hint then shows)
        (self.play if self.play.get_visible() else self.install).grab_focus()

    # --- what is there, and the buttons that follow from it

    def is_set_up(self):
        return rs_install.set_up(self.home)

    def what_happens(self):
        if self.is_set_up():
            return ("RS Re-ARMed is set up in %s.\n\nPlay starts the game. Refresh Game Files brings DLC bought since, "
                    "and checks the copy of the game's files. Settings and switches change at the next start of the "
                    "game, with nothing to reinstall. Custom songs go in %s.\n"
                    % (self.home, os.path.join(self.home, "game", "dlc")))
        return ("Click Install. The installer does the rest and tells you, in a box above this text, whenever it needs "
                "you: a few minutes with the game already downloaded, and as long as the 7 GB download takes "
                "otherwise. You can use the computer meanwhile.\n\nWhat you need: Steam on this computer, signed in "
                "with the account that owns Rocksmith 2014 Edition - Remastered (the game doesn't have to be installed "
                "in Steam), and an internet connection. If anything is missing, the installer says what to do when it "
                "gets there.\n")

    def update_buttons(self):
        running = self.child is not None
        there = self.is_set_up()
        for w in (self.game_dir, self.browse, self.settings, self.dlc, self.menu):
            w.set_sensitive(not running)
        self.install.set_sensitive(not running)
        self.install.set_label("Install Again" if there else "Install")
        self.refresh.set_visible(there)
        self.refresh.set_sensitive(there and not running)
        self.play.set_visible(there)
        self.play.set_sensitive(there and not running)
        self.show_log.set_visible(os.path.exists(self.log_path()))

    # --- the log

    def log_path(self):
        return os.path.join(self.home, "logs", "installer.log")

    def set_log(self, text):
        self.log.get_buffer().set_text(text)

    def add_log(self, text):
        buf = self.log.get_buffer()
        buf.insert(buf.get_end_iter(), text)
        adj = self.scroll.get_vadjustment()
        GLib.idle_add(lambda: adj.set_value(adj.get_upper()) and False)
        if self.log_file:
            self.log_file.write(text)
            self.log_file.flush()

    # --- the box: a step of the user's, or how a run ended

    def show_guide(self, title, text, action="", second="Stop"):
        self.guide_title.set_text(title)
        self.guide_text.set_text(text)
        self.guide_action.set_label(action)
        self.guide_action.set_visible(bool(action))
        self.guide_second.set_label(second)
        self.guide_second.set_visible(bool(second))
        self.guide.show()
        for w in (self.guide.get_child(), self.guide_title, self.guide_text):
            w.show()
        self.guide_action.get_parent().show()

    def hide_guide(self):
        self.guide_code, self.result_action = "", None
        self.guide.hide()

    def guide_clicked(self, _):
        if self.child is None:
            if self.result_action:
                self.result_action()
            return
        if self.guide_code == "steam-missing":
            self.open_uri(STEAM_PAGE)
        elif self.guide_code in ("download-stalled", "download-failed", "paste"):
            try:
                self.child.send_signal(signal.SIGUSR1)  # (gamefiles.mac_game: the download's Try Again)
            except OSError:
                pass
        else:
            rs_install.steam_open()

    def second_clicked(self, _):
        if self.child is None:
            self.reveal(self.log_path())
        else:
            self.stop()

    # --- a run

    def command(self, what):
        argv = [sys.executable, "-I", os.path.join(rs_install.HERE, "rs_install.py"), "--home", self.home]
        if self.steam:
            argv += ["--steam", self.steam]
        argv += ["setup" if what == "install" else "refresh", "--progress-lines"]
        if what == "install" and self.game_dir.get_text().strip():
            argv += ["--game", os.path.expanduser(self.game_dir.get_text().strip())]
        if not self.dlc.get_active():
            argv += ["--no-dlc"]
        return argv

    def start(self, what):
        if self.child is not None:
            return
        self.what, self.stopped = what, False
        self.hide_guide()
        os.makedirs(os.path.dirname(self.log_path()), exist_ok=True)
        self.log_file = open(self.log_path(), "w", encoding="utf-8")
        self.set_log("")
        self.bar.set_fraction(0.0)
        self.step.set_text("Starting…")
        self.detail.set_text("")
        try:
            self.child = subprocess.Popen(self.command(what), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                          stdin=subprocess.DEVNULL)
        except OSError as e:
            self.child = None
            self.finished(1, str(e))
            return
        self.pending = b""
        self.said = []
        self.watch = GLib.io_add_watch(self.child.stdout, GLib.PRIORITY_DEFAULT, GLib.IO_IN | GLib.IO_HUP, self.output)
        self.update_buttons()

    def output(self, source, condition):
        data = os.read(source.fileno(), 65536) if condition & GLib.IO_IN else b""
        if data:
            self.pending += data
            *lines, self.pending = self.pending.split(b"\n")
            for raw in lines:
                self.line(raw.decode("utf-8", "replace"))
            return True
        if self.pending:
            self.line(self.pending.decode("utf-8", "replace"))
        code = self.child.wait()
        self.child, self.watch = None, None
        self.finished(code, "\n".join(self.said[-3:]))
        return False

    def line(self, line):
        if line.startswith("progress "):
            _, fraction, text = (line.split(" ", 2) + [""])[:3]
            try:
                self.bar.set_fraction(min(max(float(fraction), 0.0), 1.0))
            except ValueError:
                pass
            # "Downloading the Mac game… 1.2 GB of 6.9 GB": the step, and how far it is
            step, sep, far = text.partition("… ")
            self.step.set_text(step + ("…" if sep else ""))
            self.detail.set_text(far)
        elif line.startswith("guide\t"):
            parts = line.split("\t")
            if len(parts) >= 4:
                self.guide_code = parts[1]
                self.show_guide(parts[2], parts[3], GUIDE_BUTTON.get(parts[1], ""))
                self.add_log("== %s\n%s\n" % (parts[2], parts[3]))
            else:
                self.hide_guide()
        elif line.strip():
            self.said.append(line)
            self.add_log(line + "\n")

    def stop(self):
        if self.child is not None:
            self.stopped = True
            try:
                self.child.terminate()
            except OSError:
                pass

    def finished(self, code, said):
        if self.log_file:
            self.log_file.close()
            self.log_file = None
        self.hide_guide()
        self.detail.set_text("")
        again = lambda: self.start(self.what)  # noqa: E731
        if self.stopped:
            self.step.set_text("Stopped")
            self.show_guide("Stopped", "What is done is kept: the next run goes on from there.", "Try Again", "Show Log File")
            self.result_action = again
        elif code:
            self.step.set_text("Failed")
            self.show_guide("The refresh did not finish" if self.what == "refresh" else "The install did not finish",
                            said or "See the log.", "Try Again", "Show Log File")
            self.result_action = again
        else:
            self.bar.set_fraction(1.0)
            note = ""
            if self.what == "install" and self.menu.get_active():
                try:
                    note = "\n" + rs_install.menu_entry(self.home)
                    self.add_log(note.strip() + "\n")
                except OSError as e:
                    note = "\nThe applications menu entry could not be written: %s" % e
            self.step.set_text("Done")
            self.show_guide("The game files are up to date" if self.what == "refresh" else "RS Re-ARMed is installed",
                            ("Custom songs go in %s." % os.path.join(self.home, "game", "dlc")) + note, "Play",
                            "Show Log File")
            self.result_action = self.start_game
        self.update_buttons()

    # --- the rest

    def start_game(self):
        if rs_install.game_running():
            alert(self, "RS Re-ARMed is already running", "Quit the game first.", Gtk.MessageType.INFO)
            return
        argv = [sys.executable, "-I", os.path.join(rs_install.HERE, "rs_install.py"), "--home", self.home, "run"]
        rs_install.spawn(argv)

    def show_settings(self, _):
        page = SettingsPage(self, self.home)
        page.run_page()
        page.destroy()

    def browse_game(self, _):
        d = Gtk.FileChooserDialog(title="The Mac game's folder", transient_for=self, action=Gtk.FileChooserAction.SELECT_FOLDER)
        d.add_buttons("Cancel", Gtk.ResponseType.CANCEL, "Choose", Gtk.ResponseType.OK)
        if d.run() == Gtk.ResponseType.OK:
            self.game_dir.set_text(d.get_filename() or "")
        d.destroy()

    def open_uri(self, uri):
        try:
            Gtk.show_uri_on_window(self, uri, Gdk.CURRENT_TIME)
        except GLib.Error:
            rs_install.spawn(["xdg-open", uri])

    def reveal(self, path):
        self.open_uri(GLib.filename_to_uri(os.path.dirname(path) if os.path.isfile(path) else path, None))

    def closing(self, *_):
        if self.child is None:
            Gtk.main_quit()
            return False
        d = Gtk.MessageDialog(transient_for=self, modal=True, message_type=Gtk.MessageType.QUESTION,
                              text="Stop the install?")
        d.format_secondary_text("What is done is kept: the next run goes on from there.")
        d.add_buttons("Keep Going", Gtk.ResponseType.CANCEL, "Stop and Close", Gtk.ResponseType.OK)
        answer = d.run()
        d.destroy()
        if answer != Gtk.ResponseType.OK:
            return True
        self.stop()
        Gtk.main_quit()
        return False


def screenshot(window, path):
    """A picture of WINDOW as it is drawn, then the end (a test's eyes)."""
    def take():
        w = window.get_window()
        pixbuf = Gdk.pixbuf_get_from_window(w, 0, 0, w.get_width(), w.get_height())
        pixbuf.savev(path, "png", [], [])
        os._exit(0)  # (a page's own loop may be the one that runs: the picture is all this start was for)
    GLib.timeout_add(700, take)


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--home")
    ap.add_argument("--steam")
    ap.add_argument("--screenshot")
    ap.add_argument("--page", default="")
    ap.add_argument("--run", default="", help=argparse.SUPPRESS)  # install or refresh, started at once (a test)
    a = ap.parse_args(argv)
    ok, _ = Gtk.init_check(sys.argv[:1])
    if not ok:
        sys.stderr.write("rs-rearmed: no display to open the installer's window on\n")
        return 1
    win = Installer(a.home, a.steam)
    if a.page in ("settings", "advanced"):
        page = SettingsPage(win, win.home)
        if a.page == "advanced":
            page.advanced_check.set_expanded(True)
            page.set_default_size(680, 1500)
            page.resize(680, 1500)
        if a.screenshot:
            screenshot(page, a.screenshot)
        GLib.idle_add(lambda: page.run_page() and False)
    elif a.screenshot and not a.run:
        screenshot(win, a.screenshot)
    if a.run in ("install", "refresh"):
        GLib.idle_add(lambda: win.start(a.run) and False)
        if a.screenshot:  # when the run has ended
            def done():
                if win.child is None and win.what:
                    screenshot(win, a.screenshot)
                    return False
                return True
            GLib.timeout_add(300, done)
    Gtk.main()
    return 0


if __name__ == "__main__":
    sys.exit(main())
