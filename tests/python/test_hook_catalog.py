"""Keep the declarative catalog in sync with actual installed hook handlers.

These checks validate entry coverage and ABI spelling, not runtime call coverage.
No DLL is loaded and no Houdini process is started by this test.
"""

import os
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
STRATEGIES = {"iat", "inline", "inline+iat", "inline+vtable+iat", "style-vtable"}
ROLES = {"draw", "measure", "context", "capture", "layout"}


def normalized_cpp(source):
    """Remove comments while retaining literals, then join wrapped ABI strings."""
    source = re.sub(
        r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/',
        lambda match: " " if match.group().startswith(("//", "/*")) else match.group(),
        source,
        flags=re.S,
    )
    return re.sub(r'"\s*"', "", source)


def implemented_handlers(source):
    handlers = set(re.findall(r'\b(hooked\w+)\s*\([^;{}]*\)\s*\{', source))
    for kind in ("PAINT", "RENDER"):
        for name in re.findall(r'HC_DECLARE_NATIVE_TEXT_' + kind + r'_HOOK\((\w+)\)', source):
            if name != "Name":
                handlers.add(f"hookedUI{name}{kind.title()}")
    return handlers


def bound_entries(source):
    """Read concrete installer bindings rather than all GetProcAddress helpers."""
    result = {}

    def put(handler, symbol, strategy):
        entry = (symbol, strategy)
        if handler in result and result[handler] != entry:
            raise AssertionError(f"Conflicting installer binding for {handler}")
        result[handler] = entry

    strategies = {"Inline": "inline", "Imported": "inline+iat", "NativeVirtual": "inline+vtable+iat"}
    for match in re.finditer(
        r'resolveAndAttach(Inline|Imported|NativeVirtual)Hook\(\s*\w+\s*,\s*"([^"]+)"([^;]*?)\);',
        source,
    ):
        handler = re.search(r'\b(hooked\w+)\s*$', match[3].rstrip())
        if handler:
            put(handler[1], match[2], strategies[match[1]])

    for symbol, handler in re.findall(r'hookOne\(\s*"([^"]+)"\s*,\s*\w+\s*,\s*&?(hooked\w+)\s*\)', source):
        put(handler, symbol, "iat")

    variables = dict(re.findall(
        r'\b(g_\w+)\s*=\s*[^;]*?GetProcAddress\(\s*\w+\s*,\s*"([^"]+)"', source))
    constants = dict(re.findall(r'constexpr const char \*(\w+)\s*=\s*"([^"]+)"', source))
    for variable, constant in re.findall(
        r'\b(g_\w+)\s*=\s*[^;]*?GetProcAddress\(\s*\w+\s*,\s*(\w+)\s*\)', source):
        if constant in constants:
            variables[variable] = constants[constant]
    for variable, handler in re.findall(r'attachInlineHook\(\s*(g_\w+)\s*,\s*&?(hooked\w+)\s*(?:,\s*(?:lateAttempt|true|false)\s*)?\)', source):
        put(handler, variables[variable], "inline")

    for variable, handler in re.findall(
        r'reinterpret_cast<void \*>\((g_\w+)\)\s*[,}]\s*reinterpret_cast<void \*>\(&?(hooked\w+)\)',
        source,
    ):
        if variable in variables:
            put(handler, variables[variable], "iat")

    # The current QStyle's virtual implementation is discovered using the base
    # export as an ABI-slot reference; it is not an import replacement.
    style = re.search(r'GetProcAddress\(\s*qtWidgets\s*,\s*"(\?drawItemText@QStyle[^"]+)"', source)
    if style:
        put("hookedStyleDrawItemText", style[1], "style-vtable")

    for kind in ("PAINT", "RENDER"):
        for name, symbol in re.findall(
            r'HC_ATTACH_NATIVE_TEXT_' + kind + r'\(\s*(\w+)\s*,\s*"([^"]+)"\s*\)', source
        ):
            put(f"hookedUI{name}{kind.title()}", symbol, "inline+vtable+iat")
    # Opt-in entry-detour experiment augments these existing IAT bindings.
    experimental = set(re.findall(
        r'tryExperimentalQtInline\(\s*g_\w+\s*,\s*&?(hooked\w+)\s*\)', source))
    if 'tryExperimentalQtInline(original, replacement)' in source:
        experimental.update(handler for _, handler in re.findall(
            r'hookOne\(\s*"([^"]+)"\s*,\s*\w+\s*,\s*&?(hooked\w+)\s*\)', source))
    for handler in experimental:
        symbol, strategy = result[handler]
        assert strategy == 'iat', (handler, strategy)
        result[handler] = (symbol, 'inline+iat')
    return result


