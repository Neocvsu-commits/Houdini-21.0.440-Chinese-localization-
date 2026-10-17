#pragma once

// ---------------------------------------------------------------------------
// hook catalog
// ---------------------------------------------------------------------------
#include <cstddef>
#include "native_abi_symbols.h"

// Declarative inventory for the verified Houdini 22.0.368 / Qt 6.8.3 ABI.
// Strategy lists permitted installation paths, not observed runtime coverage.
// Keep overloads and owner scopes distinct: IAT fallbacks do not cover internal
// direct/virtual calls. Ordinary native accessors are deliberately not hooks.
// X(Id, Module, Symbol, Feature, Role, Strategy, Scenario)
#define HC_HOOK_CATALOG(X) \
    X(hookedWrappedPaint21, L"libUI.dll", \
      "?paintWrappedText@UI_Text@@AEBAXAEAVQStylePainter@@AEBVQRect@@AEBVUT_StringView@@AEBVQPalette@@AEBVQFontMetricsF@@@Z", \
      "native-wrap21", "draw", "inline", "shelf-caption-before-wrapping") \
    X(hookedWrappedRender21, L"libUI.dll", \
      "?renderWrappedText@UI_Text@@AEAAXPEAVRE_Render@@AEBVUT_StringView@@HHHH@Z", \
      "native-wrap21", "draw", "inline", "shelf-caption-before-wrapping") \
    X(hookedMOTTooltip21, L"libMOT.dll", \
      "?getTooltip@MOT_HelpManager@@UEAA_NPEBDAEAVUT_String@@@Z", \
      "native-help21", "display-copy", "inline", "shelf-summary-before-line-wrap") \
    X(hookedMOTParmTooltip21, L"libMOT.dll", \
      "?getTooltip@MOT_HelpManager@@UEAA_NPEBVPRM_Parm@@AEAVUT_String@@@Z", \
      "native-help21", "display-copy", "inline", "parameter-help-before-line-wrap") \
    X(hookedMOTParsedTooltip21, L"libMOT.dll", \
      "?getParsedTooltip@MOT_HelpManager@@UEAA_NPEBD0AEAVUT_String@@@Z", \
      "native-help21", "display-copy", "inline", "parsed-help-before-line-wrap") \
    X(hookedBRHelpOpenTooltipWindow, L"libBR.dll", \
      "?openTooltipWindow@BR_HelpManager@@UEBAXPEBD0HHHH000_N@Z", \
      "browser-help", "draw", "inline", "shelf-tool-browser-help-card") \
    X(hookedCVFloatTextMultiLine, L"libCV.dll", \
      "?floatTextMultiLine@CV_PaintBuffer@@QEAAXNNNW4CV_VerticalAlignment@@PEBD1NPEAVUT_Color@@PEANPEAV?$UT_BoundingRectT@N@@@Z", \
      "native-cv", "draw", "inline+iat", "expanded-node-title-and-port-labels") \
    X(hookedCVFloatTextWrappedInBox, L"libCV.dll", \
      "?floatTextWrappedInBox@CV_PaintBuffer@@QEAAXAEBV?$UT_BoundingRectT@N@@W4CV_VerticalAlignment@@PEBD2NPEAVUT_Color@@PEANPEAV2@@Z", \
      "native-cv", "draw", "inline+iat", "expanded-node-title-and-port-labels") \
    X(hookedCVText, L"libCV.dll", \
      "?text@CV_PaintBuffer@@QEAAXHH_NPEBDPEAVUT_Color@@PEANPEAV?$UT_Rect@VUT_DimRectImpl@@@@@Z", \
      "native-cv", "draw", "inline+iat", "expanded-node-title-and-port-labels") \
    X(hookedCVTextInBox, L"libCV.dll", \
      "?textInBox@CV_PaintBuffer@@QEAAXAEBV?$UT_Rect@VUT_DimRectImpl@@@@_N1PEBD1HHPEAVUT_Color@@PEANPEAV2@@Z", \
      "native-cv", "draw", "inline+iat", "expanded-node-title-and-port-labels") \
    X(hookedCVTextWrappedInBox, L"libCV.dll", \
      "?textWrappedInBox@CV_PaintBuffer@@QEAAXAEBV?$UT_Rect@VUT_DimRectImpl@@@@W4CV_HorizontalAlignment@@W4CV_VerticalAlignment@@PEBDPEAVUT_Color@@PEANPEAV2@@Z", \
      "native-cv", "draw", "inline+iat", "expanded-node-title-and-port-labels") \
    X(hookedUIHelpTipLookRender, L"libUI.dll", \
      "?render@UI_HelpTipLook@@UEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-help", "context", "inline", "native-tooltip-title-body-footer") \
    X(hookedUIHelpTipGetTitle, L"libUI.dll", \
      "?getTitle@UI_HelpTipLook@@QEBAPEBDXZ", \
      "native-help", "context", "inline", "native-tooltip-title-body-footer") \
    X(hookedUIHelpFeelGetText, L"libUI.dll", \
      HC_UI_HELP_SYMBOL, \
      "native-help", "draw", "inline", "native-parameter-help-text") \
    X(hookedRERenderPutCString, L"libRE.dll", \
      "?putString@RE_RenderUI@@QEAAXPEBD@Z", \
      "native-re", "draw", "inline+iat", "viewport-persp-no-cam-and-native-text") \
    X(hookedRERenderPutRange, L"libRE.dll", \
      "?putString@RE_RenderUI@@QEAAXPEBD0@Z", \
      "native-re", "draw", "inline+iat", "viewport-persp-no-cam-and-native-text") \
    X(hookedRERenderPutString, L"libRE.dll", \
      "?putString@RE_RenderUI@@QEAAXAEAVRE_Font@@PEBD1PEAVtransform@UT_Unicode@@@Z", \
      "native-re", "draw", "inline+iat", "viewport-persp-no-cam-and-native-text") \
    X(hookedREFontStringRefWidth, L"libRE.dll", \
      "?getStringWidth@RE_Font@@QEAAMAEBVUT_StringRef@@PEBVtransform@UT_Unicode@@@Z", \
      "native-re", "measure", "inline+iat", "viewport-persp-no-cam-and-native-text") \
    X(hookedREFontStringWidth, L"libRE.dll", \
      "?getStringWidth@RE_Font@@QEAAMPEBD0PEBVtransform@UT_Unicode@@@Z", \
      "native-re", "measure", "inline+iat", "viewport-persp-no-cam-and-native-text") \
    X(hookedRVAddText, L"libRV.dll", \
      "?addText@RV_TextRender@@QEAAXPEBDAEAVRE_Font@@MMMAEBVUT_Color@@M@Z", \
      "native-rv", "draw", "inline+iat", "network-node-title-and-overlay-text") \
    X(hookedRVAddText2, L"libRV.dll", \
      "?addText@RV_TextRender@@QEAAXPEBDAEAVRE_Font@@MMAEBVUT_Color@@M@Z", \
      "native-rv", "draw", "inline+iat", "network-node-title-and-overlay-text") \
    X(hookedRVRender, L"libRV.dll", \
      "?renderText@RV_TextRender@@SAXPEAVRV_Render@@AEAVRE_Font@@PEBDMMMAEBVUT_Color@@MAEBV?$UT_Rect@VUT_DimRectImpl@@@@@Z", \
      "native-rv", "draw", "inline+iat", "network-node-title-and-overlay-text") \
    X(hookedSITreeDoRenderNode, L"libSI.dll", \
      HC_TREE_RENDER_SYMBOL, \
      "native-tree", "context", "inline", "material-gallery-and-tree-row-capture") \
    X(hookedUIFeelRenderMe, L"libUI.dll", \
      "?renderMe@UI_Feel@@QEAAXPEAVRE_Render@@HH@Z", \
      "native-ui", "capture", "inline", "visible-native-control-tree-capture") \
    X(hookedUICollapseButtonLookRender, L"libUI.dll", \
      "?render@UI_CollapseButtonLook@@UEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUIColorMenuItemLookRender, L"libUI.dll", \
      "?render@UI_ColorMenuItemLook@@UEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUIFolderTextPaint, L"libUI.dll", \
      "?paint@UI_FolderText@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUIFolderTextRender, L"libUI.dll", \
      "?render@UI_FolderText@@UEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUIFormattedTextPaint, L"libUI.dll", \
      "?paint@UI_FormattedText@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUIFormattedTextRender, L"libUI.dll", \
      "?render@UI_FormattedText@@UEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUIIconTextPaint, L"libUI.dll", \
      "?paint@UI_IconText@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUIIconTextRender, L"libUI.dll", \
      "?render@UI_IconText@@UEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUILabelPaint, L"libUI.dll", \
      "?paint@UI_Label@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUILookDraw, L"libUI.dll", \
      "?draw@UI_Look@@QEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-ui", "context", "inline", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUIMenuButtonLookPaint, L"libUI.dll", \
      "?paint@UI_MenuButtonLook@@MEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUIMenuButtonLookRender, L"libUI.dll", \
      "?render@UI_MenuButtonLook@@UEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUIOverlapRender, L"libUI.dll", \
      "?render@UI_Overlap@@UEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUITableIconTextPaint, L"libUI.dll", \
      "?paint@?$UI_TableListEntryBase@VUI_IconText@@@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUITableListHeaderPaint, L"libUI.dll", \
      "?paint@UI_TableListHeader@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUITableListHeaderRender, L"libUI.dll", \
      "?render@UI_TableListHeader@@UEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUITableTextPaint, L"libUI.dll", \
      "?paint@?$UI_TableListEntryBase@VUI_Text@@@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUITextPaint, L"libUI.dll", \
      "?paint@UI_Text@@UEAAXAEAVQStylePainter@@AEBVQRect@@PEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUITextRender, L"libUI.dll", \
      "?render@UI_Text@@UEAAXPEAVRE_Render@@HHHHPEAX@Z", \
      "native-ui", "context", "inline+vtable+iat", "native-menus-labels-material-list-and-editor-protection") \
    X(hookedUITextPaintRange, L"libUI.dll", \
      "?paintText@UI_Text@@QEBAXAEAVQStylePainter@@AEBVQRect@@PEBD2@Z", \
      "native-ui", "draw", "inline+iat", "native-label-text-range-and-width") \
    X(hookedUITextRenderRange, L"libUI.dll", \
      "?renderText@UI_Text@@QEAAXPEAVRE_Render@@PEBD1HHHH@Z", \
      "native-ui", "draw", "inline+iat", "native-label-text-range-and-width") \
    X(hookedUITextStringWidth, L"libUI.dll", \
      HC_UI_WIDTH_SYMBOL, \
      "native-ui", "measure", "inline+iat", "native-label-text-range-and-width") \
    X(hookedGraphicsSimpleTextPaint, L"Qt6Widgets.dll", \
      "?paint@QGraphicsSimpleTextItem@@UEAAXPEAVQPainter@@PEBVQStyleOptionGraphicsItem@@PEAVQWidget@@@Z", \
      "qt-graphics", "draw", "inline+iat", "graphics-scene-text-items") \
    X(hookedGraphicsTextPaint, L"Qt6Widgets.dll", \
      "?paint@QGraphicsTextItem@@UEAAXPEAVQPainter@@PEBVQStyleOptionGraphicsItem@@PEAVQWidget@@@Z", \
      "qt-graphics", "draw", "inline+iat", "graphics-scene-text-items") \
    X(hookedQQuickComboInputUpdatePaintNode, L"Qt6Quick.dll", \
      "?updatePaintNode@QQuickTextInput@@MEAAPEAVQSGNode@@PEAV2@PEAUUpdatePaintNodeData@QQuickItem@@@Z", \
      "qt-quick", "context", "inline", "noneditable-combo-current-value-display-only") \
    X(hookedQQuickTextUpdateLayout, L"Qt6Quick.dll", \
      "?updateLayout@QQuickTextPrivate@@QEAAXXZ", \
      "qt-quick", "context", "inline", "qml-display-label-owner-scope") \
    X(hookedQQuickTextUpdatePaintNode, L"Qt6Quick.dll", \
      "?updatePaintNode@QQuickText@@MEAAPEAVQSGNode@@PEAV2@PEAUUpdatePaintNodeData@QQuickItem@@@Z", \
      "qt-quick", "context", "inline", "qml-display-label-owner-scope") \
    X(hookedQSGInlineAddTextDocument, L"Qt6Quick.dll", \
      "?doAddTextDocument@QSGInternalTextNode@@MEAAXVQPointF@@PEAVQTextDocument@@HH@Z", \
      "qt-quick", "draw", "inline", "qml-scene-graph-text-and-rich-text") \
    X(hookedQSGInlineAddTextLayout, L"Qt6Quick.dll", \
      "?doAddTextLayout@QSGInternalTextNode@@MEAAXVQPointF@@PEAVQTextLayout@@HHHH@Z", \
      "qt-quick", "draw", "inline", "qml-scene-graph-text-and-rich-text") \
    X(hookedQTextDocumentSetHtml, L"Qt6Gui.dll", \
      "?setHtml@QTextDocument@@QEAAXAEBVQString@@@Z", \
      "qt-quick", "layout", "inline", "qml-plain-rich-text-layout-and-toggle") \
    X(hookedQTextDocumentSetPlainText, L"Qt6Gui.dll", \
      "?setPlainText@QTextDocument@@QEAAXAEBVQString@@@Z", \
      "qt-quick", "layout", "inline", "qml-plain-rich-text-layout-and-toggle") \
    X(hookedQTextLayoutSetText, L"Qt6Gui.dll", \
      "?setText@QTextLayout@@QEAAXAEBVQString@@@Z", \
      "qt-quick", "layout", "inline", "qml-plain-rich-text-layout-and-toggle") \
    X(hookedQuickFontAdvance, L"Qt6Quick.dll", \
      "?advanceWidth@QQuickFontMetrics@@QEBANAEBVQString@@@Z", \
      "qt-quick", "measure", "inline", "qml-label-width-elision-and-layout") \
    X(hookedQuickFontBoundingRect, L"Qt6Quick.dll", \
      "?boundingRect@QQuickFontMetrics@@QEBA?AVQRectF@@AEBVQString@@@Z", \
      "qt-quick", "measure", "inline", "qml-label-width-elision-and-layout") \
    X(hookedQuickFontElidedText, L"Qt6Quick.dll", \
      "?elidedText@QQuickFontMetrics@@QEBA?AVQString@@AEBV2@W4TextElideMode@Qt@@NH@Z", \
      "qt-quick", "measure", "inline", "qml-label-width-elision-and-layout") \
    X(hookedQuickFontTightBoundingRect, L"Qt6Quick.dll", \
      "?tightBoundingRect@QQuickFontMetrics@@QEBA?AVQRectF@@AEBVQString@@@Z", \
      "qt-quick", "measure", "inline", "qml-label-width-elision-and-layout") \
    X(hookedQuickItemSetImplicitSize, L"Qt6Quick.dll", \
      "?setImplicitSize@QQuickItem@@IEAAXNN@Z", \
      "qt-quick", "measure", "inline", "qml-label-width-elision-and-layout") \
    X(hookedQuickItemSetImplicitWidth, L"Qt6Quick.dll", \
      "?setImplicitWidth@QQuickItem@@QEAAXN@Z", \
      "qt-quick", "measure", "inline", "qml-label-width-elision-and-layout") \
    X(hookedDrawStaticText, L"Qt6Gui.dll", \
      "?drawStaticText@QPainter@@QEAAXAEBVQPointF@@AEBVQStaticText@@@Z", \
      "qt-static-text", "draw", "inline+iat", "cached-static-labels") \
    X(hookedQTextLayoutDraw, L"Qt6Gui.dll", \
      "?draw@QTextLayout@@QEBAXPEAVQPainter@@AEBVQPointF@@AEBV?$QList@UFormatRange@QTextLayout@@@@AEBVQRectF@@@Z", \
      "qt-text-layout", "draw", "inline+iat", "item-view-and-rich-text-layout") \
    X(hookedDrawPoint, L"Qt6Gui.dll", \
      "?drawText@QPainter@@QEAAXAEBVQPoint@@AEBVQString@@@Z", \
      "qt-widgets", "draw", "inline+iat", "widget-label-point-rect-overloads") \
    X(hookedDrawPointF, L"Qt6Gui.dll", \
      "?drawText@QPainter@@QEAAXAEBVQPointF@@AEBVQString@@@Z", \
      "qt-widgets", "draw", "inline+iat", "widget-label-point-rect-overloads") \
    X(hookedDrawPointF2, L"Qt6Gui.dll", \
      "?drawText@QPainter@@QEAAXAEBVQPointF@@AEBVQString@@HH@Z", \
      "qt-widgets", "draw", "inline+iat", "widget-label-point-rect-overloads") \
    X(hookedDrawRect, L"Qt6Gui.dll", \
      "?drawText@QPainter@@QEAAXAEBVQRect@@HAEBVQString@@PEAV2@@Z", \
      "qt-widgets", "draw", "inline+iat", "widget-label-point-rect-overloads") \
    X(hookedDrawRectFAlign, L"Qt6Gui.dll", \
      "?drawText@QPainter@@QEAAXAEBVQRectF@@HAEBVQString@@PEAV2@@Z", \
      "qt-widgets", "draw", "inline+iat", "widget-label-point-rect-overloads") \
    X(hookedDrawRectFOption, L"Qt6Gui.dll", \
      "?drawText@QPainter@@QEAAXAEBVQRectF@@AEBVQString@@AEBVQTextOption@@@Z", \
      "qt-widgets", "draw", "inline+iat", "widget-label-point-rect-overloads") \
    X(hookedDrawXY, L"Qt6Gui.dll", \
      "?drawText@QPainter@@QEAAXHHAEBVQString@@@Z", \
      "qt-widgets", "draw", "inline+iat", "widget-label-point-rect-overloads") \
    X(hookedDrawXYWH, L"Qt6Gui.dll", \
      "?drawText@QPainter@@QEAAXHHHHHAEBVQString@@PEAVQRect@@@Z", \
      "qt-widgets", "draw", "inline+iat", "widget-label-point-rect-overloads") \
    X(hookedStyleDrawItemText, L"Qt6Widgets.dll", \
      "?drawItemText@QStyle@@UEBAXPEAVQPainter@@AEBVQRect@@HAEBVQPalette@@_NAEBVQString@@W4ColorRole@4@@Z", \
      "qt-widgets", "draw", "style-vtable", "native-style-menu-and-list-labels") \
    X(hookedFontBRect, L"Qt6Gui.dll", \
      "?boundingRect@QFontMetrics@@QEBA?AVQRect@@AEBVQString@@@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision") \
    X(hookedFontBRectRect, L"Qt6Gui.dll", \
      "?boundingRect@QFontMetrics@@QEBA?AVQRect@@AEBV2@HAEBVQString@@HPEAH@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision") \
    X(hookedFontBRectXYWH, L"Qt6Gui.dll", \
      "?boundingRect@QFontMetrics@@QEBA?AVQRect@@HHHHHAEBVQString@@HPEAH@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision") \
    X(hookedFontElide, L"Qt6Gui.dll", \
      "?elidedText@QFontMetrics@@QEBA?AVQString@@AEBV2@W4TextElideMode@Qt@@HH@Z", \
      "qt-widgets", "measure", "inline+iat", "widget-label-width-and-elision") \
    X(hookedFontFBRect, L"Qt6Gui.dll", \
      "?boundingRect@QFontMetricsF@@QEBA?AVQRectF@@AEBVQString@@@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision") \
    X(hookedFontFBRectRect, L"Qt6Gui.dll", \
      "?boundingRect@QFontMetricsF@@QEBA?AVQRectF@@AEBV2@HAEBVQString@@HPEAH@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision") \
    X(hookedFontFElide, L"Qt6Gui.dll", \
      "?elidedText@QFontMetricsF@@QEBA?AVQString@@AEBV2@W4TextElideMode@Qt@@NH@Z", \
      "qt-widgets", "measure", "inline+iat", "widget-label-width-and-elision") \
    X(hookedFontFHAdv, L"Qt6Gui.dll", \
      "?horizontalAdvance@QFontMetricsF@@QEBANAEBVQString@@H@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision") \
    X(hookedFontFSize, L"Qt6Gui.dll", \
      "?size@QFontMetricsF@@QEBA?AVQSizeF@@HAEBVQString@@HPEAH@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision") \
    X(hookedFontFTBRect, L"Qt6Gui.dll", \
      "?tightBoundingRect@QFontMetricsF@@QEBA?AVQRectF@@AEBVQString@@@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision") \
    X(hookedFontHAdv, L"Qt6Gui.dll", \
      "?horizontalAdvance@QFontMetrics@@QEBAHAEBVQString@@H@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision") \
    X(hookedFontSize, L"Qt6Gui.dll", \
      "?size@QFontMetrics@@QEBA?AVQSize@@HAEBVQString@@HPEAH@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision") \
    X(hookedFontTBRect, L"Qt6Gui.dll", \
      "?tightBoundingRect@QFontMetrics@@QEBA?AVQRect@@AEBVQString@@@Z", \
      "qt-widgets", "measure", "iat", "widget-label-width-and-elision")

namespace HoudiniChineseHooks {

enum class HookId : std::size_t {
#define HC_HOOK_ENUM(Id, Module, Symbol, Feature, Role, Strategy, Scenario) Id,
    HC_HOOK_CATALOG(HC_HOOK_ENUM)
#undef HC_HOOK_ENUM
    Count
};

inline constexpr std::size_t kHookCount = static_cast<std::size_t>(HookId::Count);

struct HookSpec final {
    HookId id;
    const wchar_t *module;
    const char *symbol;
    const char *feature;
    const char *role;
    const char *strategy;
    const char *scenario;
};

inline constexpr HookSpec kHookSpecs[] = {
#define HC_HOOK_SPEC(Id, Module, Symbol, Feature, Role, Strategy, Scenario) \
    {HookId::Id, Module, Symbol, Feature, Role, Strategy, Scenario},
    HC_HOOK_CATALOG(HC_HOOK_SPEC)
#undef HC_HOOK_SPEC
};

static_assert(sizeof(kHookSpecs) / sizeof(kHookSpecs[0]) == kHookCount);

} // namespace HoudiniChineseHooks

// ---------------------------------------------------------------------------
// hook registry
// ---------------------------------------------------------------------------

#include <array>
#include <atomic>
#include <cstdint>
#include <unordered_map>

namespace HoudiniChineseHooks {

struct HookCounters final {
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> translated{0};
    std::atomic<std::uint64_t> lookupMisses{0};
    std::atomic<std::uint64_t> protectedText{0};
};

struct HookRecord final {
    std::uintptr_t module = 0;
    std::uintptr_t target = 0; // Raw export only, never a Detours trampoline.
    bool modulePresent = false;
    bool targetChanged = false;
    bool incompleteDiagnostics = false;
    bool inlinePatched = false;
    bool virtualPatched = false;
    bool importRegistered = false;
    bool retryLimited = false;
    unsigned attempts = 0;
    std::uint32_t lastError = 0;
    const char *errorStage = "";
    const char *detourStage = "";
    std::uint32_t detourAllocationError = 0;
    std::uint32_t detourQueryError = 0;
    std::uint32_t detourAllocationAttempts = 0;
    std::uint32_t detourQueryCount = 0;
    std::unordered_map<std::uintptr_t, std::size_t> importerSlots;
    HookCounters counters;

    std::size_t importSlots() const noexcept {
        std::size_t result = 0;
        for (const auto &entry : importerSlots)
            result += entry.second;
        return result;
    }
    const char *state() const noexcept {
        if (targetChanged)
            return "target_changed_requires_restart";
        if (!modulePresent)
            return "waiting_for_module";
        if (counters.calls.load(std::memory_order_relaxed))
            return "observed";
        if (inlinePatched || virtualPatched || importSlots())
            return "patched_not_observed";
        if (importRegistered)
            return "import_registered_not_patched";
        if (!target)
            return "symbol_missing";
        if (lastError)
            return "installation_failed";
        return "resolved_not_patched";
    }
};

// Installation metadata is owned by the GUI thread. Rendering touches only
// fixed-address atomic counters, and only during explicit diagnostic sessions.
class HookRegistry final {
  public:
    HookRecord &record(HookId id) noexcept { return records_[index(id)]; }
    const HookRecord &record(HookId id) const noexcept { return records_[index(id)]; }
    std::uint64_t generation() const noexcept { return generation_; }
    void enableObservation(bool enabled) noexcept {
        observing_.store(enabled, std::memory_order_release);
    }
    bool observing() const noexcept {
        return observing_.load(std::memory_order_acquire);
    }
    void noteNesting(HookId parent, HookId child) noexcept {
        nesting_[index(parent) * kHookCount + index(child)].fetch_add(
            1, std::memory_order_relaxed);
    }
    std::uint64_t nestingCount(HookId parent, HookId child) const noexcept {
        return nesting_[index(parent) * kHookCount + index(child)].load(
            std::memory_order_relaxed);
    }
    void resolve(HookId id, std::uintptr_t module, std::uintptr_t target) {
        HookRecord &entry = record(id);
        entry.modulePresent = module != 0;
        if (entry.target && module &&
            (entry.module != module || entry.target != target)) {
            entry.targetChanged = true;
            return;
        }
        if (entry.target || !target)
            return;
        entry.module = module;
        entry.target = target;
        ++generation_;
    }
    template <typename Callback>
    void forTarget(const void *target, Callback callback) {
        if (!target)
            return;
        for (auto &entry : records_)
            if (entry.target == reinterpret_cast<std::uintptr_t>(target))
                callback(entry);
    }
    void beginInline(const void *target) {
        forTarget(target, [](HookRecord &entry) {
            ++entry.attempts;
            entry.detourStage = "";
            entry.detourAllocationError = entry.detourQueryError = 0;
            entry.detourAllocationAttempts = entry.detourQueryCount = 0;
        });
    }
    void inlineResult(const void *target, bool success, std::uint32_t error,
                      const char *stage = "inline") {
        forTarget(target, [=](HookRecord &entry) {
            entry.inlinePatched = entry.inlinePatched || success;
            if (success) entry.retryLimited = false;
            entry.lastError = error;
            entry.errorStage = success ? "" : stage;
        });
    }
    void retryLimited(const void *target) {
        forTarget(target, [](HookRecord &entry) { entry.retryLimited = true; });
    }
    void registerImport(const void *target) {
        forTarget(target, [this](HookRecord &entry) {
            if (!entry.importRegistered) {
                entry.importRegistered = true;
                ++generation_;
            }
        });
    }
    void importResult(const void *target, std::uintptr_t importer,
                      std::size_t slots, std::uint32_t error) noexcept {
        registerImport(target);
        forTarget(target, [=](HookRecord &entry) {
            if (slots) {
                try { entry.importerSlots[importer] = slots; }
                catch (...) { entry.incompleteDiagnostics = true; }
            }
            else
                entry.importerSlots.erase(importer);
            if (error) {
                entry.lastError = error;
                entry.errorStage = "import";
            }
        });
    }
    void virtualResult(const void *target, bool success) {
        forTarget(target, [=](HookRecord &entry) {
            entry.virtualPatched = entry.virtualPatched || success;
        });
    }
    template <typename Predicate>
    void forgetAbsentImporters(Predicate present) {
        for (auto &entry : records_)
            for (auto it = entry.importerSlots.begin(); it != entry.importerSlots.end();)
                if (!present(it->first))
                    it = entry.importerSlots.erase(it);
                else
                    ++it;
    }

  private:
    static std::size_t index(HookId id) noexcept { return static_cast<std::size_t>(id); }
    std::array<HookRecord, kHookCount> records_{};
    // Bounded diagnostic-only call edges; no text, allocation or stack unwind.
    std::array<std::atomic<std::uint64_t>, kHookCount * kHookCount> nesting_{};
    std::uint64_t generation_ = 0;
    std::atomic<bool> observing_{false};
};

inline thread_local HookCounters *activeHookCounters = nullptr;
inline thread_local HookRegistry *activeHookRegistry = nullptr;
inline thread_local HookId activeHookId{};

class HookObservation final {
  public:
    HookObservation(HookRegistry &registry, HookId id) noexcept
        : previous_(activeHookCounters), enabled_(registry.observing()) {
        if (enabled_ || previous_) {
            previousRegistry_ = activeHookRegistry;
            previousId_ = activeHookId;
        }
        if (enabled_) {
            if (previous_ && previousRegistry_ == &registry)
                registry.noteNesting(previousId_, id);
            activeHookRegistry = &registry;
            activeHookId = id;
            activeHookCounters = &registry.record(id).counters;
            activeHookCounters->calls.fetch_add(1, std::memory_order_relaxed);
        } else if (previous_) {
            activeHookCounters = nullptr;
            activeHookRegistry = nullptr;
        }
    }
    ~HookObservation() {
        if (enabled_ || previous_) {
            activeHookCounters = previous_;
            activeHookRegistry = previousRegistry_;
            activeHookId = previousId_;
        }
    }
    HookObservation(const HookObservation &) = delete;
    HookObservation &operator=(const HookObservation &) = delete;

  private:
    HookCounters *previous_;
    HookRegistry *previousRegistry_ = nullptr;
    HookId previousId_{};
    bool enabled_;
};

enum class TranslationOutcome { Translated, LookupMiss, Protected };
inline void noteTranslationOutcome(TranslationOutcome outcome) noexcept {
    if (!activeHookCounters)
        return;
    switch (outcome) {
    case TranslationOutcome::Translated:
        activeHookCounters->translated.fetch_add(1, std::memory_order_relaxed); break;
    case TranslationOutcome::LookupMiss:
        activeHookCounters->lookupMisses.fetch_add(1, std::memory_order_relaxed); break;
    case TranslationOutcome::Protected:
        activeHookCounters->protectedText.fetch_add(1, std::memory_order_relaxed); break;
    }
}

} // namespace HoudiniChineseHooks
