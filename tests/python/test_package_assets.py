import json
import pathlib
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import generate_package_json as package_generator


class PackageAssetTests(unittest.TestCase):
    def test_package_preloads_only_the_display_hook(self):
        package = json.loads(
            (ROOT / "package" / "HoudiniChinese.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(package.get("enable"), "houdini_version == '22.0'")
        self.assertIs(package.get("load_package_once"), True)
        self.assertEqual(
            package.get("hpath"), "$HOUDINI_PACKAGE_PATH/HoudiniChinese"
        )
        self.assertEqual(
            package.get("preload_libraries"),
            [
                "$HOUDINI_PACKAGE_PATH/HoudiniChinese/"
                "HoudiniChineseHook.dll"
            ],
        )

    def test_package_template_is_release_scoped(self):
        template = (ROOT / "package" / "HoudiniChinese.template.json").read_text(
            encoding="utf-8"
        )
        self.assertEqual(template.count("@HOUDINI_VERSION@"), 1)
        self.assertIn("houdini_version == '@HOUDINI_VERSION@'", template)
        generated = json.loads(template.replace("@HOUDINI_VERSION@", "21.0"))
        self.assertEqual(generated["enable"], "houdini_version == '21.0'")

    def test_package_generator_writes_complete_scoped_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            output = pathlib.Path(folder) / "nested" / "HoudiniChinese.json"
            package_generator.generate(
                ROOT / "package" / "HoudiniChinese.template.json",
                output,
                "20.5",
            )
            generated = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(generated["enable"], "houdini_version == '20.5'")
            self.assertFalse(output.with_suffix(".json.tmp").exists())

    def test_package_generator_accepts_future_release_names(self):
        with tempfile.TemporaryDirectory() as folder:
            output = pathlib.Path(folder) / "HoudiniChinese.json"
            package_generator.generate(
                ROOT / "package" / "HoudiniChinese.template.json",
                output,
                "23.0",
            )
            generated = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(generated["enable"], "houdini_version == '23.0'")

    def test_package_generator_rejects_malformed_release_names(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                package_generator.generate(
                    ROOT / "package" / "HoudiniChinese.template.json",
                    pathlib.Path(folder) / "HoudiniChinese.json",
                    "23",
                )

    def test_package_generator_keeps_existing_manifest_on_invalid_template(self):
        with tempfile.TemporaryDirectory() as folder:
            template = pathlib.Path(folder) / "invalid.json"
            output = pathlib.Path(folder) / "HoudiniChinese.json"
            output.write_text('{"previous": true}', encoding="utf-8")
            template.write_text('{"enable": "@HOUDINI_VERSION@",}', encoding="utf-8")
            with self.assertRaises(ValueError):
                package_generator.generate(template, output, "22.0")
            self.assertEqual(json.loads(output.read_text()), {"previous": True})
            self.assertEqual(list(pathlib.Path(folder).glob("*.tmp")), [])

    def test_legacy_split_config_files_are_not_packaged(self):
        plugin = ROOT / "package" / "HoudiniChinese"
        self.assertFalse((plugin / "hotkeys.json").exists())
        self.assertFalse((plugin / "capture.enabled").exists())

    def test_native_build_uses_the_dynamic_msvc_runtime(self):
        build_script = (ROOT / "tools" / "build.bat").read_text(
            encoding="utf-8"
        )
        compile_lines = [
            line.strip()
            for line in build_script.splitlines()
            if line.lstrip().startswith("cl /nologo")
        ]
        self.assertEqual(len(compile_lines), 3)
        # The independent bootstrap uses its own CRT and shares only plain data.
        self.assertEqual(sum(" /MT " in f" {line} " for line in compile_lines), 1)
        for line in (line for line in compile_lines if " /MT " not in f" {line} "):
            self.assertIn(" /MD ", f" {line} ")
            self.assertNotIn(" /MT ", f" {line} ")
        self.assertEqual(
            build_script.count('/DEFAULTLIB:MSVCRT'),
            2,
        )
        analyze_script = (ROOT / "tests" / "analyze.bat").read_text(
            encoding="utf-8"
        )
        self.assertIn("cl /nologo /MD /analyze", analyze_script)

    def test_menu_does_not_create_empty_submenu_placeholders(self):
        root = ET.parse(
            ROOT / "package" / "HoudiniChinese" / "MainMenuCommon.xml"
        ).getroot()
        self.assertEqual(root.findall("./menuBar/subMenu"), [])

    def test_node_graph_canvas_text_hooks_are_packaged(self):
        source = (ROOT / "source" / "hook.cpp").read_text(encoding="utf-8")
        required_symbols = (
            "?text@CV_PaintBuffer@@",
            "?textInBox@CV_PaintBuffer@@",
            "?textWrappedInBox@CV_PaintBuffer@@",
            "?floatTextMultiLine@CV_PaintBuffer@@",
            "?floatTextWrappedInBox@CV_PaintBuffer@@",
        )
        for symbol in required_symbols:
            self.assertIn(symbol, source)
        shared_start = source.index("void installRuntimeHooks()")
        startup_start = source.index("void runOnGuiThread(", shared_start)
        shared_installer = source[shared_start:startup_start]
        self.assertIn("HC_INSTALL(installCVPaintBufferTextHooks);", shared_installer)
        deferred_start = source.index("QObject::connect(timer, &QTimer::timeout", startup_start)
        startup = source[startup_start:deferred_start]
        deferred = source[deferred_start:source.index("timer->start();", deferred_start)]
        self.assertIn("installRuntimeHooks();", startup)
        self.assertIn("installRuntimeHooks();", deferred)
        self.assertNotIn("__HC_CV_", source)
        self.assertNotIn("__HC_RV_", source)

    def test_stable_display_hook_surface_remains_covered(self):
        source = (ROOT / "source" / "hook.cpp").read_text(encoding="utf-8")
        # This is the user-validated stable-2026-09-03-tooltip-refresh surface.
        # Implementations may move from IAT/vtable patches to inline entry
        # hooks, but none of these visible-text paths may disappear.
        stable_paths = (
            "hookedDrawPoint",
            "hookedDrawPointF",
            "hookedDrawPointF2",
            "hookedDrawRect",
            "hookedDrawRectFOption",
            "hookedDrawRectFAlign",
            "hookedDrawXY",
            "hookedDrawXYWH",
            "hookedStyleDrawItemText",
            "hookedQTextLayoutDraw",
            "hookedGraphicsSimpleTextPaint",
            "hookedGraphicsTextPaint",
            "hookedDrawStaticText",
            "hookedRVAddText",
            "hookedRVAddText2",
            "hookedRVRender",
            "hookedUIHelpFeelGetText",
            "hookedQSGInlineAddTextLayout",
            "hookedQTextLayoutSetText",
            "hookedUITextPaint",
            "hookedUITableTextPaint",
            "hookedUITableIconTextPaint",
            "readMenuRowSpace",
        )
        for path in stable_paths:
            self.assertIn(path, source)

        # Native layout is read-only; Qt owns button interaction.
        self.assertNotIn("NativeMenuPressState", source)
        self.assertNotIn("hookedUIMenuBarItemHandleEvent", source)
        self.assertNotIn("g_menuEnable", source)
        buttons = (ROOT / "source" / "ui" / "menu_buttons.h").read_text(encoding="utf-8")
        self.assertIn("QToolButton::clicked", buttons)
        self.assertIn("Qt::WA_NativeWindow", buttons)

        # Keep the stable table and exact UI_Text entry points, including their
        # dispatch fallback when an inline trampoline cannot be allocated.
        self.assertIn("UI_TableListEntryBase@VUI_Text", source)
        self.assertIn("UI_TableListEntryBase@VUI_IconText", source)
        self.assertIn("Keep the stable baseline's complete UI_Text", source)

    def test_one_shot_capture_and_qt_button_actions_are_packaged(self):
        source = (ROOT / "source" / "hook.cpp").read_text(encoding="utf-8")
        config = (ROOT / "source" / "ui" / "plugin_config.h").read_text(
            encoding="utf-8"
        )
        capture = (ROOT / "source" / "capture" / "untranslated_capture.h").read_text(
            encoding="utf-8"
        )
        for marker in (
            "isOneShotCaptureShortcut",
            "captureUntranslatedOnce",
            "collectKnownWidgetTrees",
            "expandAll()",
            "showPopup()",
            "QAccessible::queryAccessibleInterface",
            "QGuiApplication::focusWindow",
            "g_knownQtWindows",
            "captureTextVariant",
            "nativeMessage->wParam == VK_OEM_3",
            "QGuiApplication::allWindows()",
            "QQuickWindow",
            "contentItem()",
            "childItems()",
            "HoudiniChineseCapture::beginOneShot",
            "HoudiniChineseCapture::endOneShot",
        ):
            self.assertIn(marker, source)
        self.assertNotIn(
            "for (QWidget *widget : QApplication::allWidgets())", source
        )
        self.assertNotIn("QApplication::topLevelWidgets()", source)
        self.assertIn("captureNativeSITreeHierarchies", source)
        self.assertIn("getNumChildNodes@SI_TreeNode", source)
        self.assertIn("getFullDescription@SI_TreeNode", source)
        tree_render_hook = source[
            source.index("void hookedSITreeDoRenderNode") :
            source.index("bool installSITreeTraversalHook")
        ]
        self.assertIn("ScopedAllowHoudiniUiText allowTreeRowText", tree_render_hook)
        self.assertLess(
            tree_render_hook.index("ScopedAllowHoudiniUiText allowTreeRowText"),
            tree_render_hook.index("g_siTreeDoRenderNode("),
        )
        self.assertIn("captureNativeUIFeelHierarchies", source)
        self.assertIn("getNumChildren@UI_Feel", source)
        self.assertIn("getMenuItemList@UI_DynamicMenuStrip", source)
        self.assertIn("getMenu@UI_NestedStringMenu", source)
        self.assertIn("getNumEntries@UI_StringChooserQuery", source)
        self.assertIn("getEntry@UI_StringChooserQuery", source)
        self.assertIn("?draw@UI_Look@@", source)
        self.assertIn(".?AVUI_Text@@", source)
        self.assertIn(".?AVUI_LabeledFeel@@", source)
        self.assertIn(".?AVUI_TriggerButton@@", source)
        self.assertIn("getText@UI_FormattedText", source)
        self.assertIn("getTitle@UI_HelpTipLook", source)
        self.assertIn("?render@UI_HelpTipLook@@", source)
        self.assertIn("installUIHelpTipLookRenderHook", source)
        self.assertIn("?openTooltipWindow@BR_HelpManager@@", source)
        self.assertIn("installBRHelpTooltipHook", source)
        self.assertIn("?paintText@UI_Text@@", source)
        self.assertIn("?renderText@UI_Text@@QEAAXPEAVRE_Render@@PEBD1", source)
        self.assertIn("?getStringWidth@UI_Text@@", source)
        self.assertIn("installUITextRangeHooks", source)
        base_text_hooks = source[
            source.index("UILookRenderFn g_uiTextRender") :
            source.index("bool installUITextPaintHook")
        ]
        self.assertIn("ScopedAllowHoudiniUiText guard", base_text_hooks)
        self.assertNotIn("if (!shouldAllowNativeLookText(self))", base_text_hooks)
        self.assertIn("?putString@RE_RenderUI@@", source)
        self.assertIn("?getStringWidth@RE_Font@@", source)
        self.assertIn("installRETextHooks", source)
        self.assertIn("getLabel@UI_CollapseButtonLook", source)
        self.assertIn("?doAddTextLayout@QSGInternalTextNode@@", source)
        self.assertIn("?doAddTextDocument@QSGInternalTextNode@@", source)
        self.assertIn("?setHtml@QTextDocument@@", source)
        self.assertIn("?setPlainText@QTextDocument@@", source)
        self.assertIn("isQuickTextUpdateLayoutCall", source)
        self.assertIn("translateQuickRichText", source)
        self.assertIn("isQuickTextObject(self->parent())", source)
        self.assertIn("?advanceWidth@QQuickFontMetrics@@", source)
        self.assertIn("?boundingRect@QQuickFontMetrics@@", source)
        self.assertIn("?tightBoundingRect@QQuickFontMetrics@@", source)
        self.assertIn("?elidedText@QQuickFontMetrics@@", source)
        self.assertIn("?setImplicitWidth@QQuickItem@@", source)
        self.assertIn("?setImplicitSize@QQuickItem@@", source)
        self.assertIn("resolveNativeFunctionRange", source)
        self.assertIn("installQtQuickTextOwnerHooks", source)
        self.assertIn("?updateLayout@QQuickTextPrivate@@", source)
        self.assertIn("?updatePaintNode@QQuickText@@", source)
        self.assertIn("ScopedQuickDisplayText", source)
        self.assertNotIn("g_quickTextUpdatePaintRange", source)
        self.assertIn("waitForNativeCaptureWriters", source)
        self.assertNotIn("g_quickUpdateLayoutEnd", source)
        self.assertNotIn("g_quickLayoutInputHooksAttempted", source)
        self.assertIn('QMetaObject::invokeMethod(current, "forceLayout"', source)
        self.assertIn('QStringLiteral("打开目录")', source)
        self.assertIn("QDesktopServices::openUrl", source)
        self.assertIn("QUrl::fromLocalFile(outputDirectory)", source)
        self.assertIn("Qt::Key_QuoteLeft", config)
        self.assertIn("Qt::Key_AsciiTilde", config)
        self.assertIn("disableSubmissions()", capture)
        self.assertNotIn("timer->setInterval(2000)", capture)
        self.assertIn("readMenuRowSpace", source)
        self.assertIn("installMenuButtons", source)
        self.assertNotIn("?paint@UI_MenuBarItemLook@@", source)
        self.assertNotIn("?handleEvent@UI_MenuBarItem@@", source)
        self.assertNotIn("??_7UI_MenuBarItem@@6BUI_Object@@@", source)
        self.assertNotIn("?enable@UI_Feel@@", source)
        self.assertIn("?isVisible@UI_Feel@@", source)
        self.assertNotIn("MenuOverlay", source)
        self.assertNotIn("HoudiniChineseMenuDiagnostics", source)
        self.assertIn("?getDevW@UI_Feel@@", source)
        self.assertIn("?getDevH@UI_Feel@@", source)
        self.assertNotIn("?mapFromFeelToScreen@UI_Feel@@", source)
        self.assertNotIn("nativeMenuActionAtPoint", source)
        self.assertNotIn("WM_LBUTTON", source)
        self.assertNotIn("WM_RBUTTON", source)
        self.assertNotIn("WM_SETCURSOR", source)
        display_start = source.index("QString displayTranslation(")
        display_end = source.index("QString displayQuickLabelTranslation(")
        display_translation = source[display_start:display_end]
        self.assertIn(
            "if (!translationEnabled && !captureEnabled)",
            display_translation,
        )
        self.assertIn("finishOneShotCapture() noexcept", source)
        self.assertIn("ScopedNativeCollection", source)
        refresh_filter = source[
            source.index("class RefreshEventFilter") :
            source.index("BOOL CALLBACK ForceRepaintWindow")
        ]
        self.assertNotIn("g_nativeMenuPress", refresh_filter)
        self.assertNotIn("handleNativeMenuAction", source)
        self.assertNotIn("isRightButtonEvent", source)
        self.assertNotIn("g_languageMenuHovered", source)
        self.assertNotIn("g_authorMenuHovered", source)
        self.assertNotIn("g_uiMenuBarItemBeginLocate", source)
        self.assertNotIn("nativeMenuActionStillHit", source)

    def test_virtual_text_painters_are_not_iat_only(self):
        source = (ROOT / "source" / "hook.cpp").read_text(encoding="utf-8")
        native_start = source.index("void installNativeDirectTextLookHooks()")
        native_end = source.index("// QGraphics text items", native_start)
        native_installer = source[native_start:native_end]
        self.assertIn("resolveAndAttachNativeVirtualHook", native_installer)
        self.assertNotIn("replaceImportInModule", native_installer)
        self.assertNotIn("resolveAndPatch", native_installer)
        fallback = source[
            source.index("bool resolveAndAttachNativeVirtualHook(") :
            source.index("bool findNativeVtableSlot(")
        ]
        self.assertIn("resolveAndAttachInlineHook", fallback)
        self.assertIn("HoudiniChineseDispatch::patchVtable", fallback)
        self.assertIn("g_nativeDispatchImports.push_back", fallback)

        graphics_start = source.index("bool installNodeTextHooks()")
        graphics_end = source.index("// Hook QPainter::drawStaticText", graphics_start)
        graphics_installer = source[graphics_start:graphics_end]
        self.assertIn("resolveAndAttachImportedHook", graphics_installer)
        self.assertNotIn("replaceImportInModule", graphics_installer)

        import_start = source.index("void patchKnownImportsInModule(HMODULE module)")
        import_end = source.index("void rememberCurrentImportModules()", import_start)
        import_patcher = source[import_start:import_end]
        self.assertIn("g_nativeDispatchImports", import_patcher)
        for trampoline in (
            "g_uiTextRender",
            "g_uiLabelPaint",
            "g_uiFormattedTextRender",
            "g_graphicsSimpleTextPaint",
            "g_graphicsTextPaint",
            "g_rvAddText",
            "g_rvAddText2",
            "g_rvRender",
            "g_cvText",
            "g_cvTextInBox",
            "g_cvTextWrappedInBox",
            "g_cvFloatTextMultiLine",
            "g_cvFloatTextWrappedInBox",
        ):
            self.assertNotIn(trampoline, import_patcher)

        for installer_name, end_marker in (
            ("void installNodeRenderTextHooks()", "// Houdini's node graph"),
            ("void installCVPaintBufferTextHooks()", "size_t g_fontHookSlotCount"),
        ):
            installer_start = source.index(installer_name)
            installer_end = source.index(end_marker, installer_start)
            installer = source[installer_start:installer_end]
            self.assertIn("resolveAndAttachImportedHook", installer)
            self.assertNotIn("replaceImportInModule", installer)


if __name__ == "__main__":
    unittest.main()