def catalog_entries():
    source = normalized_cpp((ROOT / "source/hooks/hook_registry.h").read_text(encoding="utf-8"))
    pattern = (
        r'\bX\(\s*(hooked\w+)\s*,\s*L"([^"]+)"\s*,\s*'
        r'"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*'
        r'"([^"]+)"\s*,\s*"([^"]+)"\s*\)'
    )
    return re.findall(pattern, source.replace("\\\n", ""))


def bound_modules(source):
    """Resolve module handles from their declarations; retain source bindings."""
    modules = dict(re.findall(r'HMODULE\s+(\w+)\s*=\s*GetModuleHandleW\(L"([^"]+)"\)', source))
    result = {}
    for variable, symbol in re.findall(
        r'(?:GetProcAddress|resolveAndAttach(?:Inline|Imported|NativeVirtual)Hook)'
        r'\(\s*(\w+)\s*,\s*"([^"]+)"', source,
    ):
        if variable in modules:
            result[symbol] = modules[variable]
    for symbol in re.findall(r'hookOne\(\s*"([^"]+)"', source):
        result[symbol] = modules["qtGui"]
    for symbol in re.findall(r'HC_ATTACH_NATIVE_TEXT_(?:PAINT|RENDER)\(\s*\w+\s*,\s*"([^"]+)"', source):
        result[symbol] = modules["ui"]
    constants = dict(re.findall(r'constexpr const char \*(\w+)\s*=\s*"([^"]+)"', source))
    for variable, constant in re.findall(r'GetProcAddress\(\s*(\w+)\s*,\s*(\w+)\s*\)', source):
        if variable in modules and constant in constants:
            result[constants[constant]] = modules[variable]
    return result


class HookCatalogTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = normalized_cpp((ROOT / "source/hook.cpp").read_text(encoding="utf-8"))
        cls.entries = catalog_entries()

    def test_ids_targets_and_metadata_are_valid_and_unique(self):
        self.assertTrue(self.entries, "The Hook catalog must not be empty")
        ids = [entry[0] for entry in self.entries]
        targets = [(entry[1], entry[2]) for entry in self.entries]
        self.assertEqual(len(ids), len(set(ids)), "Duplicate HookId")
        self.assertEqual(len(targets), len(set(targets)), "Duplicate module/export target")
        for handler, module, symbol, feature, role, strategy, scenario in self.entries:
            with self.subTest(handler=handler):
                self.assertTrue(module.endswith(".dll"))
                self.assertTrue(symbol.startswith("?"))
                self.assertIn(role, ROLES)
                self.assertIn(strategy, STRATEGIES)
                self.assertRegex(feature, r"^[a-z][a-z0-9-]*$")
                self.assertTrue(scenario.strip())

    def test_every_actual_handler_is_cataloged_without_accessor_entries(self):
        self.assertSetEqual(
            {entry[0] for entry in self.entries}, implemented_handlers(self.source),
            "Added/deleted drawing handlers and expanded native macros must update the catalog",
        )

    def test_every_handler_observes_its_own_scope_before_early_returns(self):
        observed = set()
        for match in re.finditer(r'\b(hooked\w+)\s*\([^;{}]*\)\s*\{', self.source):
            handler = match[1]
            with self.subTest(handler=handler):
                first_statement = re.match(
                    r'\s*HC_OBSERVE\(\s*' + re.escape(handler) + r'\s*\)\s*;',
                    self.source[match.end():],
                )
                self.assertIsNotNone(
                    first_statement,
                    "The handler must observe its own HookId before guards or forwarding",
                )
                observed.add(handler)

        # Check the generating body itself, then expand every concrete macro
        # invocation. A counter only in ordinary C++ handlers would miss these.
        flattened = self.source.replace("\\\n", "")
        for kind in ("PAINT", "RENDER"):
            macro = "HC_DECLARE_NATIVE_TEXT_" + kind + "_HOOK"
            definition = re.search(r'#define\s+' + macro + r'\(Name\)([^\n]*)', flattened)
            self.assertIsNotNone(definition, f"Missing native handler macro {macro}")
            token = "hookedUI##Name##" + kind.title()
            self.assertRegex(
                definition[1],
                r'void\s+' + re.escape(token) + r'\s*\([^;{}]*\)\s*\{\s*'
                r'HC_OBSERVE\(\s*' + re.escape(token) + r'\s*\)\s*;',
                "Generated native hooks must start with an observation of the generated ID",
            )
            for name in re.findall(macro + r'\((\w+)\)', self.source):
                if name != "Name":
                    observed.add(f"hookedUI{name}{kind.title()}")

        self.assertSetEqual(observed, {entry[0] for entry in self.entries})

    def test_catalog_matches_installed_symbols_and_declared_fallbacks(self):
        bindings = bound_entries(self.source)
        modules = bound_modules(self.source)
        self.assertSetEqual(set(bindings), implemented_handlers(self.source),
                            "Update the installer parser for new hook installation forms")
        for handler, module, symbol, feature, role, strategy, scenario in self.entries:
            with self.subTest(handler=handler):
                self.assertEqual((symbol, strategy), bindings[handler])
                self.assertEqual(module, modules[symbol])

    def test_catalog_exports_exist_in_the_verified_houdini_build(self):
        from test_native_text_exports import exported_names

        selected = os.environ.get("HFS")
        installation = Path(selected) if selected else (
            Path(os.environ.get("ProgramFiles", "C:/Program Files"))
            / "Side Effects Software" / "Houdini 22.0.368"
        )
        version_header = installation / "toolkit/include/SYS/SYS_Version.h"
        if not version_header.is_file():
            self.skipTest("Verified Houdini 22.0.368 HDK absent; export compatibility is unverified")
        version = re.search(r'#define SYS_VERSION_FULL "([^"]+)"',
                            version_header.read_text(encoding="utf-8"))
        if not version or version[1] != "22.0.368":
            self.skipTest("This catalog is verified only for Houdini 22.0.368; selected build needs its own baseline")
        exports = {}
        for handler, module, symbol, feature, role, strategy, scenario in self.entries:
            with self.subTest(handler=handler, module=module, build="22.0.368"):
                library = installation / "bin" / module
                self.assertTrue(library.is_file(), f"Required target module missing: {library}")
                if module not in exports:
                    exports[module] = exported_names(library)
                self.assertIn(symbol, exports[module])

    def test_recent_regressions_keep_distinct_draw_measurement_and_context_entries(self):
        entries = {entry[0]: entry for entry in self.entries}
        required = {
            "hookedRERenderPutCString": "draw",
            "hookedRERenderPutRange": "draw",
            "hookedREFontStringRefWidth": "measure",
            "hookedUITextPaint": "context",
            "hookedSITreeDoRenderNode": "context",
            "hookedUIFeelRenderMe": "capture",
            "hookedQuickItemSetImplicitWidth": "measure",
            "hookedQQuickTextUpdateLayout": "context",
            "hookedQTextDocumentSetHtml": "layout",
            "hookedBRHelpOpenTooltipWindow": "draw",
        }
        for handler, role in required.items():
            with self.subTest(handler=handler):
                self.assertEqual(entries[handler][4], role)
        for prefix, feature in (("hookedRV", "native-rv"), ("hookedCV", "native-cv")):
            actual = {name for name in implemented_handlers(self.source) if name.startswith(prefix)}
            self.assertTrue(actual)
            self.assertSetEqual(actual, {row[0] for row in self.entries if row[3] == feature})


if __name__ == "__main__":
    unittest.main()
