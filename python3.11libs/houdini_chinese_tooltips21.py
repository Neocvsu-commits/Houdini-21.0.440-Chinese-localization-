"""H21 help-card display adapter; never changes help sources or TipData."""
import ctypes
from dataclasses import replace
from html import escape
from html.parser import HTMLParser
import weakref

_installed = False
_filter = None

# H21's data-driven shelves sometimes put the recipe identifier in the help
# body instead of resolving its summary. Replace those identifiers with their
# English description first; the regular dictionary then supplies Chinese.
_RECIPE_HELP = {
    'mpm_configure': 'A simple setup that demonstrates how a typical MPM node tree should be wired together.',
    'mpm_configure_building_collapse': 'Puts down a simple network of nodes to simulate a building being hit by a meteorite and collapsing.',
    'mpm_configure_jello_party': 'Puts down a simple network of nodes to simulate jello cubes rolling and bouncing around.',
    'mpm_configure_landslide': 'Puts down a simple network of nodes to simulate a cliff with some soil and grass sliding down a slope.',
    'mpm_configure_metal_tearing': 'Puts down a simple network of nodes to simulate a sphere collider ripping through a metal sheet.',
    'mpm_configure_pancakes': "Puts down a simple network of nodes to simulate a stack of pancakes that's drizzled in syrup (honey) and then cut.",
    'mpm_configure_sand_instances': 'Puts down a simple network of nodes to simulate Crag walking through a sandbox.',
    'mpm_configure_rolling_snowball': 'Puts down a simple network of nodes to simulate a snowball rolling down hill.',
    'mpm_configure_softbody': 'Puts down a simple network of nodes to turn the pig head into a rubbery material.',
    'mpm_configure_spinning_tire': 'Puts down a simple network of nodes to simulate a tire spinning around in a mixture of water and mud.',
    'mpm_configure_water_glass': 'Puts down a simple network of nodes to simulate water being poured from one glass into another.',
    'aerialbarrage': 'Creates a large scale aerial explosion that moves horizontally.',
    'aerialgroundbarrage': 'Creates a large scale aerial explosion that moves towards the ground.',
    'aerialexplosion': 'Creates an aerial explosion.',
    'bullethits': 'Creates multiple small scale bullet hit effect with RBDs, instanced debris, particles, and dust.',
    'candleflame': 'Creates a small scale candle flame and generates a melted candle model.',
    'debrisdust': 'Creates a simple RBD simulation that emits dust as the pieces break apart.',
    'dryice': 'Creates a small scale smoke simulation that bubbles up from a glass and sinks to the ground.',
    'dustfireball': 'Creates a large scale dust explosion that develops into a fireball as the simulation develops.',
    'fireballwithtrails': 'Creates a large scale explosion that expands outward, rises quickly, and has debris emitting trails of smoke.',
    'fireportal': 'Creates a particle simulation for a portal that rotates in a circular motion before dissipating.',
    'groundexplosiona': 'Creates a large scale explosion and shockwave that expands forcefully outward and moves rapidly upward.',
    'groundexplosionb': 'Creates a large scale explosion, smoke trails, and shockwave that expands forcefully outward and moves rapidly upward.',
    'groundshockwave': 'Creates a large scale shockwave simulation.',
    'muzzleflash': 'Creates a visible blast from the muzzle of a firearm during shooting.',
    'smokelessflame': "Creates a small scale fire simulation with a transparent source and doesn't emit any smoke.",
    'staggeredgroundshockwaves': 'Creates a medium scale multi-shockwave simulation with staggered start frames.',
    'stylizedfire': 'Creates a medium scale fire and spark simulation in SOPs that is converted to 2D using COPs tools.',
    'synchronizedgroundshockwaves': 'Creates a medium scale multi-shockwave simulation with synchronized start frames.',
    'thrusterexhaust': 'Creates a thruster exhaust setup layering together multiple VDBs and sparks.',
    'wispysmokea': 'Creates a medium scale wispy smoke simulation that rises upward smoothly.',
    'wispysmokeb': 'Creates a medium scale wispy smoke simulation that rises upward turbulently.',
}


def normalize_help_text(text):
    stripped = text.strip()
    prefix = 'shelftool::sidefx::recipe::'
    if not stripped.startswith(prefix):
        return text
    key = stripped[len(prefix):].split('::')[-1]
    return _RECIPE_HELP.get(key, text)


def translate_html(source, translate):
    class TextNodes(HTMLParser):
        def __init__(self):
            super().__init__(convert_charrefs=True)
            self.parts = []
            self.protected = []

        def handle_starttag(self, tag, attrs):
            self.parts.append(self.get_starttag_text())
            if tag in ('script', 'style', 'code', 'pre'):
                self.protected.append(tag)

        def handle_startendtag(self, tag, attrs):
            self.parts.append(self.get_starttag_text())

        def handle_endtag(self, tag):
            self.parts.append('</' + tag + '>')
            if self.protected and self.protected[-1] == tag:
                self.protected.pop()

        def handle_data(self, text):
            result = '' if self.protected else translate(text)
            if self.protected and self.protected[-1] in ('script', 'style'):
                self.parts.append(text)
            else:
                self.parts.append(escape(result or text, quote=False))

        def handle_comment(self, data):
            self.parts.append('<!--' + data + '-->')

        def handle_decl(self, decl):
            self.parts.append('<!' + decl + '>')

    parser = TextNodes()
    parser.feed(source)
    parser.close()
    return ''.join(parser.parts)


def install(native_translate, native_restore):
    global _installed, _filter
    if _installed:
        return
    import hou
    if hou.applicationVersion() != (21, 0, 440):
        return
    from hutil.qt import tooltip
    from PySide6 import QtCore, QtWidgets
    original = tooltip.HoudiniToolTip.display
    sources = weakref.WeakKeyDictionary()

    def call_native(function, text):
        if not text or len(text) > 4096:
            return ''
        buffer = ctypes.create_unicode_buffer(8192)
        function(text, buffer, len(buffer))
        return buffer.value

    def restore(text):
        normalized = normalize_help_text(text)
        return call_native(native_restore, normalized) or normalized

    def translate(text):
        normalized = normalize_help_text(text)
        return call_native(native_translate, normalized) or normalized

    def display(self, data, target, **kwargs):
        # MOT may provide an already translated body from its own cache. Keep
        # an English canonical copy so either language can be redrawn cleanly.
        canonical = replace(data, label=restore(data.label),
                            html=translate_html(data.html, restore))
        sources[self] = (canonical, QtCore.QRect(target), dict(kwargs))
        # Only the copied display fields change. name/default/base_url are
        # identifiers, expressions and navigation data and remain untouched.
        enabled = bool(app.property('hc21_enabled'))
        shown = (replace(canonical, label=translate(canonical.label),
                         html=translate_html(canonical.html, translate))
                 if enabled else canonical)
        return original(self, shown, target, **kwargs)

    def refresh():
        for window, (data, target, kwargs) in list(sources.items()):
            try:
                if window.isVisible():
                    display(window, data, target, **kwargs)
            except RuntimeError:
                sources.pop(window, None)

    class LanguageFilter(QtCore.QObject):
        def eventFilter(self, obj, event):
            if (event.type() == QtCore.QEvent.DynamicPropertyChange
                    and event.propertyName() == b'hc21_enabled'):
                QtCore.QTimer.singleShot(0, refresh)
            return False

    app = QtWidgets.QApplication.instance()
    _filter = LanguageFilter(app)
    app.installEventFilter(_filter)
    tooltip.HoudiniToolTip.display = display
    _installed = True
