#pragma once

// Verified against the exports of the installed 21.0.440 SDK/runtime.
// Reference and pointer parameters have the same x64 calling convention;
// the help-text return object, however, requires its matching SDK type.
#if defined(HC_HOUDINI_21)
#define HC_UI_WIDTH_SYMBOL "?getStringWidth@UI_Text@@IEBAMPEBD0PEBVtransform@UT_Unicode@@@Z"
#define HC_UI_HELP_SYMBOL "?getHelpText@UI_HelpFeel@@UEAA?AVUT_DeepString@@AEAVUI_DeviceEvent@@@Z"
#define HC_TREE_RENDER_SYMBOL "?doRenderNode@SI_Tree@@AEAAXPEAVRE_Render@@AEAVRE_WindowDrawablePaintState@@AEAVQStyleOptionViewItem@@PEAVSI_TreeNode@@HHHHHH@Z"
#else
#define HC_UI_WIDTH_SYMBOL "?getStringWidth@UI_Text@@QEBAMPEBD0PEBVtransform@UT_Unicode@@@Z"
#define HC_UI_HELP_SYMBOL "?getHelpText@UI_HelpFeel@@UEAA?AVUT_StringHolder@@AEAVUI_DeviceEvent@@@Z"
#define HC_TREE_RENDER_SYMBOL "?doRenderNode@SI_Tree@@AEAAXPEAVRE_Render@@PEAVRE_WindowDrawablePaintState@@AEAVQStyleOptionViewItem@@PEAVSI_TreeNode@@HHHHHH@Z"
#endif
