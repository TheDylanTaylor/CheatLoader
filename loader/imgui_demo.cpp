#include "hash.h"
#include "spoofcalls.hpp"
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "imgui.h"
#ifndef IMGUI_DISABLE

// System includes
#include <ctype.h>          // toupper
#include <limits.h>         // INT_MIN, INT_MAX
#include <math.h>           // sqrtf, powf, cosf, sinf, floorf, ceilf
#include <stdio.h>          // vsnprintf, sscanf, printf
#include <stdlib.h>         // NULL, malloc, free, atoi
#if defined(_MSC_VER) && _MSC_VER <= 1500 // MSVC 2008 or earlier
#include <stddef.h>         // intptr_t
#else
#include <stdint.h>         // intptr_t
#endif

// Visual Studio warnings
#ifdef _MSC_VER
#pragma warning (disable: 4127)     // condition expression is constant
#pragma warning (disable: 4996)     // 'This function or variable may be unsafe': strcpy, strdup, sprintf, vsnprintf, sscanf, fopen
#pragma warning (disable: 26451)    // [Static Analyzer] Arithmetic overflow : Using operator 'xxx' on a 4 byte value and then casting the result to an 8 byte value. Cast the value to the wider type before calling operator 'xxx' to avoid overflow(io.2).
#endif

// Clang/GCC warnings with -Weverything
#if defined(__clang__)
#if __has_warning("-Wunknown-warning-option")
#pragma clang diagnostic ignored "-Wunknown-warning-option"         // warning: unknown warning group 'xxx'                     // not all warnings are known by all Clang versions and they tend to be rename-happy.. so ignoring warnings triggers new warnings on some configuration. Great!
#endif
#pragma clang diagnostic ignored "-Wunknown-pragmas"                // warning: unknown warning group 'xxx'
#pragma clang diagnostic ignored "-Wold-style-cast"                 // warning: use of old-style cast                           // yes, they are more terse.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"        // warning: 'xx' is deprecated: The POSIX name for this..   // for strdup used in demo code (so user can copy & paste the code)
#pragma clang diagnostic ignored "-Wint-to-void-pointer-cast"       // warning: cast to 'void *' from smaller integer type
#pragma clang diagnostic ignored "-Wformat-security"                // warning: format string is not a string literal
#pragma clang diagnostic ignored "-Wexit-time-destructors"          // warning: declaration requires an exit-time destructor    // exit-time destruction order is undefined. if MemFree() leads to users code that has been disabled before exit it might cause problems. ImGui coding style welcomes static/globals.
#pragma clang diagnostic ignored "-Wunused-macros"                  // warning: macro is not used                               // we define snprintf/vsnprintf on Windows so they are available, but not always used.
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"  // warning: zero as null pointer constant                   // some standard header variations use #define NULL 0
#pragma clang diagnostic ignored "-Wdouble-promotion"               // warning: implicit conversion from 'float' to 'double' when passing argument to function  // using printf() is a misery with this as C++ va_arg ellipsis changes float to double.
#pragma clang diagnostic ignored "-Wreserved-id-macro"              // warning: macro name is a reserved identifier
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"  // warning: implicit conversion from 'xxx' to 'float' may lose precision
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wpragmas"                  // warning: unknown option after '#pragma GCC diagnostic' kind
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"      // warning: cast to pointer from integer of different size
#pragma GCC diagnostic ignored "-Wformat-security"          // warning: format string is not a string literal (potentially insecure)
#pragma GCC diagnostic ignored "-Wdouble-promotion"         // warning: implicit conversion from 'float' to 'double' when passing argument to function
#pragma GCC diagnostic ignored "-Wconversion"               // warning: conversion to 'xxxx' from 'xxxx' may alter its value
#pragma GCC diagnostic ignored "-Wmisleading-indentation"   // [__GNUC__ >= 6] warning: this 'if' clause does not guard this statement      // GCC 6.0+ only. See #883 on GitHub.
#endif

// Play it nice with Windows users (Update: May 2018, Notepad now supports Unix-style carriage returns!)
#ifdef _WIN32
#define IM_NEWLINE  "\r\n"
#else
#define IM_NEWLINE  "\n"
#endif

// Helpers
#if defined(_MSC_VER) && !defined(snprintf)
#define snprintf    _snprintf
#endif
#if defined(_MSC_VER) && !defined(vsnprintf)
#define vsnprintf   _vsnprintf
#endif

// Format specifiers, printing 64-bit hasn't been decently standardized...
// In a real application you should be using PRId64 and PRIu64 from <inttypes.h> (non-windows) and on Windows define them yourself.
#ifdef _MSC_VER
#define IM_PRId64   "I64d"
#define IM_PRIu64   "I64u"
#else
#define IM_PRId64   "lld"
#define IM_PRIu64   "llu"
#endif

// Helpers macros
// We normally try to not use many helpers in imgui_demo.cpp in order to make code easier to copy and paste,
// but making an exception here as those are largely simplifying code...
// In other imgui sources we can use nicer internal functions from imgui_internal.h (ImMin/ImMax) but not in the demo.
#define IM_MIN(A, B)            (((A) < (B)) ? (A) : (B))
#define IM_MAX(A, B)            (((A) >= (B)) ? (A) : (B))
#define IM_CLAMP(V, MN, MX)     ((V) < (MN) ? (MN) : (V) > (MX) ? (MX) : (V))

// Enforce cdecl calling convention for functions called by the standard library, in case compilation settings changed the default to e.g. __vectorcall
#ifndef IMGUI_CDECL
#ifdef _MSC_VER
#define IMGUI_CDECL __cdecl
#else
#define IMGUI_CDECL
#endif
#endif

//-----------------------------------------------------------------------------
// [SECTION] Forward Declarations, Helpers
//-----------------------------------------------------------------------------

#if !defined(IMGUI_DISABLE_DEMO_WINDOWS)

// Forward Declarations
static void ShowExampleAppDocuments(bool* p_open);
static void ShowExampleAppMainMenuBar();
static void ShowExampleAppConsole(bool* p_open);
static void ShowExampleAppLog(bool* p_open);
static void ShowExampleAppLayout(bool* p_open);
static void ShowExampleAppPropertyEditor(bool* p_open);
static void ShowExampleAppLongText(bool* p_open);
static void ShowExampleAppAutoResize(bool* p_open);
static void ShowExampleAppConstrainedResize(bool* p_open);
static void ShowExampleAppSimpleOverlay(bool* p_open);
static void ShowExampleAppFullscreen(bool* p_open);
static void ShowExampleAppWindowTitles(bool* p_open);
static void ShowExampleAppCustomRendering(bool* p_open);
static void ShowExampleMenuFile();

// We split the contents of the big ShowDemoWindow() function into smaller functions
// (because the link time of very large functions grow non-linearly)
static void ShowDemoWindowWidgets();
static void ShowDemoWindowLayout();
static void ShowDemoWindowPopups();
static void ShowDemoWindowTables();
static void ShowDemoWindowColumns();
static void ShowDemoWindowInputs();

//-----------------------------------------------------------------------------
// [SECTION] Helpers
//-----------------------------------------------------------------------------

// Helper to display a little (?) mark which shows a tooltip when hovered.
// In your own code you may want to display an actual icon if you are using a merged icon fonts (see docs/FONTS.md)
static void HelpMarker(const char* desc)
{
    rtx_spoof_func;
    ImGui::TextDisabled(OBFUSCATE_STR("(?)"));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Helper to wire demo markers located in code to an interactive browser
typedef void (*ImGuiDemoMarkerCallback)(const char* file, int line, const char* section, void* user_data);
extern ImGuiDemoMarkerCallback      GImGuiDemoMarkerCallback;
extern void*                        GImGuiDemoMarkerCallbackUserData;
ImGuiDemoMarkerCallback             GImGuiDemoMarkerCallback = NULL;
void*                               GImGuiDemoMarkerCallbackUserData = NULL;
#define IMGUI_DEMO_MARKER(section)  do { if (GImGuiDemoMarkerCallback != NULL) GImGuiDemoMarkerCallback(__FILE__, __LINE__, section, GImGuiDemoMarkerCallbackUserData); } while (0)

void ImGui::ShowDemoWindow(bool* p_open)
{
    rtx_spoof_func;
    // Exceptionally add an extra assert here for people confused about initial Dear ImGui setup
    // Most functions would normally just crash if the context is missing.
    IM_ASSERT(ImGui::GetCurrentContext() != NULL && OBFUSCATE_STR("Missing dear imgui context. Refer to examples app!"));

    // Examples Apps (accessible from the "Examples" menu)
    static bool show_app_main_menu_bar = false;
    static bool show_app_documents = false;
    static bool show_app_console = false;
    static bool show_app_log = false;
    static bool show_app_layout = false;
    static bool show_app_property_editor = false;
    static bool show_app_long_text = false;
    static bool show_app_auto_resize = false;
    static bool show_app_constrained_resize = false;
    static bool show_app_simple_overlay = false;
    static bool show_app_fullscreen = false;
    static bool show_app_window_titles = false;
    static bool show_app_custom_rendering = false;

    if (show_app_main_menu_bar)       ShowExampleAppMainMenuBar();
    if (show_app_documents)           ShowExampleAppDocuments(&show_app_documents);
    if (show_app_console)             ShowExampleAppConsole(&show_app_console);
    if (show_app_log)                 ShowExampleAppLog(&show_app_log);
    if (show_app_layout)              ShowExampleAppLayout(&show_app_layout);
    if (show_app_property_editor)     ShowExampleAppPropertyEditor(&show_app_property_editor);
    if (show_app_long_text)           ShowExampleAppLongText(&show_app_long_text);
    if (show_app_auto_resize)         ShowExampleAppAutoResize(&show_app_auto_resize);
    if (show_app_constrained_resize)  ShowExampleAppConstrainedResize(&show_app_constrained_resize);
    if (show_app_simple_overlay)      ShowExampleAppSimpleOverlay(&show_app_simple_overlay);
    if (show_app_fullscreen)          ShowExampleAppFullscreen(&show_app_fullscreen);
    if (show_app_window_titles)       ShowExampleAppWindowTitles(&show_app_window_titles);
    if (show_app_custom_rendering)    ShowExampleAppCustomRendering(&show_app_custom_rendering);

    // Dear ImGui Tools/Apps (accessible from the "Tools" menu)
    static bool show_app_metrics = false;
    static bool show_app_debug_log = false;
    static bool show_app_stack_tool = false;
    static bool show_app_about = false;
    static bool show_app_style_editor = false;

    if (show_app_metrics)
        ImGui::ShowMetricsWindow(&show_app_metrics);
    if (show_app_debug_log)
        ImGui::ShowDebugLogWindow(&show_app_debug_log);
    if (show_app_stack_tool)
        ImGui::ShowStackToolWindow(&show_app_stack_tool);
    if (show_app_about)
        ImGui::ShowAboutWindow(&show_app_about);
    if (show_app_style_editor)
    {
        ImGui::Begin(OBFUSCATE_STR("Dear ImGui Style Editor"), &show_app_style_editor);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }

    // Demonstrate the various window flags. Typically you would just use the default!
    static bool no_titlebar = false;
    static bool no_scrollbar = false;
    static bool no_menu = false;
    static bool no_move = false;
    static bool no_resize = false;
    static bool no_collapse = false;
    static bool no_close = false;
    static bool no_nav = false;
    static bool no_background = false;
    static bool no_bring_to_front = false;
    static bool unsaved_document = false;

    ImGuiWindowFlags window_flags = 0;
    if (no_titlebar)        window_flags |= ImGuiWindowFlags_NoTitleBar;
    if (no_scrollbar)       window_flags |= ImGuiWindowFlags_NoScrollbar;
    if (!no_menu)           window_flags |= ImGuiWindowFlags_MenuBar;
    if (no_move)            window_flags |= ImGuiWindowFlags_NoMove;
    if (no_resize)          window_flags |= ImGuiWindowFlags_NoResize;
    if (no_collapse)        window_flags |= ImGuiWindowFlags_NoCollapse;
    if (no_nav)             window_flags |= ImGuiWindowFlags_NoNav;
    if (no_background)      window_flags |= ImGuiWindowFlags_NoBackground;
    if (no_bring_to_front)  window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (unsaved_document)   window_flags |= ImGuiWindowFlags_UnsavedDocument;
    if (no_close)           p_open = NULL; // Don't pass our bool* to Begin

    // We specify a default position/size in case there's no data in the .ini file.
    // We only do it to make the demo applications a little more welcoming, but typically this isn't required.
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x + 650, main_viewport->WorkPos.y + 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(550, 680), ImGuiCond_FirstUseEver);

    // Main body of the Demo window starts here.
    if (!ImGui::Begin(OBFUSCATE_STR("Dear ImGui Demo"), p_open, window_flags))
    {
        // Early out if the window is collapsed, as an optimization.
        ImGui::End();
        return;
    }

    // Most "big" widgets share a common width settings by default. See 'Demo->Layout->Widgets Width' for details.
    // e.g. Use 2/3 of the space for widgets and 1/3 for labels (right align)
    //ImGui::PushItemWidth(-ImGui::GetWindowWidth() * 0.35f);
    // e.g. Leave a fixed amount of width for labels (by passing a negative value), the rest goes to widgets.
    ImGui::PushItemWidth(ImGui::GetFontSize() * -12);

    // Menu Bar
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu(OBFUSCATE_STR("Menu")))
        {
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Menu/File"));
            ShowExampleMenuFile();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(OBFUSCATE_STR("Examples")))
        {
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Menu/Examples"));
            ImGui::MenuItem(OBFUSCATE_STR("Main menu bar"), NULL, &show_app_main_menu_bar);
            ImGui::MenuItem(OBFUSCATE_STR("Console"), NULL, &show_app_console);
            ImGui::MenuItem(OBFUSCATE_STR("Log"), NULL, &show_app_log);
            ImGui::MenuItem(OBFUSCATE_STR("Simple layout"), NULL, &show_app_layout);
            ImGui::MenuItem(OBFUSCATE_STR("Property editor"), NULL, &show_app_property_editor);
            ImGui::MenuItem(OBFUSCATE_STR("Long text display"), NULL, &show_app_long_text);
            ImGui::MenuItem(OBFUSCATE_STR("Auto-resizing window"), NULL, &show_app_auto_resize);
            ImGui::MenuItem(OBFUSCATE_STR("Constrained-resizing window"), NULL, &show_app_constrained_resize);
            ImGui::MenuItem(OBFUSCATE_STR("Simple overlay"), NULL, &show_app_simple_overlay);
            ImGui::MenuItem(OBFUSCATE_STR("Fullscreen window"), NULL, &show_app_fullscreen);
            ImGui::MenuItem(OBFUSCATE_STR("Manipulating window titles"), NULL, &show_app_window_titles);
            ImGui::MenuItem(OBFUSCATE_STR("Custom rendering"), NULL, &show_app_custom_rendering);
            ImGui::MenuItem(OBFUSCATE_STR("Documents"), NULL, &show_app_documents);
            ImGui::EndMenu();
        }
        //if (ImGui::MenuItem("MenuItem")) {} // You can also use MenuItem() inside a menu bar!
        if (ImGui::BeginMenu(OBFUSCATE_STR("Tools")))
        {
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Menu/Tools"));
#ifndef IMGUI_DISABLE_DEBUG_TOOLS
            const bool has_debug_tools = true;
#else
            const bool has_debug_tools = false;
#endif
            ImGui::MenuItem(OBFUSCATE_STR("Metrics/Debugger"), NULL, &show_app_metrics, has_debug_tools);
            ImGui::MenuItem(OBFUSCATE_STR("Debug Log"), NULL, &show_app_debug_log, has_debug_tools);
            ImGui::MenuItem(OBFUSCATE_STR("Stack Tool"), NULL, &show_app_stack_tool, has_debug_tools);
            ImGui::MenuItem(OBFUSCATE_STR("Style Editor"), NULL, &show_app_style_editor);
            ImGui::MenuItem(OBFUSCATE_STR("About Dear ImGui"), NULL, &show_app_about);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::Text(OBFUSCATE_STR("dear imgui says hello! (%s) (%d)"), IMGUI_VERSION, IMGUI_VERSION_NUM);
    ImGui::Spacing();

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Help"));
    if (ImGui::CollapsingHeader(OBFUSCATE_STR("Help")))
    {
        ImGui::Text("ABOUT THIS DEMO:");
        ImGui::BulletText("Sections below are demonstrating many aspects of the library.");
        ImGui::BulletText("The \"Examples\" menu above leads to more demo contents.");
        ImGui::BulletText("The \"Tools\" menu above gives access to: About Box, Style Editor,\n"
                          "and Metrics/Debugger (general purpose Dear ImGui debugging tool).");
        ImGui::Separator();

        ImGui::Text("PROGRAMMER GUIDE:");
        ImGui::BulletText("See the ShowDemoWindow() code in imgui_demo.cpp. <- you are here!");
        ImGui::BulletText("See comments in imgui.cpp.");
        ImGui::BulletText("See example applications in the examples/ folder.");
        ImGui::BulletText("Read the FAQ at http://www.dearimgui.org/faq/");
        ImGui::BulletText("Set 'io.ConfigFlags |= NavEnableKeyboard' for keyboard controls.");
        ImGui::BulletText("Set 'io.ConfigFlags |= NavEnableGamepad' for gamepad controls.");
        ImGui::Separator();

        ImGui::Text("USER GUIDE:");
        ImGui::ShowUserGuide();
    }

    IMGUI_DEMO_MARKER("Configuration");
    if (ImGui::CollapsingHeader("Configuration"))
    {
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::TreeNode("Configuration##2"))
        {
            ImGui::CheckboxFlags("io.ConfigFlags: NavEnableKeyboard",    &io.ConfigFlags, ImGuiConfigFlags_NavEnableKeyboard);
            ImGui::SameLine(); HelpMarker("Enable keyboard controls.");
            ImGui::CheckboxFlags("io.ConfigFlags: NavEnableGamepad",     &io.ConfigFlags, ImGuiConfigFlags_NavEnableGamepad);
            ImGui::SameLine(); HelpMarker("Enable gamepad controls. Require backend to set io.BackendFlags |= ImGuiBackendFlags_HasGamepad.\n\nRead instructions in imgui.cpp for details.");
            ImGui::CheckboxFlags("io.ConfigFlags: NavEnableSetMousePos", &io.ConfigFlags, ImGuiConfigFlags_NavEnableSetMousePos);
            ImGui::SameLine(); HelpMarker("Instruct navigation to move the mouse cursor. See comment for ImGuiConfigFlags_NavEnableSetMousePos.");
            ImGui::CheckboxFlags("io.ConfigFlags: NoMouse",              &io.ConfigFlags, ImGuiConfigFlags_NoMouse);
            if (io.ConfigFlags & ImGuiConfigFlags_NoMouse)
            {
                // The "NoMouse" option can get us stuck with a disabled mouse! Let's provide an alternative way to fix it:
                if (fmodf((float)ImGui::GetTime(), 0.40f) < 0.20f)
                {
                    ImGui::SameLine();
                    ImGui::Text("<<PRESS SPACE TO DISABLE>>");
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Space))
                    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            }
            ImGui::CheckboxFlags("io.ConfigFlags: NoMouseCursorChange", &io.ConfigFlags, ImGuiConfigFlags_NoMouseCursorChange);
            ImGui::SameLine(); HelpMarker("Instruct backend to not alter mouse cursor shape and visibility.");
            ImGui::Checkbox("io.ConfigInputTrickleEventQueue", &io.ConfigInputTrickleEventQueue);
            ImGui::SameLine(); HelpMarker("Enable input queue trickling: some types of events submitted during the same frame (e.g. button down + up) will be spread over multiple frames, improving interactions with low framerates.");
            ImGui::Checkbox("io.ConfigInputTextCursorBlink", &io.ConfigInputTextCursorBlink);
            ImGui::SameLine(); HelpMarker("Enable blinking cursor (optional as some users consider it to be distracting).");
            ImGui::Checkbox("io.ConfigInputTextEnterKeepActive", &io.ConfigInputTextEnterKeepActive);
            ImGui::SameLine(); HelpMarker("Pressing Enter will keep item active and select contents (single-line only).");
            ImGui::Checkbox("io.ConfigDragClickToInputText", &io.ConfigDragClickToInputText);
            ImGui::SameLine(); HelpMarker("Enable turning DragXXX widgets into text input with a simple mouse click-release (without moving).");
            ImGui::Checkbox("io.ConfigWindowsResizeFromEdges", &io.ConfigWindowsResizeFromEdges);
            ImGui::SameLine(); HelpMarker("Enable resizing of windows from their edges and from the lower-left corner.\nThis requires (io.BackendFlags & ImGuiBackendFlags_HasMouseCursors) because it needs mouse cursor feedback.");
            ImGui::Checkbox("io.ConfigWindowsMoveFromTitleBarOnly", &io.ConfigWindowsMoveFromTitleBarOnly);
            ImGui::Checkbox("io.MouseDrawCursor", &io.MouseDrawCursor);
            ImGui::SameLine(); HelpMarker("Instruct Dear ImGui to render a mouse cursor itself. Note that a mouse cursor rendered via your application GPU rendering path will feel more laggy than hardware cursor, but will be more in sync with your other visuals.\n\nSome desktop applications may use both kinds of cursors (e.g. enable software cursor only when resizing/dragging something).");
            ImGui::Text("Also see Style->Rendering for rendering options.");
            ImGui::TreePop();
            ImGui::Separator();
        }

        IMGUI_DEMO_MARKER("Configuration/Backend Flags");
        if (ImGui::TreeNode("Backend Flags"))
        {
            HelpMarker(
                "Those flags are set by the backends (imgui_impl_xxx files) to specify their capabilities.\n"
                "Here we expose them as read-only fields to avoid breaking interactions with your backend.");

            // Make a local copy to avoid modifying actual backend flags.
            // FIXME: We don't use BeginDisabled() to keep label bright, maybe we need a BeginReadonly() equivalent..
            ImGuiBackendFlags backend_flags = io.BackendFlags;
            ImGui::CheckboxFlags("io.BackendFlags: HasGamepad",           &backend_flags, ImGuiBackendFlags_HasGamepad);
            ImGui::CheckboxFlags("io.BackendFlags: HasMouseCursors",      &backend_flags, ImGuiBackendFlags_HasMouseCursors);
            ImGui::CheckboxFlags("io.BackendFlags: HasSetMousePos",       &backend_flags, ImGuiBackendFlags_HasSetMousePos);
            ImGui::CheckboxFlags("io.BackendFlags: RendererHasVtxOffset", &backend_flags, ImGuiBackendFlags_RendererHasVtxOffset);
            ImGui::TreePop();
            ImGui::Separator();
        }

        IMGUI_DEMO_MARKER("Configuration/Style");
        if (ImGui::TreeNode("Style"))
        {
            HelpMarker("The same contents can be accessed in 'Tools->Style Editor' or by calling the ShowStyleEditor() function.");
            ImGui::ShowStyleEditor();
            ImGui::TreePop();
            ImGui::Separator();
        }

        IMGUI_DEMO_MARKER("Configuration/Capture, Logging");
        if (ImGui::TreeNode("Capture/Logging"))
        {
            HelpMarker(
                "The logging API redirects all text output so you can easily capture the content of "
                "a window or a block. Tree nodes can be automatically expanded.\n"
                "Try opening any of the contents below in this window and then click one of the \"Log To\" button.");
            ImGui::LogButtons();

            HelpMarker("You can also call ImGui::LogText() to output directly to the log without a visual output.");
            if (ImGui::Button("Copy \"Hello, world!\" to clipboard"))
            {
                ImGui::LogToClipboard();
                ImGui::LogText("Hello, world!");
                ImGui::LogFinish();
            }
            ImGui::TreePop();
        }
    }

    IMGUI_DEMO_MARKER("Window options");
    if (ImGui::CollapsingHeader("Window options"))
    {
        if (ImGui::BeginTable("split", 3))
        {
            ImGui::TableNextColumn(); ImGui::Checkbox("No titlebar", &no_titlebar);
            ImGui::TableNextColumn(); ImGui::Checkbox("No scrollbar", &no_scrollbar);
            ImGui::TableNextColumn(); ImGui::Checkbox("No menu", &no_menu);
            ImGui::TableNextColumn(); ImGui::Checkbox("No move", &no_move);
            ImGui::TableNextColumn(); ImGui::Checkbox("No resize", &no_resize);
            ImGui::TableNextColumn(); ImGui::Checkbox("No collapse", &no_collapse);
            ImGui::TableNextColumn(); ImGui::Checkbox("No close", &no_close);
            ImGui::TableNextColumn(); ImGui::Checkbox("No nav", &no_nav);
            ImGui::TableNextColumn(); ImGui::Checkbox("No background", &no_background);
            ImGui::TableNextColumn(); ImGui::Checkbox("No bring to front", &no_bring_to_front);
            ImGui::TableNextColumn(); ImGui::Checkbox("Unsaved document", &unsaved_document);
            ImGui::EndTable();
        }
    }

    // All demo contents
    ShowDemoWindowWidgets();
    ShowDemoWindowLayout();
    ShowDemoWindowPopups();
    ShowDemoWindowTables();
    ShowDemoWindowInputs();

    // End of ShowDemoWindow()
    ImGui::PopItemWidth();
    ImGui::End();
}

static void ShowDemoWindowWidgets()
{
    rtx_spoof_func;
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets"));
    if (!ImGui::CollapsingHeader(OBFUSCATE_STR("Widgets")))
        return;

    static bool disable_all = false; // The Checkbox for that is inside the "Disabled" section at the bottom
    if (disable_all)
        ImGui::BeginDisabled();

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Basic")))
    {
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/Button"));
        static int clicked = 0;
        if (ImGui::Button(OBFUSCATE_STR("Button")))
            clicked++;
        if (clicked & 1)
        {
            ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("Thanks for clicking me!"));
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/Checkbox"));
        static bool check = true;
        ImGui::Checkbox(OBFUSCATE_STR("checkbox"), &check);

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/RadioButton"));
        static int e = 0;
        ImGui::RadioButton(OBFUSCATE_STR("radio a"), &e, 0); ImGui::SameLine();
        ImGui::RadioButton(OBFUSCATE_STR("radio b"), &e, 1); ImGui::SameLine();
        ImGui::RadioButton(OBFUSCATE_STR("radio c"), &e, 2);

        // Color buttons, demonstrate using PushID() to add unique identifier in the ID stack, and changing style.
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/Buttons (Colored)"));
        for (int i = 0; i < 7; i++)
        {
            if (i > 0)
                ImGui::SameLine();
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(i / 7.0f, 0.6f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(i / 7.0f, 0.7f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(i / 7.0f, 0.8f, 0.8f));
            ImGui::Button(OBFUSCATE_STR("Click"));
            ImGui::PopStyleColor(3);
            ImGui::PopID();
        }

        // Use AlignTextToFramePadding() to align text baseline to the baseline of framed widgets elements
        // (otherwise a Text+SameLine+Button sequence will have the text a little too high by default!)
        // See 'Demo->Layout->Text Baseline Alignment' for details.
        ImGui::AlignTextToFramePadding();
        ImGui::Text(OBFUSCATE_STR("Hold to repeat:"));
        ImGui::SameLine();

        // Arrow buttons with Repeater
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/Buttons (Repeating)"));
        static int counter = 0;
        float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::PushButtonRepeat(true);
        if (ImGui::ArrowButton(OBFUSCATE_STR("##left"), ImGuiDir_Left)) { counter--; }
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::ArrowButton(OBFUSCATE_STR("##right"), ImGuiDir_Right)) { counter++; }
        ImGui::PopButtonRepeat();
        ImGui::SameLine();
        ImGui::Text(OBFUSCATE_STR("%d"), counter);

        ImGui::Separator();
        ImGui::LabelText(OBFUSCATE_STR("label"), OBFUSCATE_STR("Value"));

        {
            // Using the _simplified_ one-liner Combo() api here
            // See "Combo" section for examples of how to use the more flexible BeginCombo()/EndCombo() api.
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/Combo"));
            const char* items[] = { "AAAA", "BBBB", "CCCC", "DDDD", "EEEE", "FFFF", "GGGG", "HHHH", "IIIIIII", "JJJJ", "KKKKKKK" };
            static int item_current = 0;
            ImGui::Combo(OBFUSCATE_STR("combo"), &item_current, items, IM_ARRAYSIZE(items));
            ImGui::SameLine(); HelpMarker(
                OBFUSCATE_STR("Using the simplified one-liner Combo API here.\nRefer to the \"Combo\" section below for an explanation of how to use the more flexible and general BeginCombo/EndCombo API."));
        }

        {
            // To wire InputText() with std::string or any other custom string type,
            // see the "Text Input > Resize Callback" section of this demo, and the misc/cpp/imgui_stdlib.h file.
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/InputText"));
            static char str0[128] = "Hello, world!";
            ImGui::InputText(OBFUSCATE_STR("input text"), str0, IM_ARRAYSIZE(str0));
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
                "USER:\n"
                "Hold SHIFT or use mouse to select text.\n"
                "CTRL+Left/Right to word jump.\n"
                "CTRL+A or Double-Click to select all.\n"
                "CTRL+X,CTRL+C,CTRL+V clipboard.\n"
                "CTRL+Z,CTRL+Y undo/redo.\n"
                "ESCAPE to revert.\n\n"
                "PROGRAMMER:\n"
                "You can use the ImGuiInputTextFlags_CallbackResize facility if you need to wire InputText() "
                "to a dynamic string type. See misc/cpp/imgui_stdlib.h for an example (this is not demonstrated "
                "in imgui_demo.cpp)."));

            static char str1[128] = "";
            ImGui::InputTextWithHint(OBFUSCATE_STR("input text (w/ hint)"), OBFUSCATE_STR("enter text here"), str1, IM_ARRAYSIZE(str1));

            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/InputInt, InputFloat"));
            static int i0 = 123;
            ImGui::InputInt(OBFUSCATE_STR("input int"), &i0);

            static float f0 = 0.001f;
            ImGui::InputFloat(OBFUSCATE_STR("input float"), &f0, 0.01f, 1.0f, OBFUSCATE_STR("%.3f"));

            static double d0 = 999999.00000001;
            ImGui::InputDouble(OBFUSCATE_STR("input double"), &d0, 0.01f, 1.0f, OBFUSCATE_STR("%.8f"));

            static float f1 = 1.e10f;
            ImGui::InputFloat(OBFUSCATE_STR("input scientific"), &f1, 0.0f, 0.0f, OBFUSCATE_STR("%e"));
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
                "You can input value using the scientific notation,\n"
                "  e.g. \"1e+8\" becomes \"100000000\"."));

            static float vec4a[4] = { 0.10f, 0.20f, 0.30f, 0.44f };
            ImGui::InputFloat3(OBFUSCATE_STR("input float3"), vec4a);
        }

        {
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/DragInt, DragFloat"));
            static int i1 = 50, i2 = 42;
            ImGui::DragInt(OBFUSCATE_STR("drag int"), &i1, 1);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
                "Click and drag to edit value.\n"
                "Hold SHIFT/ALT for faster/slower edit.\n"
                "Double-click or CTRL+click to input value."));

            ImGui::DragInt(OBFUSCATE_STR("drag int 0..100"), &i2, 1, 0, 100, OBFUSCATE_STR("%d%%"), ImGuiSliderFlags_AlwaysClamp);

            static float f1 = 1.00f, f2 = 0.0067f;
            ImGui::DragFloat(OBFUSCATE_STR("drag float"), &f1, 0.005f);
            ImGui::DragFloat(OBFUSCATE_STR("drag small float"), &f2, 0.0001f, 0.0f, 0.0f, OBFUSCATE_STR("%.06f ns"));
        }

        {
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/SliderInt, SliderFloat"));
            static int i1 = 0;
            ImGui::SliderInt(OBFUSCATE_STR("slider int"), &i1, -1, 3);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("CTRL+click to input value."));

            static float f1 = 0.123f, f2 = 0.0f;
            ImGui::SliderFloat(OBFUSCATE_STR("slider float"), &f1, 0.0f, 1.0f, OBFUSCATE_STR("ratio = %.3f"));
            ImGui::SliderFloat(OBFUSCATE_STR("slider float (log)"), &f2, -10.0f, 10.0f, OBFUSCATE_STR("%.4f"), ImGuiSliderFlags_Logarithmic);

            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/SliderAngle"));
            static float angle = 0.0f;
            ImGui::SliderAngle(OBFUSCATE_STR("slider angle"), &angle);

            // Using the format string to display a name instead of an integer.
            // Here we completely omit '%d' from the format string, so it'll only display a name.
            // This technique can also be used with DragInt().
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/Slider (enum)"));
            enum Element { Element_Fire, Element_Earth, Element_Air, Element_Water, Element_COUNT };
            static int elem = Element_Fire;
            const char* elems_names[Element_COUNT] = { "Fire", "Earth", "Air", "Water" };
            const char* elem_name = (elem >= 0 && elem < Element_COUNT) ? elems_names[elem] : OBFUSCATE_STR("Unknown");
            ImGui::SliderInt(OBFUSCATE_STR("slider enum"), &elem, 0, Element_COUNT - 1, elem_name);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Using the format string parameter to display a name instead of the underlying integer."));
        }

        {
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/ColorEdit3, ColorEdit4"));
            static float col1[3] = { 1.0f, 0.0f, 0.2f };
            static float col2[4] = { 0.4f, 0.7f, 0.0f, 0.5f };
            ImGui::ColorEdit3(OBFUSCATE_STR("color 1"), col1);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
                "Click on the color square to open a color picker.\n"
                "Click and hold to use drag and drop.\n"
                "Right-click on the color square to show options.\n"
                "CTRL+click on individual component to input value.\n"));

            ImGui::ColorEdit4(OBFUSCATE_STR("color 2"), col2);
        }

        {
            // Using the _simplified_ one-liner ListBox() api here
            // See "List boxes" section for examples of how to use the more flexible BeginListBox()/EndListBox() api.
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/ListBox"));
            const char* items[] = { "Apple", "Banana", "Cherry", "Kiwi", "Mango", "Orange", "Pineapple", "Strawberry", "Watermelon" };
            static int item_current = 1;
            ImGui::ListBox(OBFUSCATE_STR("listbox"), &item_current, items, IM_ARRAYSIZE(items), 4);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
                "Using the simplified one-liner ListBox API here.\nRefer to the \"List boxes\" section below for an explanation of how to use the more flexible and general BeginListBox/EndListBox API."));
        }

        {
            // Tooltips
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Basic/Tooltips"));
            ImGui::AlignTextToFramePadding();
            ImGui::Text(OBFUSCATE_STR("Tooltips:"));

            ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("Button"));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(OBFUSCATE_STR("I am a tooltip"));

            ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("Fancy"));
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text(OBFUSCATE_STR("I am a fancy tooltip"));
                static float arr[] = { 0.6f, 0.1f, 1.0f, 0.5f, 0.92f, 0.1f, 0.2f };
                ImGui::PlotLines(OBFUSCATE_STR("Curve"), arr, IM_ARRAYSIZE(arr));
                ImGui::Text(OBFUSCATE_STR("Sin(time) = %f"), sinf((float)ImGui::GetTime()));
                ImGui::EndTooltip();
            }

            ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("Delayed"));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) // Delay best used on items that highlight on hover, so this not a great example!
                ImGui::SetTooltip(OBFUSCATE_STR("I am a tooltip with a delay."));

            ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR(
                "Tooltip are created by using the IsItemHovered() function over any kind of item."));

        }

        ImGui::TreePop();
    }

    // Testing ImGuiOnceUponAFrame helper.
    //static ImGuiOnceUponAFrame once;
    //for (int i = 0; i < 5; i++)
    //    if (once)
    //        ImGui::Text("This will be displayed only once.");

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Trees"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Trees")))
    {
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Trees/Basic trees"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Basic trees")))
        {
            for (int i = 0; i < 5; i++)
            {
                // Use SetNextItemOpen() so set the default state of a node to be open. We could
                // also use TreeNodeEx() with the ImGuiTreeNodeFlags_DefaultOpen flag to achieve the same thing!
                if (i == 0)
                    ImGui::SetNextItemOpen(true, ImGuiCond_Once);

                if (ImGui::TreeNode((void*)(intptr_t)i, OBFUSCATE_STR("Child %d"), i))
                {
                    ImGui::Text(OBFUSCATE_STR("blah blah"));
                    ImGui::SameLine();
                    if (ImGui::SmallButton(OBFUSCATE_STR("button"))) {}
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Trees/Advanced, with Selectable nodes"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Advanced, with Selectable nodes")))
        {
            HelpMarker(OBFUSCATE_STR(
                "This is a more typical looking tree with selectable nodes.\n"
                "Click to select, CTRL+Click to toggle, click on arrows or double-click to open."));
            static ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
            static bool align_label_with_current_x_position = false;
            static bool test_drag_and_drop = false;
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTreeNodeFlags_OpenOnArrow"),       &base_flags, ImGuiTreeNodeFlags_OpenOnArrow);
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTreeNodeFlags_OpenOnDoubleClick"), &base_flags, ImGuiTreeNodeFlags_OpenOnDoubleClick);
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTreeNodeFlags_SpanAvailWidth"),    &base_flags, ImGuiTreeNodeFlags_SpanAvailWidth); ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Extend hit area to all available width instead of allowing more items to be laid out after the node."));
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTreeNodeFlags_SpanFullWidth"),     &base_flags, ImGuiTreeNodeFlags_SpanFullWidth);
            ImGui::Checkbox(OBFUSCATE_STR("Align label with current X position"), &align_label_with_current_x_position);
            ImGui::Checkbox(OBFUSCATE_STR("Test tree node as drag source"), &test_drag_and_drop);
            ImGui::Text(OBFUSCATE_STR("Hello!"));
            if (align_label_with_current_x_position)
                ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());

            // 'selection_mask' is dumb representation of what may be user-side selection state.
            //  You may retain selection state inside or outside your objects in whatever format you see fit.
            // 'node_clicked' is temporary storage of what node we have clicked to process selection at the end
            /// of the loop. May be a pointer to your own node type, etc.
            static int selection_mask = (1 << 2);
            int node_clicked = -1;
            for (int i = 0; i < 6; i++)
            {
                // Disable the default "open on single-click behavior" + set Selected flag according to our selection.
                // To alter selection we use IsItemClicked() && !IsItemToggledOpen(), so clicking on an arrow doesn't alter selection.
                ImGuiTreeNodeFlags node_flags = base_flags;
                const bool is_selected = (selection_mask & (1 << i)) != 0;
                if (is_selected)
                    node_flags |= ImGuiTreeNodeFlags_Selected;
                if (i < 3)
                {
                    // Items 0..2 are Tree Node
                    bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, OBFUSCATE_STR("Selectable Node %d"), i);
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                        node_clicked = i;
                    if (test_drag_and_drop && ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload(OBFUSCATE_STR("_TREENODE"), NULL, 0);
                        ImGui::Text(OBFUSCATE_STR("This is a drag and drop source"));
                        ImGui::EndDragDropSource();
                    }
                    if (node_open)
                    {
                        ImGui::BulletText(OBFUSCATE_STR("Blah blah\nBlah Blah"));
                        ImGui::TreePop();
                    }
                }
                else
                {
                    // Items 3..5 are Tree Leaves
                    // The only reason we use TreeNode at all is to allow selection of the leaf. Otherwise we can
                    // use BulletText() or advance the cursor by GetTreeNodeToLabelSpacing() and call Text().
                    node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
                    ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, OBFUSCATE_STR("Selectable Leaf %d"), i);
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                        node_clicked = i;
                    if (test_drag_and_drop && ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload(OBFUSCATE_STR("_TREENODE"), NULL, 0);
                        ImGui::Text(OBFUSCATE_STR("This is a drag and drop source"));
                        ImGui::EndDragDropSource();
                    }
                }
            }
            if (node_clicked != -1)
            {
                // Update selection state
                // (process outside of tree loop to avoid visual inconsistencies during the clicking frame)
                if (ImGui::GetIO().KeyCtrl)
                    selection_mask ^= (1 << node_clicked);          // CTRL+click to toggle
                else //if (!(selection_mask & (1 << node_clicked))) // Depending on selection behavior you want, may want to preserve selection when clicking on item that is part of the selection
                    selection_mask = (1 << node_clicked);           // Click to single-select
            }
            if (align_label_with_current_x_position)
                ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Collapsing Headers"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Collapsing Headers")))
    {
        static bool closable_group = true;
        ImGui::Checkbox(OBFUSCATE_STR("Show 2nd header"), &closable_group);
        if (ImGui::CollapsingHeader(OBFUSCATE_STR("Header"), ImGuiTreeNodeFlags_None))
        {
            ImGui::Text(OBFUSCATE_STR("IsItemHovered: %d"), ImGui::IsItemHovered());
            for (int i = 0; i < 5; i++)
                ImGui::Text(OBFUSCATE_STR("Some content %d"), i);
        }
        if (ImGui::CollapsingHeader(OBFUSCATE_STR("Header with a close button"), &closable_group))
        {
            ImGui::Text(OBFUSCATE_STR("IsItemHovered: %d"), ImGui::IsItemHovered());
            for (int i = 0; i < 5; i++)
                ImGui::Text(OBFUSCATE_STR("More content %d"), i);
        }
        /*
        if (ImGui::CollapsingHeader("Header with a bullet", ImGuiTreeNodeFlags_Bullet))
            ImGui::Text("IsItemHovered: %d", ImGui::IsItemHovered());
        */
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Bullets"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Bullets")))
    {
        ImGui::BulletText(OBFUSCATE_STR("Bullet point 1"));
        ImGui::BulletText(OBFUSCATE_STR("Bullet point 2\nOn multiple lines"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Tree node")))
        {
            ImGui::BulletText(OBFUSCATE_STR("Another bullet point"));
            ImGui::TreePop();
        }
        ImGui::Bullet(); ImGui::Text(OBFUSCATE_STR("Bullet point 3 (two calls)"));
        ImGui::Bullet(); ImGui::SmallButton(OBFUSCATE_STR("Button"));
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Text"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Text")))
    {
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Text/Colored Text"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Colorful Text")))
        {
            // Using shortcut. You can use PushStyleColor()/PopStyleColor() for more flexibility.
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), OBFUSCATE_STR("Pink"));
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), OBFUSCATE_STR("Yellow"));
            ImGui::TextDisabled(OBFUSCATE_STR("Disabled"));
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("The TextDisabled color is stored in ImGuiStyle."));
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Text/Word Wrapping"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Word Wrapping")))
        {
            // Using shortcut. You can use PushTextWrapPos()/PopTextWrapPos() for more flexibility.
            ImGui::TextWrapped(OBFUSCATE_STR(
                "This text should automatically wrap on the edge of the window. The current implementation "
                "for text wrapping follows simple rules suitable for English and possibly other languages."));
            ImGui::Spacing();

            static float wrap_width = 200.0f;
            ImGui::SliderFloat(OBFUSCATE_STR("Wrap width"), &wrap_width, -20, 600, OBFUSCATE_STR("%.0f"));

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            for (int n = 0; n < 2; n++)
            {
                ImGui::Text(OBFUSCATE_STR("Test paragraph %d:"), n);
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 marker_min = ImVec2(pos.x + wrap_width, pos.y);
                ImVec2 marker_max = ImVec2(pos.x + wrap_width + 10, pos.y + ImGui::GetTextLineHeight());
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrap_width);
                if (n == 0)
                    ImGui::Text(OBFUSCATE_STR("The lazy dog is a good dog. This paragraph should fit within %.0f pixels. Testing a 1 character word. The quick brown fox jumps over the lazy dog."), wrap_width);
                else
                    ImGui::Text(OBFUSCATE_STR("aaaaaaaa bbbbbbbb, c cccccccc,dddddddd. d eeeeeeee   ffffffff. gggggggg!hhhhhhhh"));

                // Draw actual text bounding box, following by marker of our expected limit (should not overlap!)
                draw_list->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 0, 255));
                draw_list->AddRectFilled(marker_min, marker_max, IM_COL32(255, 0, 255, 255));
                ImGui::PopTextWrapPos();
            }

            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Text/UTF-8 Text"));
        if (ImGui::TreeNode(OBFUSCATE_STR("UTF-8 Text")))
        {
            ImGui::TextWrapped(OBFUSCATE_STR(
                "CJK text will only appear if the font was loaded with the appropriate CJK character ranges. "
                "Call io.Fonts->AddFontFromFileTTF() manually to load extra character ranges. "
                "Read docs/FONTS.md for details."));
            ImGui::Text(OBFUSCATE_STR("Hiragana: \xe3\x81\x8b\xe3\x81\x8d\xe3\x81\x8f\xe3\x81\x91\xe3\x81\x93 (kakikukeko)")); // Normally we would use u8"blah blah" with the proper characters directly in the string.
            ImGui::Text(OBFUSCATE_STR("Kanjis: \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e (nihongo)"));
            static char buf[32] = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
            //static char buf[32] = u8"NIHONGO"; // <- this is how you would write it with C++11, using real kanjis
            ImGui::InputText(OBFUSCATE_STR("UTF-8 input"), buf, IM_ARRAYSIZE(buf));
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Images"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Images")))
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::TextWrapped(OBFUSCATE_STR(
            "Below we are displaying the font texture (which is the only texture we have access to in this demo). "
            "Use the 'ImTextureID' type as storage to pass pointers or identifier to your own texture data. "
            "Hover the texture for a zoomed view!"));
        ImTextureID my_tex_id = io.Fonts->TexID;
        float my_tex_w = (float)io.Fonts->TexWidth;
        float my_tex_h = (float)io.Fonts->TexHeight;
        {
            ImGui::Text(OBFUSCATE_STR("%.0fx%.0f"), my_tex_w, my_tex_h);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 uv_min = ImVec2(0.0f, 0.0f);                 // Top-left
            ImVec2 uv_max = ImVec2(1.0f, 1.0f);                 // Lower-right
            ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);   // No tint
            ImVec4 border_col = ImVec4(1.0f, 1.0f, 1.0f, 0.5f); // 50% opaque white
            ImGui::Image(my_tex_id, ImVec2(my_tex_w, my_tex_h), uv_min, uv_max, tint_col, border_col);
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                float region_sz = 32.0f;
                float region_x = io.MousePos.x - pos.x - region_sz * 0.5f;
                float region_y = io.MousePos.y - pos.y - region_sz * 0.5f;
                float zoom = 4.0f;
                if (region_x < 0.0f) { region_x = 0.0f; }
                else if (region_x > my_tex_w - region_sz) { region_x = my_tex_w - region_sz; }
                if (region_y < 0.0f) { region_y = 0.0f; }
                else if (region_y > my_tex_h - region_sz) { region_y = my_tex_h - region_sz; }
                ImGui::Text(OBFUSCATE_STR("Min: (%.2f, %.2f)"), region_x, region_y);
                ImGui::Text(OBFUSCATE_STR("Max: (%.2f, %.2f)"), region_x + region_sz, region_y + region_sz);
                ImVec2 uv0 = ImVec2((region_x) / my_tex_w, (region_y) / my_tex_h);
                ImVec2 uv1 = ImVec2((region_x + region_sz) / my_tex_w, (region_y + region_sz) / my_tex_h);
                ImGui::Image(my_tex_id, ImVec2(region_sz * zoom, region_sz * zoom), uv0, uv1, tint_col, border_col);
                ImGui::EndTooltip();
            }
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Images/Textured buttons"));
        ImGui::TextWrapped(OBFUSCATE_STR("And now some textured buttons.."));
        static int pressed_count = 0;
        for (int i = 0; i < 8; i++)
        {
            ImGui::PushID(i);
            if (i > 0)
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(i - 1.0f, i - 1.0f));
            ImVec2 size = ImVec2(32.0f, 32.0f);                         // Size of the image we want to make visible
            ImVec2 uv0 = ImVec2(0.0f, 0.0f);                            // UV coordinates for lower-left
            ImVec2 uv1 = ImVec2(32.0f / my_tex_w, 32.0f / my_tex_h);    // UV coordinates for (32,32) in our texture
            ImVec4 bg_col = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);             // Black background
            ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);           // No tint
            if (ImGui::ImageButton("", my_tex_id, size, uv0, uv1, bg_col, tint_col))
                pressed_count += 1;
            if (i > 0)
                ImGui::PopStyleVar();
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::Text(OBFUSCATE_STR("Pressed %d times."), pressed_count);
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Combo"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Combo")))
    {
        // Combo Boxes are also called "Dropdown" in other systems
        // Expose flags as checkbox for the demo
        static ImGuiComboFlags flags = 0;
        ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiComboFlags_PopupAlignLeft"), &flags, ImGuiComboFlags_PopupAlignLeft);
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Only makes a difference if the popup is larger than the combo"));
        if (ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiComboFlags_NoArrowButton"), &flags, ImGuiComboFlags_NoArrowButton))
            flags &= ~ImGuiComboFlags_NoPreview;     // Clear the other flag, as we cannot combine both
        if (ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiComboFlags_NoPreview"), &flags, ImGuiComboFlags_NoPreview))
            flags &= ~ImGuiComboFlags_NoArrowButton; // Clear the other flag, as we cannot combine both

        // Using the generic BeginCombo() API, you have full control over how to display the combo contents.
        // (your selection data could be an index, a pointer to the object, an id for the object, a flag intrusively
        // stored in the object itself, etc.)
        const char* items[] = { "AAAA", "BBBB", "CCCC", "DDDD", "EEEE", "FFFF", "GGGG", "HHHH", "IIII", "JJJJ", "KKKK", "LLLLLLL", "MMMM", "OOOOOOO" };
        static int item_current_idx = 0; // Here we store our selection data as an index.
        const char* combo_preview_value = items[item_current_idx];  // Pass in the preview value visible before opening the combo (it could be anything)
        if (ImGui::BeginCombo(OBFUSCATE_STR("combo 1"), combo_preview_value, flags))
        {
            for (int n = 0; n < IM_ARRAYSIZE(items); n++)
            {
                const bool is_selected = (item_current_idx == n);
                if (ImGui::Selectable(items[n], is_selected))
                    item_current_idx = n;

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Simplified one-liner Combo() API, using values packed in a single constant string
        // This is a convenience for when the selection set is small and known at compile-time.
        static int item_current_2 = 0;
        ImGui::Combo(OBFUSCATE_STR("combo 2 (one-liner)"), &item_current_2, OBFUSCATE_STR("aaaa\0bbbb\0cccc\0dddd\0eeee\0\0"));

        // Simplified one-liner Combo() using an array of const char*
        // This is not very useful (may obsolete): prefer using BeginCombo()/EndCombo() for full control.
        static int item_current_3 = -1; // If the selection isn't within 0..count, Combo won't display a preview
        ImGui::Combo(OBFUSCATE_STR("combo 3 (array)"), &item_current_3, items, IM_ARRAYSIZE(items));

        // Simplified one-liner Combo() using an accessor function
        struct Funcs { static bool ItemGetter(void* data, int n, const char** out_str) { *out_str = ((const char**)data)[n]; return true; } };
        static int item_current_4 = 0;
        ImGui::Combo(OBFUSCATE_STR("combo 4 (function)"), &item_current_4, &Funcs::ItemGetter, items, IM_ARRAYSIZE(items));

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/List Boxes"));
    if (ImGui::TreeNode(OBFUSCATE_STR("List boxes")))
    {
        // Using the generic BeginListBox() API, you have full control over how to display the combo contents.
        // (your selection data could be an index, a pointer to the object, an id for the object, a flag intrusively
        // stored in the object itself, etc.)
        const char* items[] = { "AAAA", "BBBB", "CCCC", "DDDD", "EEEE", "FFFF", "GGGG", "HHHH", "IIII", "JJJJ", "KKKK", "LLLLLLL", "MMMM", "OOOOOOO" };
        static int item_current_idx = 0; // Here we store our selection data as an index.
        if (ImGui::BeginListBox(OBFUSCATE_STR("listbox 1")))
        {
            for (int n = 0; n < IM_ARRAYSIZE(items); n++)
            {
                const bool is_selected = (item_current_idx == n);
                if (ImGui::Selectable(items[n], is_selected))
                    item_current_idx = n;

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }

        // Custom size: use all width, 5 items tall
        ImGui::Text(OBFUSCATE_STR("Full-width:"));
        if (ImGui::BeginListBox(OBFUSCATE_STR("##listbox 2"), ImVec2(-FLT_MIN, 5 * ImGui::GetTextLineHeightWithSpacing())))
        {
            for (int n = 0; n < IM_ARRAYSIZE(items); n++)
            {
                const bool is_selected = (item_current_idx == n);
                if (ImGui::Selectable(items[n], is_selected))
                    item_current_idx = n;

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Selectables"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Selectables")))
    {
        // Selectable() has 2 overloads:
        // - The one taking "bool selected" as a read-only selection information.
        //   When Selectable() has been clicked it returns true and you can alter selection state accordingly.
        // - The one taking "bool* p_selected" as a read-write selection information (convenient in some cases)
        // The earlier is more flexible, as in real application your selection may be stored in many different ways
        // and not necessarily inside a bool value (e.g. in flags within objects, as an external list, etc).
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Selectables/Basic"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Basic")))
        {
            static bool selection[5] = { false, true, false, false, false };
            ImGui::Selectable(OBFUSCATE_STR("1. I am selectable"), &selection[0]);
            ImGui::Selectable(OBFUSCATE_STR("2. I am selectable"), &selection[1]);
            ImGui::Text(OBFUSCATE_STR("(I am not selectable)"));
            ImGui::Selectable(OBFUSCATE_STR("4. I am selectable"), &selection[3]);
            if (ImGui::Selectable(OBFUSCATE_STR("5. I am double clickable"), selection[4], ImGuiSelectableFlags_AllowDoubleClick))
                if (ImGui::IsMouseDoubleClicked(0))
                    selection[4] = !selection[4];
            ImGui::TreePop();
        }
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Selectables/Single Selection"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Selection State: Single Selection")))
        {
            static int selected = -1;
            for (int n = 0; n < 5; n++)
            {
                char buf[32];
                sprintf(buf, OBFUSCATE_STR("Object %d"), n);
                if (ImGui::Selectable(buf, selected == n))
                    selected = n;
            }
            ImGui::TreePop();
        }
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Selectables/Multiple Selection"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Selection State: Multiple Selection")))
        {
            HelpMarker(OBFUSCATE_STR("Hold CTRL and click to select multiple items."));
            static bool selection[5] = { false, false, false, false, false };
            for (int n = 0; n < 5; n++)
            {
                char buf[32];
                sprintf(buf, OBFUSCATE_STR("Object %d"), n);
                if (ImGui::Selectable(buf, selection[n]))
                {
                    if (!ImGui::GetIO().KeyCtrl)    // Clear selection when CTRL is not held
                        memset(selection, 0, sizeof(selection));
                    selection[n] ^= 1;
                }
            }
            ImGui::TreePop();
        }
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Selectables/Rendering more text into the same line"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Rendering more text into the same line")))
        {
            // Using the Selectable() override that takes "bool* p_selected" parameter,
            // this function toggle your bool value automatically.
            static bool selected[3] = { false, false, false };
            ImGui::Selectable(OBFUSCATE_STR("main.c"),    &selected[0]); ImGui::SameLine(300); ImGui::Text(OBFUSCATE_STR(" 2,345 bytes"));
            ImGui::Selectable(OBFUSCATE_STR("Hello.cpp"), &selected[1]); ImGui::SameLine(300); ImGui::Text(OBFUSCATE_STR("12,345 bytes"));
            ImGui::Selectable(OBFUSCATE_STR("Hello.h"),   &selected[2]); ImGui::SameLine(300); ImGui::Text(OBFUSCATE_STR(" 2,345 bytes"));
            ImGui::TreePop();
        }
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Selectables/In columns"));
        if (ImGui::TreeNode(OBFUSCATE_STR("In columns")))
        {
            static bool selected[10] = {};

            if (ImGui::BeginTable(OBFUSCATE_STR("split1"), 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
            {
                for (int i = 0; i < 10; i++)
                {
                    char label[32];
                    sprintf(label, OBFUSCATE_STR("Item %d"), i);
                    ImGui::TableNextColumn();
                    ImGui::Selectable(label, &selected[i]); // FIXME-TABLE: Selection overlap
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
            if (ImGui::BeginTable(OBFUSCATE_STR("split2"), 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
            {
                for (int i = 0; i < 10; i++)
                {
                    char label[32];
                    sprintf(label, OBFUSCATE_STR("Item %d"), i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Selectable(label, &selected[i], ImGuiSelectableFlags_SpanAllColumns);
                    ImGui::TableNextColumn();
                    ImGui::Text(OBFUSCATE_STR("Some other contents"));
                    ImGui::TableNextColumn();
                    ImGui::Text(OBFUSCATE_STR("123456"));
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Selectables/Grid"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Grid")))
        {
            static char selected[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };

            // Add in a bit of silly fun...
            const float time = (float)ImGui::GetTime();
            const bool winning_state = memchr(selected, 0, sizeof(selected)) == NULL; // If all cells are selected...
            if (winning_state)
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f + 0.5f * cosf(time * 2.0f), 0.5f + 0.5f * sinf(time * 3.0f)));

            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                {
                    if (x > 0)
                        ImGui::SameLine();
                    ImGui::PushID(y * 4 + x);
                    if (ImGui::Selectable(OBFUSCATE_STR("Sailor"), selected[y][x] != 0, 0, ImVec2(50, 50)))
                    {
                        // Toggle clicked cell + toggle neighbors
                        selected[y][x] ^= 1;
                        if (x > 0) { selected[y][x - 1] ^= 1; }
                        if (x < 3) { selected[y][x + 1] ^= 1; }
                        if (y > 0) { selected[y - 1][x] ^= 1; }
                        if (y < 3) { selected[y + 1][x] ^= 1; }
                    }
                    ImGui::PopID();
                }

            if (winning_state)
                ImGui::PopStyleVar();
            ImGui::TreePop();
        }
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Selectables/Alignment"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Alignment")))
        {
            HelpMarker(OBFUSCATE_STR(
                "By default, Selectables uses style.SelectableTextAlign but it can be overridden on a per-item "
                "basis using PushStyleVar(). You'll probably want to always keep your default situation to "
                "left-align otherwise it becomes difficult to layout multiple items on a same line"));
            static bool selected[3 * 3] = { true, false, true, false, true, false, true, false, true };
            for (int y = 0; y < 3; y++)
            {
                for (int x = 0; x < 3; x++)
                {
                    ImVec2 alignment = ImVec2((float)x / 2.0f, (float)y / 2.0f);
                    char name[32];
                    sprintf(name, OBFUSCATE_STR("(%.1f,%.1f)"), alignment.x, alignment.y);
                    if (x > 0) ImGui::SameLine();
                    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, alignment);
                    ImGui::Selectable(name, &selected[3 * y + x], ImGuiSelectableFlags_None, ImVec2(80, 80));
                    ImGui::PopStyleVar();
                }
            }
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    // To wire InputText() with std::string or any other custom string type,
    // see the "Text Input > Resize Callback" section of this demo, and the misc/cpp/imgui_stdlib.h file.
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Text Input"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Text Input")))
    {
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Text Input/Multi-line Text Input"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Multi-line Text Input")))
        {
            // Note: we are using a fixed-sized buffer for simplicity here. See ImGuiInputTextFlags_CallbackResize
            // and the code in misc/cpp/imgui_stdlib.h for how to setup InputText() for dynamically resizing strings.
            static char text[1024 * 16] =
                "/*\n"
                " The Pentium F00F bug, shorthand for F0 0F C7 C8,\n"
                " the hexadecimal encoding of one offending instruction,\n"
                " more formally, the invalid operand with locked CMPXCHG8B\n"
                " instruction bug, is a design flaw in the majority of\n"
                " Intel Pentium, Pentium MMX, and Pentium OverDrive\n"
                " processors (all in the P5 microarchitecture).\n"
                "*/\n\n"
                "label:\n"
                "\tlock cmpxchg8b eax\n";

            static ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
            HelpMarker(OBFUSCATE_STR("You can use the ImGuiInputTextFlags_CallbackResize facility if you need to wire InputTextMultiline() to a dynamic string type. See misc/cpp/imgui_stdlib.h for an example. (This is not demonstrated in imgui_demo.cpp because we don't want to include <string> in here)"));
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiInputTextFlags_ReadOnly"), &flags, ImGuiInputTextFlags_ReadOnly);
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiInputTextFlags_AllowTabInput"), &flags, ImGuiInputTextFlags_AllowTabInput);
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiInputTextFlags_CtrlEnterForNewLine"), &flags, ImGuiInputTextFlags_CtrlEnterForNewLine);
            ImGui::InputTextMultiline(OBFUSCATE_STR("##source"), text, IM_ARRAYSIZE(text), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 16), flags);
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Text Input/Filtered Text Input"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Filtered Text Input")))
        {
            struct TextFilters
            {
                // Return 0 (pass) if the character is 'i' or 'm' or 'g' or 'u' or 'i'
                static int FilterImGuiLetters(ImGuiInputTextCallbackData* data)
                {
                    if (data->EventChar < 256 && strchr(OBFUSCATE_STR("imgui"), (char)data->EventChar))
                        return 0;
                    return 1;
                }
            };

            static char buf1[64] = ""; ImGui::InputText(OBFUSCATE_STR("default"),     buf1, 64);
            static char buf2[64] = ""; ImGui::InputText(OBFUSCATE_STR("decimal"),     buf2, 64, ImGuiInputTextFlags_CharsDecimal);
            static char buf3[64] = ""; ImGui::InputText(OBFUSCATE_STR("hexadecimal"), buf3, 64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
            static char buf4[64] = ""; ImGui::InputText(OBFUSCATE_STR("uppercase"),   buf4, 64, ImGuiInputTextFlags_CharsUppercase);
            static char buf5[64] = ""; ImGui::InputText(OBFUSCATE_STR("no blank"),    buf5, 64, ImGuiInputTextFlags_CharsNoBlank);
            static char buf6[64] = ""; ImGui::InputText(OBFUSCATE_STR("\"imgui\" letters"), buf6, 64, ImGuiInputTextFlags_CallbackCharFilter, TextFilters::FilterImGuiLetters);
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Text Input/Password input"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Password Input")))
        {
            static char password[64] = "password123";
            ImGui::InputText(OBFUSCATE_STR("password"), password, IM_ARRAYSIZE(password), ImGuiInputTextFlags_Password);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Display all characters as '*'.\nDisable clipboard cut and copy.\nDisable logging.\n"));
            ImGui::InputTextWithHint(OBFUSCATE_STR("password (w/ hint)"), OBFUSCATE_STR("<password>"), password, IM_ARRAYSIZE(password), ImGuiInputTextFlags_Password);
            ImGui::InputText(OBFUSCATE_STR("password (clear)"), password, IM_ARRAYSIZE(password));
            ImGui::TreePop();
        }

        if (ImGui::TreeNode(OBFUSCATE_STR("Completion, History, Edit Callbacks")))
        {
            struct Funcs
            {
                static int MyCallback(ImGuiInputTextCallbackData* data)
                {
                    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
                    {
                        data->InsertChars(data->CursorPos, "..");
                    }
                    else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
                    {
                        if (data->EventKey == ImGuiKey_UpArrow)
                        {
                            data->DeleteChars(0, data->BufTextLen);
                            data->InsertChars(0, OBFUSCATE_STR("Pressed Up!"));
                            data->SelectAll();
                        }
                        else if (data->EventKey == ImGuiKey_DownArrow)
                        {
                            data->DeleteChars(0, data->BufTextLen);
                            data->InsertChars(0, OBFUSCATE_STR("Pressed Down!"));
                            data->SelectAll();
                        }
                    }
                    else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
                    {
                        // Toggle casing of first character
                        char c = data->Buf[0];
                        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) data->Buf[0] ^= 32;
                        data->BufDirty = true;

                        // Increment a counter
                        int* p_int = (int*)data->UserData;
                        *p_int = *p_int + 1;
                    }
                    return 0;
                }
            };
            static char buf1[64];
            ImGui::InputText(OBFUSCATE_STR("Completion"), buf1, 64, ImGuiInputTextFlags_CallbackCompletion, Funcs::MyCallback);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Here we append \"..\" each time Tab is pressed. See 'Examples>Console' for a more meaningful demonstration of using this callback."));

            static char buf2[64];
            ImGui::InputText(OBFUSCATE_STR("History"), buf2, 64, ImGuiInputTextFlags_CallbackHistory, Funcs::MyCallback);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Here we replace and select text each time Up/Down are pressed. See 'Examples>Console' for a more meaningful demonstration of using this callback."));

            static char buf3[64];
            static int edit_count = 0;
            ImGui::InputText(OBFUSCATE_STR("Edit"), buf3, 64, ImGuiInputTextFlags_CallbackEdit, Funcs::MyCallback, (void*)&edit_count);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Here we toggle the casing of the first character on every edit + count edits."));
            ImGui::SameLine(); ImGui::Text(OBFUSCATE_STR("(%d)"), edit_count);

            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Text Input/Resize Callback"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Resize Callback")))
        {
            // To wire InputText() with std::string or any other custom string type,
            // you can use the ImGuiInputTextFlags_CallbackResize flag + create a custom ImGui::InputText() wrapper
            // using your preferred type. See misc/cpp/imgui_stdlib.h for an implementation of this using std::string.
            HelpMarker(OBFUSCATE_STR(
                "Using ImGuiInputTextFlags_CallbackResize to wire your custom string type to InputText().\n\n"
                "See misc/cpp/imgui_stdlib.h for an implementation of this for std::string."));
            struct Funcs
            {
                static int MyResizeCallback(ImGuiInputTextCallbackData* data)
                {
                    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
                    {
                        ImVector<char>* my_str = (ImVector<char>*)data->UserData;
                        IM_ASSERT(my_str->begin() == data->Buf);
                        my_str->resize(data->BufSize); // NB: On resizing calls, generally data->BufSize == data->BufTextLen + 1
                        data->Buf = my_str->begin();
                    }
                    return 0;
                }

                // Note: Because ImGui:: is a namespace you would typically add your own function into the namespace.
                // For example, you code may declare a function 'ImGui::InputText(const char* label, MyString* my_str)'
                static bool MyInputTextMultiline(const char* label, ImVector<char>* my_str, const ImVec2& size = ImVec2(0, 0), ImGuiInputTextFlags flags = 0)
                {
                    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
                    return ImGui::InputTextMultiline(label, my_str->begin(), (size_t)my_str->size(), size, flags | ImGuiInputTextFlags_CallbackResize, Funcs::MyResizeCallback, (void*)my_str);
                }
            };

            // For this demo we are using ImVector as a string container.
            // Note that because we need to store a terminating zero character, our size/capacity are 1 more
            // than usually reported by a typical string class.
            static ImVector<char> my_str;
            if (my_str.empty())
                my_str.push_back(0);
            Funcs::MyInputTextMultiline(OBFUSCATE_STR("##MyStr"), &my_str, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 16));
            ImGui::Text(OBFUSCATE_STR("Data: %p\nSize: %d\nCapacity: %d"), (void*)my_str.begin(), my_str.size(), my_str.capacity());
            ImGui::TreePop();
        }

        ImGui::TreePop();
    }

    // Tabs
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Tabs"));
    if (ImGui::TreeNode("Tabs"))
    {
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Tabs/Basic"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Basic")))
        {
            ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
            if (ImGui::BeginTabBar(OBFUSCATE_STR("MyTabBar"), tab_bar_flags))
            {
                if (ImGui::BeginTabItem(OBFUSCATE_STR("Avocado")))
                {
                    ImGui::Text(OBFUSCATE_STR("This is the Avocado tab!\nblah blah blah blah blah"));
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(OBFUSCATE_STR("Broccoli")))
                {
                    ImGui::Text(OBFUSCATE_STR("This is the Broccoli tab!\nblah blah blah blah blah"));
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(OBFUSCATE_STR("Cucumber")))
                {
                    ImGui::Text(OBFUSCATE_STR("This is the Cucumber tab!\nblah blah blah blah blah"));
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::Separator();
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Tabs/Advanced & Close Button"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Advanced & Close Button")))
        {
            // Expose a couple of the available flags. In most cases you may just call BeginTabBar() with no flags (0).
            static ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_Reorderable;
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTabBarFlags_Reorderable"), &tab_bar_flags, ImGuiTabBarFlags_Reorderable);
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTabBarFlags_AutoSelectNewTabs"), &tab_bar_flags, ImGuiTabBarFlags_AutoSelectNewTabs);
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTabBarFlags_TabListPopupButton"), &tab_bar_flags, ImGuiTabBarFlags_TabListPopupButton);
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTabBarFlags_NoCloseWithMiddleMouseButton"), &tab_bar_flags, ImGuiTabBarFlags_NoCloseWithMiddleMouseButton);
            if ((tab_bar_flags & ImGuiTabBarFlags_FittingPolicyMask_) == 0)
                tab_bar_flags |= ImGuiTabBarFlags_FittingPolicyDefault_;
            if (ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTabBarFlags_FittingPolicyResizeDown"), &tab_bar_flags, ImGuiTabBarFlags_FittingPolicyResizeDown))
                tab_bar_flags &= ~(ImGuiTabBarFlags_FittingPolicyMask_ ^ ImGuiTabBarFlags_FittingPolicyResizeDown);
            if (ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTabBarFlags_FittingPolicyScroll"), &tab_bar_flags, ImGuiTabBarFlags_FittingPolicyScroll))
                tab_bar_flags &= ~(ImGuiTabBarFlags_FittingPolicyMask_ ^ ImGuiTabBarFlags_FittingPolicyScroll);

            // Tab Bar
            const char* names[4] = { "Artichoke", "Beetroot", "Celery", "Daikon" };
            static bool opened[4] = { true, true, true, true }; // Persistent user state
            for (int n = 0; n < IM_ARRAYSIZE(opened); n++)
            {
                if (n > 0) { ImGui::SameLine(); }
                ImGui::Checkbox(names[n], &opened[n]);
            }

            // Passing a bool* to BeginTabItem() is similar to passing one to Begin():
            // the underlying bool will be set to false when the tab is closed.
            if (ImGui::BeginTabBar(OBFUSCATE_STR("MyTabBar"), tab_bar_flags))
            {
                for (int n = 0; n < IM_ARRAYSIZE(opened); n++)
                    if (opened[n] && ImGui::BeginTabItem(names[n], &opened[n], ImGuiTabItemFlags_None))
                    {
                        ImGui::Text(OBFUSCATE_STR("This is the %s tab!"), names[n]);
                        if (n & 1)
                            ImGui::Text(OBFUSCATE_STR("I am an odd tab."));
                        ImGui::EndTabItem();
                    }
                ImGui::EndTabBar();
            }
            ImGui::Separator();
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Tabs/TabItemButton & Leading-Trailing flags"));
        if (ImGui::TreeNode(OBFUSCATE_STR("TabItemButton & Leading/Trailing flags")))
        {
            static ImVector<int> active_tabs;
            static int next_tab_id = 0;
            if (next_tab_id == 0) // Initialize with some default tabs
                for (int i = 0; i < 3; i++)
                    active_tabs.push_back(next_tab_id++);

            // TabItemButton() and Leading/Trailing flags are distinct features which we will demo together.
            // (It is possible to submit regular tabs with Leading/Trailing flags, or TabItemButton tabs without Leading/Trailing flags...
            // but they tend to make more sense together)
            static bool show_leading_button = true;
            static bool show_trailing_button = true;
            ImGui::Checkbox(OBFUSCATE_STR("Show Leading TabItemButton()"), &show_leading_button);
            ImGui::Checkbox(OBFUSCATE_STR("Show Trailing TabItemButton()"), &show_trailing_button);

            // Expose some other flags which are useful to showcase how they interact with Leading/Trailing tabs
            static ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyResizeDown;
            ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTabBarFlags_TabListPopupButton"), &tab_bar_flags, ImGuiTabBarFlags_TabListPopupButton);
            if (ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTabBarFlags_FittingPolicyResizeDown"), &tab_bar_flags, ImGuiTabBarFlags_FittingPolicyResizeDown))
                tab_bar_flags &= ~(ImGuiTabBarFlags_FittingPolicyMask_ ^ ImGuiTabBarFlags_FittingPolicyResizeDown);
            if (ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiTabBarFlags_FittingPolicyScroll"), &tab_bar_flags, ImGuiTabBarFlags_FittingPolicyScroll))
                tab_bar_flags &= ~(ImGuiTabBarFlags_FittingPolicyMask_ ^ ImGuiTabBarFlags_FittingPolicyScroll);

            if (ImGui::BeginTabBar(OBFUSCATE_STR("MyTabBar"), tab_bar_flags))
            {
                // Demo a Leading TabItemButton(): click the "?" button to open a menu
                if (show_leading_button)
                    if (ImGui::TabItemButton(OBFUSCATE_STR("?"), ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_NoTooltip))
                        ImGui::OpenPopup(OBFUSCATE_STR("MyHelpMenu"));
                if (ImGui::BeginPopup("MyHelpMenu"))
                {
                    ImGui::Selectable(OBFUSCATE_STR("Hello!"));
                    ImGui::EndPopup();
                }

                // Demo Trailing Tabs: click the "+" button to add a new tab (in your app you may want to use a font icon instead of the "+")
                // Note that we submit it before the regular tabs, but because of the ImGuiTabItemFlags_Trailing flag it will always appear at the end.
                if (show_trailing_button)
                    if (ImGui::TabItemButton(OBFUSCATE_STR("+"), ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
                        active_tabs.push_back(next_tab_id++); // Add new tab

                // Submit our regular tabs
                for (int n = 0; n < active_tabs.Size; )
                {
                    bool open = true;
                    char name[16];
                    snprintf(name, IM_ARRAYSIZE(name), OBFUSCATE_STR("%04d"), active_tabs[n]);
                    if (ImGui::BeginTabItem(name, &open, ImGuiTabItemFlags_None))
                    {
                        ImGui::Text(OBFUSCATE_STR("This is the %s tab!"), name);
                        ImGui::EndTabItem();
                    }

                    if (!open)
                        active_tabs.erase(active_tabs.Data + n);
                    else
                        n++;
                }

                ImGui::EndTabBar();
            }
            ImGui::Separator();
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    // Plot/Graph widgets are not very good.
    // Consider using a third-party library such as ImPlot: https://github.com/epezent/implot
    // (see others https://github.com/ocornut/imgui/wiki/Useful-Extensions)
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Plotting"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Plotting")))
    {
        static bool animate = true;
        ImGui::Checkbox(OBFUSCATE_STR("Animate"), &animate);

        // Plot as lines and plot as histogram
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Plotting/PlotLines, PlotHistogram"));
        static float arr[] = { 0.6f, 0.1f, 1.0f, 0.5f, 0.92f, 0.1f, 0.2f };
        ImGui::PlotLines(OBFUSCATE_STR("Frame Times"), arr, IM_ARRAYSIZE(arr));
        ImGui::PlotHistogram(OBFUSCATE_STR("Histogram"), arr, IM_ARRAYSIZE(arr), 0, NULL, 0.0f, 1.0f, ImVec2(0, 80.0f));

        // Fill an array of contiguous float values to plot
        // Tip: If your float aren't contiguous but part of a structure, you can pass a pointer to your first float
        // and the sizeof() of your structure in the "stride" parameter.
        static float values[90] = {};
        static int values_offset = 0;
        static double refresh_time = 0.0;
        if (!animate || refresh_time == 0.0)
            refresh_time = ImGui::GetTime();
        while (refresh_time < ImGui::GetTime()) // Create data at fixed 60 Hz rate for the demo
        {
            static float phase = 0.0f;
            values[values_offset] = cosf(phase);
            values_offset = (values_offset + 1) % IM_ARRAYSIZE(values);
            phase += 0.10f * values_offset;
            refresh_time += 1.0f / 60.0f;
        }

        // Plots can display overlay texts
        // (in this example, we will display an average value)
        {
            float average = 0.0f;
            for (int n = 0; n < IM_ARRAYSIZE(values); n++)
                average += values[n];
            average /= (float)IM_ARRAYSIZE(values);
            char overlay[32];
            sprintf(overlay, OBFUSCATE_STR("avg %f"), average);
            ImGui::PlotLines(OBFUSCATE_STR("Lines"), values, IM_ARRAYSIZE(values), values_offset, overlay, -1.0f, 1.0f, ImVec2(0, 80.0f));
        }

        // Use functions to generate output
        // FIXME: This is rather awkward because current plot API only pass in indices.
        // We probably want an API passing floats and user provide sample rate/count.
        struct Funcs
        {
            static float Sin(void*, int i) { return sinf(i * 0.1f); }
            static float Saw(void*, int i) { return (i & 1) ? 1.0f : -1.0f; }
        };
        static int func_type = 0, display_count = 70;
        ImGui::Separator();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::Combo(OBFUSCATE_STR("func"), &func_type, OBFUSCATE_STR("Sin\0Saw\0"));
        ImGui::SameLine();
        ImGui::SliderInt(OBFUSCATE_STR("Sample count"), &display_count, 1, 400);
        float (*func)(void*, int) = (func_type == 0) ? Funcs::Sin : Funcs::Saw;
        ImGui::PlotLines(OBFUSCATE_STR("Lines"), func, NULL, display_count, 0, NULL, -1.0f, 1.0f, ImVec2(0, 80));
        ImGui::PlotHistogram(OBFUSCATE_STR("Histogram"), func, NULL, display_count, 0, NULL, -1.0f, 1.0f, ImVec2(0, 80));
        ImGui::Separator();

        // Animate a simple progress bar
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Plotting/ProgressBar"));
        static float progress = 0.0f, progress_dir = 1.0f;
        if (animate)
        {
            progress += progress_dir * 0.4f * ImGui::GetIO().DeltaTime;
            if (progress >= +1.1f) { progress = +1.1f; progress_dir *= -1.0f; }
            if (progress <= -0.1f) { progress = -0.1f; progress_dir *= -1.0f; }
        }

        // Typically we would use ImVec2(-1.0f,0.0f) or ImVec2(-FLT_MIN,0.0f) to use all available width,
        // or ImVec2(width,0.0f) for a specified width. ImVec2(0.0f,0.0f) uses ItemWidth.
        ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Text(OBFUSCATE_STR("Progress Bar"));

        float progress_saturated = IM_CLAMP(progress, 0.0f, 1.0f);
        char buf[32];
        sprintf(buf, OBFUSCATE_STR("%d/%d"), (int)(progress_saturated * 1753), 1753);
        ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), buf);
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Color"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Color/Picker Widgets")))
    {
        static ImVec4 color = ImVec4(114.0f / 255.0f, 144.0f / 255.0f, 154.0f / 255.0f, 200.0f / 255.0f);

        static bool alpha_preview = true;
        static bool alpha_half_preview = false;
        static bool drag_and_drop = true;
        static bool options_menu = true;
        static bool hdr = false;
        ImGui::Checkbox(OBFUSCATE_STR("With Alpha Preview"), &alpha_preview);
        ImGui::Checkbox(OBFUSCATE_STR("With Half Alpha Preview"), &alpha_half_preview);
        ImGui::Checkbox(OBFUSCATE_STR("With Drag and Drop"), &drag_and_drop);
        ImGui::Checkbox(OBFUSCATE_STR("With Options Menu"), &options_menu); ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Right-click on the individual color widget to show options."));
        ImGui::Checkbox(OBFUSCATE_STR("With HDR"), &hdr); ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Currently all this does is to lift the 0..1 limits on dragging widgets."));
        ImGuiColorEditFlags misc_flags = (hdr ? ImGuiColorEditFlags_HDR : 0) | (drag_and_drop ? 0 : ImGuiColorEditFlags_NoDragDrop) | (alpha_half_preview ? ImGuiColorEditFlags_AlphaPreviewHalf : (alpha_preview ? ImGuiColorEditFlags_AlphaPreview : 0)) | (options_menu ? 0 : ImGuiColorEditFlags_NoOptions);

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Color/ColorEdit"));
        ImGui::Text(OBFUSCATE_STR("Color widget:"));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
            "Click on the color square to open a color picker.\n"
            "CTRL+click on individual component to input value.\n"));
        ImGui::ColorEdit3(OBFUSCATE_STR("MyColor##1"), (float*)&color, misc_flags);

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Color/ColorEdit (HSV, with Alpha)"));
        ImGui::Text(OBFUSCATE_STR("Color widget HSV with Alpha:"));
        ImGui::ColorEdit4(OBFUSCATE_STR("MyColor##2"), (float*)&color, ImGuiColorEditFlags_DisplayHSV | misc_flags);

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Color/ColorEdit (float display)"));
        ImGui::Text(OBFUSCATE_STR("Color widget with Float Display:"));
        ImGui::ColorEdit4(OBFUSCATE_STR("MyColor##2f"), (float*)&color, ImGuiColorEditFlags_Float | misc_flags);

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Color/ColorButton (with Picker)"));
        ImGui::Text(OBFUSCATE_STR("Color button with Picker:"));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
            "With the ImGuiColorEditFlags_NoInputs flag you can hide all the slider/text inputs.\n"
            "With the ImGuiColorEditFlags_NoLabel flag you can pass a non-empty label which will only "
            "be used for the tooltip and picker popup."));
        ImGui::ColorEdit4(OBFUSCATE_STR("MyColor##3"), (float*)&color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | misc_flags);

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Color/ColorButton (with custom Picker popup)"));
        ImGui::Text(OBFUSCATE_STR("Color button with Custom Picker Popup:"));

        // Generate a default palette. The palette will persist and can be edited.
        static bool saved_palette_init = true;
        static ImVec4 saved_palette[32] = {};
        if (saved_palette_init)
        {
            for (int n = 0; n < IM_ARRAYSIZE(saved_palette); n++)
            {
                ImGui::ColorConvertHSVtoRGB(n / 31.0f, 0.8f, 0.8f,
                    saved_palette[n].x, saved_palette[n].y, saved_palette[n].z);
                saved_palette[n].w = 1.0f; // Alpha
            }
            saved_palette_init = false;
        }

        static ImVec4 backup_color;
        bool open_popup = ImGui::ColorButton(OBFUSCATE_STR("MyColor##3b"), color, misc_flags);
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        open_popup |= ImGui::Button(OBFUSCATE_STR("Palette"));
        if (open_popup)
        {
            ImGui::OpenPopup(OBFUSCATE_STR("mypicker"));
            backup_color = color;
        }
        if (ImGui::BeginPopup(OBFUSCATE_STR("mypicker")))
        {
            ImGui::Text(OBFUSCATE_STR("MY CUSTOM COLOR PICKER WITH AN AMAZING PALETTE!"));
            ImGui::Separator();
            ImGui::ColorPicker4(OBFUSCATE_STR("##picker"), (float*)&color, misc_flags | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
            ImGui::SameLine();

            ImGui::BeginGroup(); // Lock X position
            ImGui::Text(OBFUSCATE_STR("Current"));
            ImGui::ColorButton(OBFUSCATE_STR("##current"), color, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(60, 40));
            ImGui::Text(OBFUSCATE_STR("Previous"));
            if (ImGui::ColorButton(OBFUSCATE_STR("##previous"), backup_color, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(60, 40)))
                color = backup_color;
            ImGui::Separator();
            ImGui::Text(OBFUSCATE_STR("Palette"));
            for (int n = 0; n < IM_ARRAYSIZE(saved_palette); n++)
            {
                ImGui::PushID(n);
                if ((n % 8) != 0)
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.y);

                ImGuiColorEditFlags palette_button_flags = ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip;
                if (ImGui::ColorButton(OBFUSCATE_STR("##palette"), saved_palette[n], palette_button_flags, ImVec2(20, 20)))
                    color = ImVec4(saved_palette[n].x, saved_palette[n].y, saved_palette[n].z, color.w); // Preserve alpha!

                // Allow user to drop colors into each palette entry. Note that ColorButton() is already a
                // drag source by default, unless specifying the ImGuiColorEditFlags_NoDragDrop flag.
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F))
                        memcpy((float*)&saved_palette[n], payload->Data, sizeof(float) * 3);
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F))
                        memcpy((float*)&saved_palette[n], payload->Data, sizeof(float) * 4);
                    ImGui::EndDragDropTarget();
                }

                ImGui::PopID();
            }
            ImGui::EndGroup();
            ImGui::EndPopup();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Color/ColorButton (simple)"));
        ImGui::Text(OBFUSCATE_STR("Color button only:"));
        static bool no_border = false;
        ImGui::Checkbox(OBFUSCATE_STR("ImGuiColorEditFlags_NoBorder"), &no_border);
        ImGui::ColorButton(OBFUSCATE_STR("MyColor##3c"), *(ImVec4*)&color, misc_flags | (no_border ? ImGuiColorEditFlags_NoBorder : 0), ImVec2(80, 80));

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Color/ColorPicker"));
        ImGui::Text(OBFUSCATE_STR("Color picker:"));
        static bool alpha = true;
        static bool alpha_bar = true;
        static bool side_preview = true;
        static bool ref_color = false;
        static ImVec4 ref_color_v(1.0f, 0.0f, 1.0f, 0.5f);
        static int display_mode = 0;
        static int picker_mode = 0;
        ImGui::Checkbox(OBFUSCATE_STR("With Alpha"), &alpha);
        ImGui::Checkbox(OBFUSCATE_STR("With Alpha Bar"), &alpha_bar);
        ImGui::Checkbox(OBFUSCATE_STR("With Side Preview"), &side_preview);
        if (side_preview)
        {
            ImGui::SameLine();
            ImGui::Checkbox(OBFUSCATE_STR("With Ref Color"), &ref_color);
            if (ref_color)
            {
                ImGui::SameLine();
                ImGui::ColorEdit4(OBFUSCATE_STR("##RefColor"), &ref_color_v.x, ImGuiColorEditFlags_NoInputs | misc_flags);
            }
        }
        ImGui::Combo(OBFUSCATE_STR("Display Mode"), &display_mode, OBFUSCATE_STR("Auto/Current\0None\0RGB Only\0HSV Only\0Hex Only\0"));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
            "ColorEdit defaults to displaying RGB inputs if you don't specify a display mode, "
            "but the user can change it with a right-click on those inputs.\n\nColorPicker defaults to displaying RGB+HSV+Hex "
            "if you don't specify a display mode.\n\nYou can change the defaults using SetColorEditOptions()."));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("When not specified explicitly (Auto/Current mode), user can right-click the picker to change mode."));
        ImGuiColorEditFlags flags = misc_flags;
        if (!alpha)            flags |= ImGuiColorEditFlags_NoAlpha;        // This is by default if you call ColorPicker3() instead of ColorPicker4()
        if (alpha_bar)         flags |= ImGuiColorEditFlags_AlphaBar;
        if (!side_preview)     flags |= ImGuiColorEditFlags_NoSidePreview;
        if (picker_mode == 1)  flags |= ImGuiColorEditFlags_PickerHueBar;
        if (picker_mode == 2)  flags |= ImGuiColorEditFlags_PickerHueWheel;
        if (display_mode == 1) flags |= ImGuiColorEditFlags_NoInputs;       // Disable all RGB/HSV/Hex displays
        if (display_mode == 2) flags |= ImGuiColorEditFlags_DisplayRGB;     // Override display mode
        if (display_mode == 3) flags |= ImGuiColorEditFlags_DisplayHSV;
        if (display_mode == 4) flags |= ImGuiColorEditFlags_DisplayHex;
        ImGui::ColorPicker4(OBFUSCATE_STR("MyColor##4"), (float*)&color, flags, ref_color ? &ref_color_v.x : NULL);

        ImGui::Text(OBFUSCATE_STR("Set defaults in code:"));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
            "SetColorEditOptions() is designed to allow you to set boot-time default.\n"
            "We don't have Push/Pop functions because you can force options on a per-widget basis if needed,"
            "and the user can change non-forced ones with the options menu.\nWe don't have a getter to avoid"
            "encouraging you to persistently save values that aren't forward-compatible."));
        if (ImGui::Button(OBFUSCATE_STR("Default: Uint8 + HSV + Hue Bar")))
            ImGui::SetColorEditOptions(ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_PickerHueBar);
        if (ImGui::Button(OBFUSCATE_STR("Default: Float + HDR + Hue Wheel")))
            ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_PickerHueWheel);

        // Always both a small version of both types of pickers (to make it more visible in the demo to people who are skimming quickly through it)
        ImGui::Text(OBFUSCATE_STR("Both types:"));
        float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.y) * 0.40f;
        ImGui::SetNextItemWidth(w);
        ImGui::ColorPicker3(OBFUSCATE_STR("##MyColor##5"), (float*)&color, ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w);
        ImGui::ColorPicker3(OBFUSCATE_STR("##MyColor##6"), (float*)&color, ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);

        // HSV encoded support (to avoid RGB<>HSV round trips and singularities when S==0 or V==0)
        static ImVec4 color_hsv(0.23f, 1.0f, 1.0f, 1.0f); // Stored as HSV!
        ImGui::Spacing();
        ImGui::Text(OBFUSCATE_STR("HSV encoded colors"));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
            "By default, colors are given to ColorEdit and ColorPicker in RGB, but ImGuiColorEditFlags_InputHSV"
            "allows you to store colors as HSV and pass them to ColorEdit and ColorPicker as HSV. This comes with the"
            "added benefit that you can manipulate hue values with the picker even when saturation or value are zero."));
        ImGui::Text(OBFUSCATE_STR("Color widget with InputHSV:"));
        ImGui::ColorEdit4(OBFUSCATE_STR("HSV shown as RGB##1"), (float*)&color_hsv, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputHSV | ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4(OBFUSCATE_STR("HSV shown as HSV##1"), (float*)&color_hsv, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_InputHSV | ImGuiColorEditFlags_Float);
        ImGui::DragFloat4(OBFUSCATE_STR("Raw HSV values"), (float*)&color_hsv, 0.01f, 0.0f, 1.0f);

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Drag and Slider Flags"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Drag/Slider Flags")))
    {
        // Demonstrate using advanced flags for DragXXX and SliderXXX functions. Note that the flags are the same!
        static ImGuiSliderFlags flags = ImGuiSliderFlags_None;
        ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiSliderFlags_AlwaysClamp"), &flags, ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Always clamp value to min/max bounds (if any) when input manually with CTRL+Click."));
        ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiSliderFlags_Logarithmic"), &flags, ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Enable logarithmic editing (more precision for small values)."));
        ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiSliderFlags_NoRoundToFormat"), &flags, ImGuiSliderFlags_NoRoundToFormat);
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Disable rounding underlying value to match precision of the format string (e.g. %.3f values are rounded to those 3 digits)."));
        ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiSliderFlags_NoInput"), &flags, ImGuiSliderFlags_NoInput);
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Disable CTRL+Click or Enter key allowing to input text directly into the widget."));

        // Drags
        static float drag_f = 0.5f;
        static int drag_i = 50;
        ImGui::Text(OBFUSCATE_STR("Underlying float value: %f"), drag_f);
        ImGui::DragFloat(OBFUSCATE_STR("DragFloat (0 -> 1)"), &drag_f, 0.005f, 0.0f, 1.0f, OBFUSCATE_STR("%.3f"), flags);
        ImGui::DragFloat(OBFUSCATE_STR("DragFloat (0 -> +inf)"), &drag_f, 0.005f, 0.0f, FLT_MAX, OBFUSCATE_STR("%.3f"), flags);
        ImGui::DragFloat(OBFUSCATE_STR("DragFloat (-inf -> 1)"), &drag_f, 0.005f, -FLT_MAX, 1.0f, OBFUSCATE_STR("%.3f"), flags);
        ImGui::DragFloat(OBFUSCATE_STR("DragFloat (-inf -> +inf)"), &drag_f, 0.005f, -FLT_MAX, +FLT_MAX, OBFUSCATE_STR("%.3f"), flags);
        ImGui::DragInt(OBFUSCATE_STR("DragInt (0 -> 100)"), &drag_i, 0.5f, 0, 100, OBFUSCATE_STR("%d"), flags);

        // Sliders
        static float slider_f = 0.5f;
        static int slider_i = 50;
        ImGui::Text(OBFUSCATE_STR("Underlying float value: %f"), slider_f);
        ImGui::SliderFloat(OBFUSCATE_STR("SliderFloat (0 -> 1)"), &slider_f, 0.0f, 1.0f, OBFUSCATE_STR("%.3f"), flags);
        ImGui::SliderInt(OBFUSCATE_STR("SliderInt (0 -> 100)"), &slider_i, 0, 100, OBFUSCATE_STR("%d"), flags);

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Range Widgets"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Range Widgets")))
    {
        static float begin = 10, end = 90;
        static int begin_i = 100, end_i = 1000;
        ImGui::DragFloatRange2(OBFUSCATE_STR("range float"), &begin, &end, 0.25f, 0.0f, 100.0f, OBFUSCATE_STR("Min: %.1f %%"), OBFUSCATE_STR("Max: %.1f %%"), ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragIntRange2(OBFUSCATE_STR("range int"), &begin_i, &end_i, 5, 0, 1000, OBFUSCATE_STR("Min: %d units"), OBFUSCATE_STR("Max: %d units"));
        ImGui::DragIntRange2(OBFUSCATE_STR("range int (no bounds)"), &begin_i, &end_i, 5, 0, 0, OBFUSCATE_STR("Min: %d units"), OBFUSCATE_STR("Max: %d units"));
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Data Types"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Data Types")))
    {
        #ifndef LLONG_MIN
        ImS64 LLONG_MIN = -9223372036854775807LL - 1;
        ImS64 LLONG_MAX = 9223372036854775807LL;
        ImU64 ULLONG_MAX = (2ULL * 9223372036854775807LL + 1);
        #endif
        const char    s8_zero  = 0,   s8_one  = 1,   s8_fifty  = 50, s8_min  = -128,        s8_max = 127;
        const ImU8    u8_zero  = 0,   u8_one  = 1,   u8_fifty  = 50, u8_min  = 0,           u8_max = 255;
        const short   s16_zero = 0,   s16_one = 1,   s16_fifty = 50, s16_min = -32768,      s16_max = 32767;
        const ImU16   u16_zero = 0,   u16_one = 1,   u16_fifty = 50, u16_min = 0,           u16_max = 65535;
        const ImS32   s32_zero = 0,   s32_one = 1,   s32_fifty = 50, s32_min = INT_MIN/2,   s32_max = INT_MAX/2,    s32_hi_a = INT_MAX/2 - 100,    s32_hi_b = INT_MAX/2;
        const ImU32   u32_zero = 0,   u32_one = 1,   u32_fifty = 50, u32_min = 0,           u32_max = UINT_MAX/2,   u32_hi_a = UINT_MAX/2 - 100,   u32_hi_b = UINT_MAX/2;
        const ImS64   s64_zero = 0,   s64_one = 1,   s64_fifty = 50, s64_min = LLONG_MIN/2, s64_max = LLONG_MAX/2,  s64_hi_a = LLONG_MAX/2 - 100,  s64_hi_b = LLONG_MAX/2;
        const ImU64   u64_zero = 0,   u64_one = 1,   u64_fifty = 50, u64_min = 0,           u64_max = ULLONG_MAX/2, u64_hi_a = ULLONG_MAX/2 - 100, u64_hi_b = ULLONG_MAX/2;
        const float   f32_zero = 0.f, f32_one = 1.f, f32_lo_a = -10000000000.0f, f32_hi_a = +10000000000.0f;
        const double  f64_zero = 0.,  f64_one = 1.,  f64_lo_a = -1000000000000000.0, f64_hi_a = +1000000000000000.0;

        // State
        static char   s8_v  = 127;
        static ImU8   u8_v  = 255;
        static short  s16_v = 32767;
        static ImU16  u16_v = 65535;
        static ImS32  s32_v = -1;
        static ImU32  u32_v = (ImU32)-1;
        static ImS64  s64_v = -1;
        static ImU64  u64_v = (ImU64)-1;
        static float  f32_v = 0.123f;
        static double f64_v = 90000.01234567890123456789;

        const float drag_speed = 0.2f;
        static bool drag_clamp = false;
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Data Types/Drags"));
        ImGui::Text(OBFUSCATE_STR("Drags:"));
        ImGui::Checkbox(OBFUSCATE_STR("Clamp integers to 0..50"), &drag_clamp);
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
            "As with every widget in dear imgui, we never modify values unless there is a user interaction.\n"
            "You can override the clamping limits by using CTRL+Click to input a value."));
        ImGui::DragScalar(OBFUSCATE_STR("drag s8"),        ImGuiDataType_S8,     &s8_v,  drag_speed, drag_clamp ? &s8_zero  : NULL, drag_clamp ? &s8_fifty  : NULL);
        ImGui::DragScalar(OBFUSCATE_STR("drag u8"),        ImGuiDataType_U8,     &u8_v,  drag_speed, drag_clamp ? &u8_zero  : NULL, drag_clamp ? &u8_fifty  : NULL, OBFUSCATE_STR("%u ms"));
        ImGui::DragScalar(OBFUSCATE_STR("drag s16"),       ImGuiDataType_S16,    &s16_v, drag_speed, drag_clamp ? &s16_zero : NULL, drag_clamp ? &s16_fifty : NULL);
        ImGui::DragScalar(OBFUSCATE_STR("drag u16"),       ImGuiDataType_U16,    &u16_v, drag_speed, drag_clamp ? &u16_zero : NULL, drag_clamp ? &u16_fifty : NULL, OBFUSCATE_STR("%u ms"));
        ImGui::DragScalar(OBFUSCATE_STR("drag s32"),       ImGuiDataType_S32,    &s32_v, drag_speed, drag_clamp ? &s32_zero : NULL, drag_clamp ? &s32_fifty : NULL);
        ImGui::DragScalar(OBFUSCATE_STR("drag s32 hex"),   ImGuiDataType_S32,    &s32_v, drag_speed, drag_clamp ? &s32_zero : NULL, drag_clamp ? &s32_fifty : NULL, OBFUSCATE_STR("0x%08X"));
        ImGui::DragScalar(OBFUSCATE_STR("drag u32"),       ImGuiDataType_U32,    &u32_v, drag_speed, drag_clamp ? &u32_zero : NULL, drag_clamp ? &u32_fifty : NULL, OBFUSCATE_STR("%u ms"));
        ImGui::DragScalar(OBFUSCATE_STR("drag s64"),       ImGuiDataType_S64,    &s64_v, drag_speed, drag_clamp ? &s64_zero : NULL, drag_clamp ? &s64_fifty : NULL);
        ImGui::DragScalar(OBFUSCATE_STR("drag u64"),       ImGuiDataType_U64,    &u64_v, drag_speed, drag_clamp ? &u64_zero : NULL, drag_clamp ? &u64_fifty : NULL);
        ImGui::DragScalar(OBFUSCATE_STR("drag float"),     ImGuiDataType_Float,  &f32_v, 0.005f,  &f32_zero, &f32_one, OBFUSCATE_STR("%f"));
        ImGui::DragScalar(OBFUSCATE_STR("drag float log"), ImGuiDataType_Float,  &f32_v, 0.005f,  &f32_zero, &f32_one, OBFUSCATE_STR("%f"), ImGuiSliderFlags_Logarithmic);
        ImGui::DragScalar(OBFUSCATE_STR("drag double"),    ImGuiDataType_Double, &f64_v, 0.0005f, &f64_zero, NULL, OBFUSCATE_STR("%.10f grams"));
        ImGui::DragScalar(OBFUSCATE_STR("drag double log"),ImGuiDataType_Double, &f64_v, 0.0005f, &f64_zero, &f64_one, OBFUSCATE_STR("0 < %.10f < 1"), ImGuiSliderFlags_Logarithmic);

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Data Types/Sliders"));
        ImGui::Text(OBFUSCATE_STR("Sliders"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider s8 full"),       ImGuiDataType_S8,     &s8_v,  &s8_min,   &s8_max, OBFUSCATE_STR("%d"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider u8 full"),       ImGuiDataType_U8,     &u8_v,  &u8_min,   &u8_max, OBFUSCATE_STR("%u"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider s16 full"),      ImGuiDataType_S16,    &s16_v, &s16_min,  &s16_max, OBFUSCATE_STR("%d"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider u16 full"),      ImGuiDataType_U16,    &u16_v, &u16_min,  &u16_max, OBFUSCATE_STR("%u"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider s32 low"),       ImGuiDataType_S32,    &s32_v, &s32_zero, &s32_fifty, OBFUSCATE_STR("%d"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider s32 high"),      ImGuiDataType_S32,    &s32_v, &s32_hi_a, &s32_hi_b, OBFUSCATE_STR("%d"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider s32 full"),      ImGuiDataType_S32,    &s32_v, &s32_min,  &s32_max, OBFUSCATE_STR("%d"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider s32 hex"),       ImGuiDataType_S32,    &s32_v, &s32_zero, &s32_fifty, OBFUSCATE_STR("0x%04X"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider u32 low"),       ImGuiDataType_U32,    &u32_v, &u32_zero, &u32_fifty, OBFUSCATE_STR("%u"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider u32 high"),      ImGuiDataType_U32,    &u32_v, &u32_hi_a, &u32_hi_b, OBFUSCATE_STR("%u"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider u32 full"),      ImGuiDataType_U32,    &u32_v, &u32_min,  &u32_max, OBFUSCATE_STR("%u"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider s64 low"),       ImGuiDataType_S64,    &s64_v, &s64_zero, &s64_fifty,"%" IM_PRId64);
        ImGui::SliderScalar(OBFUSCATE_STR("slider s64 high"),      ImGuiDataType_S64,    &s64_v, &s64_hi_a, &s64_hi_b, "%" IM_PRId64);
        ImGui::SliderScalar(OBFUSCATE_STR("slider s64 full"),      ImGuiDataType_S64,    &s64_v, &s64_min,  &s64_max,  "%" IM_PRId64);
        ImGui::SliderScalar(OBFUSCATE_STR("slider u64 low"),       ImGuiDataType_U64,    &u64_v, &u64_zero, &u64_fifty,"%" IM_PRIu64 " ms");
        ImGui::SliderScalar(OBFUSCATE_STR("slider u64 high"),      ImGuiDataType_U64,    &u64_v, &u64_hi_a, &u64_hi_b, "%" IM_PRIu64 " ms");
        ImGui::SliderScalar(OBFUSCATE_STR("slider u64 full"),      ImGuiDataType_U64,    &u64_v, &u64_min,  &u64_max,  "%" IM_PRIu64 " ms");
        ImGui::SliderScalar(OBFUSCATE_STR("slider float low"),     ImGuiDataType_Float,  &f32_v, &f32_zero, &f32_one);
        ImGui::SliderScalar(OBFUSCATE_STR("slider float low log"), ImGuiDataType_Float,  &f32_v, &f32_zero, &f32_one, OBFUSCATE_STR("%.10f"), ImGuiSliderFlags_Logarithmic);
        ImGui::SliderScalar(OBFUSCATE_STR("slider float high"),    ImGuiDataType_Float,  &f32_v, &f32_lo_a, &f32_hi_a, OBFUSCATE_STR("%e"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider double low"),    ImGuiDataType_Double, &f64_v, &f64_zero, &f64_one, OBFUSCATE_STR("%.10f grams"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider double low log"),ImGuiDataType_Double, &f64_v, &f64_zero, &f64_one, OBFUSCATE_STR("%.10f"), ImGuiSliderFlags_Logarithmic);
        ImGui::SliderScalar(OBFUSCATE_STR("slider double high"),   ImGuiDataType_Double, &f64_v, &f64_lo_a, &f64_hi_a, OBFUSCATE_STR("%e grams"));

        ImGui::Text(OBFUSCATE_STR("Sliders (reverse)"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider s8 reverse"),    ImGuiDataType_S8,   &s8_v,  &s8_max,    &s8_min, OBFUSCATE_STR("%d"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider u8 reverse"),    ImGuiDataType_U8,   &u8_v,  &u8_max,    &u8_min, OBFUSCATE_STR("%u"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider s32 reverse"),   ImGuiDataType_S32,  &s32_v, &s32_fifty, &s32_zero, OBFUSCATE_STR("%d"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider u32 reverse"),   ImGuiDataType_U32,  &u32_v, &u32_fifty, &u32_zero, OBFUSCATE_STR("%u"));
        ImGui::SliderScalar(OBFUSCATE_STR("slider s64 reverse"),   ImGuiDataType_S64,  &s64_v, &s64_fifty, &s64_zero, "%" IM_PRId64);
        ImGui::SliderScalar(OBFUSCATE_STR("slider u64 reverse"),   ImGuiDataType_U64,  &u64_v, &u64_fifty, &u64_zero, "%" IM_PRIu64 " ms");

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Data Types/Inputs"));
        static bool inputs_step = true;
        ImGui::Text(OBFUSCATE_STR("Inputs"));
        ImGui::Checkbox(OBFUSCATE_STR("Show step buttons"), &inputs_step);
        ImGui::InputScalar(OBFUSCATE_STR("input s8"),      ImGuiDataType_S8,     &s8_v,  inputs_step ? &s8_one  : NULL, NULL, OBFUSCATE_STR("%d"));
        ImGui::InputScalar(OBFUSCATE_STR("input u8"),      ImGuiDataType_U8,     &u8_v,  inputs_step ? &u8_one  : NULL, NULL, OBFUSCATE_STR("%u"));
        ImGui::InputScalar(OBFUSCATE_STR("input s16"),     ImGuiDataType_S16,    &s16_v, inputs_step ? &s16_one : NULL, NULL, OBFUSCATE_STR("%d"));
        ImGui::InputScalar(OBFUSCATE_STR("input u16"),     ImGuiDataType_U16,    &u16_v, inputs_step ? &u16_one : NULL, NULL, OBFUSCATE_STR("%u"));
        ImGui::InputScalar(OBFUSCATE_STR("input s32"),     ImGuiDataType_S32,    &s32_v, inputs_step ? &s32_one : NULL, NULL, OBFUSCATE_STR("%d"));
        ImGui::InputScalar(OBFUSCATE_STR("input s32 hex"), ImGuiDataType_S32,    &s32_v, inputs_step ? &s32_one : NULL, NULL, OBFUSCATE_STR("%04X"));
        ImGui::InputScalar(OBFUSCATE_STR("input u32"),     ImGuiDataType_U32,    &u32_v, inputs_step ? &u32_one : NULL, NULL, OBFUSCATE_STR("%u"));
        ImGui::InputScalar(OBFUSCATE_STR("input u32 hex"), ImGuiDataType_U32,    &u32_v, inputs_step ? &u32_one : NULL, NULL, OBFUSCATE_STR("%08X"));
        ImGui::InputScalar(OBFUSCATE_STR("input s64"),     ImGuiDataType_S64,    &s64_v, inputs_step ? &s64_one : NULL);
        ImGui::InputScalar(OBFUSCATE_STR("input u64"),     ImGuiDataType_U64,    &u64_v, inputs_step ? &u64_one : NULL);
        ImGui::InputScalar(OBFUSCATE_STR("input float"),   ImGuiDataType_Float,  &f32_v, inputs_step ? &f32_one : NULL);
        ImGui::InputScalar(OBFUSCATE_STR("input double"),  ImGuiDataType_Double, &f64_v, inputs_step ? &f64_one : NULL);

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Multi-component Widgets"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Multi-component Widgets")))
    {
        static float vec4f[4] = { 0.10f, 0.20f, 0.30f, 0.44f };
        static int vec4i[4] = { 1, 5, 100, 255 };

        ImGui::InputFloat2(OBFUSCATE_STR("input float2"), vec4f);
        ImGui::DragFloat2(OBFUSCATE_STR("drag float2"), vec4f, 0.01f, 0.0f, 1.0f);
        ImGui::SliderFloat2(OBFUSCATE_STR("slider float2"), vec4f, 0.0f, 1.0f);
        ImGui::InputInt2(OBFUSCATE_STR("input int2"), vec4i);
        ImGui::DragInt2(OBFUSCATE_STR("drag int2"), vec4i, 1, 0, 255);
        ImGui::SliderInt2(OBFUSCATE_STR("slider int2"), vec4i, 0, 255);
        ImGui::Spacing();

        ImGui::InputFloat3(OBFUSCATE_STR("input float3"), vec4f);
        ImGui::DragFloat3(OBFUSCATE_STR("drag float3"), vec4f, 0.01f, 0.0f, 1.0f);
        ImGui::SliderFloat3(OBFUSCATE_STR("slider float3"), vec4f, 0.0f, 1.0f);
        ImGui::InputInt3(OBFUSCATE_STR("input int3"), vec4i);
        ImGui::DragInt3(OBFUSCATE_STR("drag int3"), vec4i, 1, 0, 255);
        ImGui::SliderInt3(OBFUSCATE_STR("slider int3"), vec4i, 0, 255);
        ImGui::Spacing();

        ImGui::InputFloat4(OBFUSCATE_STR("input float4"), vec4f);
        ImGui::DragFloat4(OBFUSCATE_STR("drag float4"), vec4f, 0.01f, 0.0f, 1.0f);
        ImGui::SliderFloat4(OBFUSCATE_STR("slider float4"), vec4f, 0.0f, 1.0f);
        ImGui::InputInt4(OBFUSCATE_STR("input int4"), vec4i);
        ImGui::DragInt4(OBFUSCATE_STR("drag int4"), vec4i, 1, 0, 255);
        ImGui::SliderInt4(OBFUSCATE_STR("slider int4"), vec4i, 0, 255);

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Vertical Sliders"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Vertical Sliders")))
    {
        const float spacing = 4;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));

        static int int_value = 0;
        ImGui::VSliderInt(OBFUSCATE_STR("##int"), ImVec2(18, 160), &int_value, 0, 5);
        ImGui::SameLine();

        static float values[7] = { 0.0f, 0.60f, 0.35f, 0.9f, 0.70f, 0.20f, 0.0f };
        ImGui::PushID(OBFUSCATE_STR("set1"));
        for (int i = 0; i < 7; i++)
        {
            if (i > 0) ImGui::SameLine();
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(i / 7.0f, 0.5f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, (ImVec4)ImColor::HSV(i / 7.0f, 0.6f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, (ImVec4)ImColor::HSV(i / 7.0f, 0.7f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, (ImVec4)ImColor::HSV(i / 7.0f, 0.9f, 0.9f));
            ImGui::VSliderFloat(OBFUSCATE_STR("##v"), ImVec2(18, 160), &values[i], 0.0f, 1.0f, "");
            if (ImGui::IsItemActive() || ImGui::IsItemHovered())
                ImGui::SetTooltip(OBFUSCATE_STR("%.3f"), values[i]);
            ImGui::PopStyleColor(4);
            ImGui::PopID();
        }
        ImGui::PopID();

        ImGui::SameLine();
        ImGui::PushID(OBFUSCATE_STR("set2"));
        static float values2[4] = { 0.20f, 0.80f, 0.40f, 0.25f };
        const int rows = 3;
        const ImVec2 small_slider_size(18, (float)(int)((160.0f - (rows - 1) * spacing) / rows));
        for (int nx = 0; nx < 4; nx++)
        {
            if (nx > 0) ImGui::SameLine();
            ImGui::BeginGroup();
            for (int ny = 0; ny < rows; ny++)
            {
                ImGui::PushID(nx * rows + ny);
                ImGui::VSliderFloat(OBFUSCATE_STR("##v"), small_slider_size, &values2[nx], 0.0f, 1.0f, "");
                if (ImGui::IsItemActive() || ImGui::IsItemHovered())
                    ImGui::SetTooltip(OBFUSCATE_STR("%.3f"), values2[nx]);
                ImGui::PopID();
            }
            ImGui::EndGroup();
        }
        ImGui::PopID();

        ImGui::SameLine();
        ImGui::PushID(OBFUSCATE_STR("set3"));
        for (int i = 0; i < 4; i++)
        {
            if (i > 0) ImGui::SameLine();
            ImGui::PushID(i);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 40);
            ImGui::VSliderFloat(OBFUSCATE_STR("##v"), ImVec2(40, 160), &values[i], 0.0f, 1.0f, OBFUSCATE_STR("%.2f\nsec"));
            ImGui::PopStyleVar();
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Drag and drop"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Drag and Drop")))
    {
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Drag and drop/Standard widgets"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Drag and drop in standard widgets")))
        {
            // ColorEdit widgets automatically act as drag source and drag target.
            // They are using standardized payload strings IMGUI_PAYLOAD_TYPE_COLOR_3F and IMGUI_PAYLOAD_TYPE_COLOR_4F
            // to allow your own widgets to use colors in their drag and drop interaction.
            // Also see 'Demo->Widgets->Color/Picker Widgets->Palette' demo.
            HelpMarker(OBFUSCATE_STR("You can drag from the color squares."));
            static float col1[3] = { 1.0f, 0.0f, 0.2f };
            static float col2[4] = { 0.4f, 0.7f, 0.0f, 0.5f };
            ImGui::ColorEdit3(OBFUSCATE_STR("color 1"), col1);
            ImGui::ColorEdit4(OBFUSCATE_STR("color 2"), col2);
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Drag and drop/Copy-swap items"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Drag and drop to copy/swap items")))
        {
            enum Mode
            {
                Mode_Copy,
                Mode_Move,
                Mode_Swap
            };
            static int mode = 0;
            if (ImGui::RadioButton(OBFUSCATE_STR("Copy"), mode == Mode_Copy)) { mode = Mode_Copy; } ImGui::SameLine();
            if (ImGui::RadioButton(OBFUSCATE_STR("Move"), mode == Mode_Move)) { mode = Mode_Move; } ImGui::SameLine();
            if (ImGui::RadioButton(OBFUSCATE_STR("Swap"), mode == Mode_Swap)) { mode = Mode_Swap; }
            static const char* names[9] =
            {
                "Bobby", "Beatrice", "Betty",
                "Brianna", "Barry", "Bernard",
                "Bibi", "Blaine", "Bryn"
            };
            for (int n = 0; n < IM_ARRAYSIZE(names); n++)
            {
                ImGui::PushID(n);
                if ((n % 3) != 0)
                    ImGui::SameLine();
                ImGui::Button(names[n], ImVec2(60, 60));

                // Our buttons are both drag sources and drag targets here!
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    // Set payload to carry the index of our item (could be anything)
                    ImGui::SetDragDropPayload(OBFUSCATE_STR("DND_DEMO_CELL"), &n, sizeof(int));

                    // Display preview (could be anything, e.g. when dragging an image we could decide to display
                    // the filename and a small preview of the image, etc.)
                    if (mode == Mode_Copy) { ImGui::Text(OBFUSCATE_STR("Copy %s"), names[n]); }
                    if (mode == Mode_Move) { ImGui::Text(OBFUSCATE_STR("Move %s"), names[n]); }
                    if (mode == Mode_Swap) { ImGui::Text(OBFUSCATE_STR("Swap %s"), names[n]); }
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(OBFUSCATE_STR("DND_DEMO_CELL")))
                    {
                        IM_ASSERT(payload->DataSize == sizeof(int));
                        int payload_n = *(const int*)payload->Data;
                        if (mode == Mode_Copy)
                        {
                            names[n] = names[payload_n];
                        }
                        if (mode == Mode_Move)
                        {
                            names[n] = names[payload_n];
                            names[payload_n] = "";
                        }
                        if (mode == Mode_Swap)
                        {
                            const char* tmp = names[n];
                            names[n] = names[payload_n];
                            names[payload_n] = tmp;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Drag and Drop/Drag to reorder items (simple)"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Drag to reorder items (simple)")))
        {
            // Simple reordering
            HelpMarker(OBFUSCATE_STR(
                "We don't use the drag and drop api at all here! "
                "Instead we query when the item is held but not hovered, and order items accordingly."));
            static const char* item_names[] = { "Item One", "Item Two", "Item Three", "Item Four", "Item Five" };
            for (int n = 0; n < IM_ARRAYSIZE(item_names); n++)
            {
                const char* item = item_names[n];
                ImGui::Selectable(item);

                if (ImGui::IsItemActive() && !ImGui::IsItemHovered())
                {
                    int n_next = n + (ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1);
                    if (n_next >= 0 && n_next < IM_ARRAYSIZE(item_names))
                    {
                        item_names[n] = item_names[n_next];
                        item_names[n_next] = item;
                        ImGui::ResetMouseDragDelta();
                    }
                }
            }
            ImGui::TreePop();
        }

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Querying Item Status (Edited,Active,Hovered etc.)"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Querying Item Status (Edited/Active/Hovered etc.)")))
    {
        // Select an item type
        const char* item_names[] =
        {
            "Text", "Button", "Button (w/ repeat)", "Checkbox", "SliderFloat", "InputText", "InputTextMultiline", "InputFloat",
            "InputFloat3", "ColorEdit4", "Selectable", "MenuItem", "TreeNode", "TreeNode (w/ double-click)", "Combo", "ListBox"
        };
        static int item_type = 4;
        static bool item_disabled = false;
        ImGui::Combo(OBFUSCATE_STR("Item Type"), &item_type, item_names, IM_ARRAYSIZE(item_names), IM_ARRAYSIZE(item_names));
        ImGui::SameLine();
        HelpMarker(OBFUSCATE_STR("Testing how various types of items are interacting with the IsItemXXX functions. Note that the bool return value of most ImGui function is generally equivalent to calling ImGui::IsItemHovered()."));
        ImGui::Checkbox(OBFUSCATE_STR("Item Disabled"),  &item_disabled);

        // Submit selected items so we can query their status in the code following it.
        bool ret = false;
        static bool b = false;
        static float col4f[4] = { 1.0f, 0.5, 0.0f, 1.0f };
        static char str[16] = {};
        if (item_disabled)
            ImGui::BeginDisabled(true);
        if (item_type == 0) { ImGui::Text(OBFUSCATE_STR("ITEM: Text")); }                                              // Testing text items with no identifier/interaction
        if (item_type == 1) { ret = ImGui::Button(OBFUSCATE_STR("ITEM: Button")); }                                    // Testing button
        if (item_type == 2) { ImGui::PushButtonRepeat(true); ret = ImGui::Button(OBFUSCATE_STR("ITEM: Button")); ImGui::PopButtonRepeat(); } // Testing button (with repeater)
        if (item_type == 3) { ret = ImGui::Checkbox(OBFUSCATE_STR("ITEM: Checkbox"), &b); }                            // Testing checkbox
        if (item_type == 4) { ret = ImGui::SliderFloat(OBFUSCATE_STR("ITEM: SliderFloat"), &col4f[0], 0.0f, 1.0f); }   // Testing basic item
        if (item_type == 5) { ret = ImGui::InputText(OBFUSCATE_STR("ITEM: InputText"), &str[0], IM_ARRAYSIZE(str)); }  // Testing input text (which handles tabbing)
        if (item_type == 6) { ret = ImGui::InputTextMultiline(OBFUSCATE_STR("ITEM: InputTextMultiline"), &str[0], IM_ARRAYSIZE(str)); } // Testing input text (which uses a child window)
        if (item_type == 7) { ret = ImGui::InputFloat(OBFUSCATE_STR("ITEM: InputFloat"), col4f, 1.0f); }               // Testing +/- buttons on scalar input
        if (item_type == 8) { ret = ImGui::InputFloat3(OBFUSCATE_STR("ITEM: InputFloat3"), col4f); }                   // Testing multi-component items (IsItemXXX flags are reported merged)
        if (item_type == 9) { ret = ImGui::ColorEdit4(OBFUSCATE_STR("ITEM: ColorEdit4"), col4f); }                     // Testing multi-component items (IsItemXXX flags are reported merged)
        if (item_type == 10){ ret = ImGui::Selectable(OBFUSCATE_STR("ITEM: Selectable")); }                            // Testing selectable item
        if (item_type == 11){ ret = ImGui::MenuItem(OBFUSCATE_STR("ITEM: MenuItem")); }                                // Testing menu item (they use ImGuiButtonFlags_PressedOnRelease button policy)
        if (item_type == 12){ ret = ImGui::TreeNode(OBFUSCATE_STR("ITEM: TreeNode")); if (ret) ImGui::TreePop(); }     // Testing tree node
        if (item_type == 13){ ret = ImGui::TreeNodeEx(OBFUSCATE_STR("ITEM: TreeNode w/ ImGuiTreeNodeFlags_OpenOnDoubleClick"), ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_NoTreePushOnOpen); } // Testing tree node with ImGuiButtonFlags_PressedOnDoubleClick button policy.
        if (item_type == 14){ const char* items[] = { "Apple", "Banana", "Cherry", "Kiwi" }; static int current = 1; ret = ImGui::Combo(OBFUSCATE_STR("ITEM: Combo"), &current, items, IM_ARRAYSIZE(items)); }
        if (item_type == 15){ const char* items[] = { "Apple", "Banana", "Cherry", "Kiwi" }; static int current = 1; ret = ImGui::ListBox(OBFUSCATE_STR("ITEM: ListBox"), &current, items, IM_ARRAYSIZE(items), IM_ARRAYSIZE(items)); }

        bool hovered_delay_none = ImGui::IsItemHovered();
        bool hovered_delay_short = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
        bool hovered_delay_normal = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);

        // Display the values of IsItemHovered() and other common item state functions.
        // Note that the ImGuiHoveredFlags_XXX flags can be combined.
        // Because BulletText is an item itself and that would affect the output of IsItemXXX functions,
        // we query every state in a single call to avoid storing them and to simplify the code.
        ImGui::BulletText(OBFUSCATE_STR(
            "Return value = %d\n"
            "IsItemFocused() = %d\n"
            "IsItemHovered() = %d\n"
            "IsItemHovered(_AllowWhenBlockedByPopup) = %d\n"
            "IsItemHovered(_AllowWhenBlockedByActiveItem) = %d\n"
            "IsItemHovered(_AllowWhenOverlapped) = %d\n"
            "IsItemHovered(_AllowWhenDisabled) = %d\n"
            "IsItemHovered(_RectOnly) = %d\n"
            "IsItemActive() = %d\n"
            "IsItemEdited() = %d\n"
            "IsItemActivated() = %d\n"
            "IsItemDeactivated() = %d\n"
            "IsItemDeactivatedAfterEdit() = %d\n"
            "IsItemVisible() = %d\n"
            "IsItemClicked() = %d\n"
            "IsItemToggledOpen() = %d\n"
            "GetItemRectMin() = (%.1f, %.1f)\n"
            "GetItemRectMax() = (%.1f, %.1f)\n"
            "GetItemRectSize() = (%.1f, %.1f)"),
            ret,
            ImGui::IsItemFocused(),
            ImGui::IsItemHovered(),
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup),
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem),
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlapped),
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled),
            ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly),
            ImGui::IsItemActive(),
            ImGui::IsItemEdited(),
            ImGui::IsItemActivated(),
            ImGui::IsItemDeactivated(),
            ImGui::IsItemDeactivatedAfterEdit(),
            ImGui::IsItemVisible(),
            ImGui::IsItemClicked(),
            ImGui::IsItemToggledOpen(),
            ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y,
            ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y,
            ImGui::GetItemRectSize().x, ImGui::GetItemRectSize().y
        );
        ImGui::BulletText(OBFUSCATE_STR(
            "w/ Hovering Delay: None = %d, Fast %d, Normal = %d"), hovered_delay_none, hovered_delay_short, hovered_delay_normal);

        if (item_disabled)
            ImGui::EndDisabled();

        char buf[1] = "";
        ImGui::InputText(OBFUSCATE_STR("unused"), buf, IM_ARRAYSIZE(buf), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        HelpMarker(OBFUSCATE_STR("This widget is only here to be able to tab-out of the widgets above and see e.g. Deactivated() status."));

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Querying Window Status (Focused,Hovered etc.)"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Querying Window Status (Focused/Hovered etc.)")))
    {
        static bool embed_all_inside_a_child_window = false;
        ImGui::Checkbox(OBFUSCATE_STR("Embed everything inside a child window for testing _RootWindow flag."), &embed_all_inside_a_child_window);
        if (embed_all_inside_a_child_window)
            ImGui::BeginChild(OBFUSCATE_STR("outer_child"), ImVec2(0, ImGui::GetFontSize() * 20.0f), true);

        // Testing IsWindowFocused() function with its various flags.
        ImGui::BulletText(OBFUSCATE_STR(
            "IsWindowFocused() = %d\n"
            "IsWindowFocused(_ChildWindows) = %d\n"
            "IsWindowFocused(_ChildWindows|_NoPopupHierarchy) = %d\n"
            "IsWindowFocused(_ChildWindows|_RootWindow) = %d\n"
            "IsWindowFocused(_ChildWindows|_RootWindow|_NoPopupHierarchy) = %d\n"
            "IsWindowFocused(_RootWindow) = %d\n"
            "IsWindowFocused(_RootWindow|_NoPopupHierarchy) = %d\n"
            "IsWindowFocused(_AnyWindow) = %d\n"),
            ImGui::IsWindowFocused(),
            ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows),
            ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_NoPopupHierarchy),
            ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_RootWindow),
            ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_RootWindow | ImGuiFocusedFlags_NoPopupHierarchy),
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow),
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow | ImGuiFocusedFlags_NoPopupHierarchy),
            ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow));

        // Testing IsWindowHovered() function with its various flags.
        ImGui::BulletText(OBFUSCATE_STR(
            "IsWindowHovered() = %d\n"
            "IsWindowHovered(_AllowWhenBlockedByPopup) = %d\n"
            "IsWindowHovered(_AllowWhenBlockedByActiveItem) = %d\n"
            "IsWindowHovered(_ChildWindows) = %d\n"
            "IsWindowHovered(_ChildWindows|_NoPopupHierarchy) = %d\n"
            "IsWindowHovered(_ChildWindows|_RootWindow) = %d\n"
            "IsWindowHovered(_ChildWindows|_RootWindow|_NoPopupHierarchy) = %d\n"
            "IsWindowHovered(_RootWindow) = %d\n"
            "IsWindowHovered(_RootWindow|_NoPopupHierarchy) = %d\n"
            "IsWindowHovered(_ChildWindows|_AllowWhenBlockedByPopup) = %d\n"
            "IsWindowHovered(_AnyWindow) = %d\n"),
            ImGui::IsWindowHovered(),
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup),
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem),
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows),
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_NoPopupHierarchy),
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_RootWindow),
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_RootWindow | ImGuiHoveredFlags_NoPopupHierarchy),
            ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow),
            ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow | ImGuiHoveredFlags_NoPopupHierarchy),
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup),
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow));

        ImGui::BeginChild(OBFUSCATE_STR("child"), ImVec2(0, 50), true);
        ImGui::Text(OBFUSCATE_STR("This is another child window for testing the _ChildWindows flag."));
        ImGui::EndChild();
        if (embed_all_inside_a_child_window)
            ImGui::EndChild();

        // Calling IsItemHovered() after begin returns the hovered status of the title bar.
        // This is useful in particular if you want to create a context menu associated to the title bar of a window.
        static bool test_window = false;
        ImGui::Checkbox(OBFUSCATE_STR("Hovered/Active tests after Begin() for title bar testing"), &test_window);
        if (test_window)
        {
            ImGui::Begin(OBFUSCATE_STR("Title bar Hovered/Active tests"), &test_window);
            if (ImGui::BeginPopupContextItem()) // <-- This is using IsItemHovered()
            {
                if (ImGui::MenuItem(OBFUSCATE_STR("Close"))) { test_window = false; }
                ImGui::EndPopup();
            }
            ImGui::Text(OBFUSCATE_STR(
                "IsItemHovered() after begin = %d (== is title bar hovered)\n"
                "IsItemActive() after begin = %d (== is window being clicked/moved)\n"),
                ImGui::IsItemHovered(), ImGui::IsItemActive());
            ImGui::End();
        }

        ImGui::TreePop();
    }

    // Demonstrate BeginDisabled/EndDisabled using a checkbox located at the bottom of the section (which is a bit odd:
    // logically we'd have this checkbox at the top of the section, but we don't want this feature to steal that space)
    if (disable_all)
        ImGui::EndDisabled();

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Disable Block"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Disable block")))
    {
        ImGui::Checkbox(OBFUSCATE_STR("Disable entire section above"), &disable_all);
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Demonstrate using BeginDisabled()/EndDisabled() across this section."));
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Widgets/Text Filter"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Text Filter")))
    {
        // Helper class to easy setup a text filter.
        // You may want to implement a more feature-full filtering scheme in your own application.
        HelpMarker(OBFUSCATE_STR("Not a widget per-se, but ImGuiTextFilter is a helper to perform simple filtering on text strings."));
        static ImGuiTextFilter filter;
        ImGui::Text(OBFUSCATE_STR("Filter usage:\n"
            "  \"\"         display all lines\n"
            "  \"xxx\"      display lines containing \"xxx\"\n"
            "  \"xxx,yyy\"  display lines containing \"xxx\" or \"yyy\"\n"
            "  \"-xxx\"     hide lines containing \"xxx\""));
        filter.Draw();
        const char* lines[] = { "aaa1.c", "bbb1.c", "ccc1.c", "aaa2.cpp", "bbb2.cpp", "ccc2.cpp", "abc.h", "hello, world" };
        for (int i = 0; i < IM_ARRAYSIZE(lines); i++)
            if (filter.PassFilter(lines[i]))
                ImGui::BulletText(OBFUSCATE_STR("%s"), lines[i]);
        ImGui::TreePop();
    }
}

static void ShowDemoWindowLayout()
{
    rtx_spoof_func;
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout"));
    if (!ImGui::CollapsingHeader(OBFUSCATE_STR("Layout & Scrolling")))
        return;

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Child windows"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Child windows")))
    {
        HelpMarker(OBFUSCATE_STR("Use child windows to begin into a self-contained independent scrolling/clipping regions within a host window."));
        static bool disable_mouse_wheel = false;
        static bool disable_menu = false;
        ImGui::Checkbox(OBFUSCATE_STR("Disable Mouse Wheel"), &disable_mouse_wheel);
        ImGui::Checkbox(OBFUSCATE_STR("Disable Menu"), &disable_menu);

        // Child 1: no border, enable horizontal scrollbar
        {
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
            if (disable_mouse_wheel)
                window_flags |= ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::BeginChild(OBFUSCATE_STR("ChildL"), ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 260), false, window_flags);
            for (int i = 0; i < 100; i++)
                ImGui::Text(OBFUSCATE_STR("%04d: scrollable region"), i);
            ImGui::EndChild();
        }

        ImGui::SameLine();

        // Child 2: rounded border
        {
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
            if (disable_mouse_wheel)
                window_flags |= ImGuiWindowFlags_NoScrollWithMouse;
            if (!disable_menu)
                window_flags |= ImGuiWindowFlags_MenuBar;
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
            ImGui::BeginChild(OBFUSCATE_STR("ChildR"), ImVec2(0, 260), true, window_flags);
            if (!disable_menu && ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu(OBFUSCATE_STR("Menu")))
                {
                    ShowExampleMenuFile();
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            if (ImGui::BeginTable(OBFUSCATE_STR("split"), 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
            {
                for (int i = 0; i < 100; i++)
                {
                    char buf[32];
                    sprintf(buf, OBFUSCATE_STR("%03d"), i);
                    ImGui::TableNextColumn();
                    ImGui::Button(buf, ImVec2(-FLT_MIN, 0.0f));
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        ImGui::Separator();
        {
            static int offset_x = 0;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
            ImGui::DragInt(OBFUSCATE_STR("Offset X"), &offset_x, 1.0f, -1000, 1000);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (float)offset_x);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(255, 0, 0, 100));
            ImGui::BeginChild(OBFUSCATE_STR("Red"), ImVec2(200, 100), true, ImGuiWindowFlags_None);
            for (int n = 0; n < 50; n++)
                ImGui::Text(OBFUSCATE_STR("Some test %d"), n);
            ImGui::EndChild();
            bool child_is_hovered = ImGui::IsItemHovered();
            ImVec2 child_rect_min = ImGui::GetItemRectMin();
            ImVec2 child_rect_max = ImGui::GetItemRectMax();
            ImGui::PopStyleColor();
            ImGui::Text(OBFUSCATE_STR("Hovered: %d"), child_is_hovered);
            ImGui::Text(OBFUSCATE_STR("Rect of child window is: (%.0f,%.0f) (%.0f,%.0f)"), child_rect_min.x, child_rect_min.y, child_rect_max.x, child_rect_max.y);
        }

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Widgets Width"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Widgets Width")))
    {
        static float f = 0.0f;
        static bool show_indented_items = true;
        ImGui::Checkbox(OBFUSCATE_STR("Show indented items"), &show_indented_items);
        ImGui::Text(OBFUSCATE_STR("SetNextItemWidth/PushItemWidth(100)"));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Fixed width."));
        ImGui::PushItemWidth(100);
        ImGui::DragFloat(OBFUSCATE_STR("float##1b"), &f);
        if (show_indented_items)
        {
            ImGui::Indent();
            ImGui::DragFloat(OBFUSCATE_STR("float (indented)##1b"), &f);
            ImGui::Unindent();
        }
        ImGui::PopItemWidth();

        ImGui::Text(OBFUSCATE_STR("SetNextItemWidth/PushItemWidth(-100)"));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Align to right edge minus 100"));
        ImGui::PushItemWidth(-100);
        ImGui::DragFloat(OBFUSCATE_STR("float##2a"), &f);
        if (show_indented_items)
        {
            ImGui::Indent();
            ImGui::DragFloat(OBFUSCATE_STR("float (indented)##2b"), &f);
            ImGui::Unindent();
        }
        ImGui::PopItemWidth();

        ImGui::Text(OBFUSCATE_STR("SetNextItemWidth/PushItemWidth(GetContentRegionAvail().x * 0.5f)"));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Half of available width.\n(~ right-cursor_pos)\n(works within a column set)"));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::DragFloat(OBFUSCATE_STR("float##3a"), &f);
        if (show_indented_items)
        {
            ImGui::Indent();
            ImGui::DragFloat(OBFUSCATE_STR("float (indented)##3b"), &f);
            ImGui::Unindent();
        }
        ImGui::PopItemWidth();

        ImGui::Text(OBFUSCATE_STR("SetNextItemWidth/PushItemWidth(-GetContentRegionAvail().x * 0.5f)"));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Align to right edge minus half"));
        ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::DragFloat(OBFUSCATE_STR("float##4a"), &f);
        if (show_indented_items)
        {
            ImGui::Indent();
            ImGui::DragFloat(OBFUSCATE_STR("float (indented)##4b"), &f);
            ImGui::Unindent();
        }
        ImGui::PopItemWidth();

        // Demonstrate using PushItemWidth to surround three items.
        // Calling SetNextItemWidth() before each of them would have the same effect.
        ImGui::Text(OBFUSCATE_STR("SetNextItemWidth/PushItemWidth(-FLT_MIN)"));
        ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Align to right edge"));
        ImGui::PushItemWidth(-FLT_MIN);
        ImGui::DragFloat(OBFUSCATE_STR("##float5a"), &f);
        if (show_indented_items)
        {
            ImGui::Indent();
            ImGui::DragFloat(OBFUSCATE_STR("float (indented)##5b"), &f);
            ImGui::Unindent();
        }
        ImGui::PopItemWidth();

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Basic Horizontal Layout"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Basic Horizontal Layout")))
    {
        ImGui::TextWrapped(OBFUSCATE_STR("(Use ImGui::SameLine() to keep adding items to the right of the preceding item)"));

        // Text
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Basic Horizontal Layout/SameLine"));
        ImGui::Text(OBFUSCATE_STR("Two items: Hello")); ImGui::SameLine();
        ImGui::TextColored(ImVec4(1,1,0,1), OBFUSCATE_STR("Sailor"));

        // Adjust spacing
        ImGui::Text(OBFUSCATE_STR("More spacing: Hello")); ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(1,1,0,1), OBFUSCATE_STR("Sailor"));

        // Button
        ImGui::AlignTextToFramePadding();
        ImGui::Text(OBFUSCATE_STR("Normal buttons")); ImGui::SameLine();
        ImGui::Button(OBFUSCATE_STR("Banana")); ImGui::SameLine();
        ImGui::Button(OBFUSCATE_STR("Apple")); ImGui::SameLine();
        ImGui::Button(OBFUSCATE_STR("Corniflower"));

        // Button
        ImGui::Text(OBFUSCATE_STR("Small buttons")); ImGui::SameLine();
        ImGui::SmallButton(OBFUSCATE_STR("Like this one")); ImGui::SameLine();
        ImGui::Text(OBFUSCATE_STR("can fit within a text block."));

        // Aligned to arbitrary position. Easy/cheap column.
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Basic Horizontal Layout/SameLine (with offset)"));
        ImGui::Text(OBFUSCATE_STR("Aligned"));
        ImGui::SameLine(150); ImGui::Text(OBFUSCATE_STR("x=150"));
        ImGui::SameLine(300); ImGui::Text(OBFUSCATE_STR("x=300"));
        ImGui::Text(OBFUSCATE_STR("Aligned"));
        ImGui::SameLine(150); ImGui::SmallButton(OBFUSCATE_STR("x=150"));
        ImGui::SameLine(300); ImGui::SmallButton(OBFUSCATE_STR("x=300"));

        // Checkbox
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Basic Horizontal Layout/SameLine (more)"));
        static bool c1 = false, c2 = false, c3 = false, c4 = false;
        ImGui::Checkbox(OBFUSCATE_STR("My"), &c1); ImGui::SameLine();
        ImGui::Checkbox(OBFUSCATE_STR("Tailor"), &c2); ImGui::SameLine();
        ImGui::Checkbox(OBFUSCATE_STR("Is"), &c3); ImGui::SameLine();
        ImGui::Checkbox(OBFUSCATE_STR("Rich"), &c4);

        // Various
        static float f0 = 1.0f, f1 = 2.0f, f2 = 3.0f;
        ImGui::PushItemWidth(80);
        const char* items[] = { "AAAA", "BBBB", "CCCC", "DDDD" };
        static int item = -1;
        ImGui::Combo(OBFUSCATE_STR("Combo"), &item, items, IM_ARRAYSIZE(items)); ImGui::SameLine();
        ImGui::SliderFloat(OBFUSCATE_STR("X"), &f0, 0.0f, 5.0f); ImGui::SameLine();
        ImGui::SliderFloat(OBFUSCATE_STR("Y"), &f1, 0.0f, 5.0f); ImGui::SameLine();
        ImGui::SliderFloat(OBFUSCATE_STR("Z"), &f2, 0.0f, 5.0f);
        ImGui::PopItemWidth();

        ImGui::PushItemWidth(80);
        ImGui::Text(OBFUSCATE_STR("Lists:"));
        static int selection[4] = { 0, 1, 2, 3 };
        for (int i = 0; i < 4; i++)
        {
            if (i > 0) ImGui::SameLine();
            ImGui::PushID(i);
            ImGui::ListBox("", &selection[i], items, IM_ARRAYSIZE(items));
            ImGui::PopID();
        }
        ImGui::PopItemWidth();

        // Dummy
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Basic Horizontal Layout/Dummy"));
        ImVec2 button_sz(40, 40);
        ImGui::Button(OBFUSCATE_STR("A"), button_sz); ImGui::SameLine();
        ImGui::Dummy(button_sz); ImGui::SameLine();
        ImGui::Button(OBFUSCATE_STR("B"), button_sz);

        // Manually wrapping
        // (we should eventually provide this as an automatic layout feature, but for now you can do it manually)
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Basic Horizontal Layout/Manual wrapping"));
        ImGui::Text(OBFUSCATE_STR("Manual wrapping:"));
        ImGuiStyle& style = ImGui::GetStyle();
        int buttons_count = 20;
        float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        for (int n = 0; n < buttons_count; n++)
        {
            ImGui::PushID(n);
            ImGui::Button(OBFUSCATE_STR("Box"), button_sz);
            float last_button_x2 = ImGui::GetItemRectMax().x;
            float next_button_x2 = last_button_x2 + style.ItemSpacing.x + button_sz.x; // Expected position if next button was on same line
            if (n + 1 < buttons_count && next_button_x2 < window_visible_x2)
                ImGui::SameLine();
            ImGui::PopID();
        }

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Groups"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Groups")))
    {
        HelpMarker(OBFUSCATE_STR(
            "BeginGroup() basically locks the horizontal position for new line. "
            "EndGroup() bundles the whole group so that you can use \"item\" functions such as "
            "IsItemHovered()/IsItemActive() or SameLine() etc. on the whole group."));
        ImGui::BeginGroup();
        {
            ImGui::BeginGroup();
            ImGui::Button(OBFUSCATE_STR("AAA"));
            ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("BBB"));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Button(OBFUSCATE_STR("CCC"));
            ImGui::Button(OBFUSCATE_STR("DDD"));
            ImGui::EndGroup();
            ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("EEE"));
            ImGui::EndGroup();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(OBFUSCATE_STR("First group hovered"));
        }
        // Capture the group size and create widgets using the same size
        ImVec2 size = ImGui::GetItemRectSize();
        const float values[5] = { 0.5f, 0.20f, 0.80f, 0.60f, 0.25f };
        ImGui::PlotHistogram(OBFUSCATE_STR("##values"), values, IM_ARRAYSIZE(values), 0, NULL, 0.0f, 1.0f, size);

        ImGui::Button(OBFUSCATE_STR("ACTION"), ImVec2((size.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f, size.y));
        ImGui::SameLine();
        ImGui::Button(OBFUSCATE_STR("REACTION"), ImVec2((size.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f, size.y));
        ImGui::EndGroup();
        ImGui::SameLine();

        ImGui::Button(OBFUSCATE_STR("LEVERAGE\nBUZZWORD"), size);
        ImGui::SameLine();

        if (ImGui::BeginListBox(OBFUSCATE_STR("List"), size))
        {
            ImGui::Selectable(OBFUSCATE_STR("Selected"), true);
            ImGui::Selectable(OBFUSCATE_STR("Not Selected"), false);
            ImGui::EndListBox();
        }

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Text Baseline Alignment"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Text Baseline Alignment")))
    {
        {
            ImGui::BulletText(OBFUSCATE_STR("Text baseline:"));
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
                "This is testing the vertical alignment that gets applied on text to keep it aligned with widgets. "
                "Lines only composed of text or \"small\" widgets use less vertical space than lines with framed widgets."));
            ImGui::Indent();

            ImGui::Text(OBFUSCATE_STR("KO Blahblah")); ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("Some framed item")); ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR("Baseline of button will look misaligned with text.."));
            ImGui::AlignTextToFramePadding();
            ImGui::Text(OBFUSCATE_STR("OK Blahblah")); ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("Some framed item")); ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR("We call AlignTextToFramePadding() to vertically align the text baseline by +FramePadding.y"));

            // SmallButton() uses the same vertical padding as Text
            ImGui::Button(OBFUSCATE_STR("TEST##1")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("TEST")); ImGui::SameLine();
            ImGui::SmallButton(OBFUSCATE_STR("TEST##2"));

            // If your line starts with text, call AlignTextToFramePadding() to align text to upcoming widgets.
            ImGui::AlignTextToFramePadding();
            ImGui::Text(OBFUSCATE_STR("Text aligned to framed item")); ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("Item##1")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("Item")); ImGui::SameLine();
            ImGui::SmallButton(OBFUSCATE_STR("Item##2")); ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("Item##3"));

            ImGui::Unindent();
        }

        ImGui::Spacing();

        {
            ImGui::BulletText(OBFUSCATE_STR("Multi-line text:"));
            ImGui::Indent();
            ImGui::Text(OBFUSCATE_STR("One\nTwo\nThree")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("Hello\nWorld")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("Banana"));

            ImGui::Text(OBFUSCATE_STR("Banana")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("Hello\nWorld")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("One\nTwo\nThree"));

            ImGui::Button(OBFUSCATE_STR("HOP##1")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("Banana")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("Hello\nWorld")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("Banana"));

            ImGui::Button(OBFUSCATE_STR("HOP##2")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("Hello\nWorld")); ImGui::SameLine();
            ImGui::Text(OBFUSCATE_STR("Banana"));
            ImGui::Unindent();
        }

        ImGui::Spacing();

        {
            ImGui::BulletText(OBFUSCATE_STR("Misc items:"));
            ImGui::Indent();

            // SmallButton() sets FramePadding to zero. Text baseline is aligned to match baseline of previous Button.
            ImGui::Button(OBFUSCATE_STR("80x80"), ImVec2(80, 80));
            ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("50x50"), ImVec2(50, 50));
            ImGui::SameLine();
            ImGui::Button(OBFUSCATE_STR("Button()"));
            ImGui::SameLine();
            ImGui::SmallButton(OBFUSCATE_STR("SmallButton()"));

            // Tree
            const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::Button(OBFUSCATE_STR("Button##1"));
            ImGui::SameLine(0.0f, spacing);
            if (ImGui::TreeNode(OBFUSCATE_STR("Node##1")))
            {
                // Placeholder tree data
                for (int i = 0; i < 6; i++)
                    ImGui::BulletText(OBFUSCATE_STR("Item %d.."), i);
                ImGui::TreePop();
            }

            // Vertically align text node a bit lower so it'll be vertically centered with upcoming widget.
            // Otherwise you can use SmallButton() (smaller fit).
            ImGui::AlignTextToFramePadding();
            bool node_open = ImGui::TreeNode(OBFUSCATE_STR("Node##2"));
            ImGui::SameLine(0.0f, spacing); ImGui::Button(OBFUSCATE_STR("Button##2"));
            if (node_open)
            {
                // Placeholder tree data
                for (int i = 0; i < 6; i++)
                    ImGui::BulletText(OBFUSCATE_STR("Item %d.."), i);
                ImGui::TreePop();
            }

            // Bullet
            ImGui::Button(OBFUSCATE_STR("Button##3"));
            ImGui::SameLine(0.0f, spacing);
            ImGui::BulletText(OBFUSCATE_STR("Bullet text"));

            ImGui::AlignTextToFramePadding();
            ImGui::BulletText(OBFUSCATE_STR("Node"));
            ImGui::SameLine(0.0f, spacing); ImGui::Button(OBFUSCATE_STR("Button##4"));
            ImGui::Unindent();
        }

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Scrolling"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Scrolling")))
    {
        // Vertical scroll functions
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Scrolling/Vertical"));
        HelpMarker(OBFUSCATE_STR("Use SetScrollHereY() or SetScrollFromPosY() to scroll to a given vertical position."));

        static int track_item = 50;
        static bool enable_track = true;
        static bool enable_extra_decorations = false;
        static float scroll_to_off_px = 0.0f;
        static float scroll_to_pos_px = 200.0f;

        ImGui::Checkbox(OBFUSCATE_STR("Decoration"), &enable_extra_decorations);

        ImGui::Checkbox(OBFUSCATE_STR("Track"), &enable_track);
        ImGui::PushItemWidth(100);
        ImGui::SameLine(140); enable_track |= ImGui::DragInt(OBFUSCATE_STR("##item"), &track_item, 0.25f, 0, 99, OBFUSCATE_STR("Item = %d"));

        bool scroll_to_off = ImGui::Button(OBFUSCATE_STR("Scroll Offset"));
        ImGui::SameLine(140); scroll_to_off |= ImGui::DragFloat(OBFUSCATE_STR("##off"), &scroll_to_off_px, 1.00f, 0, FLT_MAX, OBFUSCATE_STR("+%.0f px"));

        bool scroll_to_pos = ImGui::Button(OBFUSCATE_STR("Scroll To Pos"));
        ImGui::SameLine(140); scroll_to_pos |= ImGui::DragFloat(OBFUSCATE_STR("##pos"), &scroll_to_pos_px, 1.00f, -10, FLT_MAX, OBFUSCATE_STR("X/Y = %.0f px"));
        ImGui::PopItemWidth();

        if (scroll_to_off || scroll_to_pos)
            enable_track = false;

        ImGuiStyle& style = ImGui::GetStyle();
        float child_w = (ImGui::GetContentRegionAvail().x - 4 * style.ItemSpacing.x) / 5;
        if (child_w < 1.0f)
            child_w = 1.0f;
        ImGui::PushID(OBFUSCATE_STR("##VerticalScrolling"));
        for (int i = 0; i < 5; i++)
        {
            if (i > 0) ImGui::SameLine();
            ImGui::BeginGroup();
            const char* names[] = { "Top", "25%", "Center", "75%", "Bottom" };
            ImGui::TextUnformatted(names[i]);

            const ImGuiWindowFlags child_flags = enable_extra_decorations ? ImGuiWindowFlags_MenuBar : 0;
            const ImGuiID child_id = ImGui::GetID((void*)(intptr_t)i);
            const bool child_is_visible = ImGui::BeginChild(child_id, ImVec2(child_w, 200.0f), true, child_flags);
            if (ImGui::BeginMenuBar())
            {
                ImGui::TextUnformatted(OBFUSCATE_STR("abc"));
                ImGui::EndMenuBar();
            }
            if (scroll_to_off)
                ImGui::SetScrollY(scroll_to_off_px);
            if (scroll_to_pos)
                ImGui::SetScrollFromPosY(ImGui::GetCursorStartPos().y + scroll_to_pos_px, i * 0.25f);
            if (child_is_visible) // Avoid calling SetScrollHereY when running with culled items
            {
                for (int item = 0; item < 100; item++)
                {
                    if (enable_track && item == track_item)
                    {
                        ImGui::TextColored(ImVec4(1, 1, 0, 1), OBFUSCATE_STR("Item %d"), item);
                        ImGui::SetScrollHereY(i * 0.25f); // 0.0f:top, 0.5f:center, 1.0f:bottom
                    }
                    else
                    {
                        ImGui::Text(OBFUSCATE_STR("Item %d"), item);
                    }
                }
            }
            float scroll_y = ImGui::GetScrollY();
            float scroll_max_y = ImGui::GetScrollMaxY();
            ImGui::EndChild();
            ImGui::Text(OBFUSCATE_STR("%.0f/%.0f"), scroll_y, scroll_max_y);
            ImGui::EndGroup();
        }
        ImGui::PopID();

        // Horizontal scroll functions
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Scrolling/Horizontal"));
        ImGui::Spacing();
        HelpMarker(OBFUSCATE_STR(
            "Use SetScrollHereX() or SetScrollFromPosX() to scroll to a given horizontal position.\n\n"
            "Because the clipping rectangle of most window hides half worth of WindowPadding on the "
            "left/right, using SetScrollFromPosX(+1) will usually result in clipped text whereas the "
            "equivalent SetScrollFromPosY(+1) wouldn't."));
        ImGui::PushID(OBFUSCATE_STR("##HorizontalScrolling"));
        for (int i = 0; i < 5; i++)
        {
            float child_height = ImGui::GetTextLineHeight() + style.ScrollbarSize + style.WindowPadding.y * 2.0f;
            ImGuiWindowFlags child_flags = ImGuiWindowFlags_HorizontalScrollbar | (enable_extra_decorations ? ImGuiWindowFlags_AlwaysVerticalScrollbar : 0);
            ImGuiID child_id = ImGui::GetID((void*)(intptr_t)i);
            bool child_is_visible = ImGui::BeginChild(child_id, ImVec2(-100, child_height), true, child_flags);
            if (scroll_to_off)
                ImGui::SetScrollX(scroll_to_off_px);
            if (scroll_to_pos)
                ImGui::SetScrollFromPosX(ImGui::GetCursorStartPos().x + scroll_to_pos_px, i * 0.25f);
            if (child_is_visible) // Avoid calling SetScrollHereY when running with culled items
            {
                for (int item = 0; item < 100; item++)
                {
                    if (item > 0)
                        ImGui::SameLine();
                    if (enable_track && item == track_item)
                    {
                        ImGui::TextColored(ImVec4(1, 1, 0, 1), OBFUSCATE_STR("Item %d"), item);
                        ImGui::SetScrollHereX(i * 0.25f); // 0.0f:left, 0.5f:center, 1.0f:right
                    }
                    else
                    {
                        ImGui::Text(OBFUSCATE_STR("Item %d"), item);
                    }
                }
            }
            float scroll_x = ImGui::GetScrollX();
            float scroll_max_x = ImGui::GetScrollMaxX();
            ImGui::EndChild();
            ImGui::SameLine();
            const char* names[] = { "Left", "25%", "Center", "75%", "Right" };
            ImGui::Text(OBFUSCATE_STR("%s\n%.0f/%.0f"), names[i], scroll_x, scroll_max_x);
            ImGui::Spacing();
        }
        ImGui::PopID();

        // Miscellaneous Horizontal Scrolling Demo
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Scrolling/Horizontal (more)"));
        HelpMarker(OBFUSCATE_STR(
            "Horizontal scrolling for a window is enabled via the ImGuiWindowFlags_HorizontalScrollbar flag.\n\n"
            "You may want to also explicitly specify content width by using SetNextWindowContentWidth() before Begin()."));
        static int lines = 7;
        ImGui::SliderInt(OBFUSCATE_STR("Lines"), &lines, 1, 15);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
        ImVec2 scrolling_child_size = ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 7 + 30);
        ImGui::BeginChild(OBFUSCATE_STR("scrolling"), scrolling_child_size, true, ImGuiWindowFlags_HorizontalScrollbar);
        for (int line = 0; line < lines; line++)
        {
            int num_buttons = 10 + ((line & 1) ? line * 9 : line * 3);
            for (int n = 0; n < num_buttons; n++)
            {
                if (n > 0) ImGui::SameLine();
                ImGui::PushID(n + line * 1000);
                char num_buf[16];
                sprintf(num_buf, OBFUSCATE_STR("%d"), n);
                const char* label = (!(n % 15)) ? "FizzBuzz" : (!(n % 3)) ? "Fizz" : (!(n % 5)) ? "Buzz" : num_buf;
                float hue = n * 0.05f;
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(hue, 0.6f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(hue, 0.7f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(hue, 0.8f, 0.8f));
                ImGui::Button(label, ImVec2(40.0f + sinf((float)(line + n)) * 20.0f, 0.0f));
                ImGui::PopStyleColor(3);
                ImGui::PopID();
            }
        }
        float scroll_x = ImGui::GetScrollX();
        float scroll_max_x = ImGui::GetScrollMaxX();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        float scroll_x_delta = 0.0f;
        ImGui::SmallButton(OBFUSCATE_STR("<<"));
        if (ImGui::IsItemActive())
            scroll_x_delta = -ImGui::GetIO().DeltaTime * 1000.0f;
        ImGui::SameLine();
        ImGui::Text(OBFUSCATE_STR("Scroll from code")); ImGui::SameLine();
        ImGui::SmallButton(OBFUSCATE_STR(">>"));
        if (ImGui::IsItemActive())
            scroll_x_delta = +ImGui::GetIO().DeltaTime * 1000.0f;
        ImGui::SameLine();
        ImGui::Text(OBFUSCATE_STR("%.0f/%.0f"), scroll_x, scroll_max_x);
        if (scroll_x_delta != 0.0f)
        {
            // Demonstrate a trick: you can use Begin to set yourself in the context of another window
            // (here we are already out of your child window)
            ImGui::BeginChild(OBFUSCATE_STR("scrolling"));
            ImGui::SetScrollX(ImGui::GetScrollX() + scroll_x_delta);
            ImGui::EndChild();
        }
        ImGui::Spacing();

        static bool show_horizontal_contents_size_demo_window = false;
        ImGui::Checkbox(OBFUSCATE_STR("Show Horizontal contents size demo window"), &show_horizontal_contents_size_demo_window);

        if (show_horizontal_contents_size_demo_window)
        {
            static bool show_h_scrollbar = true;
            static bool show_button = true;
            static bool show_tree_nodes = true;
            static bool show_text_wrapped = false;
            static bool show_columns = true;
            static bool show_tab_bar = true;
            static bool show_child = false;
            static bool explicit_content_size = false;
            static float contents_size_x = 300.0f;
            if (explicit_content_size)
                ImGui::SetNextWindowContentSize(ImVec2(contents_size_x, 0.0f));
            ImGui::Begin(OBFUSCATE_STR("Horizontal contents size demo window"), &show_horizontal_contents_size_demo_window, show_h_scrollbar ? ImGuiWindowFlags_HorizontalScrollbar : 0);
            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Scrolling/Horizontal contents size demo window"));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 0));
            HelpMarker(OBFUSCATE_STR("Test of different widgets react and impact the work rectangle growing when horizontal scrolling is enabled.\n\nUse 'Metrics->Tools->Show windows rectangles' to visualize rectangles."));
            ImGui::Checkbox(OBFUSCATE_STR("H-scrollbar"), &show_h_scrollbar);
            ImGui::Checkbox(OBFUSCATE_STR("Button"), &show_button);            // Will grow contents size (unless explicitly overwritten)
            ImGui::Checkbox(OBFUSCATE_STR("Tree nodes"), &show_tree_nodes);    // Will grow contents size and display highlight over full width
            ImGui::Checkbox(OBFUSCATE_STR("Text wrapped"), &show_text_wrapped);// Will grow and use contents size
            ImGui::Checkbox(OBFUSCATE_STR("Columns"), &show_columns);          // Will use contents size
            ImGui::Checkbox(OBFUSCATE_STR("Tab bar"), &show_tab_bar);          // Will use contents size
            ImGui::Checkbox(OBFUSCATE_STR("Child"), &show_child);              // Will grow and use contents size
            ImGui::Checkbox(OBFUSCATE_STR("Explicit content size"), &explicit_content_size);
            ImGui::Text(OBFUSCATE_STR("Scroll %.1f/%.1f %.1f/%.1f"), ImGui::GetScrollX(), ImGui::GetScrollMaxX(), ImGui::GetScrollY(), ImGui::GetScrollMaxY());
            if (explicit_content_size)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                ImGui::DragFloat(OBFUSCATE_STR("##csx"), &contents_size_x);
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 10, p.y + 10), IM_COL32_WHITE);
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + contents_size_x - 10, p.y), ImVec2(p.x + contents_size_x, p.y + 10), IM_COL32_WHITE);
                ImGui::Dummy(ImVec2(0, 10));
            }
            ImGui::PopStyleVar(2);
            ImGui::Separator();
            if (show_button)
            {
                ImGui::Button(OBFUSCATE_STR("this is a 300-wide button"), ImVec2(300, 0));
            }
            if (show_tree_nodes)
            {
                bool open = true;
                if (ImGui::TreeNode(OBFUSCATE_STR("this is a tree node")))
                {
                    if (ImGui::TreeNode(OBFUSCATE_STR("another one of those tree node...")))
                    {
                        ImGui::Text(OBFUSCATE_STR("Some tree contents"));
                        ImGui::TreePop();
                    }
                    ImGui::TreePop();
                }
                ImGui::CollapsingHeader(OBFUSCATE_STR("CollapsingHeader"), &open);
            }
            if (show_text_wrapped)
            {
                ImGui::TextWrapped(OBFUSCATE_STR("This text should automatically wrap on the edge of the work rectangle."));
            }
            if (show_columns)
            {
                ImGui::Text(OBFUSCATE_STR("Tables:"));
                if (ImGui::BeginTable(OBFUSCATE_STR("table"), 4, ImGuiTableFlags_Borders))
                {
                    for (int n = 0; n < 4; n++)
                    {
                        ImGui::TableNextColumn();
                        ImGui::Text(OBFUSCATE_STR("Width %.2f"), ImGui::GetContentRegionAvail().x);
                    }
                    ImGui::EndTable();
                }
                ImGui::Text(OBFUSCATE_STR("Columns:"));
                ImGui::Columns(4);
                for (int n = 0; n < 4; n++)
                {
                    ImGui::Text(OBFUSCATE_STR("Width %.2f"), ImGui::GetColumnWidth());
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
            }
            if (show_tab_bar && ImGui::BeginTabBar(OBFUSCATE_STR("Hello")))
            {
                if (ImGui::BeginTabItem(OBFUSCATE_STR("OneOneOne"))) { ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem(OBFUSCATE_STR("TwoTwoTwo"))) { ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem(OBFUSCATE_STR("ThreeThreeThree"))) { ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem(OBFUSCATE_STR("FourFourFour"))) { ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
            if (show_child)
            {
                ImGui::BeginChild(OBFUSCATE_STR("child"), ImVec2(0, 0), true);
                ImGui::EndChild();
            }
            ImGui::End();
        }

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Layout/Clipping"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Clipping")))
    {
        static ImVec2 size(100.0f, 100.0f);
        static ImVec2 offset(30.0f, 30.0f);
        ImGui::DragFloat2(OBFUSCATE_STR("size"), (float*)&size, 0.5f, 1.0f, 200.0f, OBFUSCATE_STR("%.0f"));
        ImGui::TextWrapped(OBFUSCATE_STR("(Click and drag to scroll)"));

        HelpMarker(OBFUSCATE_STR(
            "(Left) Using ImGui::PushClipRect():\n"
            "Will alter ImGui hit-testing logic + ImDrawList rendering.\n"
            "(use this if you want your clipping rectangle to affect interactions)\n\n"
            "(Center) Using ImDrawList::PushClipRect():\n"
            "Will alter ImDrawList rendering only.\n"
            "(use this as a shortcut if you are only using ImDrawList calls)\n\n"
            "(Right) Using ImDrawList::AddText() with a fine ClipRect:\n"
            "Will alter only this specific ImDrawList::AddText() rendering.\n"
            "This is often used internally to avoid altering the clipping rectangle and minimize draw calls."));

        for (int n = 0; n < 3; n++)
        {
            if (n > 0)
                ImGui::SameLine();

            ImGui::PushID(n);
            ImGui::InvisibleButton(OBFUSCATE_STR("##canvas"), size);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                offset.x += ImGui::GetIO().MouseDelta.x;
                offset.y += ImGui::GetIO().MouseDelta.y;
            }
            ImGui::PopID();
            if (!ImGui::IsItemVisible()) // Skip rendering as ImDrawList elements are not clipped.
                continue;

            const ImVec2 p0 = ImGui::GetItemRectMin();
            const ImVec2 p1 = ImGui::GetItemRectMax();
            const char* text_str = "Line 1 hello\nLine 2 clip me!";
            const ImVec2 text_pos = ImVec2(p0.x + offset.x, p0.y + offset.y);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            switch (n)
            {
            case 0:
                ImGui::PushClipRect(p0, p1, true);
                draw_list->AddRectFilled(p0, p1, IM_COL32(90, 90, 120, 255));
                draw_list->AddText(text_pos, IM_COL32_WHITE, text_str);
                ImGui::PopClipRect();
                break;
            case 1:
                draw_list->PushClipRect(p0, p1, true);
                draw_list->AddRectFilled(p0, p1, IM_COL32(90, 90, 120, 255));
                draw_list->AddText(text_pos, IM_COL32_WHITE, text_str);
                draw_list->PopClipRect();
                break;
            case 2:
                ImVec4 clip_rect(p0.x, p0.y, p1.x, p1.y); // AddText() takes a ImVec4* here so let's convert.
                draw_list->AddRectFilled(p0, p1, IM_COL32(90, 90, 120, 255));
                draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos, IM_COL32_WHITE, text_str, NULL, 0.0f, &clip_rect);
                break;
            }
        }

        ImGui::TreePop();
    }
}

static void ShowDemoWindowPopups()
{
    rtx_spoof_func;
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Popups"));
    if (!ImGui::CollapsingHeader(OBFUSCATE_STR("Popups & Modal windows")))
        return;

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Popups/Popups"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Popups")))
    {
        ImGui::TextWrapped(OBFUSCATE_STR(
            "When a popup is active, it inhibits interacting with windows that are behind the popup. "
            "Clicking outside the popup closes it."));

        static int selected_fish = -1;
        const char* names[] = { "Bream", "Haddock", "Mackerel", "Pollock", "Tilefish" };
        static bool toggles[] = { true, false, false, false, false };

        // Simple selection popup (if you want to show the current selection inside the Button itself,
        // you may want to build a string using the "###" operator to preserve a constant ID with a variable label)
        if (ImGui::Button(OBFUSCATE_STR("Select..")))
            ImGui::OpenPopup(OBFUSCATE_STR("my_select_popup"));
        ImGui::SameLine();
        ImGui::TextUnformatted(selected_fish == -1 ? OBFUSCATE_STR("<None>") : names[selected_fish]);
        if (ImGui::BeginPopup(OBFUSCATE_STR("my_select_popup")))
        {
            ImGui::Text(OBFUSCATE_STR("Aquarium"));
            ImGui::Separator();
            for (int i = 0; i < IM_ARRAYSIZE(names); i++)
                if (ImGui::Selectable(names[i]))
                    selected_fish = i;
            ImGui::EndPopup();
        }

        // Showing a menu with toggles
        if (ImGui::Button(OBFUSCATE_STR("Toggle..")))
            ImGui::OpenPopup(OBFUSCATE_STR("my_toggle_popup"));
        if (ImGui::BeginPopup(OBFUSCATE_STR("my_toggle_popup")))
        {
            for (int i = 0; i < IM_ARRAYSIZE(names); i++)
                ImGui::MenuItem(names[i], OBFUSCATE_STR(""), &toggles[i]);
            if (ImGui::BeginMenu(OBFUSCATE_STR("Sub-menu")))
            {
                ImGui::MenuItem(OBFUSCATE_STR("Click me"));
                ImGui::EndMenu();
            }

            ImGui::Separator();
            ImGui::Text(OBFUSCATE_STR("Tooltip here"));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(OBFUSCATE_STR("I am a tooltip over a popup"));

            if (ImGui::Button(OBFUSCATE_STR("Stacked Popup")))
                ImGui::OpenPopup(OBFUSCATE_STR("another popup"));
            if (ImGui::BeginPopup(OBFUSCATE_STR("another popup")))
            {
                for (int i = 0; i < IM_ARRAYSIZE(names); i++)
                    ImGui::MenuItem(names[i], OBFUSCATE_STR(""), &toggles[i]);
                if (ImGui::BeginMenu(OBFUSCATE_STR("Sub-menu")))
                {
                    ImGui::MenuItem(OBFUSCATE_STR("Click me"));
                    if (ImGui::Button(OBFUSCATE_STR("Stacked Popup")))
                        ImGui::OpenPopup(OBFUSCATE_STR("another popup"));
                    if (ImGui::BeginPopup(OBFUSCATE_STR("another popup")))
                    {
                        ImGui::Text(OBFUSCATE_STR("I am the last one here."));
                        ImGui::EndPopup();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
            ImGui::EndPopup();
        }

        // Call the more complete ShowExampleMenuFile which we use in various places of this demo
        if (ImGui::Button(OBFUSCATE_STR("With a menu..")))
            ImGui::OpenPopup(OBFUSCATE_STR("my_file_popup"));
        if (ImGui::BeginPopup(OBFUSCATE_STR("my_file_popup"), ImGuiWindowFlags_MenuBar))
        {
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu(OBFUSCATE_STR("File")))
                {
                    ShowExampleMenuFile();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(OBFUSCATE_STR("Edit")))
                {
                    ImGui::MenuItem(OBFUSCATE_STR("Dummy"));
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::Text(OBFUSCATE_STR("Hello from popup!"));
            ImGui::Button(OBFUSCATE_STR("This is a dummy button.."));
            ImGui::EndPopup();
        }

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Popups/Context menus"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Context menus")))
    {
        HelpMarker(OBFUSCATE_STR("\"Context\" functions are simple helpers to associate a Popup to a given Item or Window identifier."));
        {
            const char* names[5] = { "Label1", "Label2", "Label3", "Label4", "Label5" };
            static int selected = -1;
            for (int n = 0; n < 5; n++)
            {
                if (ImGui::Selectable(names[n], selected == n))
                    selected = n;
                if (ImGui::BeginPopupContextItem()) // <-- use last item id as popup id
                {
                    selected = n;
                    ImGui::Text(OBFUSCATE_STR("This a popup for \"%s\"!"), names[n]);
                    if (ImGui::Button(OBFUSCATE_STR("Close")))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(OBFUSCATE_STR("Right-click to open popup"));
            }
        }
        {
            HelpMarker(OBFUSCATE_STR("Text() elements don't have stable identifiers so we need to provide one."));
            static float value = 0.5f;
            ImGui::Text(OBFUSCATE_STR("Value = %.3f <-- (1) right-click this text"), value);
            if (ImGui::BeginPopupContextItem(OBFUSCATE_STR("my popup")))
            {
                if (ImGui::Selectable(OBFUSCATE_STR("Set to zero"))) value = 0.0f;
                if (ImGui::Selectable(OBFUSCATE_STR("Set to PI"))) value = 3.1415f;
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat(OBFUSCATE_STR("##Value"), &value, 0.1f, 0.0f, 0.0f);
                ImGui::EndPopup();
            }

            // We can also use OpenPopupOnItemClick() to toggle the visibility of a given popup.
            // Here we make it that right-clicking this other text element opens the same popup as above.
            // The popup itself will be submitted by the code above.
            ImGui::Text(OBFUSCATE_STR("(2) Or right-click this text"));
            ImGui::OpenPopupOnItemClick(OBFUSCATE_STR("my popup"), ImGuiPopupFlags_MouseButtonRight);

            // Back to square one: manually open the same popup.
            if (ImGui::Button(OBFUSCATE_STR("(3) Or click this button")))
                ImGui::OpenPopup(OBFUSCATE_STR("my popup"));
        }
        {
            HelpMarker(OBFUSCATE_STR("Showcase using a popup ID linked to item ID, with the item having a changing label + stable ID using the ### operator."));
            static char name[32] = "Label1";
            char buf[64];
            sprintf(buf, OBFUSCATE_STR("Button: %s###Button"), name); // ### operator override ID ignoring the preceding label
            ImGui::Button(buf);
            if (ImGui::BeginPopupContextItem())
            {
                ImGui::Text(OBFUSCATE_STR("Edit name:"));
                ImGui::InputText(OBFUSCATE_STR("##edit"), name, IM_ARRAYSIZE(name));
                if (ImGui::Button(OBFUSCATE_STR("Close")))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::SameLine(); ImGui::Text(OBFUSCATE_STR("(<-- right-click here)"));
        }

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Popups/Modals"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Modals")))
    {
        ImGui::TextWrapped(OBFUSCATE_STR("Modal windows are like popups but the user cannot close them by clicking outside."));

        if (ImGui::Button(OBFUSCATE_STR("Delete..")))
            ImGui::OpenPopup(OBFUSCATE_STR("Delete?"));

        // Always center this window when appearing
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(OBFUSCATE_STR("Delete?"), NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(OBFUSCATE_STR("All those beautiful files will be deleted.\nThis operation cannot be undone!\n\n"));
            ImGui::Separator();
            static bool dont_ask_me_next_time = false;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::Checkbox(OBFUSCATE_STR("Don't ask me next time"), &dont_ask_me_next_time);
            ImGui::PopStyleVar();

            if (ImGui::Button(OBFUSCATE_STR("OK"), ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button(OBFUSCATE_STR("Cancel"), ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        if (ImGui::Button(OBFUSCATE_STR("Stacked modals..")))
            ImGui::OpenPopup(OBFUSCATE_STR("Stacked 1"));
        if (ImGui::BeginPopupModal(OBFUSCATE_STR("Stacked 1"), NULL, ImGuiWindowFlags_MenuBar))
        {
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu(OBFUSCATE_STR("File")))
                {
                    if (ImGui::MenuItem(OBFUSCATE_STR("Some menu item"))) {}
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::Text(OBFUSCATE_STR("Hello from Stacked The First\nUsing style.Colors[ImGuiCol_ModalWindowDimBg] behind it."));

            // Testing behavior of widgets stacking their own regular popups over the modal.
            static int item = 1;
            static float color[4] = { 0.4f, 0.7f, 0.0f, 0.5f };
            ImGui::Combo(OBFUSCATE_STR("Combo"), &item, OBFUSCATE_STR("aaaa\0bbbb\0cccc\0dddd\0eeee\0\0"));
            ImGui::ColorEdit4(OBFUSCATE_STR("color"), color);

            if (ImGui::Button(OBFUSCATE_STR("Add another modal..")))
                ImGui::OpenPopup(OBFUSCATE_STR("Stacked 2"));
            bool unused_open = true;
            if (ImGui::BeginPopupModal(OBFUSCATE_STR("Stacked 2"), &unused_open))
            {
                ImGui::Text(OBFUSCATE_STR("Hello from Stacked The Second!"));
                if (ImGui::Button(OBFUSCATE_STR("Close")))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            if (ImGui::Button(OBFUSCATE_STR("Close")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Popups/Menus inside a regular window"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Menus inside a regular window")))
    {
        ImGui::TextWrapped(OBFUSCATE_STR("Below we are testing adding menu items to a regular window. It's rather unusual but should work!"));
        ImGui::Separator();

        ImGui::MenuItem(OBFUSCATE_STR("Menu item", "CTRL+M"));
        if (ImGui::BeginMenu(OBFUSCATE_STR("Menu inside a regular window")))
        {
            ShowExampleMenuFile();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::TreePop();
    }
}

namespace
{
enum MyItemColumnID
{
    MyItemColumnID_ID,
    MyItemColumnID_Name,
    MyItemColumnID_Action,
    MyItemColumnID_Quantity,
    MyItemColumnID_Description
};

struct MyItem
{
    int         ID;
    const char* Name;
    int         Quantity;

    // We have a problem which is affecting _only this demo_ and should not affect your code:
    // As we don't rely on std:: or other third-party library to compile dear imgui, we only have reliable access to qsort(),
    // however qsort doesn't allow passing user data to comparing function.
    // As a workaround, we are storing the sort specs in a static/global for the comparing function to access.
    // In your own use case you would probably pass the sort specs to your sorting/comparing functions directly and not use a global.
    // We could technically call ImGui::TableGetSortSpecs() in CompareWithSortSpecs(), but considering that this function is called
    // very often by the sorting algorithm it would be a little wasteful.
    static const ImGuiTableSortSpecs* s_current_sort_specs;

    // Compare function to be used by qsort()
    static int IMGUI_CDECL CompareWithSortSpecs(const void* lhs, const void* rhs)
    {
        rtx_spoof_func;
        const MyItem* a = (const MyItem*)lhs;
        const MyItem* b = (const MyItem*)rhs;
        for (int n = 0; n < s_current_sort_specs->SpecsCount; n++)
        {
            // Here we identify columns using the ColumnUserID value that we ourselves passed to TableSetupColumn()
            // We could also choose to identify columns based on their index (sort_spec->ColumnIndex), which is simpler!
            const ImGuiTableColumnSortSpecs* sort_spec = &s_current_sort_specs->Specs[n];
            int delta = 0;
            switch (sort_spec->ColumnUserID)
            {
            case MyItemColumnID_ID:             delta = (a->ID - b->ID);                break;
            case MyItemColumnID_Name:           delta = (strcmp(a->Name, b->Name));     break;
            case MyItemColumnID_Quantity:       delta = (a->Quantity - b->Quantity);    break;
            case MyItemColumnID_Description:    delta = (strcmp(a->Name, b->Name));     break;
            default: IM_ASSERT(0); break;
            }
            if (delta > 0)
                return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? +1 : -1;
            if (delta < 0)
                return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? -1 : +1;
        }

        // qsort() is instable so always return a way to differenciate items.
        // Your own compare function may want to avoid fallback on implicit sort specs e.g. a Name compare if it wasn't already part of the sort specs.
        return (a->ID - b->ID);
    }
};
const ImGuiTableSortSpecs* MyItem::s_current_sort_specs = NULL;
}

// Make the UI compact because there are so many fields
static void PushStyleCompact()
{
    rtx_spoof_func;
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, (float)(int)(style.FramePadding.y * 0.60f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, (float)(int)(style.ItemSpacing.y * 0.60f)));
}

static void PopStyleCompact()
{
    rtx_spoof_func;
    ImGui::PopStyleVar(2);
}

// Show a combo box with a choice of sizing policies
static void EditTableSizingFlags(ImGuiTableFlags* p_flags)
{
    rtx_spoof_func;
    struct EnumDesc { ImGuiTableFlags Value; const char* Name; const char* Tooltip; };
    static const EnumDesc policies[] =
    {
        { ImGuiTableFlags_None,               "Default",                            "Use default sizing policy:\n- ImGuiTableFlags_SizingFixedFit if ScrollX is on or if host window has ImGuiWindowFlags_AlwaysAutoResize.\n- ImGuiTableFlags_SizingStretchSame otherwise." },
        { ImGuiTableFlags_SizingFixedFit,     "ImGuiTableFlags_SizingFixedFit",     "Columns default to _WidthFixed (if resizable) or _WidthAuto (if not resizable), matching contents width." },
        { ImGuiTableFlags_SizingFixedSame,    "ImGuiTableFlags_SizingFixedSame",    "Columns are all the same width, matching the maximum contents width.\nImplicitly disable ImGuiTableFlags_Resizable and enable ImGuiTableFlags_NoKeepColumnsVisible." },
        { ImGuiTableFlags_SizingStretchProp,  "ImGuiTableFlags_SizingStretchProp",  "Columns default to _WidthStretch with weights proportional to their widths." },
        { ImGuiTableFlags_SizingStretchSame,  "ImGuiTableFlags_SizingStretchSame",  "Columns default to _WidthStretch with same weights." }
    };
    int idx;
    for (idx = 0; idx < IM_ARRAYSIZE(policies); idx++)
        if (policies[idx].Value == (*p_flags & ImGuiTableFlags_SizingMask_))
            break;
    const char* preview_text = (idx < IM_ARRAYSIZE(policies)) ? policies[idx].Name + (idx > 0 ? strlen(OBFUSCATE_STR("ImGuiTableFlags")) : 0) : "";
    if (ImGui::BeginCombo(OBFUSCATE_STR("Sizing Policy"), preview_text))
    {
        for (int n = 0; n < IM_ARRAYSIZE(policies); n++)
            if (ImGui::Selectable(policies[n].Name, idx == n))
                *p_flags = (*p_flags & ~ImGuiTableFlags_SizingMask_) | policies[n].Value;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(OBFUSCATE_STR("(?)"));
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 50.0f);
        for (int m = 0; m < IM_ARRAYSIZE(policies); m++)
        {
            ImGui::Separator();
            ImGui::Text(OBFUSCATE_STR("%s:"), policies[m].Name);
            ImGui::Separator();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().IndentSpacing * 0.5f);
            ImGui::TextUnformatted(policies[m].Tooltip);
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void EditTableColumnsFlags(ImGuiTableColumnFlags* p_flags)
{
    rtx_spoof_func;
    ImGui::CheckboxFlags(OBFUSCATE_STR("_Disabled"), p_flags, ImGuiTableColumnFlags_Disabled); ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Master disable flag (also hide from context menu)"));
    ImGui::CheckboxFlags(OBFUSCATE_STR("_DefaultHide"), p_flags, ImGuiTableColumnFlags_DefaultHide);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_DefaultSort"), p_flags, ImGuiTableColumnFlags_DefaultSort);
    if (ImGui::CheckboxFlags(OBFUSCATE_STR("_WidthStretch"), p_flags, ImGuiTableColumnFlags_WidthStretch))
        *p_flags &= ~(ImGuiTableColumnFlags_WidthMask_ ^ ImGuiTableColumnFlags_WidthStretch);
    if (ImGui::CheckboxFlags(OBFUSCATE_STR("_WidthFixed"), p_flags, ImGuiTableColumnFlags_WidthFixed))
        *p_flags &= ~(ImGuiTableColumnFlags_WidthMask_ ^ ImGuiTableColumnFlags_WidthFixed);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_NoResize"), p_flags, ImGuiTableColumnFlags_NoResize);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_NoReorder"), p_flags, ImGuiTableColumnFlags_NoReorder);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_NoHide"), p_flags, ImGuiTableColumnFlags_NoHide);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_NoClip"), p_flags, ImGuiTableColumnFlags_NoClip);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_NoSort"), p_flags, ImGuiTableColumnFlags_NoSort);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_NoSortAscending"), p_flags, ImGuiTableColumnFlags_NoSortAscending);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_NoSortDescending"), p_flags, ImGuiTableColumnFlags_NoSortDescending);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_NoHeaderLabel"), p_flags, ImGuiTableColumnFlags_NoHeaderLabel);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_NoHeaderWidth"), p_flags, ImGuiTableColumnFlags_NoHeaderWidth);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_PreferSortAscending"), p_flags, ImGuiTableColumnFlags_PreferSortAscending);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_PreferSortDescending"), p_flags, ImGuiTableColumnFlags_PreferSortDescending);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_IndentEnable"), p_flags, ImGuiTableColumnFlags_IndentEnable); ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Default for column 0"));
    ImGui::CheckboxFlags(OBFUSCATE_STR("_IndentDisable"), p_flags, ImGuiTableColumnFlags_IndentDisable); ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Default for column >0"));
}

static void ShowTableColumnsStatusFlags(ImGuiTableColumnFlags flags)
{
    rtx_spoof_func;
    ImGui::CheckboxFlags(OBFUSCATE_STR("_IsEnabled"), &flags, ImGuiTableColumnFlags_IsEnabled);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_IsVisible"), &flags, ImGuiTableColumnFlags_IsVisible);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_IsSorted"), &flags, ImGuiTableColumnFlags_IsSorted);
    ImGui::CheckboxFlags(OBFUSCATE_STR("_IsHovered"), &flags, ImGuiTableColumnFlags_IsHovered);
}

static void ShowDemoWindowTables()
{
    rtx_spoof_func;
    //ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Tables"));
    if (!ImGui::CollapsingHeader(OBFUSCATE_STR("Tables & Columns")))
        return;

    // Using those as a base value to create width/height that are factor of the size of our font
    const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;
    const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();

    ImGui::PushID("Tables");

    int open_action = -1;
    if (ImGui::Button("Open all"))
        open_action = 1;
    ImGui::SameLine();
    if (ImGui::Button("Close all"))
        open_action = 0;
    ImGui::SameLine();

    // Options
    static bool disable_indent = false;
    ImGui::Checkbox("Disable tree indentation", &disable_indent);
    ImGui::SameLine();
    HelpMarker("Disable the indenting of tree nodes so demo tables can use the full window width.");
    ImGui::Separator();
    if (disable_indent)
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.0f);

    // About Styling of tables
    // Most settings are configured on a per-table basis via the flags passed to BeginTable() and TableSetupColumns APIs.
    // There are however a few settings that a shared and part of the ImGuiStyle structure:
    //   style.CellPadding                          // Padding within each cell
    //   style.Colors[ImGuiCol_TableHeaderBg]       // Table header background
    //   style.Colors[ImGuiCol_TableBorderStrong]   // Table outer and header borders
    //   style.Colors[ImGuiCol_TableBorderLight]    // Table inner borders
    //   style.Colors[ImGuiCol_TableRowBg]          // Table row background when ImGuiTableFlags_RowBg is enabled (even rows)
    //   style.Colors[ImGuiCol_TableRowBgAlt]       // Table row background when ImGuiTableFlags_RowBg is enabled (odds rows)

    // Demos
    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Basic");
    if (ImGui::TreeNode("Basic"))
    {
        // Here we will showcase three different ways to output a table.
        // They are very simple variations of a same thing!

        // [Method 1] Using TableNextRow() to create a new row, and TableSetColumnIndex() to select the column.
        // In many situations, this is the most flexible and easy to use pattern.
        HelpMarker("Using TableNextRow() + calling TableSetColumnIndex() _before_ each cell, in a loop.");
        if (ImGui::BeginTable("table1", 3))
        {
            for (int row = 0; row < 4; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("Row %d Column %d", row, column);
                }
            }
            ImGui::EndTable();
        }

        // [Method 2] Using TableNextColumn() called multiple times, instead of using a for loop + TableSetColumnIndex().
        // This is generally more convenient when you have code manually submitting the contents of each column.
        HelpMarker("Using TableNextRow() + calling TableNextColumn() _before_ each cell, manually.");
        if (ImGui::BeginTable("table2", 3))
        {
            for (int row = 0; row < 4; row++)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Row %d", row);
                ImGui::TableNextColumn();
                ImGui::Text("Some contents");
                ImGui::TableNextColumn();
                ImGui::Text("123.456");
            }
            ImGui::EndTable();
        }

        // [Method 3] We call TableNextColumn() _before_ each cell. We never call TableNextRow(),
        // as TableNextColumn() will automatically wrap around and create new rows as needed.
        // This is generally more convenient when your cells all contains the same type of data.
        HelpMarker(
            "Only using TableNextColumn(), which tends to be convenient for tables where every cell contains the same type of contents.\n"
            "This is also more similar to the old NextColumn() function of the Columns API, and provided to facilitate the Columns->Tables API transition.");
        if (ImGui::BeginTable("table3", 3))
        {
            for (int item = 0; item < 14; item++)
            {
                ImGui::TableNextColumn();
                ImGui::Text("Item %d", item);
            }
            ImGui::EndTable();
        }

        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Borders, background");
    if (ImGui::TreeNode("Borders, background"))
    {
        // Expose a few Borders related flags interactively
        enum ContentsType { CT_Text, CT_FillButton };
        static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
        static bool display_headers = false;
        static int contents_type = CT_Text;

        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_RowBg", &flags, ImGuiTableFlags_RowBg);
        ImGui::CheckboxFlags("ImGuiTableFlags_Borders", &flags, ImGuiTableFlags_Borders);
        ImGui::SameLine(); HelpMarker("ImGuiTableFlags_Borders\n = ImGuiTableFlags_BordersInnerV\n | ImGuiTableFlags_BordersOuterV\n | ImGuiTableFlags_BordersInnerV\n | ImGuiTableFlags_BordersOuterH");
        ImGui::Indent();

        ImGui::CheckboxFlags("ImGuiTableFlags_BordersH", &flags, ImGuiTableFlags_BordersH);
        ImGui::Indent();
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersOuterH", &flags, ImGuiTableFlags_BordersOuterH);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersInnerH", &flags, ImGuiTableFlags_BordersInnerH);
        ImGui::Unindent();

        ImGui::CheckboxFlags("ImGuiTableFlags_BordersV", &flags, ImGuiTableFlags_BordersV);
        ImGui::Indent();
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersOuterV", &flags, ImGuiTableFlags_BordersOuterV);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersInnerV", &flags, ImGuiTableFlags_BordersInnerV);
        ImGui::Unindent();

        ImGui::CheckboxFlags("ImGuiTableFlags_BordersOuter", &flags, ImGuiTableFlags_BordersOuter);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersInner", &flags, ImGuiTableFlags_BordersInner);
        ImGui::Unindent();

        ImGui::AlignTextToFramePadding(); ImGui::Text("Cell contents:");
        ImGui::SameLine(); ImGui::RadioButton("Text", &contents_type, CT_Text);
        ImGui::SameLine(); ImGui::RadioButton("FillButton", &contents_type, CT_FillButton);
        ImGui::Checkbox("Display headers", &display_headers);
        ImGui::CheckboxFlags("ImGuiTableFlags_NoBordersInBody", &flags, ImGuiTableFlags_NoBordersInBody); ImGui::SameLine(); HelpMarker("Disable vertical borders in columns Body (borders will always appear in Headers");
        PopStyleCompact();

        if (ImGui::BeginTable("table1", 3, flags))
        {
            // Display headers so we can inspect their interaction with borders.
            // (Headers are not the main purpose of this section of the demo, so we are not elaborating on them too much. See other sections for details)
            if (display_headers)
            {
                ImGui::TableSetupColumn("One");
                ImGui::TableSetupColumn("Two");
                ImGui::TableSetupColumn("Three");
                ImGui::TableHeadersRow();
            }

            for (int row = 0; row < 5; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    char buf[32];
                    sprintf(buf, "Hello %d,%d", column, row);
                    if (contents_type == CT_Text)
                        ImGui::TextUnformatted(buf);
                    else if (contents_type == CT_FillButton)
                        ImGui::Button(buf, ImVec2(-FLT_MIN, 0.0f));
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Resizable, stretch");
    if (ImGui::TreeNode("Resizable, stretch"))
    {
        // By default, if we don't enable ScrollX the sizing policy for each column is "Stretch"
        // All columns maintain a sizing weight, and they will occupy all available width.
        static ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_ContextMenuInBody;
        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_Resizable", &flags, ImGuiTableFlags_Resizable);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersV", &flags, ImGuiTableFlags_BordersV);
        ImGui::SameLine(); HelpMarker("Using the _Resizable flag automatically enables the _BordersInnerV flag as well, this is why the resize borders are still showing when unchecking this.");
        PopStyleCompact();

        if (ImGui::BeginTable("table1", 3, flags))
        {
            for (int row = 0; row < 5; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("Hello %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Resizable, fixed");
    if (ImGui::TreeNode("Resizable, fixed"))
    {
        // Here we use ImGuiTableFlags_SizingFixedFit (even though _ScrollX is not set)
        // So columns will adopt the "Fixed" policy and will maintain a fixed width regardless of the whole available width (unless table is small)
        // If there is not enough available width to fit all columns, they will however be resized down.
        // FIXME-TABLE: Providing a stretch-on-init would make sense especially for tables which don't have saved settings
        HelpMarker(
            "Using _Resizable + _SizingFixedFit flags.\n"
            "Fixed-width columns generally makes more sense if you want to use horizontal scrolling.\n\n"
            "Double-click a column border to auto-fit the column to its contents.");
        PushStyleCompact();
        static ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_ContextMenuInBody;
        ImGui::CheckboxFlags("ImGuiTableFlags_NoHostExtendX", &flags, ImGuiTableFlags_NoHostExtendX);
        PopStyleCompact();

        if (ImGui::BeginTable("table1", 3, flags))
        {
            for (int row = 0; row < 5; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("Hello %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Resizable, mixed");
    if (ImGui::TreeNode("Resizable, mixed"))
    {
        HelpMarker(
            "Using TableSetupColumn() to alter resizing policy on a per-column basis.\n\n"
            "When combining Fixed and Stretch columns, generally you only want one, maybe two trailing columns to use _WidthStretch.");
        static ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;

        if (ImGui::BeginTable("table1", 3, flags))
        {
            ImGui::TableSetupColumn("AAA", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("BBB", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("CCC", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (int row = 0; row < 5; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("%s %d,%d", (column == 2) ? "Stretch" : "Fixed", column, row);
                }
            }
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("table2", 6, flags))
        {
            ImGui::TableSetupColumn("AAA", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("BBB", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("CCC", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide);
            ImGui::TableSetupColumn("DDD", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("EEE", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("FFF", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultHide);
            ImGui::TableHeadersRow();
            for (int row = 0; row < 5; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 6; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("%s %d,%d", (column >= 3) ? "Stretch" : "Fixed", column, row);
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Reorderable, hideable, with headers");
    if (ImGui::TreeNode("Reorderable, hideable, with headers"))
    {
        HelpMarker(
            "Click and drag column headers to reorder columns.\n\n"
            "Right-click on a header to open a context menu.");
        static ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV;
        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_Resizable", &flags, ImGuiTableFlags_Resizable);
        ImGui::CheckboxFlags("ImGuiTableFlags_Reorderable", &flags, ImGuiTableFlags_Reorderable);
        ImGui::CheckboxFlags("ImGuiTableFlags_Hideable", &flags, ImGuiTableFlags_Hideable);
        ImGui::CheckboxFlags("ImGuiTableFlags_NoBordersInBody", &flags, ImGuiTableFlags_NoBordersInBody);
        ImGui::CheckboxFlags("ImGuiTableFlags_NoBordersInBodyUntilResize", &flags, ImGuiTableFlags_NoBordersInBodyUntilResize); ImGui::SameLine(); HelpMarker("Disable vertical borders in columns Body until hovered for resize (borders will always appear in Headers)");
        PopStyleCompact();

        if (ImGui::BeginTable("table1", 3, flags))
        {
            // Submit columns name with TableSetupColumn() and call TableHeadersRow() to create a row with a header in each column.
            // (Later we will show how TableSetupColumn() has other uses, optional flags, sizing weight etc.)
            ImGui::TableSetupColumn("One");
            ImGui::TableSetupColumn("Two");
            ImGui::TableSetupColumn("Three");
            ImGui::TableHeadersRow();
            for (int row = 0; row < 6; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("Hello %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }

        // Use outer_size.x == 0.0f instead of default to make the table as tight as possible (only valid when no scrolling and no stretch column)
        if (ImGui::BeginTable("table2", 3, flags | ImGuiTableFlags_SizingFixedFit, ImVec2(0.0f, 0.0f)))
        {
            ImGui::TableSetupColumn("One");
            ImGui::TableSetupColumn("Two");
            ImGui::TableSetupColumn("Three");
            ImGui::TableHeadersRow();
            for (int row = 0; row < 6; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("Fixed %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Padding");
    if (ImGui::TreeNode("Padding"))
    {
        // First example: showcase use of padding flags and effect of BorderOuterV/BorderInnerV on X padding.
        // We don't expose BorderOuterH/BorderInnerH here because they have no effect on X padding.
        HelpMarker(
            "We often want outer padding activated when any using features which makes the edges of a column visible:\n"
            "e.g.:\n"
            "- BorderOuterV\n"
            "- any form of row selection\n"
            "Because of this, activating BorderOuterV sets the default to PadOuterX. Using PadOuterX or NoPadOuterX you can override the default.\n\n"
            "Actual padding values are using style.CellPadding.\n\n"
            "In this demo we don't show horizontal borders to emphasize how they don't affect default horizontal padding.");

        static ImGuiTableFlags flags1 = ImGuiTableFlags_BordersV;
        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_PadOuterX", &flags1, ImGuiTableFlags_PadOuterX);
        ImGui::SameLine(); HelpMarker("Enable outer-most padding (default if ImGuiTableFlags_BordersOuterV is set)");
        ImGui::CheckboxFlags("ImGuiTableFlags_NoPadOuterX", &flags1, ImGuiTableFlags_NoPadOuterX);
        ImGui::SameLine(); HelpMarker("Disable outer-most padding (default if ImGuiTableFlags_BordersOuterV is not set)");
        ImGui::CheckboxFlags("ImGuiTableFlags_NoPadInnerX", &flags1, ImGuiTableFlags_NoPadInnerX);
        ImGui::SameLine(); HelpMarker("Disable inner padding between columns (double inner padding if BordersOuterV is on, single inner padding if BordersOuterV is off)");
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersOuterV", &flags1, ImGuiTableFlags_BordersOuterV);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersInnerV", &flags1, ImGuiTableFlags_BordersInnerV);
        static bool show_headers = false;
        ImGui::Checkbox("show_headers", &show_headers);
        PopStyleCompact();

        if (ImGui::BeginTable("table_padding", 3, flags1))
        {
            if (show_headers)
            {
                ImGui::TableSetupColumn("One");
                ImGui::TableSetupColumn("Two");
                ImGui::TableSetupColumn("Three");
                ImGui::TableHeadersRow();
            }

            for (int row = 0; row < 5; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    if (row == 0)
                    {
                        ImGui::Text("Avail %.2f", ImGui::GetContentRegionAvail().x);
                    }
                    else
                    {
                        char buf[32];
                        sprintf(buf, "Hello %d,%d", column, row);
                        ImGui::Button(buf, ImVec2(-FLT_MIN, 0.0f));
                    }
                    //if (ImGui::TableGetColumnFlags() & ImGuiTableColumnFlags_IsHovered)
                    //    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(0, 100, 0, 255));
                }
            }
            ImGui::EndTable();
        }

        // Second example: set style.CellPadding to (0.0) or a custom value.
        // FIXME-TABLE: Vertical border effectively not displayed the same way as horizontal one...
        HelpMarker("Setting style.CellPadding to (0,0) or a custom value.");
        static ImGuiTableFlags flags2 = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
        static ImVec2 cell_padding(0.0f, 0.0f);
        static bool show_widget_frame_bg = true;

        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_Borders", &flags2, ImGuiTableFlags_Borders);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersH", &flags2, ImGuiTableFlags_BordersH);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersV", &flags2, ImGuiTableFlags_BordersV);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersInner", &flags2, ImGuiTableFlags_BordersInner);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersOuter", &flags2, ImGuiTableFlags_BordersOuter);
        ImGui::CheckboxFlags("ImGuiTableFlags_RowBg", &flags2, ImGuiTableFlags_RowBg);
        ImGui::CheckboxFlags("ImGuiTableFlags_Resizable", &flags2, ImGuiTableFlags_Resizable);
        ImGui::Checkbox("show_widget_frame_bg", &show_widget_frame_bg);
        ImGui::SliderFloat2("CellPadding", &cell_padding.x, 0.0f, 10.0f, "%.0f");
        PopStyleCompact();

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cell_padding);
        if (ImGui::BeginTable("table_padding_2", 3, flags2))
        {
            static char text_bufs[3 * 5][16]; // Mini text storage for 3x5 cells
            static bool init = true;
            if (!show_widget_frame_bg)
                ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
            for (int cell = 0; cell < 3 * 5; cell++)
            {
                ImGui::TableNextColumn();
                if (init)
                    strcpy(text_bufs[cell], "edit me");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::PushID(cell);
                ImGui::InputText("##cell", text_bufs[cell], IM_ARRAYSIZE(text_bufs[cell]));
                ImGui::PopID();
            }
            if (!show_widget_frame_bg)
                ImGui::PopStyleColor();
            init = false;
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();

        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Explicit widths");
    if (ImGui::TreeNode("Sizing policies"))
    {
        static ImGuiTableFlags flags1 = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ContextMenuInBody;
        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_Resizable", &flags1, ImGuiTableFlags_Resizable);
        ImGui::CheckboxFlags("ImGuiTableFlags_NoHostExtendX", &flags1, ImGuiTableFlags_NoHostExtendX);
        PopStyleCompact();

        static ImGuiTableFlags sizing_policy_flags[4] = { ImGuiTableFlags_SizingFixedFit, ImGuiTableFlags_SizingFixedSame, ImGuiTableFlags_SizingStretchProp, ImGuiTableFlags_SizingStretchSame };
        for (int table_n = 0; table_n < 4; table_n++)
        {
            ImGui::PushID(table_n);
            ImGui::SetNextItemWidth(TEXT_BASE_WIDTH * 30);
            EditTableSizingFlags(&sizing_policy_flags[table_n]);

            // To make it easier to understand the different sizing policy,
            // For each policy: we display one table where the columns have equal contents width, and one where the columns have different contents width.
            if (ImGui::BeginTable("table1", 3, sizing_policy_flags[table_n] | flags1))
            {
                for (int row = 0; row < 3; row++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("Oh dear");
                    ImGui::TableNextColumn(); ImGui::Text("Oh dear");
                    ImGui::TableNextColumn(); ImGui::Text("Oh dear");
                }
                ImGui::EndTable();
            }
            if (ImGui::BeginTable("table2", 3, sizing_policy_flags[table_n] | flags1))
            {
                for (int row = 0; row < 3; row++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("AAAA");
                    ImGui::TableNextColumn(); ImGui::Text("BBBBBBBB");
                    ImGui::TableNextColumn(); ImGui::Text("CCCCCCCCCCCC");
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Advanced");
        ImGui::SameLine();
        HelpMarker("This section allows you to interact and see the effect of various sizing policies depending on whether Scroll is enabled and the contents of your columns.");

        enum ContentsType { CT_ShowWidth, CT_ShortText, CT_LongText, CT_Button, CT_FillButton, CT_InputText };
        static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
        static int contents_type = CT_ShowWidth;
        static int column_count = 3;

        PushStyleCompact();
        ImGui::PushID("Advanced");
        ImGui::PushItemWidth(TEXT_BASE_WIDTH * 30);
        EditTableSizingFlags(&flags);
        ImGui::Combo("Contents", &contents_type, "Show width\0Short Text\0Long Text\0Button\0Fill Button\0InputText\0");
        if (contents_type == CT_FillButton)
        {
            ImGui::SameLine();
            HelpMarker("Be mindful that using right-alignment (e.g. size.x = -FLT_MIN) creates a feedback loop where contents width can feed into auto-column width can feed into contents width.");
        }
        ImGui::DragInt("Columns", &column_count, 0.1f, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::CheckboxFlags("ImGuiTableFlags_Resizable", &flags, ImGuiTableFlags_Resizable);
        ImGui::CheckboxFlags("ImGuiTableFlags_PreciseWidths", &flags, ImGuiTableFlags_PreciseWidths);
        ImGui::SameLine(); HelpMarker("Disable distributing remainder width to stretched columns (width allocation on a 100-wide table with 3 columns: Without this flag: 33,33,34. With this flag: 33,33,33). With larger number of columns, resizing will appear to be less smooth.");
        ImGui::CheckboxFlags("ImGuiTableFlags_ScrollX", &flags, ImGuiTableFlags_ScrollX);
        ImGui::CheckboxFlags("ImGuiTableFlags_ScrollY", &flags, ImGuiTableFlags_ScrollY);
        ImGui::CheckboxFlags("ImGuiTableFlags_NoClip", &flags, ImGuiTableFlags_NoClip);
        ImGui::PopItemWidth();
        ImGui::PopID();
        PopStyleCompact();

        if (ImGui::BeginTable("table2", column_count, flags, ImVec2(0.0f, TEXT_BASE_HEIGHT * 7)))
        {
            for (int cell = 0; cell < 10 * column_count; cell++)
            {
                ImGui::TableNextColumn();
                int column = ImGui::TableGetColumnIndex();
                int row = ImGui::TableGetRowIndex();

                ImGui::PushID(cell);
                char label[32];
                static char text_buf[32] = "";
                sprintf(label, "Hello %d,%d", column, row);
                switch (contents_type)
                {
                case CT_ShortText:  ImGui::TextUnformatted(label); break;
                case CT_LongText:   ImGui::Text("Some %s text %d,%d\nOver two lines..", column == 0 ? "long" : "longeeer", column, row); break;
                case CT_ShowWidth:  ImGui::Text("W: %.1f", ImGui::GetContentRegionAvail().x); break;
                case CT_Button:     ImGui::Button(label); break;
                case CT_FillButton: ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f)); break;
                case CT_InputText:  ImGui::SetNextItemWidth(-FLT_MIN); ImGui::InputText("##", text_buf, IM_ARRAYSIZE(text_buf)); break;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Vertical scrolling, with clipping");
    if (ImGui::TreeNode("Vertical scrolling, with clipping"))
    {
        HelpMarker("Here we activate ScrollY, which will create a child window container to allow hosting scrollable contents.\n\nWe also demonstrate using ImGuiListClipper to virtualize the submission of many items.");
        static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;

        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_ScrollY", &flags, ImGuiTableFlags_ScrollY);
        PopStyleCompact();

        // When using ScrollX or ScrollY we need to specify a size for our table container!
        // Otherwise by default the table will fit all available space, like a BeginChild() call.
        ImVec2 outer_size = ImVec2(0.0f, TEXT_BASE_HEIGHT * 8);
        if (ImGui::BeginTable("table_scrolly", 3, flags, outer_size))
        {
            ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
            ImGui::TableSetupColumn("One", ImGuiTableColumnFlags_None);
            ImGui::TableSetupColumn("Two", ImGuiTableColumnFlags_None);
            ImGui::TableSetupColumn("Three", ImGuiTableColumnFlags_None);
            ImGui::TableHeadersRow();

            // Demonstrate using clipper for large vertical lists
            ImGuiListClipper clipper;
            clipper.Begin(1000);
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
                {
                    ImGui::TableNextRow();
                    for (int column = 0; column < 3; column++)
                    {
                        ImGui::TableSetColumnIndex(column);
                        ImGui::Text("Hello %d,%d", column, row);
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Horizontal scrolling");
    if (ImGui::TreeNode("Horizontal scrolling"))
    {
        HelpMarker(
            "When ScrollX is enabled, the default sizing policy becomes ImGuiTableFlags_SizingFixedFit, "
            "as automatically stretching columns doesn't make much sense with horizontal scrolling.\n\n"
            "Also note that as of the current version, you will almost always want to enable ScrollY along with ScrollX,"
            "because the container window won't automatically extend vertically to fix contents (this may be improved in future versions).");
        static ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;
        static int freeze_cols = 1;
        static int freeze_rows = 1;

        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_Resizable", &flags, ImGuiTableFlags_Resizable);
        ImGui::CheckboxFlags("ImGuiTableFlags_ScrollX", &flags, ImGuiTableFlags_ScrollX);
        ImGui::CheckboxFlags("ImGuiTableFlags_ScrollY", &flags, ImGuiTableFlags_ScrollY);
        ImGui::SetNextItemWidth(ImGui::GetFrameHeight());
        ImGui::DragInt("freeze_cols", &freeze_cols, 0.2f, 0, 9, NULL, ImGuiSliderFlags_NoInput);
        ImGui::SetNextItemWidth(ImGui::GetFrameHeight());
        ImGui::DragInt("freeze_rows", &freeze_rows, 0.2f, 0, 9, NULL, ImGuiSliderFlags_NoInput);
        PopStyleCompact();

        // When using ScrollX or ScrollY we need to specify a size for our table container!
        // Otherwise by default the table will fit all available space, like a BeginChild() call.
        ImVec2 outer_size = ImVec2(0.0f, TEXT_BASE_HEIGHT * 8);
        if (ImGui::BeginTable("table_scrollx", 7, flags, outer_size))
        {
            ImGui::TableSetupScrollFreeze(freeze_cols, freeze_rows);
            ImGui::TableSetupColumn("Line #", ImGuiTableColumnFlags_NoHide); // Make the first column not hideable to match our use of TableSetupScrollFreeze()
            ImGui::TableSetupColumn("One");
            ImGui::TableSetupColumn("Two");
            ImGui::TableSetupColumn("Three");
            ImGui::TableSetupColumn("Four");
            ImGui::TableSetupColumn("Five");
            ImGui::TableSetupColumn("Six");
            ImGui::TableHeadersRow();
            for (int row = 0; row < 20; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 7; column++)
                {
                    // Both TableNextColumn() and TableSetColumnIndex() return true when a column is visible or performing width measurement.
                    // Because here we know that:
                    // - A) all our columns are contributing the same to row height
                    // - B) column 0 is always visible,
                    // We only always submit this one column and can skip others.
                    // More advanced per-column clipping behaviors may benefit from polling the status flags via TableGetColumnFlags().
                    if (!ImGui::TableSetColumnIndex(column) && column > 0)
                        continue;
                    if (column == 0)
                        ImGui::Text("Line %d", row);
                    else
                        ImGui::Text("Hello world %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Stretch + ScrollX");
        ImGui::SameLine();
        HelpMarker(
            "Showcase using Stretch columns + ScrollX together: "
            "this is rather unusual and only makes sense when specifying an 'inner_width' for the table!\n"
            "Without an explicit value, inner_width is == outer_size.x and therefore using Stretch columns + ScrollX together doesn't make sense.");
        static ImGuiTableFlags flags2 = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_ContextMenuInBody;
        static float inner_width = 1000.0f;
        PushStyleCompact();
        ImGui::PushID("flags3");
        ImGui::PushItemWidth(TEXT_BASE_WIDTH * 30);
        ImGui::CheckboxFlags("ImGuiTableFlags_ScrollX", &flags2, ImGuiTableFlags_ScrollX);
        ImGui::DragFloat("inner_width", &inner_width, 1.0f, 0.0f, FLT_MAX, "%.1f");
        ImGui::PopItemWidth();
        ImGui::PopID();
        PopStyleCompact();
        if (ImGui::BeginTable("table2", 7, flags2, outer_size, inner_width))
        {
            for (int cell = 0; cell < 20 * 7; cell++)
            {
                ImGui::TableNextColumn();
                ImGui::Text("Hello world %d,%d", ImGui::TableGetColumnIndex(), ImGui::TableGetRowIndex());
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Columns flags");
    if (ImGui::TreeNode("Columns flags"))
    {
        // Create a first table just to show all the options/flags we want to make visible in our example!
        const int column_count = 3;
        const char* column_names[column_count] = { "One", "Two", "Three" };
        static ImGuiTableColumnFlags column_flags[column_count] = { ImGuiTableColumnFlags_DefaultSort, ImGuiTableColumnFlags_None, ImGuiTableColumnFlags_DefaultHide };
        static ImGuiTableColumnFlags column_flags_out[column_count] = { 0, 0, 0 }; // Output from TableGetColumnFlags()

        if (ImGui::BeginTable("table_columns_flags_checkboxes", column_count, ImGuiTableFlags_None))
        {
            PushStyleCompact();
            for (int column = 0; column < column_count; column++)
            {
                ImGui::TableNextColumn();
                ImGui::PushID(column);
                ImGui::AlignTextToFramePadding(); // FIXME-TABLE: Workaround for wrong text baseline propagation across columns
                ImGui::Text("'%s'", column_names[column]);
                ImGui::Spacing();
                ImGui::Text("Input flags:");
                EditTableColumnsFlags(&column_flags[column]);
                ImGui::Spacing();
                ImGui::Text("Output flags:");
                ImGui::BeginDisabled();
                ShowTableColumnsStatusFlags(column_flags_out[column]);
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            PopStyleCompact();
            ImGui::EndTable();
        }

        // Create the real table we care about for the example!
        // We use a scrolling table to be able to showcase the difference between the _IsEnabled and _IsVisible flags above, otherwise in
        // a non-scrolling table columns are always visible (unless using ImGuiTableFlags_NoKeepColumnsVisible + resizing the parent window down)
        const ImGuiTableFlags flags
            = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY
            | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV
            | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable;
        ImVec2 outer_size = ImVec2(0.0f, TEXT_BASE_HEIGHT * 9);
        if (ImGui::BeginTable("table_columns_flags", column_count, flags, outer_size))
        {
            for (int column = 0; column < column_count; column++)
                ImGui::TableSetupColumn(column_names[column], column_flags[column]);
            ImGui::TableHeadersRow();
            for (int column = 0; column < column_count; column++)
                column_flags_out[column] = ImGui::TableGetColumnFlags(column);
            float indent_step = (float)((int)TEXT_BASE_WIDTH / 2);
            for (int row = 0; row < 8; row++)
            {
                ImGui::Indent(indent_step); // Add some indentation to demonstrate usage of per-column IndentEnable/IndentDisable flags.
                ImGui::TableNextRow();
                for (int column = 0; column < column_count; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("%s %s", (column == 0) ? "Indented" : "Hello", ImGui::TableGetColumnName(column));
                }
            }
            ImGui::Unindent(indent_step * 8.0f);

            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Columns widths");
    if (ImGui::TreeNode("Columns widths"))
    {
        HelpMarker("Using TableSetupColumn() to setup default width.");

        static ImGuiTableFlags flags1 = ImGuiTableFlags_Borders | ImGuiTableFlags_NoBordersInBodyUntilResize;
        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_Resizable", &flags1, ImGuiTableFlags_Resizable);
        ImGui::CheckboxFlags("ImGuiTableFlags_NoBordersInBodyUntilResize", &flags1, ImGuiTableFlags_NoBordersInBodyUntilResize);
        PopStyleCompact();
        if (ImGui::BeginTable("table1", 3, flags1))
        {
            // We could also set ImGuiTableFlags_SizingFixedFit on the table and all columns will default to ImGuiTableColumnFlags_WidthFixed.
            ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, 100.0f); // Default to 100.0f
            ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthFixed, 200.0f); // Default to 200.0f
            ImGui::TableSetupColumn("three", ImGuiTableColumnFlags_WidthFixed);       // Default to auto
            ImGui::TableHeadersRow();
            for (int row = 0; row < 4; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    if (row == 0)
                        ImGui::Text("(w: %5.1f)", ImGui::GetContentRegionAvail().x);
                    else
                        ImGui::Text("Hello %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }

        HelpMarker("Using TableSetupColumn() to setup explicit width.\n\nUnless _NoKeepColumnsVisible is set, fixed columns with set width may still be shrunk down if there's not enough space in the host.");

        static ImGuiTableFlags flags2 = ImGuiTableFlags_None;
        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_NoKeepColumnsVisible", &flags2, ImGuiTableFlags_NoKeepColumnsVisible);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersInnerV", &flags2, ImGuiTableFlags_BordersInnerV);
        ImGui::CheckboxFlags("ImGuiTableFlags_BordersOuterV", &flags2, ImGuiTableFlags_BordersOuterV);
        PopStyleCompact();
        if (ImGui::BeginTable("table2", 4, flags2))
        {
            // We could also set ImGuiTableFlags_SizingFixedFit on the table and all columns will default to ImGuiTableColumnFlags_WidthFixed.
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 15.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 30.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 15.0f);
            for (int row = 0; row < 5; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 4; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    if (row == 0)
                        ImGui::Text("(w: %5.1f)", ImGui::GetContentRegionAvail().x);
                    else
                        ImGui::Text("Hello %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Nested tables");
    if (ImGui::TreeNode("Nested tables"))
    {
        HelpMarker("This demonstrates embedding a table into another table cell.");

        if (ImGui::BeginTable("table_nested1", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable))
        {
            ImGui::TableSetupColumn("A0");
            ImGui::TableSetupColumn("A1");
            ImGui::TableHeadersRow();

            ImGui::TableNextColumn();
            ImGui::Text("A0 Row 0");
            {
                float rows_height = TEXT_BASE_HEIGHT * 2;
                if (ImGui::BeginTable("table_nested2", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable))
                {
                    ImGui::TableSetupColumn("B0");
                    ImGui::TableSetupColumn("B1");
                    ImGui::TableHeadersRow();

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, rows_height);
                    ImGui::TableNextColumn();
                    ImGui::Text("B0 Row 0");
                    ImGui::TableNextColumn();
                    ImGui::Text("B1 Row 0");
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, rows_height);
                    ImGui::TableNextColumn();
                    ImGui::Text("B0 Row 1");
                    ImGui::TableNextColumn();
                    ImGui::Text("B1 Row 1");

                    ImGui::EndTable();
                }
            }
            ImGui::TableNextColumn(); ImGui::Text("A1 Row 0");
            ImGui::TableNextColumn(); ImGui::Text("A0 Row 1");
            ImGui::TableNextColumn(); ImGui::Text("A1 Row 1");
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Row height");
    if (ImGui::TreeNode("Row height"))
    {
        HelpMarker("You can pass a 'min_row_height' to TableNextRow().\n\nRows are padded with 'style.CellPadding.y' on top and bottom, so effectively the minimum row height will always be >= 'style.CellPadding.y * 2.0f'.\n\nWe cannot honor a _maximum_ row height as that would require a unique clipping rectangle per row.");
        if (ImGui::BeginTable("table_row_height", 1, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV))
        {
            for (int row = 0; row < 10; row++)
            {
                float min_row_height = (float)(int)(TEXT_BASE_HEIGHT * 0.30f * row);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, min_row_height);
                ImGui::TableNextColumn();
                ImGui::Text("min_row_height = %.2f", min_row_height);
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Outer size");
    if (ImGui::TreeNode("Outer size"))
    {
        // Showcasing use of ImGuiTableFlags_NoHostExtendX and ImGuiTableFlags_NoHostExtendY
        // Important to that note how the two flags have slightly different behaviors!
        ImGui::Text("Using NoHostExtendX and NoHostExtendY:");
        PushStyleCompact();
        static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ContextMenuInBody | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
        ImGui::CheckboxFlags("ImGuiTableFlags_NoHostExtendX", &flags, ImGuiTableFlags_NoHostExtendX);
        ImGui::SameLine(); HelpMarker("Make outer width auto-fit to columns, overriding outer_size.x value.\n\nOnly available when ScrollX/ScrollY are disabled and Stretch columns are not used.");
        ImGui::CheckboxFlags("ImGuiTableFlags_NoHostExtendY", &flags, ImGuiTableFlags_NoHostExtendY);
        ImGui::SameLine(); HelpMarker("Make outer height stop exactly at outer_size.y (prevent auto-extending table past the limit).\n\nOnly available when ScrollX/ScrollY are disabled. Data below the limit will be clipped and not visible.");
        PopStyleCompact();

        ImVec2 outer_size = ImVec2(0.0f, TEXT_BASE_HEIGHT * 5.5f);
        if (ImGui::BeginTable("table1", 3, flags, outer_size))
        {
            for (int row = 0; row < 10; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("Cell %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }
        ImGui::SameLine();
        ImGui::Text("Hello!");

        ImGui::Spacing();

        ImGui::Text("Using explicit size:");
        if (ImGui::BeginTable("table2", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(TEXT_BASE_WIDTH * 30, 0.0f)))
        {
            for (int row = 0; row < 5; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("Cell %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }
        ImGui::SameLine();
        if (ImGui::BeginTable("table3", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(TEXT_BASE_WIDTH * 30, 0.0f)))
        {
            for (int row = 0; row < 3; row++)
            {
                ImGui::TableNextRow(0, TEXT_BASE_HEIGHT * 1.5f);
                for (int column = 0; column < 3; column++)
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("Cell %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }

        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Background color");
    if (ImGui::TreeNode("Background color"))
    {
        static ImGuiTableFlags flags = ImGuiTableFlags_RowBg;
        static int row_bg_type = 1;
        static int row_bg_target = 1;
        static int cell_bg_type = 1;

        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_Borders", &flags, ImGuiTableFlags_Borders);
        ImGui::CheckboxFlags("ImGuiTableFlags_RowBg", &flags, ImGuiTableFlags_RowBg);
        ImGui::SameLine(); HelpMarker("ImGuiTableFlags_RowBg automatically sets RowBg0 to alternative colors pulled from the Style.");
        ImGui::Combo("row bg type", (int*)&row_bg_type, "None\0Red\0Gradient\0");
        ImGui::Combo("row bg target", (int*)&row_bg_target, "RowBg0\0RowBg1\0"); ImGui::SameLine(); HelpMarker("Target RowBg0 to override the alternating odd/even colors,\nTarget RowBg1 to blend with them.");
        ImGui::Combo("cell bg type", (int*)&cell_bg_type, "None\0Blue\0"); ImGui::SameLine(); HelpMarker("We are colorizing cells to B1->C2 here.");
        IM_ASSERT(row_bg_type >= 0 && row_bg_type <= 2);
        IM_ASSERT(row_bg_target >= 0 && row_bg_target <= 1);
        IM_ASSERT(cell_bg_type >= 0 && cell_bg_type <= 1);
        PopStyleCompact();

        if (ImGui::BeginTable("table1", 5, flags))
        {
            for (int row = 0; row < 6; row++)
            {
                ImGui::TableNextRow();

                // Demonstrate setting a row background color with 'ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBgX, ...)'
                // We use a transparent color so we can see the one behind in case our target is RowBg1 and RowBg0 was already targeted by the ImGuiTableFlags_RowBg flag.
                if (row_bg_type != 0)
                {
                    ImU32 row_bg_color = ImGui::GetColorU32(row_bg_type == 1 ? ImVec4(0.7f, 0.3f, 0.3f, 0.65f) : ImVec4(0.2f + row * 0.1f, 0.2f, 0.2f, 0.65f)); // Flat or Gradient?
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0 + row_bg_target, row_bg_color);
                }

                // Fill cells
                for (int column = 0; column < 5; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("%c%c", 'A' + row, '0' + column);

                    // Change background of Cells B1->C2
                    // Demonstrate setting a cell background color with 'ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ...)'
                    // (the CellBg color will be blended over the RowBg and ColumnBg colors)
                    // We can also pass a column number as a third parameter to TableSetBgColor() and do this outside the column loop.
                    if (row >= 1 && row <= 2 && column >= 1 && column <= 2 && cell_bg_type == 1)
                    {
                        ImU32 cell_bg_color = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.7f, 0.65f));
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cell_bg_color);
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Tree view");
    if (ImGui::TreeNode("Tree view"))
    {
        static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;

        if (ImGui::BeginTable("3ways", 3, flags))
        {
            // The first column will use the default _WidthStretch when ScrollX is Off and _WidthFixed when ScrollX is On
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 12.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 18.0f);
            ImGui::TableHeadersRow();

            // Simple storage to output a dummy file-system.
            struct MyTreeNode
            {
                const char*     Name;
                const char*     Type;
                int             Size;
                int             ChildIdx;
                int             ChildCount;
                static void DisplayNode(const MyTreeNode* node, const MyTreeNode* all_nodes)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const bool is_folder = (node->ChildCount > 0);
                    if (is_folder)
                    {
                        bool open = ImGui::TreeNodeEx(node->Name, ImGuiTreeNodeFlags_SpanFullWidth);
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("--");
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(node->Type);
                        if (open)
                        {
                            for (int child_n = 0; child_n < node->ChildCount; child_n++)
                                DisplayNode(&all_nodes[node->ChildIdx + child_n], all_nodes);
                            ImGui::TreePop();
                        }
                    }
                    else
                    {
                        ImGui::TreeNodeEx(node->Name, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", node->Size);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(node->Type);
                    }
                }
            };
            static const MyTreeNode nodes[] =
            {
                { "Root",                         "Folder",       -1,       1, 3    }, // 0
                { "Music",                        "Folder",       -1,       4, 2    }, // 1
                { "Textures",                     "Folder",       -1,       6, 3    }, // 2
                { "desktop.ini",                  "System file",  1024,    -1,-1    }, // 3
                { "File1_a.wav",                  "Audio file",   123000,  -1,-1    }, // 4
                { "File1_b.wav",                  "Audio file",   456000,  -1,-1    }, // 5
                { "Image001.png",                 "Image file",   203128,  -1,-1    }, // 6
                { "Copy of Image001.png",         "Image file",   203256,  -1,-1    }, // 7
                { "Copy of Image001 (Final2).png","Image file",   203512,  -1,-1    }, // 8
            };

            MyTreeNode::DisplayNode(&nodes[0], nodes);

            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Item width");
    if (ImGui::TreeNode("Item width"))
    {
        HelpMarker(
            "Showcase using PushItemWidth() and how it is preserved on a per-column basis.\n\n"
            "Note that on auto-resizing non-resizable fixed columns, querying the content width for e.g. right-alignment doesn't make sense.");
        if (ImGui::BeginTable("table_item_width", 3, ImGuiTableFlags_Borders))
        {
            ImGui::TableSetupColumn("small");
            ImGui::TableSetupColumn("half");
            ImGui::TableSetupColumn("right-align");
            ImGui::TableHeadersRow();

            for (int row = 0; row < 3; row++)
            {
                ImGui::TableNextRow();
                if (row == 0)
                {
                    // Setup ItemWidth once (instead of setting up every time, which is also possible but less efficient)
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushItemWidth(TEXT_BASE_WIDTH * 3.0f); // Small
                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushItemWidth(-FLT_MIN); // Right-aligned
                }

                // Draw our contents
                static float dummy_f = 0.0f;
                ImGui::PushID(row);
                ImGui::TableSetColumnIndex(0);
                ImGui::SliderFloat("float0", &dummy_f, 0.0f, 1.0f);
                ImGui::TableSetColumnIndex(1);
                ImGui::SliderFloat("float1", &dummy_f, 0.0f, 1.0f);
                ImGui::TableSetColumnIndex(2);
                ImGui::SliderFloat("##float2", &dummy_f, 0.0f, 1.0f); // No visible label since right-aligned
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    // Demonstrate using TableHeader() calls instead of TableHeadersRow()
    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Custom headers");
    if (ImGui::TreeNode("Custom headers"))
    {
        const int COLUMNS_COUNT = 3;
        if (ImGui::BeginTable("table_custom_headers", COLUMNS_COUNT, ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable))
        {
            ImGui::TableSetupColumn("Apricot");
            ImGui::TableSetupColumn("Banana");
            ImGui::TableSetupColumn("Cherry");

            // Dummy entire-column selection storage
            // FIXME: It would be nice to actually demonstrate full-featured selection using those checkbox.
            static bool column_selected[3] = {};

            // Instead of calling TableHeadersRow() we'll submit custom headers ourselves
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            for (int column = 0; column < COLUMNS_COUNT; column++)
            {
                ImGui::TableSetColumnIndex(column);
                const char* column_name = ImGui::TableGetColumnName(column); // Retrieve name passed to TableSetupColumn()
                ImGui::PushID(column);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                ImGui::Checkbox("##checkall", &column_selected[column]);
                ImGui::PopStyleVar();
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TableHeader(column_name);
                ImGui::PopID();
            }

            for (int row = 0; row < 5; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < 3; column++)
                {
                    char buf[32];
                    sprintf(buf, "Cell %d,%d", column, row);
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Selectable(buf, column_selected[column]);
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    // Demonstrate creating custom context menus inside columns, while playing it nice with context menus provided by TableHeadersRow()/TableHeader()
    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Context menus");
    if (ImGui::TreeNode("Context menus"))
    {
        HelpMarker("By default, right-clicking over a TableHeadersRow()/TableHeader() line will open the default context-menu.\nUsing ImGuiTableFlags_ContextMenuInBody we also allow right-clicking over columns body.");
        static ImGuiTableFlags flags1 = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders | ImGuiTableFlags_ContextMenuInBody;

        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_ContextMenuInBody", &flags1, ImGuiTableFlags_ContextMenuInBody);
        PopStyleCompact();

        // Context Menus: first example
        // [1.1] Right-click on the TableHeadersRow() line to open the default table context menu.
        // [1.2] Right-click in columns also open the default table context menu (if ImGuiTableFlags_ContextMenuInBody is set)
        const int COLUMNS_COUNT = 3;
        if (ImGui::BeginTable("table_context_menu", COLUMNS_COUNT, flags1))
        {
            ImGui::TableSetupColumn("One");
            ImGui::TableSetupColumn("Two");
            ImGui::TableSetupColumn("Three");

            // [1.1]] Right-click on the TableHeadersRow() line to open the default table context menu.
            ImGui::TableHeadersRow();

            // Submit dummy contents
            for (int row = 0; row < 4; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < COLUMNS_COUNT; column++)
                {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("Cell %d,%d", column, row);
                }
            }
            ImGui::EndTable();
        }

        // Context Menus: second example
        // [2.1] Right-click on the TableHeadersRow() line to open the default table context menu.
        // [2.2] Right-click on the ".." to open a custom popup
        // [2.3] Right-click in columns to open another custom popup
        HelpMarker("Demonstrate mixing table context menu (over header), item context button (over button) and custom per-colum context menu (over column body).");
        ImGuiTableFlags flags2 = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders;
        if (ImGui::BeginTable("table_context_menu_2", COLUMNS_COUNT, flags2))
        {
            ImGui::TableSetupColumn("One");
            ImGui::TableSetupColumn("Two");
            ImGui::TableSetupColumn("Three");

            // [2.1] Right-click on the TableHeadersRow() line to open the default table context menu.
            ImGui::TableHeadersRow();
            for (int row = 0; row < 4; row++)
            {
                ImGui::TableNextRow();
                for (int column = 0; column < COLUMNS_COUNT; column++)
                {
                    // Submit dummy contents
                    ImGui::TableSetColumnIndex(column);
                    ImGui::Text("Cell %d,%d", column, row);
                    ImGui::SameLine();

                    // [2.2] Right-click on the ".." to open a custom popup
                    ImGui::PushID(row * COLUMNS_COUNT + column);
                    ImGui::SmallButton("..");
                    if (ImGui::BeginPopupContextItem())
                    {
                        ImGui::Text("This is the popup for Button(\"..\") in Cell %d,%d", column, row);
                        if (ImGui::Button("Close"))
                            ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }

            // [2.3] Right-click anywhere in columns to open another custom popup
            // (instead of testing for !IsAnyItemHovered() we could also call OpenPopup() with ImGuiPopupFlags_NoOpenOverExistingPopup
            // to manage popup priority as the popups triggers, here "are we hovering a column" are overlapping)
            int hovered_column = -1;
            for (int column = 0; column < COLUMNS_COUNT + 1; column++)
            {
                ImGui::PushID(column);
                if (ImGui::TableGetColumnFlags(column) & ImGuiTableColumnFlags_IsHovered)
                    hovered_column = column;
                if (hovered_column == column && !ImGui::IsAnyItemHovered() && ImGui::IsMouseReleased(1))
                    ImGui::OpenPopup("MyPopup");
                if (ImGui::BeginPopup("MyPopup"))
                {
                    if (column == COLUMNS_COUNT)
                        ImGui::Text("This is a custom popup for unused space after the last column.");
                    else
                        ImGui::Text("This is a custom popup for Column %d", column);
                    if (ImGui::Button("Close"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
            ImGui::Text("Hovered column: %d", hovered_column);
        }
        ImGui::TreePop();
    }

    // Demonstrate creating multiple tables with the same ID
    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Synced instances");
    if (ImGui::TreeNode("Synced instances"))
    {
        HelpMarker("Multiple tables with the same identifier will share their settings, width, visibility, order etc.");

        static ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
        ImGui::CheckboxFlags("ImGuiTableFlags_ScrollY", &flags, ImGuiTableFlags_ScrollY);
        ImGui::CheckboxFlags("ImGuiTableFlags_SizingFixedFit", &flags, ImGuiTableFlags_SizingFixedFit);
        for (int n = 0; n < 3; n++)
        {
            char buf[32];
            sprintf(buf, "Synced Table %d", n);
            bool open = ImGui::CollapsingHeader(buf, ImGuiTreeNodeFlags_DefaultOpen);
            if (open && ImGui::BeginTable("Table", 3, flags, ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 5)))
            {
                ImGui::TableSetupColumn("One");
                ImGui::TableSetupColumn("Two");
                ImGui::TableSetupColumn("Three");
                ImGui::TableHeadersRow();
                const int cell_count = (n == 1) ? 27 : 9; // Make second table have a scrollbar to verify that additional decoration is not affecting column positions.
                for (int cell = 0; cell < cell_count; cell++)
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("this cell %d", cell);
                }
                ImGui::EndTable();
            }
        }
        ImGui::TreePop();
    }

    // Demonstrate using Sorting facilities
    // This is a simplified version of the "Advanced" example, where we mostly focus on the code necessary to handle sorting.
    // Note that the "Advanced" example also showcase manually triggering a sort (e.g. if item quantities have been modified)
    static const char* template_items_names[] =
    {
        "Banana", "Apple", "Cherry", "Watermelon", "Grapefruit", "Strawberry", "Mango",
        "Kiwi", "Orange", "Pineapple", "Blueberry", "Plum", "Coconut", "Pear", "Apricot"
    };
    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Sorting");
    if (ImGui::TreeNode("Sorting"))
    {
        // Create item list
        static ImVector<MyItem> items;
        if (items.Size == 0)
        {
            items.resize(50, MyItem());
            for (int n = 0; n < items.Size; n++)
            {
                const int template_n = n % IM_ARRAYSIZE(template_items_names);
                MyItem& item = items[n];
                item.ID = n;
                item.Name = template_items_names[template_n];
                item.Quantity = (n * n - n) % 20; // Assign default quantities
            }
        }

        // Options
        static ImGuiTableFlags flags =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti
            | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody
            | ImGuiTableFlags_ScrollY;
        PushStyleCompact();
        ImGui::CheckboxFlags("ImGuiTableFlags_SortMulti", &flags, ImGuiTableFlags_SortMulti);
        ImGui::SameLine(); HelpMarker("When sorting is enabled: hold shift when clicking headers to sort on multiple column. TableGetSortSpecs() may return specs where (SpecsCount > 1).");
        ImGui::CheckboxFlags("ImGuiTableFlags_SortTristate", &flags, ImGuiTableFlags_SortTristate);
        ImGui::SameLine(); HelpMarker("When sorting is enabled: allow no sorting, disable default sorting. TableGetSortSpecs() may return specs where (SpecsCount == 0).");
        PopStyleCompact();

        if (ImGui::BeginTable("table_sorting", 4, flags, ImVec2(0.0f, TEXT_BASE_HEIGHT * 15), 0.0f))
        {
            // Declare columns
            // We use the "user_id" parameter of TableSetupColumn() to specify a user id that will be stored in the sort specifications.
            // This is so our sort function can identify a column given our own identifier. We could also identify them based on their index!
            // Demonstrate using a mixture of flags among available sort-related flags:
            // - ImGuiTableColumnFlags_DefaultSort
            // - ImGuiTableColumnFlags_NoSort / ImGuiTableColumnFlags_NoSortAscending / ImGuiTableColumnFlags_NoSortDescending
            // - ImGuiTableColumnFlags_PreferSortAscending / ImGuiTableColumnFlags_PreferSortDescending
            ImGui::TableSetupColumn("ID",       ImGuiTableColumnFlags_DefaultSort          | ImGuiTableColumnFlags_WidthFixed,   0.0f, MyItemColumnID_ID);
            ImGui::TableSetupColumn("Name",                                                  ImGuiTableColumnFlags_WidthFixed,   0.0f, MyItemColumnID_Name);
            ImGui::TableSetupColumn("Action",   ImGuiTableColumnFlags_NoSort               | ImGuiTableColumnFlags_WidthFixed,   0.0f, MyItemColumnID_Action);
            ImGui::TableSetupColumn("Quantity", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthStretch, 0.0f, MyItemColumnID_Quantity);
            ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
            ImGui::TableHeadersRow();

            // Sort our data if sort specs have been changed!
            if (ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs())
                if (sorts_specs->SpecsDirty)
                {
                    MyItem::s_current_sort_specs = sorts_specs; // Store in variable accessible by the sort function.
                    if (items.Size > 1)
                        qsort(&items[0], (size_t)items.Size, sizeof(items[0]), MyItem::CompareWithSortSpecs);
                    MyItem::s_current_sort_specs = NULL;
                    sorts_specs->SpecsDirty = false;
                }

            // Demonstrate using clipper for large vertical lists
            ImGuiListClipper clipper;
            clipper.Begin(items.Size);
            while (clipper.Step())
                for (int row_n = clipper.DisplayStart; row_n < clipper.DisplayEnd; row_n++)
                {
                    // Display a data item
                    MyItem* item = &items[row_n];
                    ImGui::PushID(item->ID);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%04d", item->ID);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(item->Name);
                    ImGui::TableNextColumn();
                    ImGui::SmallButton("None");
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", item->Quantity);
                    ImGui::PopID();
                }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    // In this example we'll expose most table flags and settings.
    // For specific flags and settings refer to the corresponding section for more detailed explanation.
    // This section is mostly useful to experiment with combining certain flags or settings with each others.
    //ImGui::SetNextItemOpen(true, ImGuiCond_Once); // [DEBUG]
    if (open_action != -1)
        ImGui::SetNextItemOpen(open_action != 0);
    IMGUI_DEMO_MARKER("Tables/Advanced");
    if (ImGui::TreeNode("Advanced"))
    {
        static ImGuiTableFlags flags =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable
            | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti
            | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_NoBordersInBody
            | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY
            | ImGuiTableFlags_SizingFixedFit;

        enum ContentsType { CT_Text, CT_Button, CT_SmallButton, CT_FillButton, CT_Selectable, CT_SelectableSpanRow };
        static int contents_type = CT_SelectableSpanRow;
        const char* contents_type_names[] = { "Text", "Button", "SmallButton", "FillButton", "Selectable", "Selectable (span row)" };
        static int freeze_cols = 1;
        static int freeze_rows = 1;
        static int items_count = IM_ARRAYSIZE(template_items_names) * 2;
        static ImVec2 outer_size_value = ImVec2(0.0f, TEXT_BASE_HEIGHT * 12);
        static float row_min_height = 0.0f; // Auto
        static float inner_width_with_scroll = 0.0f; // Auto-extend
        static bool outer_size_enabled = true;
        static bool show_headers = true;
        static bool show_wrapped_text = false;
        //static ImGuiTextFilter filter;
        //ImGui::SetNextItemOpen(true, ImGuiCond_Once); // FIXME-TABLE: Enabling this results in initial clipped first pass on table which tend to affect column sizing
        if (ImGui::TreeNode("Options"))
        {
            // Make the UI compact because there are so many fields
            PushStyleCompact();
            ImGui::PushItemWidth(TEXT_BASE_WIDTH * 28.0f);

            if (ImGui::TreeNodeEx("Features:", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::CheckboxFlags("ImGuiTableFlags_Resizable", &flags, ImGuiTableFlags_Resizable);
                ImGui::CheckboxFlags("ImGuiTableFlags_Reorderable", &flags, ImGuiTableFlags_Reorderable);
                ImGui::CheckboxFlags("ImGuiTableFlags_Hideable", &flags, ImGuiTableFlags_Hideable);
                ImGui::CheckboxFlags("ImGuiTableFlags_Sortable", &flags, ImGuiTableFlags_Sortable);
                ImGui::CheckboxFlags("ImGuiTableFlags_NoSavedSettings", &flags, ImGuiTableFlags_NoSavedSettings);
                ImGui::CheckboxFlags("ImGuiTableFlags_ContextMenuInBody", &flags, ImGuiTableFlags_ContextMenuInBody);
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Decorations:", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::CheckboxFlags("ImGuiTableFlags_RowBg", &flags, ImGuiTableFlags_RowBg);
                ImGui::CheckboxFlags("ImGuiTableFlags_BordersV", &flags, ImGuiTableFlags_BordersV);
                ImGui::CheckboxFlags("ImGuiTableFlags_BordersOuterV", &flags, ImGuiTableFlags_BordersOuterV);
                ImGui::CheckboxFlags("ImGuiTableFlags_BordersInnerV", &flags, ImGuiTableFlags_BordersInnerV);
                ImGui::CheckboxFlags("ImGuiTableFlags_BordersH", &flags, ImGuiTableFlags_BordersH);
                ImGui::CheckboxFlags("ImGuiTableFlags_BordersOuterH", &flags, ImGuiTableFlags_BordersOuterH);
                ImGui::CheckboxFlags("ImGuiTableFlags_BordersInnerH", &flags, ImGuiTableFlags_BordersInnerH);
                ImGui::CheckboxFlags("ImGuiTableFlags_NoBordersInBody", &flags, ImGuiTableFlags_NoBordersInBody); ImGui::SameLine(); HelpMarker("Disable vertical borders in columns Body (borders will always appear in Headers");
                ImGui::CheckboxFlags("ImGuiTableFlags_NoBordersInBodyUntilResize", &flags, ImGuiTableFlags_NoBordersInBodyUntilResize); ImGui::SameLine(); HelpMarker("Disable vertical borders in columns Body until hovered for resize (borders will always appear in Headers)");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Sizing:", ImGuiTreeNodeFlags_DefaultOpen))
            {
                EditTableSizingFlags(&flags);
                ImGui::SameLine(); HelpMarker("In the Advanced demo we override the policy of each column so those table-wide settings have less effect that typical.");
                ImGui::CheckboxFlags("ImGuiTableFlags_NoHostExtendX", &flags, ImGuiTableFlags_NoHostExtendX);
                ImGui::SameLine(); HelpMarker("Make outer width auto-fit to columns, overriding outer_size.x value.\n\nOnly available when ScrollX/ScrollY are disabled and Stretch columns are not used.");
                ImGui::CheckboxFlags("ImGuiTableFlags_NoHostExtendY", &flags, ImGuiTableFlags_NoHostExtendY);
                ImGui::SameLine(); HelpMarker("Make outer height stop exactly at outer_size.y (prevent auto-extending table past the limit).\n\nOnly available when ScrollX/ScrollY are disabled. Data below the limit will be clipped and not visible.");
                ImGui::CheckboxFlags("ImGuiTableFlags_NoKeepColumnsVisible", &flags, ImGuiTableFlags_NoKeepColumnsVisible);
                ImGui::SameLine(); HelpMarker("Only available if ScrollX is disabled.");
                ImGui::CheckboxFlags("ImGuiTableFlags_PreciseWidths", &flags, ImGuiTableFlags_PreciseWidths);
                ImGui::SameLine(); HelpMarker("Disable distributing remainder width to stretched columns (width allocation on a 100-wide table with 3 columns: Without this flag: 33,33,34. With this flag: 33,33,33). With larger number of columns, resizing will appear to be less smooth.");
                ImGui::CheckboxFlags("ImGuiTableFlags_NoClip", &flags, ImGuiTableFlags_NoClip);
                ImGui::SameLine(); HelpMarker("Disable clipping rectangle for every individual columns (reduce draw command count, items will be able to overflow into other columns). Generally incompatible with ScrollFreeze options.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Padding:", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::CheckboxFlags("ImGuiTableFlags_PadOuterX", &flags, ImGuiTableFlags_PadOuterX);
                ImGui::CheckboxFlags("ImGuiTableFlags_NoPadOuterX", &flags, ImGuiTableFlags_NoPadOuterX);
                ImGui::CheckboxFlags("ImGuiTableFlags_NoPadInnerX", &flags, ImGuiTableFlags_NoPadInnerX);
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Scrolling:", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::CheckboxFlags("ImGuiTableFlags_ScrollX", &flags, ImGuiTableFlags_ScrollX);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetFrameHeight());
                ImGui::DragInt("freeze_cols", &freeze_cols, 0.2f, 0, 9, NULL, ImGuiSliderFlags_NoInput);
                ImGui::CheckboxFlags("ImGuiTableFlags_ScrollY", &flags, ImGuiTableFlags_ScrollY);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetFrameHeight());
                ImGui::DragInt("freeze_rows", &freeze_rows, 0.2f, 0, 9, NULL, ImGuiSliderFlags_NoInput);
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Sorting:", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::CheckboxFlags("ImGuiTableFlags_SortMulti", &flags, ImGuiTableFlags_SortMulti);
                ImGui::SameLine(); HelpMarker("When sorting is enabled: hold shift when clicking headers to sort on multiple column. TableGetSortSpecs() may return specs where (SpecsCount > 1).");
                ImGui::CheckboxFlags("ImGuiTableFlags_SortTristate", &flags, ImGuiTableFlags_SortTristate);
                ImGui::SameLine(); HelpMarker("When sorting is enabled: allow no sorting, disable default sorting. TableGetSortSpecs() may return specs where (SpecsCount == 0).");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Other:", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("show_headers", &show_headers);
                ImGui::Checkbox("show_wrapped_text", &show_wrapped_text);

                ImGui::DragFloat2("##OuterSize", &outer_size_value.x);
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::Checkbox("outer_size", &outer_size_enabled);
                ImGui::SameLine();
                HelpMarker("If scrolling is disabled (ScrollX and ScrollY not set):\n"
                    "- The table is output directly in the parent window.\n"
                    "- OuterSize.x < 0.0f will right-align the table.\n"
                    "- OuterSize.x = 0.0f will narrow fit the table unless there are any Stretch columns.\n"
                    "- OuterSize.y then becomes the minimum size for the table, which will extend vertically if there are more rows (unless NoHostExtendY is set).");

                // From a user point of view we will tend to use 'inner_width' differently depending on whether our table is embedding scrolling.
                // To facilitate toying with this demo we will actually pass 0.0f to the BeginTable() when ScrollX is disabled.
                ImGui::DragFloat("inner_width (when ScrollX active)", &inner_width_with_scroll, 1.0f, 0.0f, FLT_MAX);

                ImGui::DragFloat("row_min_height", &row_min_height, 1.0f, 0.0f, FLT_MAX);
                ImGui::SameLine(); HelpMarker("Specify height of the Selectable item.");

                ImGui::DragInt("items_count", &items_count, 0.1f, 0, 9999);
                ImGui::Combo("items_type (first column)", &contents_type, contents_type_names, IM_ARRAYSIZE(contents_type_names));
                //filter.Draw("filter");
                ImGui::TreePop();
            }

            ImGui::PopItemWidth();
            PopStyleCompact();
            ImGui::Spacing();
            ImGui::TreePop();
        }

        // Update item list if we changed the number of items
        static ImVector<MyItem> items;
        static ImVector<int> selection;
        static bool items_need_sort = false;
        if (items.Size != items_count)
        {
            items.resize(items_count, MyItem());
            for (int n = 0; n < items_count; n++)
            {
                const int template_n = n % IM_ARRAYSIZE(template_items_names);
                MyItem& item = items[n];
                item.ID = n;
                item.Name = template_items_names[template_n];
                item.Quantity = (template_n == 3) ? 10 : (template_n == 4) ? 20 : 0; // Assign default quantities
            }
        }

        const ImDrawList* parent_draw_list = ImGui::GetWindowDrawList();
        const int parent_draw_list_draw_cmd_count = parent_draw_list->CmdBuffer.Size;
        ImVec2 table_scroll_cur, table_scroll_max; // For debug display
        const ImDrawList* table_draw_list = NULL;  // "

        // Submit table
        const float inner_width_to_use = (flags & ImGuiTableFlags_ScrollX) ? inner_width_with_scroll : 0.0f;
        if (ImGui::BeginTable("table_advanced", 6, flags, outer_size_enabled ? outer_size_value : ImVec2(0, 0), inner_width_to_use))
        {
            // Declare columns
            // We use the "user_id" parameter of TableSetupColumn() to specify a user id that will be stored in the sort specifications.
            // This is so our sort function can identify a column given our own identifier. We could also identify them based on their index!
            ImGui::TableSetupColumn("ID",           ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 0.0f, MyItemColumnID_ID);
            ImGui::TableSetupColumn("Name",         ImGuiTableColumnFlags_WidthFixed, 0.0f, MyItemColumnID_Name);
            ImGui::TableSetupColumn("Action",       ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 0.0f, MyItemColumnID_Action);
            ImGui::TableSetupColumn("Quantity",     ImGuiTableColumnFlags_PreferSortDescending, 0.0f, MyItemColumnID_Quantity);
            ImGui::TableSetupColumn("Description",  (flags & ImGuiTableFlags_NoHostExtendX) ? 0 : ImGuiTableColumnFlags_WidthStretch, 0.0f, MyItemColumnID_Description);
            ImGui::TableSetupColumn("Hidden",       ImGuiTableColumnFlags_DefaultHide | ImGuiTableColumnFlags_NoSort);
            ImGui::TableSetupScrollFreeze(freeze_cols, freeze_rows);

            // Sort our data if sort specs have been changed!
            ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs();
            if (sorts_specs && sorts_specs->SpecsDirty)
                items_need_sort = true;
            if (sorts_specs && items_need_sort && items.Size > 1)
            {
                MyItem::s_current_sort_specs = sorts_specs; // Store in variable accessible by the sort function.
                qsort(&items[0], (size_t)items.Size, sizeof(items[0]), MyItem::CompareWithSortSpecs);
                MyItem::s_current_sort_specs = NULL;
                sorts_specs->SpecsDirty = false;
            }
            items_need_sort = false;

            // Take note of whether we are currently sorting based on the Quantity field,
            // we will use this to trigger sorting when we know the data of this column has been modified.
            const bool sorts_specs_using_quantity = (ImGui::TableGetColumnFlags(3) & ImGuiTableColumnFlags_IsSorted) != 0;

            // Show headers
            if (show_headers)
                ImGui::TableHeadersRow();

            // Show data
            // FIXME-TABLE FIXME-NAV: How we can get decent up/down even though we have the buttons here?
            ImGui::PushButtonRepeat(true);
#if 1
            // Demonstrate using clipper for large vertical lists
            ImGuiListClipper clipper;
            clipper.Begin(items.Size);
            while (clipper.Step())
            {
                for (int row_n = clipper.DisplayStart; row_n < clipper.DisplayEnd; row_n++)
#else
            // Without clipper
            {
                for (int row_n = 0; row_n < items.Size; row_n++)
#endif
                {
                    MyItem* item = &items[row_n];
                    //if (!filter.PassFilter(item->Name))
                    //    continue;

                    const bool item_is_selected = selection.contains(item->ID);
                    ImGui::PushID(item->ID);
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, row_min_height);

                    // For the demo purpose we can select among different type of items submitted in the first column
                    ImGui::TableSetColumnIndex(0);
                    char label[32];
                    sprintf(label, "%04d", item->ID);
                    if (contents_type == CT_Text)
                        ImGui::TextUnformatted(label);
                    else if (contents_type == CT_Button)
                        ImGui::Button(label);
                    else if (contents_type == CT_SmallButton)
                        ImGui::SmallButton(label);
                    else if (contents_type == CT_FillButton)
                        ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f));
                    else if (contents_type == CT_Selectable || contents_type == CT_SelectableSpanRow)
                    {
                        ImGuiSelectableFlags selectable_flags = (contents_type == CT_SelectableSpanRow) ? ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap : ImGuiSelectableFlags_None;
                        if (ImGui::Selectable(label, item_is_selected, selectable_flags, ImVec2(0, row_min_height)))
                        {
                            if (ImGui::GetIO().KeyCtrl)
                            {
                                if (item_is_selected)
                                    selection.find_erase_unsorted(item->ID);
                                else
                                    selection.push_back(item->ID);
                            }
                            else
                            {
                                selection.clear();
                                selection.push_back(item->ID);
                            }
                        }
                    }

                    if (ImGui::TableSetColumnIndex(1))
                        ImGui::TextUnformatted(item->Name);

                    // Here we demonstrate marking our data set as needing to be sorted again if we modified a quantity,
                    // and we are currently sorting on the column showing the Quantity.
                    // To avoid triggering a sort while holding the button, we only trigger it when the button has been released.
                    // You will probably need a more advanced system in your code if you want to automatically sort when a specific entry changes.
                    if (ImGui::TableSetColumnIndex(2))
                    {
                        if (ImGui::SmallButton("Chop")) { item->Quantity += 1; }
                        if (sorts_specs_using_quantity && ImGui::IsItemDeactivated()) { items_need_sort = true; }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Eat")) { item->Quantity -= 1; }
                        if (sorts_specs_using_quantity && ImGui::IsItemDeactivated()) { items_need_sort = true; }
                    }

                    if (ImGui::TableSetColumnIndex(3))
                        ImGui::Text("%d", item->Quantity);

                    ImGui::TableSetColumnIndex(4);
                    if (show_wrapped_text)
                        ImGui::TextWrapped("Lorem ipsum dolor sit amet");
                    else
                        ImGui::Text("Lorem ipsum dolor sit amet");

                    if (ImGui::TableSetColumnIndex(5))
                        ImGui::Text("1234");

                    ImGui::PopID();
                }
            }
            ImGui::PopButtonRepeat();

            // Store some info to display debug details below
            table_scroll_cur = ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());
            table_scroll_max = ImVec2(ImGui::GetScrollMaxX(), ImGui::GetScrollMaxY());
            table_draw_list = ImGui::GetWindowDrawList();
            ImGui::EndTable();
        }
        static bool show_debug_details = false;
        ImGui::Checkbox("Debug details", &show_debug_details);
        if (show_debug_details && table_draw_list)
        {
            ImGui::SameLine(0.0f, 0.0f);
            const int table_draw_list_draw_cmd_count = table_draw_list->CmdBuffer.Size;
            if (table_draw_list == parent_draw_list)
                ImGui::Text(": DrawCmd: +%d (in same window)",
                    table_draw_list_draw_cmd_count - parent_draw_list_draw_cmd_count);
            else
                ImGui::Text(": DrawCmd: +%d (in child window), Scroll: (%.f/%.f) (%.f/%.f)",
                    table_draw_list_draw_cmd_count - 1, table_scroll_cur.x, table_scroll_max.x, table_scroll_cur.y, table_scroll_max.y);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();

    ShowDemoWindowColumns();

    if (disable_indent)
        ImGui::PopStyleVar();
}

// Demonstrate old/legacy Columns API!
// [2020: Columns are under-featured and not maintained. Prefer using the more flexible and powerful BeginTable() API!]
static void ShowDemoWindowColumns()
{
    rtx_spoof_func;
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Columns (legacy API)"));
    bool open = ImGui::TreeNode(OBFUSCATE_STR("Legacy Columns API"));
    ImGui::SameLine();
    HelpMarker(OBFUSCATE_STR("Columns() is an old API! Prefer using the more flexible and powerful BeginTable() API!"));
    if (!open)
        return;

    // Basic columns
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Columns (legacy API)/Basic"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Basic")))
    {
        ImGui::Text(OBFUSCATE_STR("Without border:"));
        ImGui::Columns(3, OBFUSCATE_STR("mycolumns3"), false);  // 3-ways, no border
        ImGui::Separator();
        for (int n = 0; n < 14; n++)
        {
            char label[32];
            sprintf(label, OBFUSCATE_STR("Item %d"), n);
            if (ImGui::Selectable(label)) {}
            //if (ImGui::Button(label, ImVec2(-FLT_MIN,0.0f))) {}
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
        ImGui::Separator();

        ImGui::Text(OBFUSCATE_STR("With border:"));
        ImGui::Columns(4, OBFUSCATE_STR("mycolumns")); // 4-ways, with border
        ImGui::Separator();
        ImGui::Text(OBFUSCATE_STR("ID")); ImGui::NextColumn();
        ImGui::Text(OBFUSCATE_STR("Name")); ImGui::NextColumn();
        ImGui::Text(OBFUSCATE_STR("Path")); ImGui::NextColumn();
        ImGui::Text(OBFUSCATE_STR("Hovered")); ImGui::NextColumn();
        ImGui::Separator();
        const char* names[3] = { "One", "Two", "Three" };
        const char* paths[3] = { "/path/one", "/path/two", "/path/three" };
        static int selected = -1;
        for (int i = 0; i < 3; i++)
        {
            char label[32];
            sprintf(label, OBFUSCATE_STR("%04d"), i);
            if (ImGui::Selectable(label, selected == i, ImGuiSelectableFlags_SpanAllColumns))
                selected = i;
            bool hovered = ImGui::IsItemHovered();
            ImGui::NextColumn();
            ImGui::Text(names[i]); ImGui::NextColumn();
            ImGui::Text(paths[i]); ImGui::NextColumn();
            ImGui::Text(OBFUSCATE_STR("%d"), hovered); ImGui::NextColumn();
        }
        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Columns (legacy API)/Borders"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Borders")))
    {
        // NB: Future columns API should allow automatic horizontal borders.
        static bool h_borders = true;
        static bool v_borders = true;
        static int columns_count = 4;
        const int lines_count = 3;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::DragInt(OBFUSCATE_STR("##columns_count"), &columns_count, 0.1f, 2, 10, OBFUSCATE_STR("%d columns"));
        if (columns_count < 2)
            columns_count = 2;
        ImGui::SameLine();
        ImGui::Checkbox(OBFUSCATE_STR("horizontal"), &h_borders);
        ImGui::SameLine();
        ImGui::Checkbox(OBFUSCATE_STR("vertical"), &v_borders);
        ImGui::Columns(columns_count, NULL, v_borders);
        for (int i = 0; i < columns_count * lines_count; i++)
        {
            if (h_borders && ImGui::GetColumnIndex() == 0)
                ImGui::Separator();
            ImGui::Text(OBFUSCATE_STR("%c%c%c"), 'a' + i, 'a' + i, 'a' + i);
            ImGui::Text(OBFUSCATE_STR("Width %.2f"), ImGui::GetColumnWidth());
            ImGui::Text(OBFUSCATE_STR("Avail %.2f"), ImGui::GetContentRegionAvail().x);
            ImGui::Text(OBFUSCATE_STR("Offset %.2f"), ImGui::GetColumnOffset());
            ImGui::Text(OBFUSCATE_STR("Long text that is likely to clip"));
            ImGui::Button(OBFUSCATE_STR("Button"), ImVec2(-FLT_MIN, 0.0f));
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
        if (h_borders)
            ImGui::Separator();
        ImGui::TreePop();
    }

    // Create multiple items in a same cell before switching to next column
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Columns (legacy API)/Mixed items"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Mixed items")))
    {
        ImGui::Columns(3, OBFUSCATE_STR("mixed"));
        ImGui::Separator();

        ImGui::Text(OBFUSCATE_STR("Hello"));
        ImGui::Button(OBFUSCATE_STR("Banana"));
        ImGui::NextColumn();

        ImGui::Text(OBFUSCATE_STR("ImGui"));
        ImGui::Button(OBFUSCATE_STR("Apple"));
        static float foo = 1.0f;
        ImGui::InputFloat(OBFUSCATE_STR("red"), &foo, 0.05f, 0, OBFUSCATE_STR("%.3f"));
        ImGui::Text(OBFUSCATE_STR("An extra line here."));
        ImGui::NextColumn();

        ImGui::Text(OBFUSCATE_STR("Sailor"));
        ImGui::Button(OBFUSCATE_STR("Corniflower"));
        static float bar = 1.0f;
        ImGui::InputFloat(OBFUSCATE_STR("blue"), &bar, 0.05f, 0, OBFUSCATE_STR("%.3f"));
        ImGui::NextColumn();

        if (ImGui::CollapsingHeader(OBFUSCATE_STR("Category A"))) { ImGui::Text(OBFUSCATE_STR("Blah blah blah")); } ImGui::NextColumn();
        if (ImGui::CollapsingHeader(OBFUSCATE_STR("Category B"))) { ImGui::Text(OBFUSCATE_STR("Blah blah blah")); } ImGui::NextColumn();
        if (ImGui::CollapsingHeader(OBFUSCATE_STR("Category C"))) { ImGui::Text(OBFUSCATE_STR("Blah blah blah")); } ImGui::NextColumn();
        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::TreePop();
    }

    // Word wrapping
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Columns (legacy API)/Word-wrapping"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Word-wrapping")))
    {
        ImGui::Columns(2, OBFUSCATE_STR("word-wrapping"));
        ImGui::Separator();
        ImGui::TextWrapped(OBFUSCATE_STR("The quick brown fox jumps over the lazy dog."));
        ImGui::TextWrapped(OBFUSCATE_STR("Hello Left"));
        ImGui::NextColumn();
        ImGui::TextWrapped(OBFUSCATE_STR("The quick brown fox jumps over the lazy dog."));
        ImGui::TextWrapped(OBFUSCATE_STR("Hello Right"));
        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Columns (legacy API)/Horizontal Scrolling"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Horizontal Scrolling")))
    {
        ImGui::SetNextWindowContentSize(ImVec2(1500.0f, 0.0f));
        ImVec2 child_size = ImVec2(0, ImGui::GetFontSize() * 20.0f);
        ImGui::BeginChild(OBFUSCATE_STR("##ScrollingRegion"), child_size, false, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::Columns(10);

        // Also demonstrate using clipper for large vertical lists
        int ITEMS_COUNT = 2000;
        ImGuiListClipper clipper;
        clipper.Begin(ITEMS_COUNT);
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                for (int j = 0; j < 10; j++)
                {
                    ImGui::Text(OBFUSCATE_STR("Line %d Column %d..."), i, j);
                    ImGui::NextColumn();
                }
        }
        ImGui::Columns(1);
        ImGui::EndChild();
        ImGui::TreePop();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Columns (legacy API)/Tree"));
    if (ImGui::TreeNode(OBFUSCATE_STR("Tree")))
    {
        ImGui::Columns(2, OBFUSCATE_STR("tree"), true);
        for (int x = 0; x < 3; x++)
        {
            bool open1 = ImGui::TreeNode((void*)(intptr_t)x, OBFUSCATE_STR("Node%d"), x);
            ImGui::NextColumn();
            ImGui::Text(OBFUSCATE_STR("Node contents"));
            ImGui::NextColumn();
            if (open1)
            {
                for (int y = 0; y < 3; y++)
                {
                    bool open2 = ImGui::TreeNode((void*)(intptr_t)y, OBFUSCATE_STR("Node%d.%d"), x, y);
                    ImGui::NextColumn();
                    ImGui::Text(OBFUSCATE_STR("Node contents"));
                    if (open2)
                    {
                        ImGui::Text(OBFUSCATE_STR("Even more contents"));
                        if (ImGui::TreeNode(OBFUSCATE_STR("Tree in column")))
                        {
                            ImGui::Text(OBFUSCATE_STR("The quick brown fox jumps over the lazy dog"));
                            ImGui::TreePop();
                        }
                    }
                    ImGui::NextColumn();
                    if (open2)
                        ImGui::TreePop();
                }
                ImGui::TreePop();
            }
        }
        ImGui::Columns(1);
        ImGui::TreePop();
    }

    ImGui::TreePop();
}

namespace ImGui { extern ImGuiKeyData* GetKeyData(ImGuiKey key); }

static void ShowDemoWindowInputs()
{
    rtx_spoof_func;
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Inputs & Focus"));
    if (ImGui::CollapsingHeader(OBFUSCATE_STR("Inputs & Focus")))
    {
        ImGuiIO& io = ImGui::GetIO();

        // Display inputs submitted to ImGuiIO
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Inputs & Focus/Inputs"));
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::TreeNode(OBFUSCATE_STR("Inputs")))
        {
            HelpMarker(OBFUSCATE_STR(
                "This is a simplified view. See more detailed input state:\n"
                "- in 'Tools->Metrics/Debugger->Inputs'.\n"
                "- in 'Tools->Debug Log->IO'."));
            if (ImGui::IsMousePosValid())
                ImGui::Text(OBFUSCATE_STR("Mouse pos: (%g, %g)"), io.MousePos.x, io.MousePos.y);
            else
                ImGui::Text(OBFUSCATE_STR("Mouse pos: <INVALID>"));
            ImGui::Text(OBFUSCATE_STR("Mouse delta: (%g, %g)"), io.MouseDelta.x, io.MouseDelta.y);
            ImGui::Text(OBFUSCATE_STR("Mouse down:"));
            for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++) if (ImGui::IsMouseDown(i)) { ImGui::SameLine(); ImGui::Text(OBFUSCATE_STR("b%d (%.02f secs)"), i, io.MouseDownDuration[i]); }
            ImGui::Text(OBFUSCATE_STR("Mouse wheel: %.1f"), io.MouseWheel);

            // We iterate both legacy native range and named ImGuiKey ranges, which is a little odd but this allows displaying the data for old/new backends.
            // User code should never have to go through such hoops: old code may use native keycodes, new code may use ImGuiKey codes.
#ifdef IMGUI_DISABLE_OBSOLETE_KEYIO
            struct funcs { static bool IsLegacyNativeDupe(ImGuiKey) { return false; } };
#else
            struct funcs { static bool IsLegacyNativeDupe(ImGuiKey key) { return key < 512 && ImGui::GetIO().KeyMap[key] != -1; } }; // Hide Native<>ImGuiKey duplicates when both exists in the array
#endif
            ImGui::Text(OBFUSCATE_STR("Keys down:"));         for (ImGuiKey key = ImGuiKey_KeysData_OFFSET; key < ImGuiKey_COUNT; key = (ImGuiKey)(key + 1)) { if (funcs::IsLegacyNativeDupe(key) || !ImGui::IsKeyDown(key)) continue; ImGui::SameLine(); ImGui::Text((key < ImGuiKey_NamedKey_BEGIN) ? OBFUSCATE_STR("\"%s\"") : OBFUSCATE_STR("\"%s\" %d"), ImGui::GetKeyName(key), key); ImGui::SameLine(); ImGui::Text(OBFUSCATE_STR("(%.02f)"), ImGui::GetKeyData(key)->DownDuration); }
            ImGui::Text(OBFUSCATE_STR("Keys mods: %s%s%s%s"), io.KeyCtrl ? OBFUSCATE_STR("CTRL ") : OBFUSCATE_STR(""), io.KeyShift ? OBFUSCATE_STR("SHIFT ") : OBFUSCATE_STR(""), io.KeyAlt ? OBFUSCATE_STR("ALT ") : OBFUSCATE_STR(""), io.KeySuper ? OBFUSCATE_STR("SUPER ") : OBFUSCATE_STR(""));
            ImGui::Text(OBFUSCATE_STR("Chars queue:"));       for (int i = 0; i < io.InputQueueCharacters.Size; i++) { ImWchar c = io.InputQueueCharacters[i]; ImGui::SameLine();  ImGui::Text(OBFUSCATE_STR("\'%c\' (0x%04X)"), (c > ' ' && c <= 255) ? (char)c : '?', c); } // FIXME: We should convert 'c' to UTF-8 here but the functions are not public.

            ImGui::TreePop();
        }

        // Display ImGuiIO output flags
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Inputs & Focus/Outputs"));
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::TreeNode(OBFUSCATE_STR("Outputs")))
        {
            HelpMarker(OBFUSCATE_STR(
                "The value of io.WantCaptureMouse and io.WantCaptureKeyboard are normally set by Dear ImGui "
                "to instruct your application of how to route inputs. Typically, when a value is true, it means "
                "Dear ImGui wants the corresponding inputs and we expect the underlying application to ignore them.\n\n"
                "The most typical case is: when hovering a window, Dear ImGui set io.WantCaptureMouse to true, "
                "and underlying application should ignore mouse inputs (in practice there are many and more subtle "
                "rules leading to how those flags are set)."));
            ImGui::Text(OBFUSCATE_STR("io.WantCaptureMouse: %d"), io.WantCaptureMouse);
            ImGui::Text(OBFUSCATE_STR("io.WantCaptureMouseUnlessPopupClose: %d"), io.WantCaptureMouseUnlessPopupClose);
            ImGui::Text(OBFUSCATE_STR("io.WantCaptureKeyboard: %d"), io.WantCaptureKeyboard);
            ImGui::Text(OBFUSCATE_STR("io.WantTextInput: %d"), io.WantTextInput);
            ImGui::Text(OBFUSCATE_STR("io.WantSetMousePos: %d"), io.WantSetMousePos);
            ImGui::Text(OBFUSCATE_STR("io.NavActive: %d, io.NavVisible: %d"), io.NavActive, io.NavVisible);

            IMGUI_DEMO_MARKER(OBFUSCATE_STR("Inputs & Focus/Outputs/WantCapture override"));
            if (ImGui::TreeNode(OBFUSCATE_STR("WantCapture override")))
            {
                HelpMarker(OBFUSCATE_STR(
                    "Hovering the colored canvas will override io.WantCaptureXXX fields.\n"
                    "Notice how normally (when set to none), the value of io.WantCaptureKeyboard would be false when hovering and true when clicking."));
                static int capture_override_mouse = -1;
                static int capture_override_keyboard = -1;
                const char* capture_override_desc[] = { "None", "Set to false", "Set to true" };
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 15);
                ImGui::SliderInt(OBFUSCATE_STR("SetNextFrameWantCaptureMouse() on hover"), &capture_override_mouse, -1, +1, capture_override_desc[capture_override_mouse + 1], ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 15);
                ImGui::SliderInt(OBFUSCATE_STR("SetNextFrameWantCaptureKeyboard() on hover"), &capture_override_keyboard, -1, +1, capture_override_desc[capture_override_keyboard + 1], ImGuiSliderFlags_AlwaysClamp);

                ImGui::ColorButton(OBFUSCATE_STR("##panel"), ImVec4(0.7f, 0.1f, 0.7f, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(128.0f, 96.0f)); // Dummy item
                if (ImGui::IsItemHovered() && capture_override_mouse != -1)
                    ImGui::SetNextFrameWantCaptureMouse(capture_override_mouse == 1);
                if (ImGui::IsItemHovered() && capture_override_keyboard != -1)
                    ImGui::SetNextFrameWantCaptureKeyboard(capture_override_keyboard == 1);

                ImGui::TreePop();
            }
            ImGui::TreePop();
        }

        // Display mouse cursors
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Inputs & Focus/Mouse Cursors"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Mouse Cursors")))
        {
            const char* mouse_cursors_names[] = { "Arrow", "TextInput", "ResizeAll", "ResizeNS", "ResizeEW", "ResizeNESW", "ResizeNWSE", "Hand", "NotAllowed" };
            IM_ASSERT(IM_ARRAYSIZE(mouse_cursors_names) == ImGuiMouseCursor_COUNT);

            ImGuiMouseCursor current = ImGui::GetMouseCursor();
            ImGui::Text(OBFUSCATE_STR("Current mouse cursor = %d: %s"), current, mouse_cursors_names[current]);
            ImGui::BeginDisabled(true);
            ImGui::CheckboxFlags(OBFUSCATE_STR("io.BackendFlags: HasMouseCursors"), &io.BackendFlags, ImGuiBackendFlags_HasMouseCursors);
            ImGui::EndDisabled();

            ImGui::Text(OBFUSCATE_STR("Hover to see mouse cursors:"));
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR(
                "Your application can render a different mouse cursor based on what ImGui::GetMouseCursor() returns. "
                "If software cursor rendering (io.MouseDrawCursor) is set ImGui will draw the right cursor for you, "
                "otherwise your backend needs to handle it."));
            for (int i = 0; i < ImGuiMouseCursor_COUNT; i++)
            {
                char label[32];
                sprintf(label, OBFUSCATE_STR("Mouse cursor %d: %s"), i, mouse_cursors_names[i]);
                ImGui::Bullet(); ImGui::Selectable(label, false);
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(i);
            }
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Inputs & Focus/Tabbing"));
        if (ImGui::TreeNode("Tabbing"))
        {
            ImGui::Text(OBFUSCATE_STR("Use TAB/SHIFT+TAB to cycle through keyboard editable fields."));
            static char buf[32] = "hello";
            ImGui::InputText(OBFUSCATE_STR("1"), buf, IM_ARRAYSIZE(buf));
            ImGui::InputText(OBFUSCATE_STR("2"), buf, IM_ARRAYSIZE(buf));
            ImGui::InputText(OBFUSCATE_STR("3"), buf, IM_ARRAYSIZE(buf));
            ImGui::PushAllowKeyboardFocus(false);
            ImGui::InputText(OBFUSCATE_STR("4 (tab skip)"), buf, IM_ARRAYSIZE(buf));
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Item won't be cycled through when using TAB or Shift+Tab."));
            ImGui::PopAllowKeyboardFocus();
            ImGui::InputText(OBFUSCATE_STR("5"), buf, IM_ARRAYSIZE(buf));
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Inputs & Focus/Focus from code"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Focus from code")))
        {
            bool focus_1 = ImGui::Button(OBFUSCATE_STR("Focus on 1")); ImGui::SameLine();
            bool focus_2 = ImGui::Button(OBFUSCATE_STR("Focus on 2")); ImGui::SameLine();
            bool focus_3 = ImGui::Button(OBFUSCATE_STR("Focus on 3"));
            int has_focus = 0;
            static char buf[128] = "click on a button to set focus";

            if (focus_1) ImGui::SetKeyboardFocusHere();
            ImGui::InputText(OBFUSCATE_STR("1"), buf, IM_ARRAYSIZE(buf));
            if (ImGui::IsItemActive()) has_focus = 1;

            if (focus_2) ImGui::SetKeyboardFocusHere();
            ImGui::InputText(OBFUSCATE_STR("2"), buf, IM_ARRAYSIZE(buf));
            if (ImGui::IsItemActive()) has_focus = 2;

            ImGui::PushAllowKeyboardFocus(false);
            if (focus_3) ImGui::SetKeyboardFocusHere();
            ImGui::InputText(OBFUSCATE_STR("3 (tab skip)"), buf, IM_ARRAYSIZE(buf));
            if (ImGui::IsItemActive()) has_focus = 3;
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Item won't be cycled through when using TAB or Shift+Tab."));
            ImGui::PopAllowKeyboardFocus();

            if (has_focus)
                ImGui::Text(OBFUSCATE_STR("Item with focus: %d"), has_focus);
            else
                ImGui::Text(OBFUSCATE_STR("Item with focus: <none>"));

            // Use >= 0 parameter to SetKeyboardFocusHere() to focus an upcoming item
            static float f3[3] = { 0.0f, 0.0f, 0.0f };
            int focus_ahead = -1;
            if (ImGui::Button(OBFUSCATE_STR("Focus on X"))) { focus_ahead = 0; } ImGui::SameLine();
            if (ImGui::Button(OBFUSCATE_STR("Focus on Y"))) { focus_ahead = 1; } ImGui::SameLine();
            if (ImGui::Button(OBFUSCATE_STR("Focus on Z"))) { focus_ahead = 2; }
            if (focus_ahead != -1) ImGui::SetKeyboardFocusHere(focus_ahead);
            ImGui::SliderFloat3(OBFUSCATE_STR("Float3"), &f3[0], 0.0f, 1.0f);

            ImGui::TextWrapped(OBFUSCATE_STR("NB: Cursor & selection are preserved when refocusing last used item in code."));
            ImGui::TreePop();
        }

        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Inputs & Focus/Dragging"));
        if (ImGui::TreeNode(OBFUSCATE_STR("Dragging")))
        {
            ImGui::TextWrapped(OBFUSCATE_STR("You can use ImGui::GetMouseDragDelta(0) to query for the dragged amount on any widget."));
            for (int button = 0; button < 3; button++)
            {
                ImGui::Text(OBFUSCATE_STR("IsMouseDragging(%d):"), button);
                ImGui::Text(OBFUSCATE_STR("  w/ default threshold: %d,"), ImGui::IsMouseDragging(button));
                ImGui::Text(OBFUSCATE_STR("  w/ zero threshold: %d,"), ImGui::IsMouseDragging(button, 0.0f));
                ImGui::Text(OBFUSCATE_STR("  w/ large threshold: %d,"), ImGui::IsMouseDragging(button, 20.0f));
            }

            ImGui::Button(OBFUSCATE_STR("Drag Me"));
            if (ImGui::IsItemActive())
                ImGui::GetForegroundDrawList()->AddLine(io.MouseClickedPos[0], io.MousePos, ImGui::GetColorU32(ImGuiCol_Button), 4.0f); // Draw a line between the button and the mouse cursor

            // Drag operations gets "unlocked" when the mouse has moved past a certain threshold
            // (the default threshold is stored in io.MouseDragThreshold). You can request a lower or higher
            // threshold using the second parameter of IsMouseDragging() and GetMouseDragDelta().
            ImVec2 value_raw = ImGui::GetMouseDragDelta(0, 0.0f);
            ImVec2 value_with_lock_threshold = ImGui::GetMouseDragDelta(0);
            ImVec2 mouse_delta = io.MouseDelta;
            ImGui::Text(OBFUSCATE_STR("GetMouseDragDelta(0):"));
            ImGui::Text(OBFUSCATE_STR("  w/ default threshold: (%.1f, %.1f)"), value_with_lock_threshold.x, value_with_lock_threshold.y);
            ImGui::Text(OBFUSCATE_STR("  w/ zero threshold: (%.1f, %.1f)"), value_raw.x, value_raw.y);
            ImGui::Text(OBFUSCATE_STR("io.MouseDelta: (%.1f, %.1f)"), mouse_delta.x, mouse_delta.y);
            ImGui::TreePop();
        }
    }
}

//-----------------------------------------------------------------------------
// [SECTION] About Window / ShowAboutWindow()
// Access from Dear ImGui Demo -> Tools -> About
//-----------------------------------------------------------------------------

void ImGui::ShowAboutWindow(bool* p_open)
{
    rtx_spoof_func;
    if (!ImGui::Begin(OBFUSCATE_STR("About Dear ImGui"), p_open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Tools/About Dear ImGui"));
    ImGui::Text(OBFUSCATE_STR("Dear ImGui %s"), ImGui::GetVersion());
    ImGui::Separator();
    ImGui::Text(OBFUSCATE_STR("By Omar Cornut and all Dear ImGui contributors."));
    ImGui::Text(OBFUSCATE_STR("Dear ImGui is licensed under the MIT License, see LICENSE for more information."));

    static bool show_config_info = false;
    ImGui::Checkbox(OBFUSCATE_STR("Config/Build Information"), &show_config_info);
    if (show_config_info)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& style = ImGui::GetStyle();

        bool copy_to_clipboard = ImGui::Button(OBFUSCATE_STR("Copy to clipboard"));
        ImVec2 child_size = ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 18);
        ImGui::BeginChildFrame(ImGui::GetID(OBFUSCATE_STR("cfg_infos")), child_size, ImGuiWindowFlags_NoMove);
        if (copy_to_clipboard)
        {
            ImGui::LogToClipboard();
            ImGui::LogText("```\n"); // Back quotes will make text appears without formatting when pasting on GitHub
        }

        ImGui::Text(OBFUSCATE_STR("Dear ImGui %s (%d)"), IMGUI_VERSION, IMGUI_VERSION_NUM);
        ImGui::Separator();
        ImGui::Text(OBFUSCATE_STR("sizeof(size_t): %d, sizeof(ImDrawIdx): %d, sizeof(ImDrawVert): %d"), (int)sizeof(size_t), (int)sizeof(ImDrawIdx), (int)sizeof(ImDrawVert));
        ImGui::Text(OBFUSCATE_STR("define: __cplusplus=%d"), (int)__cplusplus);
#ifdef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_DISABLE_OBSOLETE_FUNCTIONS"));
#endif
#ifdef IMGUI_DISABLE_OBSOLETE_KEYIO
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_DISABLE_OBSOLETE_KEYIO"));
#endif
#ifdef IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS"));
#endif
#ifdef IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS"));
#endif
#ifdef IMGUI_DISABLE_WIN32_FUNCTIONS
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_DISABLE_WIN32_FUNCTIONS"));
#endif
#ifdef IMGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS"));
#endif
#ifdef IMGUI_DISABLE_DEFAULT_MATH_FUNCTIONS
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_DISABLE_DEFAULT_MATH_FUNCTIONS"));
#endif
#ifdef IMGUI_DISABLE_DEFAULT_FILE_FUNCTIONS
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_DISABLE_DEFAULT_FILE_FUNCTIONS"));
#endif
#ifdef IMGUI_DISABLE_FILE_FUNCTIONS
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_DISABLE_FILE_FUNCTIONS"));
#endif
#ifdef IMGUI_DISABLE_DEFAULT_ALLOCATORS
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_DISABLE_DEFAULT_ALLOCATORS"));
#endif
#ifdef IMGUI_USE_BGRA_PACKED_COLOR
        ImGui::Text(OBFUSCATE_STR("define: IMGUI_USE_BGRA_PACKED_COLOR"));
#endif
#ifdef _WIN32
        ImGui::Text(OBFUSCATE_STR("define: _WIN32"));
#endif
#ifdef _WIN64
        ImGui::Text(OBFUSCATE_STR("define: _WIN64"));
#endif
#ifdef __linux__
        ImGui::Text(OBFUSCATE_STR("define: __linux__"));
#endif
#ifdef __APPLE__
        ImGui::Text(OBFUSCATE_STR("define: __APPLE__"));
#endif
#ifdef _MSC_VER
        ImGui::Text(OBFUSCATE_STR("define: _MSC_VER=%d"), _MSC_VER);
#endif
#ifdef _MSVC_LANG
        ImGui::Text(OBFUSCATE_STR("define: _MSVC_LANG=%d"), (int)_MSVC_LANG);
#endif
#ifdef __MINGW32__
        ImGui::Text(OBFUSCATE_STR("define: __MINGW32__"));
#endif
#ifdef __MINGW64__
        ImGui::Text(OBFUSCATE_STR("define: __MINGW64__"));
#endif
#ifdef __GNUC__
        ImGui::Text(OBFUSCATE_STR("define: __GNUC__=%d"), (int)__GNUC__);
#endif
#ifdef __clang_version__
        ImGui::Text(OBFUSCATE_STR("define: __clang_version__=%s"), __clang_version__);
#endif
        ImGui::Separator();
        ImGui::Text(OBFUSCATE_STR("io.BackendPlatformName: %s"), io.BackendPlatformName ? io.BackendPlatformName : OBFUSCATE_STR("NULL"));
        ImGui::Text(OBFUSCATE_STR("io.BackendRendererName: %s"), io.BackendRendererName ? io.BackendRendererName : OBFUSCATE_STR("NULL"));
        ImGui::Text(OBFUSCATE_STR("io.ConfigFlags: 0x%08X"), io.ConfigFlags);
        if (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard)        ImGui::Text(OBFUSCATE_STR(" NavEnableKeyboard"));
        if (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad)         ImGui::Text(OBFUSCATE_STR(" NavEnableGamepad"));
        if (io.ConfigFlags & ImGuiConfigFlags_NavEnableSetMousePos)     ImGui::Text(OBFUSCATE_STR(" NavEnableSetMousePos"));
        if (io.ConfigFlags & ImGuiConfigFlags_NavNoCaptureKeyboard)     ImGui::Text(OBFUSCATE_STR(" NavNoCaptureKeyboard"));
        if (io.ConfigFlags & ImGuiConfigFlags_NoMouse)                  ImGui::Text(OBFUSCATE_STR(" NoMouse"));
        if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)      ImGui::Text(OBFUSCATE_STR(" NoMouseCursorChange"));
        if (io.MouseDrawCursor)                                         ImGui::Text(OBFUSCATE_STR("io.MouseDrawCursor"));
        if (io.ConfigMacOSXBehaviors)                                   ImGui::Text(OBFUSCATE_STR("io.ConfigMacOSXBehaviors"));
        if (io.ConfigInputTextCursorBlink)                              ImGui::Text(OBFUSCATE_STR("io.ConfigInputTextCursorBlink"));
        if (io.ConfigWindowsResizeFromEdges)                            ImGui::Text(OBFUSCATE_STR("io.ConfigWindowsResizeFromEdges"));
        if (io.ConfigWindowsMoveFromTitleBarOnly)                       ImGui::Text(OBFUSCATE_STR("io.ConfigWindowsMoveFromTitleBarOnly"));
        if (io.ConfigMemoryCompactTimer >= 0.0f)                        ImGui::Text(OBFUSCATE_STR("io.ConfigMemoryCompactTimer = %.1f"), io.ConfigMemoryCompactTimer);
        ImGui::Text("io.BackendFlags: 0x%08X", io.BackendFlags);
        if (io.BackendFlags & ImGuiBackendFlags_HasGamepad)             ImGui::Text(OBFUSCATE_STR(" HasGamepad"));
        if (io.BackendFlags & ImGuiBackendFlags_HasMouseCursors)        ImGui::Text(OBFUSCATE_STR(" HasMouseCursors"));
        if (io.BackendFlags & ImGuiBackendFlags_HasSetMousePos)         ImGui::Text(OBFUSCATE_STR(" HasSetMousePos"));
        if (io.BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset)   ImGui::Text(OBFUSCATE_STR(" RendererHasVtxOffset"));
        ImGui::Separator();
        ImGui::Text(OBFUSCATE_STR("io.Fonts: %d fonts, Flags: 0x%08X, TexSize: %d,%d"), io.Fonts->Fonts.Size, io.Fonts->Flags, io.Fonts->TexWidth, io.Fonts->TexHeight);
        ImGui::Text(OBFUSCATE_STR("io.DisplaySize: %.2f,%.2f"), io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Text(OBFUSCATE_STR("io.DisplayFramebufferScale: %.2f,%.2f"), io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        ImGui::Separator();
        ImGui::Text(OBFUSCATE_STR("style.WindowPadding: %.2f,%.2f"), style.WindowPadding.x, style.WindowPadding.y);
        ImGui::Text(OBFUSCATE_STR("style.WindowBorderSize: %.2f"), style.WindowBorderSize);
        ImGui::Text(OBFUSCATE_STR("style.FramePadding: %.2f,%.2f"), style.FramePadding.x, style.FramePadding.y);
        ImGui::Text(OBFUSCATE_STR("style.FrameRounding: %.2f"), style.FrameRounding);
        ImGui::Text(OBFUSCATE_STR("style.FrameBorderSize: %.2f"), style.FrameBorderSize);
        ImGui::Text(OBFUSCATE_STR("style.ItemSpacing: %.2f,%.2f"), style.ItemSpacing.x, style.ItemSpacing.y);
        ImGui::Text(OBFUSCATE_STR("style.ItemInnerSpacing: %.2f,%.2f"), style.ItemInnerSpacing.x, style.ItemInnerSpacing.y);

        if (copy_to_clipboard)
        {
            ImGui::LogText("\n```\n");
            ImGui::LogFinish();
        }
        ImGui::EndChildFrame();
    }
    ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] Style Editor / ShowStyleEditor()
//-----------------------------------------------------------------------------
// - ShowFontSelector()
// - ShowStyleSelector()
// - ShowStyleEditor()
//-----------------------------------------------------------------------------

// Forward declare ShowFontAtlas() which isn't worth putting in public API yet
namespace ImGui { IMGUI_API void ShowFontAtlas(ImFontAtlas* atlas); }

// Demo helper function to select among loaded fonts.
// Here we use the regular BeginCombo()/EndCombo() api which is the more flexible one.
void ImGui::ShowFontSelector(const char* label)
{
    rtx_spoof_func;
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font_current = ImGui::GetFont();
    if (ImGui::BeginCombo(label, font_current->GetDebugName()))
    {
        for (int n = 0; n < io.Fonts->Fonts.Size; n++)
        {
            ImFont* font = io.Fonts->Fonts[n];
            ImGui::PushID((void*)font);
            if (ImGui::Selectable(font->GetDebugName(), font == font_current))
                io.FontDefault = font;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    HelpMarker(
        OBFUSCATE_STR("- Load additional fonts with io.Fonts->AddFontFromFileTTF().\n"
        "- The font atlas is built when calling io.Fonts->GetTexDataAsXXXX() or io.Fonts->Build().\n"
        "- Read FAQ and docs/FONTS.md for more details.\n"
        "- If you need to add/remove fonts at runtime (e.g. for DPI change), do it before calling NewFrame()."));
}

// Demo helper function to select among default colors. See ShowStyleEditor() for more advanced options.
// Here we use the simplified Combo() api that packs items into a single literal string.
// Useful for quick combo boxes where the choices are known locally.
bool ImGui::ShowStyleSelector(const char* label)
{
    rtx_spoof_func;
    static int style_idx = -1;
    if (ImGui::Combo(label, &style_idx, OBFUSCATE_STR("Dark\0Light\0Classic\0")))
    {
        switch (style_idx)
        {
        case 0: ImGui::StyleColorsDark(); break;
        case 1: ImGui::StyleColorsLight(); break;
        case 2: ImGui::StyleColorsClassic(); break;
        }
        return true;
    }
    return false;
}

void ImGui::ShowStyleEditor(ImGuiStyle* ref)
{
    rtx_spoof_func;
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Tools/Style Editor"));
    // You can pass in a reference ImGuiStyle structure to compare to, revert to and save to
    // (without a reference style pointer, we will use one compared locally as a reference)
    ImGuiStyle& style = ImGui::GetStyle();
    static ImGuiStyle ref_saved_style;

    // Default to using internal storage as reference
    static bool init = true;
    if (init && ref == NULL)
        ref_saved_style = style;
    init = false;
    if (ref == NULL)
        ref = &ref_saved_style;

    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.50f);

    if (ImGui::ShowStyleSelector(OBFUSCATE_STR("Colors##Selector")))
        ref_saved_style = style;
    ImGui::ShowFontSelector(OBFUSCATE_STR("Fonts##Selector"));

    // Simplified Settings (expose floating-pointer border sizes as boolean representing 0.0f or 1.0f)
    if (ImGui::SliderFloat(OBFUSCATE_STR("FrameRounding"), &style.FrameRounding, 0.0f, 12.0f, OBFUSCATE_STR("%.0f")))
        style.GrabRounding = style.FrameRounding; // Make GrabRounding always the same value as FrameRounding
    { bool border = (style.WindowBorderSize > 0.0f); if (ImGui::Checkbox(OBFUSCATE_STR("WindowBorder"), &border)) { style.WindowBorderSize = border ? 1.0f : 0.0f; } }
    ImGui::SameLine();
    { bool border = (style.FrameBorderSize > 0.0f);  if (ImGui::Checkbox(OBFUSCATE_STR("FrameBorder"),  &border)) { style.FrameBorderSize  = border ? 1.0f : 0.0f; } }
    ImGui::SameLine();
    { bool border = (style.PopupBorderSize > 0.0f);  if (ImGui::Checkbox(OBFUSCATE_STR("PopupBorder"),  &border)) { style.PopupBorderSize  = border ? 1.0f : 0.0f; } }

    // Save/Revert button
    if (ImGui::Button(OBFUSCATE_STR("Save Ref")))
        *ref = ref_saved_style = style;
    ImGui::SameLine();
    if (ImGui::Button(OBFUSCATE_STR("Revert Ref")))
        style = *ref;
    ImGui::SameLine();
    HelpMarker(OBFUSCATE_STR(
        "Save/Revert in local non-persistent storage. Default Colors definition are not affected. "
        "Use \"Export\" below to save them somewhere."));

    ImGui::Separator();

    if (ImGui::BeginTabBar(OBFUSCATE_STR("##tabs"), ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem(OBFUSCATE_STR("Sizes")))
        {
            ImGui::Text(OBFUSCATE_STR("Main"));
            ImGui::SliderFloat2(OBFUSCATE_STR("WindowPadding"), (float*)&style.WindowPadding, 0.0f, 20.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat2(OBFUSCATE_STR("FramePadding"), (float*)&style.FramePadding, 0.0f, 20.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat2(OBFUSCATE_STR("CellPadding"), (float*)&style.CellPadding, 0.0f, 20.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat2(OBFUSCATE_STR("ItemSpacing"), (float*)&style.ItemSpacing, 0.0f, 20.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat2(OBFUSCATE_STR("ItemInnerSpacing"), (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat2(OBFUSCATE_STR("TouchExtraPadding"), (float*)&style.TouchExtraPadding, 0.0f, 10.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("IndentSpacing"), &style.IndentSpacing, 0.0f, 30.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("ScrollbarSize"), &style.ScrollbarSize, 1.0f, 20.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("GrabMinSize"), &style.GrabMinSize, 1.0f, 20.0f, OBFUSCATE_STR("%.0f"));
            ImGui::Text(OBFUSCATE_STR("Borders"));
            ImGui::SliderFloat(OBFUSCATE_STR("WindowBorderSize"), &style.WindowBorderSize, 0.0f, 1.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("ChildBorderSize"), &style.ChildBorderSize, 0.0f, 1.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("PopupBorderSize"), &style.PopupBorderSize, 0.0f, 1.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("FrameBorderSize"), &style.FrameBorderSize, 0.0f, 1.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("TabBorderSize"), &style.TabBorderSize, 0.0f, 1.0f, OBFUSCATE_STR("%.0f"));
            ImGui::Text(OBFUSCATE_STR("Rounding"));
            ImGui::SliderFloat(OBFUSCATE_STR("WindowRounding"), &style.WindowRounding, 0.0f, 12.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("ChildRounding"), &style.ChildRounding, 0.0f, 12.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("FrameRounding"), &style.FrameRounding, 0.0f, 12.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("PopupRounding"), &style.PopupRounding, 0.0f, 12.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("ScrollbarRounding"), &style.ScrollbarRounding, 0.0f, 12.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("GrabRounding"), &style.GrabRounding, 0.0f, 12.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("LogSliderDeadzone"), &style.LogSliderDeadzone, 0.0f, 12.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderFloat(OBFUSCATE_STR("TabRounding"), &style.TabRounding, 0.0f, 12.0f, OBFUSCATE_STR("%.0f"));
            ImGui::Text(OBFUSCATE_STR("Alignment"));
            ImGui::SliderFloat2(OBFUSCATE_STR("WindowTitleAlign"), (float*)&style.WindowTitleAlign, 0.0f, 1.0f, OBFUSCATE_STR("%.2f"));
            int window_menu_button_position = style.WindowMenuButtonPosition + 1;
            if (ImGui::Combo(OBFUSCATE_STR("WindowMenuButtonPosition"), (int*)&window_menu_button_position, OBFUSCATE_STR("None\0Left\0Right\0")))
                style.WindowMenuButtonPosition = window_menu_button_position - 1;
            ImGui::Combo(OBFUSCATE_STR("ColorButtonPosition"), (int*)&style.ColorButtonPosition, OBFUSCATE_STR("Left\0Right\0"));
            ImGui::SliderFloat2(OBFUSCATE_STR("ButtonTextAlign"), (float*)&style.ButtonTextAlign, 0.0f, 1.0f, OBFUSCATE_STR("%.2f"));
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Alignment applies when a button is larger than its text content."));
            ImGui::SliderFloat2(OBFUSCATE_STR("SelectableTextAlign"), (float*)&style.SelectableTextAlign, 0.0f, 1.0f, OBFUSCATE_STR("%.2f"));
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Alignment applies when a selectable is larger than its text content."));
            ImGui::Text(OBFUSCATE_STR("Safe Area Padding"));
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Adjust if you cannot see the edges of your screen (e.g. on a TV where scaling has not been configured)."));
            ImGui::SliderFloat2(OBFUSCATE_STR("DisplaySafeAreaPadding"), (float*)&style.DisplaySafeAreaPadding, 0.0f, 30.0f, OBFUSCATE_STR("%.0f"));
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(OBFUSCATE_STR("Colors")))
        {
            static int output_dest = 0;
            static bool output_only_modified = true;
            if (ImGui::Button(OBFUSCATE_STR("Export")))
            {
                if (output_dest == 0)
                    ImGui::LogToClipboard();
                else
                    ImGui::LogToTTY();
                ImGui::LogText("ImVec4* colors = ImGui::GetStyle().Colors;" IM_NEWLINE);
                for (int i = 0; i < ImGuiCol_COUNT; i++)
                {
                    const ImVec4& col = style.Colors[i];
                    const char* name = ImGui::GetStyleColorName(i);
                    if (!output_only_modified || memcmp(&col, &ref->Colors[i], sizeof(ImVec4)) != 0)
                        ImGui::LogText("colors[ImGuiCol_%s]%*s= ImVec4(%.2ff, %.2ff, %.2ff, %.2ff);" IM_NEWLINE,
                            name, 23 - (int)strlen(name), "", col.x, col.y, col.z, col.w);
                }
                ImGui::LogFinish();
            }
            ImGui::SameLine(); ImGui::SetNextItemWidth(120); ImGui::Combo(OBFUSCATE_STR("##output_type"), &output_dest, OBFUSCATE_STR("To Clipboard\0To TTY\0"));
            ImGui::SameLine(); ImGui::Checkbox(OBFUSCATE_STR("Only Modified Colors"), &output_only_modified);

            static ImGuiTextFilter filter;
            filter.Draw(OBFUSCATE_STR("Filter colors"), ImGui::GetFontSize() * 16);

            static ImGuiColorEditFlags alpha_flags = 0;
            if (ImGui::RadioButton(OBFUSCATE_STR("Opaque"), alpha_flags == ImGuiColorEditFlags_None))             { alpha_flags = ImGuiColorEditFlags_None; } ImGui::SameLine();
            if (ImGui::RadioButton(OBFUSCATE_STR("Alpha"),  alpha_flags == ImGuiColorEditFlags_AlphaPreview))     { alpha_flags = ImGuiColorEditFlags_AlphaPreview; } ImGui::SameLine();
            if (ImGui::RadioButton(OBFUSCATE_STR("Both"),   alpha_flags == ImGuiColorEditFlags_AlphaPreviewHalf)) { alpha_flags = ImGuiColorEditFlags_AlphaPreviewHalf; } ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR(
                "In the color list:\n"
                "Left-click on color square to open color picker,\n"
                "Right-click to open edit options menu."));

            ImGui::BeginChild(OBFUSCATE_STR("##colors"), ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NavFlattened);
            ImGui::PushItemWidth(-160);
            for (int i = 0; i < ImGuiCol_COUNT; i++)
            {
                const char* name = ImGui::GetStyleColorName(i);
                if (!filter.PassFilter(name))
                    continue;
                ImGui::PushID(i);
                ImGui::ColorEdit4(OBFUSCATE_STR("##color"), (float*)&style.Colors[i], ImGuiColorEditFlags_AlphaBar | alpha_flags);
                if (memcmp(&style.Colors[i], &ref->Colors[i], sizeof(ImVec4)) != 0)
                {
                    // Tips: in a real user application, you may want to merge and use an icon font into the main font,
                    // so instead of "Save"/"Revert" you'd use icons!
                    // Read the FAQ and docs/FONTS.md about using icon fonts. It's really easy and super convenient!
                    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x); if (ImGui::Button(OBFUSCATE_STR("Save"))) { ref->Colors[i] = style.Colors[i]; }
                    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x); if (ImGui::Button(OBFUSCATE_STR("Revert"))) { style.Colors[i] = ref->Colors[i]; }
                }
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                ImGui::TextUnformatted(name);
                ImGui::PopID();
            }
            ImGui::PopItemWidth();
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(OBFUSCATE_STR("Fonts")))
        {
            ImGuiIO& io = ImGui::GetIO();
            ImFontAtlas* atlas = io.Fonts;
            HelpMarker(OBFUSCATE_STR("Read FAQ and docs/FONTS.md for details on font loading."));
            ImGui::ShowFontAtlas(atlas);

            // Post-baking font scaling. Note that this is NOT the nice way of scaling fonts, read below.
            // (we enforce hard clamping manually as by default DragFloat/SliderFloat allows CTRL+Click text to get out of bounds).
            const float MIN_SCALE = 0.3f;
            const float MAX_SCALE = 2.0f;
            HelpMarker(OBFUSCATE_STR(
                "Those are old settings provided for convenience.\n"
                "However, the _correct_ way of scaling your UI is currently to reload your font at the designed size, "
                "rebuild the font atlas, and call style.ScaleAllSizes() on a reference ImGuiStyle structure.\n"
                "Using those settings here will give you poor quality results."));
            static float window_scale = 1.0f;
            ImGui::PushItemWidth(ImGui::GetFontSize() * 8);
            if (ImGui::DragFloat(OBFUSCATE_STR("window scale"), &window_scale, 0.005f, MIN_SCALE, MAX_SCALE, OBFUSCATE_STR("%.2f"), ImGuiSliderFlags_AlwaysClamp)) // Scale only this window
                ImGui::SetWindowFontScale(window_scale);
            ImGui::DragFloat(OBFUSCATE_STR("global scale"), &io.FontGlobalScale, 0.005f, MIN_SCALE, MAX_SCALE, OBFUSCATE_STR("%.2f"), ImGuiSliderFlags_AlwaysClamp); // Scale everything
            ImGui::PopItemWidth();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(OBFUSCATE_STR("Rendering")))
        {
            ImGui::Checkbox(OBFUSCATE_STR("Anti-aliased lines"), &style.AntiAliasedLines);
            ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR("When disabling anti-aliasing lines, you'll probably want to disable borders in your style as well."));

            ImGui::Checkbox(OBFUSCATE_STR("Anti-aliased lines use texture"), &style.AntiAliasedLinesUseTex);
            ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR("Faster lines using texture data. Require backend to render with bilinear filtering (not point/nearest filtering)."));

            ImGui::Checkbox(OBFUSCATE_STR("Anti-aliased fill"), &style.AntiAliasedFill);
            ImGui::PushItemWidth(ImGui::GetFontSize() * 8);
            ImGui::DragFloat(OBFUSCATE_STR("Curve Tessellation Tolerance"), &style.CurveTessellationTol, 0.02f, 0.10f, 10.0f, OBFUSCATE_STR("%.2f"));
            if (style.CurveTessellationTol < 0.10f) style.CurveTessellationTol = 0.10f;

            // When editing the "Circle Segment Max Error" value, draw a preview of its effect on auto-tessellated circles.
            ImGui::DragFloat(OBFUSCATE_STR("Circle Tessellation Max Error"), &style.CircleTessellationMaxError , 0.005f, 0.10f, 5.0f, OBFUSCATE_STR("%.2f"), ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemActive())
            {
                ImGui::SetNextWindowPos(ImGui::GetCursorScreenPos());
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(OBFUSCATE_STR("(R = radius, N = number of segments)"));
                ImGui::Spacing();
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                const float min_widget_width = ImGui::CalcTextSize(OBFUSCATE_STR("N: MMM\nR: MMM")).x;
                for (int n = 0; n < 8; n++)
                {
                    const float RAD_MIN = 5.0f;
                    const float RAD_MAX = 70.0f;
                    const float rad = RAD_MIN + (RAD_MAX - RAD_MIN) * (float)n / (8.0f - 1.0f);

                    ImGui::BeginGroup();

                    ImGui::Text(OBFUSCATE_STR("R: %.f\nN: %d"), rad, draw_list->_CalcCircleAutoSegmentCount(rad));

                    const float canvas_width = IM_MAX(min_widget_width, rad * 2.0f);
                    const float offset_x     = floorf(canvas_width * 0.5f);
                    const float offset_y     = floorf(RAD_MAX);

                    const ImVec2 p1 = ImGui::GetCursorScreenPos();
                    draw_list->AddCircle(ImVec2(p1.x + offset_x, p1.y + offset_y), rad, ImGui::GetColorU32(ImGuiCol_Text));
                    ImGui::Dummy(ImVec2(canvas_width, RAD_MAX * 2));

                    /*
                    const ImVec2 p2 = ImGui::GetCursorScreenPos();
                    draw_list->AddCircleFilled(ImVec2(p2.x + offset_x, p2.y + offset_y), rad, ImGui::GetColorU32(ImGuiCol_Text));
                    ImGui::Dummy(ImVec2(canvas_width, RAD_MAX * 2));
                    */

                    ImGui::EndGroup();
                    ImGui::SameLine();
                }
                ImGui::EndTooltip();
            }
            ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR("When drawing circle primitives with \"num_segments == 0\" tesselation will be calculated automatically."));

            ImGui::DragFloat(OBFUSCATE_STR("Global Alpha"), &style.Alpha, 0.005f, 0.20f, 1.0f, OBFUSCATE_STR("%.2f")); // Not exposing zero here so user doesn't "lose" the UI (zero alpha clips all widgets). But application code could have a toggle to switch between zero and non-zero.
            ImGui::DragFloat(OBFUSCATE_STR("Disabled Alpha"), &style.DisabledAlpha, 0.005f, 0.0f, 1.0f, OBFUSCATE_STR("%.2f")); ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("Additional alpha multiplier for disabled items (multiply over current value of Alpha)."));
            ImGui::PopItemWidth();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(OBFUSCATE_STR("Shadows")))
        {
            ImGui::Text(OBFUSCATE_STR("Window shadows:"));
            ImGui::ColorEdit4(OBFUSCATE_STR("Color"), (float*)&style.Colors[ImGuiCol_WindowShadow], ImGuiColorEditFlags_AlphaBar);
            ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR("Same as 'WindowShadow' in Colors tab."));

            ImGui::SliderFloat(OBFUSCATE_STR("Size"), &style.WindowShadowSize, 0.0f, 128.0f, OBFUSCATE_STR("%.1f"));
            ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR("Set shadow size to zero to disable shadows."));
            ImGui::SliderFloat(OBFUSCATE_STR("Offset distance"), &style.WindowShadowOffsetDist, 0.0f, 64.0f, OBFUSCATE_STR("%.0f"));
            ImGui::SliderAngle(OBFUSCATE_STR("Offset angle"), &style.WindowShadowOffsetAngle);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::PopItemWidth();
}

//-----------------------------------------------------------------------------
// [SECTION] User Guide / ShowUserGuide()
//-----------------------------------------------------------------------------

void ImGui::ShowUserGuide()
{
    rtx_spoof_func;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::BulletText(OBFUSCATE_STR("Double-click on title bar to collapse window."));
    ImGui::BulletText(OBFUSCATE_STR("Click and drag on lower corner to resize window\n""(double-click to auto fit window to its contents)."));
    ImGui::BulletText(OBFUSCATE_STR("CTRL+Click on a slider or drag box to input value as text."));
    ImGui::BulletText(OBFUSCATE_STR("TAB/SHIFT+TAB to cycle through keyboard editable fields."));
    ImGui::BulletText(OBFUSCATE_STR("CTRL+Tab to select a window."));
    if (io.FontAllowUserScaling)
        ImGui::BulletText(OBFUSCATE_STR("CTRL+Mouse Wheel to zoom window contents."));
    ImGui::BulletText(OBFUSCATE_STR("While inputing text:\n"));
    ImGui::Indent();
    ImGui::BulletText(OBFUSCATE_STR("CTRL+Left/Right to word jump."));
    ImGui::BulletText(OBFUSCATE_STR("CTRL+A or double-click to select all."));
    ImGui::BulletText(OBFUSCATE_STR("CTRL+X/C/V to use clipboard cut/copy/paste."));
    ImGui::BulletText(OBFUSCATE_STR("CTRL+Z,CTRL+Y to undo/redo."));
    ImGui::BulletText(OBFUSCATE_STR("ESCAPE to revert."));
    ImGui::Unindent();
    ImGui::BulletText(OBFUSCATE_STR("With keyboard navigation enabled:"));
    ImGui::Indent();
    ImGui::BulletText(OBFUSCATE_STR("Arrow keys to navigate."));
    ImGui::BulletText(OBFUSCATE_STR("Space to activate a widget."));
    ImGui::BulletText(OBFUSCATE_STR("Return to input text into a widget."));
    ImGui::BulletText(OBFUSCATE_STR("Escape to deactivate a widget, close popup, exit child window."));
    ImGui::BulletText(OBFUSCATE_STR("Alt to jump to the menu layer of a window."));
    ImGui::Unindent();
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Main Menu Bar / ShowExampleAppMainMenuBar()
//-----------------------------------------------------------------------------
// - ShowExampleAppMainMenuBar()
// - ShowExampleMenuFile()
//-----------------------------------------------------------------------------

// Demonstrate creating a "main" fullscreen menu bar and populating it.
// Note the difference between BeginMainMenuBar() and BeginMenuBar():
// - BeginMenuBar() = menu-bar inside current window (which needs the ImGuiWindowFlags_MenuBar flag!)
// - BeginMainMenuBar() = helper to create menu-bar-sized window at the top of the main viewport + call BeginMenuBar() into it.
static void ShowExampleAppMainMenuBar()
{
    rtx_spoof_func;
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu(OBFUSCATE_STR("File")))
        {
            ShowExampleMenuFile();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(OBFUSCATE_STR("Edit")))
        {
            if (ImGui::MenuItem(OBFUSCATE_STR("Undo"), OBFUSCATE_STR("CTRL+Z"))) {}
            if (ImGui::MenuItem(OBFUSCATE_STR("Redo"), OBFUSCATE_STR("CTRL+Y"), false, false)) {}  // Disabled item
            ImGui::Separator();
            if (ImGui::MenuItem(OBFUSCATE_STR("Cut"), OBFUSCATE_STR("CTRL+X"))) {}
            if (ImGui::MenuItem(OBFUSCATE_STR("Copy"), OBFUSCATE_STR("CTRL+C"))) {}
            if (ImGui::MenuItem(OBFUSCATE_STR("Paste"), OBFUSCATE_STR("CTRL+V"))) {}
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

// Note that shortcuts are currently provided for display only
// (future version will add explicit flags to BeginMenu() to request processing shortcuts)
static void ShowExampleMenuFile()
{
    rtx_spoof_func;
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Menu"));
    ImGui::MenuItem(OBFUSCATE_STR("(demo menu)"), NULL, false, false);
    if (ImGui::MenuItem(OBFUSCATE_STR("New"))) {}
    if (ImGui::MenuItem(OBFUSCATE_STR("Open"), OBFUSCATE_STR("Ctrl+O"))) {}
    if (ImGui::BeginMenu(OBFUSCATE_STR("Open Recent")))
    {
        ImGui::MenuItem(OBFUSCATE_STR("fish_hat.c"));
        ImGui::MenuItem(OBFUSCATE_STR("fish_hat.inl"));
        ImGui::MenuItem(OBFUSCATE_STR("fish_hat.h"));
        if (ImGui::BeginMenu(OBFUSCATE_STR("More..")))
        {
            ImGui::MenuItem(OBFUSCATE_STR("Hello"));
            ImGui::MenuItem(OBFUSCATE_STR("Sailor"));
            if (ImGui::BeginMenu(OBFUSCATE_STR("Recurse..")))
            {
                ShowExampleMenuFile();
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(OBFUSCATE_STR("Save"), OBFUSCATE_STR("Ctrl+S"))) {}
    if (ImGui::MenuItem(OBFUSCATE_STR("Save As.."))) {}

    ImGui::Separator();
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Menu/Options"));
    if (ImGui::BeginMenu(OBFUSCATE_STR("Options")))
    {
        static bool enabled = true;
        ImGui::MenuItem(OBFUSCATE_STR("Enabled"), OBFUSCATE_STR(""), &enabled);
        ImGui::BeginChild(OBFUSCATE_STR("child"), ImVec2(0, 60), true);
        for (int i = 0; i < 10; i++)
            ImGui::Text(OBFUSCATE_STR("Scrolling Text %d"), i);
        ImGui::EndChild();
        static float f = 0.5f;
        static int n = 0;
        ImGui::SliderFloat(OBFUSCATE_STR("Value"), &f, 0.0f, 1.0f);
        ImGui::InputFloat(OBFUSCATE_STR("Input"), &f, 0.1f);
        ImGui::Combo(OBFUSCATE_STR("Combo"), &n, OBFUSCATE_STR("Yes\0No\0Maybe\0\0"));
        ImGui::EndMenu();
    }

    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Menu/Colors"));
    if (ImGui::BeginMenu(OBFUSCATE_STR("Colors")))
    {
        float sz = ImGui::GetTextLineHeight();
        for (int i = 0; i < ImGuiCol_COUNT; i++)
        {
            const char* name = ImGui::GetStyleColorName((ImGuiCol)i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), ImGui::GetColorU32((ImGuiCol)i));
            ImGui::Dummy(ImVec2(sz, sz));
            ImGui::SameLine();
            ImGui::MenuItem(name);
        }
        ImGui::EndMenu();
    }

    // Here we demonstrate appending again to the "Options" menu (which we already created above)
    // Of course in this demo it is a little bit silly that this function calls BeginMenu("Options") twice.
    // In a real code-base using it would make senses to use this feature from very different code locations.
    if (ImGui::BeginMenu(OBFUSCATE_STR("Options"))) // <-- Append!
    {
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Menu/Append to an existing menu"));
        static bool b = true;
        ImGui::Checkbox(OBFUSCATE_STR("SomeOption"), &b);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(OBFUSCATE_STR("Disabled"), false)) // Disabled
    {
        IM_ASSERT(0);
    }
    if (ImGui::MenuItem(OBFUSCATE_STR("Checked"), NULL, true)) {}
    if (ImGui::MenuItem(OBFUSCATE_STR("Quit"), OBFUSCATE_STR("Alt+F4"))) {}
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Debug Console / ShowExampleAppConsole()
//-----------------------------------------------------------------------------

// Demonstrate creating a simple console window, with scrolling, filtering, completion and history.
// For the console example, we are using a more C++ like approach of declaring a class to hold both data and functions.
struct ExampleAppConsole
{
    char                  InputBuf[256];
    ImVector<char*>       Items;
    ImVector<const char*> Commands;
    ImVector<char*>       History;
    int                   HistoryPos;    // -1: new line, 0..History.Size-1 browsing history.
    ImGuiTextFilter       Filter;
    bool                  AutoScroll;
    bool                  ScrollToBottom;

    ExampleAppConsole()
    {
        rtx_spoof_func;
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Console"));
        ClearLog();
        memset(InputBuf, 0, sizeof(InputBuf));
        HistoryPos = -1;

        // "CLASSIFY" is here to provide the test case where "C"+[tab] completes to "CL" and display multiple matches.
        Commands.push_back(OBFUSCATE_STR("HELP"));
        Commands.push_back(OBFUSCATE_STR("HISTORY"));
        Commands.push_back(OBFUSCATE_STR("CLEAR"));
        Commands.push_back(OBFUSCATE_STR("CLASSIFY"));
        AutoScroll = true;
        ScrollToBottom = false;
        AddLog(OBFUSCATE_STR("Welcome to Dear ImGui!"));
    }
    ~ExampleAppConsole()
    {
        rtx_spoof_func;
        ClearLog();
        for (int i = 0; i < History.Size; i++)
            free(History[i]);
    }

    // Portable helpers
    static int   Stricmp(const char* s1, const char* s2)         { int d; while ((d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; } return d; }
    static int   Strnicmp(const char* s1, const char* s2, int n) { int d = 0; while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; } return d; }
    static char* Strdup(const char* s)                           { IM_ASSERT(s); size_t len = strlen(s) + 1; void* buf = malloc(len); IM_ASSERT(buf); return (char*)memcpy(buf, (const void*)s, len); }
    static void  Strtrim(char* s)                                { char* str_end = s + strlen(s); while (str_end > s && str_end[-1] == ' ') str_end--; *str_end = 0; }

    void    ClearLog()
    {
        rtx_spoof_func;
        for (int i = 0; i < Items.Size; i++)
            free(Items[i]);
        Items.clear();
    }

    void    AddLog(const char* fmt, ...) IM_FMTARGS(2)
    {
        rtx_spoof_func;
        // FIXME-OPT
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
        buf[IM_ARRAYSIZE(buf)-1] = 0;
        va_end(args);
        Items.push_back(Strdup(buf));
    }

    void    Draw(const char* title, bool* p_open)
    {
        rtx_spoof_func;
        ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title, p_open))
        {
            ImGui::End();
            return;
        }

        // As a specific feature guaranteed by the library, after calling Begin() the last Item represent the title bar.
        // So e.g. IsItemHovered() will return true when hovering the title bar.
        // Here we create a context menu only available from the title bar.
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem(OBFUSCATE_STR("Close Console")))
                *p_open = false;
            ImGui::EndPopup();
        }

        ImGui::TextWrapped(OBFUSCATE_STR(
            "This example implements a console with basic coloring, completion (TAB key) and history (Up/Down keys). A more elaborate "
            "implementation may want to store entries along with extra data such as timestamp, emitter, etc."));
        ImGui::TextWrapped(OBFUSCATE_STR("Enter 'HELP' for help."));

        // TODO: display items starting from the bottom

        if (ImGui::SmallButton(OBFUSCATE_STR("Add Debug Text")))  { AddLog(OBFUSCATE_STR("%d some text"), Items.Size); AddLog(OBFUSCATE_STR("some more text")); AddLog(OBFUSCATE_STR("display very important message here!")); }
        ImGui::SameLine();
        if (ImGui::SmallButton(OBFUSCATE_STR("Add Debug Error"))) { AddLog(OBFUSCATE_STR("[error] something went wrong")); }
        ImGui::SameLine();
        if (ImGui::SmallButton(OBFUSCATE_STR("Clear")))           { ClearLog(); }
        ImGui::SameLine();
        bool copy_to_clipboard = ImGui::SmallButton(OBFUSCATE_STR("Copy"));
        //static float t = 0.0f; if (ImGui::GetTime() - t > 0.02f) { t = ImGui::GetTime(); AddLog("Spam %f", t); }

        ImGui::Separator();

        // Options menu
        if (ImGui::BeginPopup(OBFUSCATE_STR("Options")))
        {
            ImGui::Checkbox(OBFUSCATE_STR("Auto-scroll"), &AutoScroll);
            ImGui::EndPopup();
        }

        // Options, Filter
        if (ImGui::Button(OBFUSCATE_STR("Options")))
            ImGui::OpenPopup(OBFUSCATE_STR("Options"));
        ImGui::SameLine();
        Filter.Draw(OBFUSCATE_STR("Filter (\"incl,-excl\") (\"error\")"), 180);
        ImGui::Separator();

        // Reserve enough left-over height for 1 separator + 1 input text
        const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild(OBFUSCATE_STR("ScrollingRegion"), ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar))
        {
            if (ImGui::BeginPopupContextWindow())
            {
                if (ImGui::Selectable(OBFUSCATE_STR("Clear"))) ClearLog();
                ImGui::EndPopup();
            }

            // Display every line as a separate entry so we can change their color or add custom widgets.
            // If you only want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
            // NB- if you have thousands of entries this approach may be too inefficient and may require user-side clipping
            // to only process visible items. The clipper will automatically measure the height of your first item and then
            // "seek" to display only items in the visible area.
            // To use the clipper we can replace your standard loop:
            //      for (int i = 0; i < Items.Size; i++)
            //   With:
            //      ImGuiListClipper clipper;
            //      clipper.Begin(Items.Size);
            //      while (clipper.Step())
            //         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
            // - That your items are evenly spaced (same height)
            // - That you have cheap random access to your elements (you can access them given their index,
            //   without processing all the ones before)
            // You cannot this code as-is if a filter is active because it breaks the 'cheap random-access' property.
            // We would need random-access on the post-filtered list.
            // A typical application wanting coarse clipping and filtering may want to pre-compute an array of indices
            // or offsets of items that passed the filtering test, recomputing this array when user changes the filter,
            // and appending newly elements as they are inserted. This is left as a task to the user until we can manage
            // to improve this example code!
            // If your items are of variable height:
            // - Split them into same height items would be simpler and facilitate random-seeking into your list.
            // - Consider using manual call to IsRectVisible() and skipping extraneous decoration from your items.
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
            if (copy_to_clipboard)
                ImGui::LogToClipboard();
            for (int i = 0; i < Items.Size; i++)
            {
                const char* item = Items[i];
                if (!Filter.PassFilter(item))
                    continue;

                // Normally you would store more information in your item than just a string.
                // (e.g. make Items[] an array of structure, store color/type etc.)
                ImVec4 color;
                bool has_color = false;
                if (strstr(item, OBFUSCATE_STR("[error]"))) { color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); has_color = true; }
                else if (strncmp(item, OBFUSCATE_STR("# "), 2) == 0) { color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f); has_color = true; }
                if (has_color)
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(item);
                if (has_color)
                    ImGui::PopStyleColor();
            }
            if (copy_to_clipboard)
                ImGui::LogFinish();

            // Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
            // Using a scrollbar or mouse-wheel will take away from the bottom edge.
            if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
                ImGui::SetScrollHereY(1.0f);
            ScrollToBottom = false;

            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::Separator();

        // Command-line
        bool reclaim_focus = false;
        ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
        if (ImGui::InputText(OBFUSCATE_STR("Input"), InputBuf, IM_ARRAYSIZE(InputBuf), input_text_flags, &TextEditCallbackStub, (void*)this))
        {
            char* s = InputBuf;
            Strtrim(s);
            if (s[0])
                ExecCommand(s);
            strcpy(s, "");
            reclaim_focus = true;
        }

        // Auto-focus on window apparition
        ImGui::SetItemDefaultFocus();
        if (reclaim_focus)
            ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

        ImGui::End();
    }

    void    ExecCommand(const char* command_line)
    {
        rtx_spoof_func;
        AddLog(OBFUSCATE_STR("# %s\n"), command_line);

        // Insert into history. First find match and delete it so it can be pushed to the back.
        // This isn't trying to be smart or optimal.
        HistoryPos = -1;
        for (int i = History.Size - 1; i >= 0; i--)
            if (Stricmp(History[i], command_line) == 0)
            {
                free(History[i]);
                History.erase(History.begin() + i);
                break;
            }
        History.push_back(Strdup(command_line));

        // Process command
        if (Stricmp(command_line, OBFUSCATE_STR("CLEAR")) == 0)
        {
            ClearLog();
        }
        else if (Stricmp(command_line, OBFUSCATE_STR("HELP")) == 0)
        {
            AddLog(OBFUSCATE_STR("Commands:"));
            for (int i = 0; i < Commands.Size; i++)
                AddLog(OBFUSCATE_STR("- %s"), Commands[i]);
        }
        else if (Stricmp(command_line, OBFUSCATE_STR("HISTORY")) == 0)
        {
            int first = History.Size - 10;
            for (int i = first > 0 ? first : 0; i < History.Size; i++)
                AddLog(OBFUSCATE_STR("%3d: %s\n"), i, History[i]);
        }
        else
        {
            AddLog(OBFUSCATE_STR("Unknown command: '%s'\n"), command_line);
        }

        // On command input, we scroll to bottom even if AutoScroll==false
        ScrollToBottom = true;
    }

    // In C++11 you'd be better off using lambdas for this sort of forwarding callbacks
    static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
    {
        rtx_spoof_func;
        ExampleAppConsole* console = (ExampleAppConsole*)data->UserData;
        return console->TextEditCallback(data);
    }

    int     TextEditCallback(ImGuiInputTextCallbackData* data)
    {
        rtx_spoof_func;
        //AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
        switch (data->EventFlag)
        {
        case ImGuiInputTextFlags_CallbackCompletion:
            {
                // Example of TEXT COMPLETION

                // Locate beginning of current word
                const char* word_end = data->Buf + data->CursorPos;
                const char* word_start = word_end;
                while (word_start > data->Buf)
                {
                    const char c = word_start[-1];
                    if (c == ' ' || c == '\t' || c == ',' || c == ';')
                        break;
                    word_start--;
                }

                // Build a list of candidates
                ImVector<const char*> candidates;
                for (int i = 0; i < Commands.Size; i++)
                    if (Strnicmp(Commands[i], word_start, (int)(word_end - word_start)) == 0)
                        candidates.push_back(Commands[i]);

                if (candidates.Size == 0)
                {
                    // No match
                    AddLog(OBFUSCATE_STR("No match for \"%.*s\"!\n"), (int)(word_end - word_start), word_start);
                }
                else if (candidates.Size == 1)
                {
                    // Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
                    data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
                    data->InsertChars(data->CursorPos, candidates[0]);
                    data->InsertChars(data->CursorPos, " ");
                }
                else
                {
                    // Multiple matches. Complete as much as we can..
                    // So inputing "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
                    int match_len = (int)(word_end - word_start);
                    for (;;)
                    {
                        int c = 0;
                        bool all_candidates_matches = true;
                        for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
                            if (i == 0)
                                c = toupper(candidates[i][match_len]);
                            else if (c == 0 || c != toupper(candidates[i][match_len]))
                                all_candidates_matches = false;
                        if (!all_candidates_matches)
                            break;
                        match_len++;
                    }

                    if (match_len > 0)
                    {
                        data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
                        data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
                    }

                    // List matches
                    AddLog(OBFUSCATE_STR("Possible matches:\n"));
                    for (int i = 0; i < candidates.Size; i++)
                        AddLog(OBFUSCATE_STR("- %s\n"), candidates[i]);
                }

                break;
            }
        case ImGuiInputTextFlags_CallbackHistory:
            {
                // Example of HISTORY
                const int prev_history_pos = HistoryPos;
                if (data->EventKey == ImGuiKey_UpArrow)
                {
                    if (HistoryPos == -1)
                        HistoryPos = History.Size - 1;
                    else if (HistoryPos > 0)
                        HistoryPos--;
                }
                else if (data->EventKey == ImGuiKey_DownArrow)
                {
                    if (HistoryPos != -1)
                        if (++HistoryPos >= History.Size)
                            HistoryPos = -1;
                }

                // A better implementation would preserve the data on the current input line along with cursor position.
                if (prev_history_pos != HistoryPos)
                {
                    const char* history_str = (HistoryPos >= 0) ? History[HistoryPos] : "";
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, history_str);
                }
            }
        }
        return 0;
    }
};

static void ShowExampleAppConsole(bool* p_open)
{
    rtx_spoof_func;
    static ExampleAppConsole console;
    console.Draw(OBFUSCATE_STR("Example: Console"), p_open);
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Debug Log / ShowExampleAppLog()
//-----------------------------------------------------------------------------

// Usage:
//  static ExampleAppLog my_log;
//  my_log.AddLog("Hello %d world\n", 123);
//  my_log.Draw("title");
struct ExampleAppLog
{
    ImGuiTextBuffer     Buf;
    ImGuiTextFilter     Filter;
    ImVector<int>       LineOffsets; // Index to lines offset. We maintain this with AddLog() calls.
    bool                AutoScroll;  // Keep scrolling if already at the bottom.

    ExampleAppLog()
    {
        rtx_spoof_func;
        AutoScroll = true;
        Clear();
    }

    void    Clear()
    {
        rtx_spoof_func;
        Buf.clear();
        LineOffsets.clear();
        LineOffsets.push_back(0);
    }

    void    AddLog(const char* fmt, ...) IM_FMTARGS(2)
    {
        rtx_spoof_func;
        int old_size = Buf.size();
        va_list args;
        va_start(args, fmt);
        Buf.appendfv(fmt, args);
        va_end(args);
        for (int new_size = Buf.size(); old_size < new_size; old_size++)
            if (Buf[old_size] == '\n')
                LineOffsets.push_back(old_size + 1);
    }

    void    Draw(const char* title, bool* p_open = NULL)
    {
        rtx_spoof_func;
        if (!ImGui::Begin(title, p_open))
        {
            ImGui::End();
            return;
        }

        // Options menu
        if (ImGui::BeginPopup(OBFUSCATE_STR("Options")))
        {
            ImGui::Checkbox(OBFUSCATE_STR("Auto-scroll"), &AutoScroll);
            ImGui::EndPopup();
        }

        // Main window
        if (ImGui::Button(OBFUSCATE_STR("Options")))
            ImGui::OpenPopup(OBFUSCATE_STR("Options"));
        ImGui::SameLine();
        bool clear = ImGui::Button(OBFUSCATE_STR("Clear"));
        ImGui::SameLine();
        bool copy = ImGui::Button(OBFUSCATE_STR("Copy"));
        ImGui::SameLine();
        Filter.Draw(OBFUSCATE_STR("Filter"), -100.0f);

        ImGui::Separator();

        if (ImGui::BeginChild(OBFUSCATE_STR("scrolling"), ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
        {
            if (clear)
                Clear();
            if (copy)
                ImGui::LogToClipboard();

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            const char* buf = Buf.begin();
            const char* buf_end = Buf.end();
            if (Filter.IsActive())
            {
                // In this example we don't use the clipper when Filter is enabled.
                // This is because we don't have random access to the result of our filter.
                // A real application processing logs with ten of thousands of entries may want to store the result of
                // search/filter.. especially if the filtering function is not trivial (e.g. reg-exp).
                for (int line_no = 0; line_no < LineOffsets.Size; line_no++)
                {
                    const char* line_start = buf + LineOffsets[line_no];
                    const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                    if (Filter.PassFilter(line_start, line_end))
                        ImGui::TextUnformatted(line_start, line_end);
                }
            }
            else
            {
                // The simplest and easy way to display the entire buffer:
                //   ImGui::TextUnformatted(buf_begin, buf_end);
                // And it'll just work. TextUnformatted() has specialization for large blob of text and will fast-forward
                // to skip non-visible lines. Here we instead demonstrate using the clipper to only process lines that are
                // within the visible area.
                // If you have tens of thousands of items and their processing cost is non-negligible, coarse clipping them
                // on your side is recommended. Using ImGuiListClipper requires
                // - A) random access into your data
                // - B) items all being the  same height,
                // both of which we can handle since we have an array pointing to the beginning of each line of text.
                // When using the filter (in the block of code above) we don't have random access into the data to display
                // anymore, which is why we don't use the clipper. Storing or skimming through the search result would make
                // it possible (and would be recommended if you want to search through tens of thousands of entries).
                ImGuiListClipper clipper;
                clipper.Begin(LineOffsets.Size);
                while (clipper.Step())
                {
                    for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
                    {
                        const char* line_start = buf + LineOffsets[line_no];
                        const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                        ImGui::TextUnformatted(line_start, line_end);
                    }
                }
                clipper.End();
            }
            ImGui::PopStyleVar();

            // Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
            // Using a scrollbar or mouse-wheel will take away from the bottom edge.
            if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }
};

// Demonstrate creating a simple log window with basic filtering.
static void ShowExampleAppLog(bool* p_open)
{
    rtx_spoof_func;
    static ExampleAppLog log;

    // For the demo: add a debug button _BEFORE_ the normal log window contents
    // We take advantage of a rarely used feature: multiple calls to Begin()/End() are appending to the _same_ window.
    // Most of the contents of the window will be added by the log.Draw() call.
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin(OBFUSCATE_STR("Example: Log"), p_open);
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Log"));
    if (ImGui::SmallButton(OBFUSCATE_STR("[Debug] Add 5 entries")))
    {
        static int counter = 0;
        const char* categories[3] = { "info", "warn", "error" };
        const char* words[] = { "Bumfuzzled", "Cattywampus", "Snickersnee", "Abibliophobia", "Absquatulate", "Nincompoop", "Pauciloquent" };
        for (int n = 0; n < 5; n++)
        {
            const char* category = categories[counter % IM_ARRAYSIZE(categories)];
            const char* word = words[counter % IM_ARRAYSIZE(words)];
            log.AddLog(OBFUSCATE_STR("[%05d] [%s] Hello, current time is %.1f, here's a word: '%s'\n"),
                ImGui::GetFrameCount(), category, ImGui::GetTime(), word);
            counter++;
        }
    }
    ImGui::End();

    // Actually call in the regular Log helper (which will Begin() into the same window as we just did)
    log.Draw(OBFUSCATE_STR("Example: Log"), p_open);
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Simple Layout / ShowExampleAppLayout()
//-----------------------------------------------------------------------------

// Demonstrate create a window with multiple child windows.
static void ShowExampleAppLayout(bool* p_open)
{
    rtx_spoof_func;
    ImGui::SetNextWindowSize(ImVec2(500, 440), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(OBFUSCATE_STR("Example: Simple layout"), p_open, ImGuiWindowFlags_MenuBar))
    {
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Simple layout"));
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu(OBFUSCATE_STR("File")))
            {
                if (ImGui::MenuItem(OBFUSCATE_STR("Close"), OBFUSCATE_STR("Ctrl+W"))) { *p_open = false; }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Left
        static int selected = 0;
        {
            ImGui::BeginChild("left pane", ImVec2(150, 0), true);
            for (int i = 0; i < 100; i++)
            {
                // FIXME: Good candidate to use ImGuiSelectableFlags_SelectOnNav
                char label[128];
                sprintf(label, "MyObject %d", i);
                if (ImGui::Selectable(label, selected == i))
                    selected = i;
            }
            ImGui::EndChild();
        }
        ImGui::SameLine();

        // Right
        {
            ImGui::BeginGroup();
            ImGui::BeginChild("item view", ImVec2(0, -ImGui::GetFrameHeightWithSpacing())); // Leave room for 1 line below us
            ImGui::Text("MyObject: %d", selected);
            ImGui::Separator();
            if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None))
            {
                if (ImGui::BeginTabItem("Description"))
                {
                    ImGui::TextWrapped("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Details"))
                {
                    ImGui::Text("ID: 0123456789");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndChild();
            if (ImGui::Button("Revert")) {}
            ImGui::SameLine();
            if (ImGui::Button("Save")) {}
            ImGui::EndGroup();
        }
    }
    ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Property Editor / ShowExampleAppPropertyEditor()
//-----------------------------------------------------------------------------

static void ShowPlaceholderObject(const char* prefix, int uid)
{
    rtx_spoof_func;
    // Use object uid as identifier. Most commonly you could also use the object pointer as a base ID.
    ImGui::PushID(uid);

    // Text and Tree nodes are less high than framed widgets, using AlignTextToFramePadding() we add vertical spacing to make the tree lines equal high.
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    bool node_open = ImGui::TreeNode(OBFUSCATE_STR("Object"), OBFUSCATE_STR("%s_%u"), prefix, uid);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text(OBFUSCATE_STR("my sailor is rich"));

    if (node_open)
    {
        static float placeholder_members[8] = { 0.0f, 0.0f, 1.0f, 3.1416f, 100.0f, 999.0f };
        for (int i = 0; i < 8; i++)
        {
            ImGui::PushID(i); // Use field index as identifier.
            if (i < 2)
            {
                ShowPlaceholderObject(OBFUSCATE_STR("Child"), 424242);
            }
            else
            {
                // Here we use a TreeNode to highlight on hover (we could use e.g. Selectable as well)
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
                ImGui::TreeNodeEx(OBFUSCATE_STR("Field"), flags, OBFUSCATE_STR("Field_%d"), i);

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (i >= 5)
                    ImGui::InputFloat(OBFUSCATE_STR("##value"), &placeholder_members[i], 1.0f);
                else
                    ImGui::DragFloat(OBFUSCATE_STR("##value"), &placeholder_members[i], 0.01f);
                ImGui::NextColumn();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

// Demonstrate create a simple property editor.
static void ShowExampleAppPropertyEditor(bool* p_open)
{
    rtx_spoof_func;
    ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(OBFUSCATE_STR("Example: Property editor"), p_open))
    {
        ImGui::End();
        return;
    }
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Property Editor"));

    HelpMarker(OBFUSCATE_STR(
        "This example shows how you may implement a property editor using two columns.\n"
        "All objects/fields data are dummies here.\n"
        "Remember that in many simple cases, you can use ImGui::SameLine(xxx) to position\n"
        "your cursor horizontally instead of using the Columns() API."));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
    if (ImGui::BeginTable(OBFUSCATE_STR("split"), 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable))
    {
        // Iterate placeholder objects (all the same data)
        for (int obj_i = 0; obj_i < 4; obj_i++)
        {
            ShowPlaceholderObject(OBFUSCATE_STR("Object"), obj_i);
            //ImGui::Separator();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Long Text / ShowExampleAppLongText()
//-----------------------------------------------------------------------------

// Demonstrate/test rendering huge amount of text, and the incidence of clipping.
static void ShowExampleAppLongText(bool* p_open)
{
    rtx_spoof_func;
    ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(OBFUSCATE_STR("Example: Long text display"), p_open))
    {
        ImGui::End();
        return;
    }
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Long text display"));

    static int test_type = 0;
    static ImGuiTextBuffer log;
    static int lines = 0;
    ImGui::Text("Printing unusually long amount of text.");
    ImGui::Combo(OBFUSCATE_STR("Test type"), &test_type,
        OBFUSCATE_STR("Single call to TextUnformatted()\0"
        "Multiple calls to Text(), clipped\0"
        "Multiple calls to Text(), not clipped (slow)\0"));
    ImGui::Text(OBFUSCATE_STR("Buffer contents: %d lines, %d bytes"), lines, log.size());
    if (ImGui::Button(OBFUSCATE_STR("Clear"))) { log.clear(); lines = 0; }
    ImGui::SameLine();
    if (ImGui::Button(OBFUSCATE_STR("Add 1000 lines")))
    {
        for (int i = 0; i < 1000; i++)
            log.appendf(OBFUSCATE_STR("%i The quick brown fox jumps over the lazy dog\n"), lines + i);
        lines += 1000;
    }
    ImGui::BeginChild(OBFUSCATE_STR("Log"));
    switch (test_type)
    {
    case 0:
        // Single call to TextUnformatted() with a big buffer
        ImGui::TextUnformatted(log.begin(), log.end());
        break;
    case 1:
        {
            // Multiple calls to Text(), manually coarsely clipped - demonstrate how to use the ImGuiListClipper helper.
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGuiListClipper clipper;
            clipper.Begin(lines);
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                    ImGui::Text(OBFUSCATE_STR("%i The quick brown fox jumps over the lazy dog"), i);
            ImGui::PopStyleVar();
            break;
        }
    case 2:
        // Multiple calls to Text(), not clipped (slow)
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        for (int i = 0; i < lines; i++)
            ImGui::Text(OBFUSCATE_STR("%i The quick brown fox jumps over the lazy dog"), i);
        ImGui::PopStyleVar();
        break;
    }
    ImGui::EndChild();
    ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Auto Resize / ShowExampleAppAutoResize()
//-----------------------------------------------------------------------------

// Demonstrate creating a window which gets auto-resized according to its content.
static void ShowExampleAppAutoResize(bool* p_open)
{
    rtx_spoof_func;
    if (!ImGui::Begin(OBFUSCATE_STR("Example: Auto-resizing window"), p_open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }
    IMGUI_DEMO_MARKER("Examples/Auto-resizing window");

    static int lines = 10;
    ImGui::TextUnformatted(OBFUSCATE_STR(
        "Window will resize every-frame to the size of its content.\n"
        "Note that you probably don't want to query the window size to\n"
        "output your content because that would create a feedback loop."));
    ImGui::SliderInt(OBFUSCATE_STR("Number of lines"), &lines, 1, 20);
    for (int i = 0; i < lines; i++)
        ImGui::Text(OBFUSCATE_STR("%*sThis is line %d"), i * 4, OBFUSCATE_STR(""), i); // Pad with space to extend size horizontally
    ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Constrained Resize / ShowExampleAppConstrainedResize()
//-----------------------------------------------------------------------------

// Demonstrate creating a window with custom resize constraints.
// Note that size constraints currently don't work on a docked window (when in 'docking' branch)
static void ShowExampleAppConstrainedResize(bool* p_open)
{
    rtx_spoof_func;
    struct CustomConstraints
    {
        // Helper functions to demonstrate programmatic constraints
        // FIXME: This doesn't take account of decoration size (e.g. title bar), library should make this easier.
        static void AspectRatio(ImGuiSizeCallbackData* data)    { float aspect_ratio = *(float*)data->UserData; data->DesiredSize.x = IM_MAX(data->CurrentSize.x, data->CurrentSize.y); data->DesiredSize.y = (float)(int)(data->DesiredSize.x / aspect_ratio); }
        static void Square(ImGuiSizeCallbackData* data)         { data->DesiredSize.x = data->DesiredSize.y = IM_MAX(data->CurrentSize.x, data->CurrentSize.y); }
        static void Step(ImGuiSizeCallbackData* data)           { float step = *(float*)data->UserData; data->DesiredSize = ImVec2((int)(data->CurrentSize.x / step + 0.5f) * step, (int)(data->CurrentSize.y / step + 0.5f) * step); }
    };

    const char* test_desc[] =
    {
        "Between 100x100 and 500x500",
        "At least 100x100",
        "Resize vertical only",
        "Resize horizontal only",
        "Width Between 400 and 500",
        "Custom: Aspect Ratio 16:9",
        "Custom: Always Square",
        "Custom: Fixed Steps (100)",
    };

    // Options
    static bool auto_resize = false;
    static bool window_padding = true;
    static int type = 5; // Aspect Ratio
    static int display_lines = 10;

    // Submit constraint
    float aspect_ratio = 16.0f / 9.0f;
    float fixed_step = 100.0f;
    if (type == 0) ImGui::SetNextWindowSizeConstraints(ImVec2(100, 100), ImVec2(500, 500));         // Between 100x100 and 500x500
    if (type == 1) ImGui::SetNextWindowSizeConstraints(ImVec2(100, 100), ImVec2(FLT_MAX, FLT_MAX)); // Width > 100, Height > 100
    if (type == 2) ImGui::SetNextWindowSizeConstraints(ImVec2(-1, 0),    ImVec2(-1, FLT_MAX));      // Vertical only
    if (type == 3) ImGui::SetNextWindowSizeConstraints(ImVec2(0, -1),    ImVec2(FLT_MAX, -1));      // Horizontal only
    if (type == 4) ImGui::SetNextWindowSizeConstraints(ImVec2(400, -1),  ImVec2(500, -1));          // Width Between and 400 and 500
    if (type == 5) ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),     ImVec2(FLT_MAX, FLT_MAX), CustomConstraints::AspectRatio, (void*)&aspect_ratio);   // Aspect ratio
    if (type == 6) ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),     ImVec2(FLT_MAX, FLT_MAX), CustomConstraints::Square);                              // Always Square
    if (type == 7) ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),     ImVec2(FLT_MAX, FLT_MAX), CustomConstraints::Step, (void*)&fixed_step);            // Fixed Step

    // Submit window
    if (!window_padding)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags window_flags = auto_resize ? ImGuiWindowFlags_AlwaysAutoResize : 0;
    const bool window_open = ImGui::Begin(OBFUSCATE_STR("Example: Constrained Resize"), p_open, window_flags);
    if (!window_padding)
        ImGui::PopStyleVar();
    if (window_open)
    {
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Constrained Resizing window"));
        if (ImGui::GetIO().KeyShift)
        {
            // Display a dummy viewport (in your real app you would likely use ImageButton() to display a texture.
            ImVec2 avail_size = ImGui::GetContentRegionAvail();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::ColorButton(OBFUSCATE_STR("viewport"), ImVec4(0.5f, 0.2f, 0.5f, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, avail_size);
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 10, pos.y + 10));
            ImGui::Text(OBFUSCATE_STR("%.2f x %.2f"), avail_size.x, avail_size.y);
        }
        else
        {
            ImGui::Text(OBFUSCATE_STR("(Hold SHIFT to display a dummy viewport)"));
            if (ImGui::Button(OBFUSCATE_STR("Set 200x200"))) { ImGui::SetWindowSize(ImVec2(200, 200)); } ImGui::SameLine();
            if (ImGui::Button(OBFUSCATE_STR("Set 500x500"))) { ImGui::SetWindowSize(ImVec2(500, 500)); } ImGui::SameLine();
            if (ImGui::Button(OBFUSCATE_STR("Set 800x200"))) { ImGui::SetWindowSize(ImVec2(800, 200)); }
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
            ImGui::Combo(OBFUSCATE_STR("Constraint"), &type, test_desc, IM_ARRAYSIZE(test_desc));
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
            ImGui::DragInt(OBFUSCATE_STR("Lines"), &display_lines, 0.2f, 1, 100);
            ImGui::Checkbox(OBFUSCATE_STR("Auto-resize"), &auto_resize);
            ImGui::Checkbox(OBFUSCATE_STR("Window padding"), &window_padding);
            for (int i = 0; i < display_lines; i++)
                ImGui::Text(OBFUSCATE_STR("%*sHello, sailor! Making this line long enough for the example."), i * 4, "");
        }
    }
    ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Simple overlay / ShowExampleAppSimpleOverlay()
//-----------------------------------------------------------------------------

// Demonstrate creating a simple static window with no decoration
// + a context-menu to choose which corner of the screen to use.
static void ShowExampleAppSimpleOverlay(bool* p_open)
{
    rtx_spoof_func;
    static int location = 0;
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (location >= 0)
    {
        const float PAD = 10.0f;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 work_pos = viewport->WorkPos; // Use work area to avoid menu-bar/task-bar, if any!
        ImVec2 work_size = viewport->WorkSize;
        ImVec2 window_pos, window_pos_pivot;
        window_pos.x = (location & 1) ? (work_pos.x + work_size.x - PAD) : (work_pos.x + PAD);
        window_pos.y = (location & 2) ? (work_pos.y + work_size.y - PAD) : (work_pos.y + PAD);
        window_pos_pivot.x = (location & 1) ? 1.0f : 0.0f;
        window_pos_pivot.y = (location & 2) ? 1.0f : 0.0f;
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        window_flags |= ImGuiWindowFlags_NoMove;
    }
    else if (location == -2)
    {
        // Center window
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        window_flags |= ImGuiWindowFlags_NoMove;
    }
    ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
    if (ImGui::Begin(OBFUSCATE_STR("Example: Simple overlay"), p_open, window_flags))
    {
        IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Simple Overlay"));
        ImGui::Text(OBFUSCATE_STR("Simple overlay\n" "(right-click to change position)"));
        ImGui::Separator();
        if (ImGui::IsMousePosValid())
            ImGui::Text(OBFUSCATE_STR("Mouse Position: (%.1f,%.1f)", io.MousePos.x, io.MousePos.y));
        else
            ImGui::Text(OBFUSCATE_STR("Mouse Position: <invalid>"));
        if (ImGui::BeginPopupContextWindow())
        {
            if (ImGui::MenuItem(OBFUSCATE_STR("Custom"),       NULL, location == -1)) location = -1;
            if (ImGui::MenuItem(OBFUSCATE_STR("Center"),       NULL, location == -2)) location = -2;
            if (ImGui::MenuItem(OBFUSCATE_STR("Top-left"),     NULL, location == 0)) location = 0;
            if (ImGui::MenuItem(OBFUSCATE_STR("Top-right"),    NULL, location == 1)) location = 1;
            if (ImGui::MenuItem(OBFUSCATE_STR("Bottom-left"),  NULL, location == 2)) location = 2;
            if (ImGui::MenuItem(OBFUSCATE_STR("Bottom-right"), NULL, location == 3)) location = 3;
            if (p_open && ImGui::MenuItem(OBFUSCATE_STR("Close"))) *p_open = false;
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Fullscreen window / ShowExampleAppFullscreen()
//-----------------------------------------------------------------------------

// Demonstrate creating a window covering the entire screen/viewport
static void ShowExampleAppFullscreen(bool* p_open)
{
    rtx_spoof_func;
    static bool use_work_area = true;
    static ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    // We demonstrate using the full viewport area or the work area (without menu-bars, task-bars etc.)
    // Based on your use case you may want one of the other.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(use_work_area ? viewport->WorkPos : viewport->Pos);
    ImGui::SetNextWindowSize(use_work_area ? viewport->WorkSize : viewport->Size);

    if (ImGui::Begin(OBFUSCATE_STR("Example: Fullscreen window"), p_open, flags))
    {
        ImGui::Checkbox(OBFUSCATE_STR("Use work area instead of main area"), &use_work_area);
        ImGui::SameLine();
        HelpMarker(OBFUSCATE_STR("Main Area = entire viewport,\nWork Area = entire viewport minus sections used by the main menu bars, task bars etc.\n\nEnable the main-menu bar in Examples menu to see the difference."));

        ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiWindowFlags_NoBackground"), &flags, ImGuiWindowFlags_NoBackground);
        ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiWindowFlags_NoDecoration"), &flags, ImGuiWindowFlags_NoDecoration);
        ImGui::Indent();
        ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiWindowFlags_NoTitleBar"), &flags, ImGuiWindowFlags_NoTitleBar);
        ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiWindowFlags_NoCollapse"), &flags, ImGuiWindowFlags_NoCollapse);
        ImGui::CheckboxFlags(OBFUSCATE_STR("ImGuiWindowFlags_NoScrollbar"), &flags, ImGuiWindowFlags_NoScrollbar);
        ImGui::Unindent();

        if (p_open && ImGui::Button(OBFUSCATE_STR("Close this window")))
            *p_open = false;
    }
    ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Manipulating Window Titles / ShowExampleAppWindowTitles()
//-----------------------------------------------------------------------------

// Demonstrate the use of "##" and "###" in identifiers to manipulate ID generation.
// This applies to all regular items as well.
// Read FAQ section "How can I have multiple widgets with the same label?" for details.
static void ShowExampleAppWindowTitles(bool*)
{
    rtx_spoof_func;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 base_pos = viewport->Pos;

    // By default, Windows are uniquely identified by their title.
    // You can use the "##" and "###" markers to manipulate the display/ID.

    // Using "##" to display same title but have unique identifier.
    ImGui::SetNextWindowPos(ImVec2(base_pos.x + 100, base_pos.y + 100), ImGuiCond_FirstUseEver);
    ImGui::Begin(OBFUSCATE_STR("Same title as another window##1"));
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Manipulating window titles"));
    ImGui::Text(OBFUSCATE_STR("This is window 1.\nMy title is the same as window 2, but my identifier is unique."));
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(base_pos.x + 100, base_pos.y + 200), ImGuiCond_FirstUseEver);
    ImGui::Begin(OBFUSCATE_STR("Same title as another window##2"));
    ImGui::Text(OBFUSCATE_STR("This is window 2.\nMy title is the same as window 1, but my identifier is unique."));
    ImGui::End();
    char buf[128];
    sprintf(buf, "Animated title %c %d###AnimatedTitle", "|/-\\"[(int)(ImGui::GetTime() / 0.25f) & 3], ImGui::GetFrameCount());
    ImGui::SetNextWindowPos(ImVec2(base_pos.x + 100, base_pos.y + 300), ImGuiCond_FirstUseEver);
    ImGui::Begin(buf);
    ImGui::Text(OBFUSCATE_STR("This window has a changing title."));
    ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Custom Rendering using ImDrawList API / ShowExampleAppCustomRendering()
//-----------------------------------------------------------------------------

// Demonstrate using the low-level ImDrawList to draw custom shapes.
static void ShowExampleAppCustomRendering(bool* p_open)
{
    rtx_spoof_func;
    if (!ImGui::Begin(OBFUSCATE_STR("Example: Custom rendering"), p_open))
    {
        ImGui::End();
        return;
    }
    IMGUI_DEMO_MARKER(OBFUSCATE_STR("Examples/Custom Rendering"));

    // Tip: If you do a lot of custom rendering, you probably want to use your own geometrical types and benefit of
    // overloaded operators, etc. Define IM_VEC2_CLASS_EXTRA in imconfig.h to create implicit conversions between your
    // types and ImVec2/ImVec4. Dear ImGui defines overloaded operators but they are internal to imgui.cpp and not
    // exposed outside (to avoid messing with your types) In this example we are not using the maths operators!

    if (ImGui::BeginTabBar(OBFUSCATE_STR("##TabBar")))
    {
        if (ImGui::BeginTabItem(OBFUSCATE_STR("Primitives")))
        {
            ImGui::PushItemWidth(-ImGui::GetFontSize() * 15);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            // Draw gradients
            // (note that those are currently exacerbating our sRGB/Linear issues)
            // Calling ImGui::GetColorU32() multiplies the given colors by the current Style Alpha, but you may pass the IM_COL32() directly as well..
            ImGui::Text(OBFUSCATE_STR("Gradients"));
            ImVec2 gradient_size = ImVec2(ImGui::CalcItemWidth(), ImGui::GetFrameHeight());
            {
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImVec2 p1 = ImVec2(p0.x + gradient_size.x, p0.y + gradient_size.y);
                ImU32 col_a = ImGui::GetColorU32(IM_COL32(0, 0, 0, 255));
                ImU32 col_b = ImGui::GetColorU32(IM_COL32(255, 255, 255, 255));
                draw_list->AddRectFilledMultiColor(p0, p1, col_a, col_b, col_b, col_a);
                ImGui::InvisibleButton(OBFUSCATE_STR("##gradient1"), gradient_size);
            }
            {
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImVec2 p1 = ImVec2(p0.x + gradient_size.x, p0.y + gradient_size.y);
                ImU32 col_a = ImGui::GetColorU32(IM_COL32(0, 255, 0, 255));
                ImU32 col_b = ImGui::GetColorU32(IM_COL32(255, 0, 0, 255));
                draw_list->AddRectFilledMultiColor(p0, p1, col_a, col_b, col_b, col_a);
                ImGui::InvisibleButton(OBFUSCATE_STR("##gradient2"), gradient_size);
            }

            // Draw a bunch of primitives
            ImGui::Text(OBFUSCATE_STR("All primitives"));
            static float sz = 36.0f;
            static float thickness = 3.0f;
            static int ngon_sides = 6;
            static bool circle_segments_override = false;
            static int circle_segments_override_v = 12;
            static bool curve_segments_override = false;
            static int curve_segments_override_v = 8;
            static ImVec4 colf = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
            ImGui::DragFloat(OBFUSCATE_STR("Size"), &sz, 0.2f, 2.0f, 100.0f, OBFUSCATE_STR("%.0f"));
            ImGui::DragFloat(OBFUSCATE_STR("Thickness"), &thickness, 0.05f, 1.0f, 8.0f, OBFUSCATE_STR("%.02f"));
            ImGui::SliderInt(OBFUSCATE_STR("N-gon sides"), &ngon_sides, 3, 12);
            ImGui::Checkbox(OBFUSCATE_STR("##circlesegmentoverride"), &circle_segments_override);
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            circle_segments_override |= ImGui::SliderInt(OBFUSCATE_STR("Circle segments override"), &circle_segments_override_v, 3, 40);
            ImGui::Checkbox(OBFUSCATE_STR("##curvessegmentoverride"), &curve_segments_override);
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            curve_segments_override |= ImGui::SliderInt(OBFUSCATE_STR("Curves segments override"), &curve_segments_override_v, 3, 40);
            ImGui::ColorEdit4(OBFUSCATE_STR("Color"), &colf.x);

            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImU32 col = ImColor(colf);
            const float spacing = 10.0f;
            const ImDrawFlags corners_tl_br = ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomRight;
            const float rounding = sz / 5.0f;
            const int circle_segments = circle_segments_override ? circle_segments_override_v : 0;
            const int curve_segments = curve_segments_override ? curve_segments_override_v : 0;
            float x = p.x + 4.0f;
            float y = p.y + 4.0f;
            for (int n = 0; n < 2; n++)
            {
                // First line uses a thickness of 1.0f, second line uses the configurable thickness
                float th = (n == 0) ? 1.0f : thickness;
                draw_list->AddNgon(ImVec2(x + sz*0.5f, y + sz*0.5f), sz*0.5f, col, ngon_sides, th);                 x += sz + spacing;  // N-gon
                draw_list->AddCircle(ImVec2(x + sz*0.5f, y + sz*0.5f), sz*0.5f, col, circle_segments, th);          x += sz + spacing;  // Circle
                draw_list->AddRect(ImVec2(x, y), ImVec2(x + sz, y + sz), col, 0.0f, ImDrawFlags_None, th);          x += sz + spacing;  // Square
                draw_list->AddRect(ImVec2(x, y), ImVec2(x + sz, y + sz), col, rounding, ImDrawFlags_None, th);      x += sz + spacing;  // Square with all rounded corners
                draw_list->AddRect(ImVec2(x, y), ImVec2(x + sz, y + sz), col, rounding, corners_tl_br, th);         x += sz + spacing;  // Square with two rounded corners
                draw_list->AddTriangle(ImVec2(x+sz*0.5f,y), ImVec2(x+sz, y+sz-0.5f), ImVec2(x, y+sz-0.5f), col, th);x += sz + spacing;  // Triangle
                //draw_list->AddTriangle(ImVec2(x+sz*0.2f,y), ImVec2(x, y+sz-0.5f), ImVec2(x+sz*0.4f, y+sz-0.5f), col, th);x+= sz*0.4f + spacing; // Thin triangle
                draw_list->AddLine(ImVec2(x, y), ImVec2(x + sz, y), col, th);                                       x += sz + spacing;  // Horizontal line (note: drawing a filled rectangle will be faster!)
                draw_list->AddLine(ImVec2(x, y), ImVec2(x, y + sz), col, th);                                       x += spacing;       // Vertical line (note: drawing a filled rectangle will be faster!)
                draw_list->AddLine(ImVec2(x, y), ImVec2(x + sz, y + sz), col, th);                                  x += sz + spacing;  // Diagonal line

                // Quadratic Bezier Curve (3 control points)
                ImVec2 cp3[3] = { ImVec2(x, y + sz * 0.6f), ImVec2(x + sz * 0.5f, y - sz * 0.4f), ImVec2(x + sz, y + sz) };
                draw_list->AddBezierQuadratic(cp3[0], cp3[1], cp3[2], col, th, curve_segments); x += sz + spacing;

                // Cubic Bezier Curve (4 control points)
                ImVec2 cp4[4] = { ImVec2(x, y), ImVec2(x + sz * 1.3f, y + sz * 0.3f), ImVec2(x + sz - sz * 1.3f, y + sz - sz * 0.3f), ImVec2(x + sz, y + sz) };
                draw_list->AddBezierCubic(cp4[0], cp4[1], cp4[2], cp4[3], col, th, curve_segments);

                x = p.x + 4;
                y += sz + spacing;
            }
            draw_list->AddNgonFilled(ImVec2(x + sz * 0.5f, y + sz * 0.5f), sz*0.5f, col, ngon_sides);               x += sz + spacing;  // N-gon
            draw_list->AddCircleFilled(ImVec2(x + sz*0.5f, y + sz*0.5f), sz*0.5f, col, circle_segments);            x += sz + spacing;  // Circle
            draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), col);                                    x += sz + spacing;  // Square
            draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), col, 10.0f);                             x += sz + spacing;  // Square with all rounded corners
            draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), col, 10.0f, corners_tl_br);              x += sz + spacing;  // Square with two rounded corners
            draw_list->AddTriangleFilled(ImVec2(x+sz*0.5f,y), ImVec2(x+sz, y+sz-0.5f), ImVec2(x, y+sz-0.5f), col);  x += sz + spacing;  // Triangle
            //draw_list->AddTriangleFilled(ImVec2(x+sz*0.2f,y), ImVec2(x, y+sz-0.5f), ImVec2(x+sz*0.4f, y+sz-0.5f), col); x += sz*0.4f + spacing; // Thin triangle
            draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + thickness), col);                             x += sz + spacing;  // Horizontal line (faster than AddLine, but only handle integer thickness)
            draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + thickness, y + sz), col);                             x += spacing * 2.0f;// Vertical line (faster than AddLine, but only handle integer thickness)
            draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + 1, y + 1), col);                                      x += sz;            // Pixel (faster than AddLine)
            draw_list->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + sz, y + sz), IM_COL32(0, 0, 0, 255), IM_COL32(255, 0, 0, 255), IM_COL32(255, 255, 0, 255), IM_COL32(0, 255, 0, 255));

            ImGui::Dummy(ImVec2((sz + spacing) * 10.2f, (sz + spacing) * 3.0f));
            ImGui::PopItemWidth();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(OBFUSCATE_STR("Canvas")))
        {
            static ImVector<ImVec2> points;
            static ImVec2 scrolling(0.0f, 0.0f);
            static bool opt_enable_grid = true;
            static bool opt_enable_context_menu = true;
            static bool adding_line = false;

            ImGui::Checkbox(OBFUSCATE_STR("Enable grid"), &opt_enable_grid);
            ImGui::Checkbox(OBFUSCATE_STR("Enable context menu"), &opt_enable_context_menu);
            ImGui::Text(OBFUSCATE_STR("Mouse Left: drag to add lines,\nMouse Right: drag to scroll, click for context menu."));

            // Typically you would use a BeginChild()/EndChild() pair to benefit from a clipping region + own scrolling.
            // Here we demonstrate that this can be replaced by simple offsetting + custom drawing + PushClipRect/PopClipRect() calls.
            // To use a child window instead we could use, e.g:
            //      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));      // Disable padding
            //      ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(50, 50, 50, 255));  // Set a background color
            //      ImGui::BeginChild("canvas", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoMove);
            //      ImGui::PopStyleColor();
            //      ImGui::PopStyleVar();
            //      [...]
            //      ImGui::EndChild();

            // Using InvisibleButton() as a convenience 1) it will advance the layout cursor and 2) allows us to use IsItemHovered()/IsItemActive()
            ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();      // ImDrawList API uses screen coordinates!
            ImVec2 canvas_sz = ImGui::GetContentRegionAvail();   // Resize canvas to what's available
            if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
            if (canvas_sz.y < 50.0f) canvas_sz.y = 50.0f;
            ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

            // Draw border and background color
            ImGuiIO& io = ImGui::GetIO();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(50, 50, 50, 255));
            draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(255, 255, 255, 255));

            // This will catch our interactions
            ImGui::InvisibleButton(OBFUSCATE_STR("canvas"), canvas_sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const bool is_hovered = ImGui::IsItemHovered(); // Hovered
            const bool is_active = ImGui::IsItemActive();   // Held
            const ImVec2 origin(canvas_p0.x + scrolling.x, canvas_p0.y + scrolling.y); // Lock scrolled origin
            const ImVec2 mouse_pos_in_canvas(io.MousePos.x - origin.x, io.MousePos.y - origin.y);

            // Add first and second point
            if (is_hovered && !adding_line && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                points.push_back(mouse_pos_in_canvas);
                points.push_back(mouse_pos_in_canvas);
                adding_line = true;
            }
            if (adding_line)
            {
                points.back() = mouse_pos_in_canvas;
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    adding_line = false;
            }

            // Pan (we use a zero mouse threshold when there's no context menu)
            // You may decide to make that threshold dynamic based on whether the mouse is hovering something etc.
            const float mouse_threshold_for_pan = opt_enable_context_menu ? -1.0f : 0.0f;
            if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Right, mouse_threshold_for_pan))
            {
                scrolling.x += io.MouseDelta.x;
                scrolling.y += io.MouseDelta.y;
            }

            // Context menu (under default mouse threshold)
            ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
            if (opt_enable_context_menu && drag_delta.x == 0.0f && drag_delta.y == 0.0f)
                ImGui::OpenPopupOnItemClick(OBFUSCATE_STR("context"), ImGuiPopupFlags_MouseButtonRight);
            if (ImGui::BeginPopup(OBFUSCATE_STR("context")))
            {
                if (adding_line)
                    points.resize(points.size() - 2);
                adding_line = false;
                if (ImGui::MenuItem(OBFUSCATE_STR("Remove one"), NULL, false, points.Size > 0)) { points.resize(points.size() - 2); }
                if (ImGui::MenuItem(OBFUSCATE_STR("Remove all"), NULL, false, points.Size > 0)) { points.clear(); }
                ImGui::EndPopup();
            }

            // Draw grid + all lines in the canvas
            draw_list->PushClipRect(canvas_p0, canvas_p1, true);
            if (opt_enable_grid)
            {
                const float GRID_STEP = 64.0f;
                for (float x = fmodf(scrolling.x, GRID_STEP); x < canvas_sz.x; x += GRID_STEP)
                    draw_list->AddLine(ImVec2(canvas_p0.x + x, canvas_p0.y), ImVec2(canvas_p0.x + x, canvas_p1.y), IM_COL32(200, 200, 200, 40));
                for (float y = fmodf(scrolling.y, GRID_STEP); y < canvas_sz.y; y += GRID_STEP)
                    draw_list->AddLine(ImVec2(canvas_p0.x, canvas_p0.y + y), ImVec2(canvas_p1.x, canvas_p0.y + y), IM_COL32(200, 200, 200, 40));
            }
            for (int n = 0; n < points.Size; n += 2)
                draw_list->AddLine(ImVec2(origin.x + points[n].x, origin.y + points[n].y), ImVec2(origin.x + points[n + 1].x, origin.y + points[n + 1].y), IM_COL32(255, 255, 0, 255), 2.0f);
            draw_list->PopClipRect();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(OBFUSCATE_STR("Shadows")))
        {
            static float shadow_thickness = 40.0f;
            static ImVec4 shadow_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            static bool shadow_filled = false;
            static ImVec4 shape_color = ImVec4(0.9f, 0.6f, 0.3f, 1.0f);
            static float shape_rounding = 0.0f;
            static ImVec2 shadow_offset(0.0f, 0.0f);
            static ImVec4 background_color = ImVec4(0.5f, 0.5f, 0.7f, 1.0f);
            static bool wireframe = false;
            static bool aa = true;
            static int poly_shape_index = 0;
            ImGui::Checkbox(OBFUSCATE_STR("Shadow filled"), &shadow_filled);
            ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR("This will fill the section behind the shape to shadow. It's often unnecessary and wasteful but provided for consistency."));
            ImGui::Checkbox(OBFUSCATE_STR("Wireframe shapes"), &wireframe);
            ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR("This draws the shapes in wireframe so you can see the shadow underneath."));
            ImGui::Checkbox(OBFUSCATE_STR("Anti-aliasing"), &aa);

            ImGui::DragFloat(OBFUSCATE_STR("Shadow Thickness"), &shadow_thickness, 1.0f, 0.0f, 100.0f, OBFUSCATE_STR("%.02f"));
            ImGui::SliderFloat2(OBFUSCATE_STR("Offset"), (float*)&shadow_offset, -32.0f, 32.0f);
            ImGui::SameLine();
            HelpMarker(OBFUSCATE_STR("Note that currently circles/convex shapes do not support non-zero offsets for unfilled shadows."));
            ImGui::ColorEdit4(OBFUSCATE_STR("Background Color"), &background_color.x);
            ImGui::ColorEdit4(OBFUSCATE_STR("Shadow Color"), &shadow_color.x);
            ImGui::ColorEdit4(OBFUSCATE_STR("Shape Color"), &shape_color.x);
            ImGui::DragFloat(OBFUSCATE_STR("Shape Rounding"), &shape_rounding, 1.0f, 0.0f, 20.0f, OBFUSCATE_STR("%.02f"));
            ImGui::Combo(OBFUSCATE_STR("Convex shape"), &poly_shape_index, OBFUSCATE_STR("Shape 1\0Shape 2\0Shape 3\0Shape 4\0Shape 4 (winding reversed)"));

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImDrawListFlags old_flags = draw_list->Flags;

            if (aa)
                draw_list->Flags |= ~ImDrawListFlags_AntiAliasedFill;
            else
                draw_list->Flags &= ~ImDrawListFlags_AntiAliasedFill;

            // Fill a strip of background
            draw_list->AddRectFilled(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y), ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetWindowContentRegionMax().x, ImGui::GetCursorScreenPos().y + 200.0f), ImGui::GetColorU32(background_color));

            // Rectangle
            {
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(200.0f, 200.0f));

                ImVec2 r1(p.x + 50.0f, p.y + 50.0f);
                ImVec2 r2(p.x + 150.0f, p.y + 150.0f);
                ImDrawFlags draw_flags = shadow_filled ? ImDrawFlags_None : ImDrawFlags_ShadowCutOutShapeBackground;
                draw_list->AddShadowRect(r1, r2, ImGui::GetColorU32(shadow_color), shadow_thickness, shadow_offset, draw_flags, shape_rounding);

                if (wireframe)
                    draw_list->AddRect(r1, r2, ImGui::GetColorU32(shape_color), shape_rounding);
                else
                    draw_list->AddRectFilled(r1, r2, ImGui::GetColorU32(shape_color), shape_rounding);
            }

            ImGui::SameLine();

            // Circle
            {
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(200.0f, 200.0f));

                // FIXME-SHADOWS: Offset forced to zero when shadow is not filled because it isn't supported
                float off = 10.0f;
                ImVec2 r1(p.x + 50.0f + off, p.y + 50.0f + off);
                ImVec2 r2(p.x + 150.0f - off, p.y + 150.0f - off);
                ImVec2 center(p.x + 100.0f, p.y + 100.0f);
                ImDrawFlags draw_flags = shadow_filled ? ImDrawFlags_None : ImDrawFlags_ShadowCutOutShapeBackground;
                draw_list->AddShadowCircle(center, 50.0f, ImGui::GetColorU32(shadow_color), shadow_thickness, shadow_filled ? shadow_offset : ImVec2(0.0f, 0.0f), draw_flags, 0);

                if (wireframe)
                    draw_list->AddCircle(center, 50.0f, ImGui::GetColorU32(shape_color), 0);
                else
                    draw_list->AddCircleFilled(center, 50.0f, ImGui::GetColorU32(shape_color), 0);
            }

            ImGui::SameLine();

            // Convex shape
            {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(200.0f, 200.0f));

                const ImVec2 poly_centre(pos.x + 50.0f, pos.y + 100.0f);
                ImVec2 poly_points[8];
                int poly_points_count = 0;

                switch (poly_shape_index)
                {
                default:
                case 0:
                {
                    poly_points[0] = ImVec2(poly_centre.x - 32.0f, poly_centre.y);
                    poly_points[1] = ImVec2(poly_centre.x - 24.0f, poly_centre.y + 24.0f);
                    poly_points[2] = ImVec2(poly_centre.x, poly_centre.y + 32.0f);
                    poly_points[3] = ImVec2(poly_centre.x + 24.0f, poly_centre.y + 24.0f);
                    poly_points[4] = ImVec2(poly_centre.x + 32.0f, poly_centre.y);
                    poly_points[5] = ImVec2(poly_centre.x + 24.0f, poly_centre.y - 24.0f);
                    poly_points[6] = ImVec2(poly_centre.x, poly_centre.y - 32.0f);
                    poly_points[7] = ImVec2(poly_centre.x - 32.0f, poly_centre.y - 32.0f);
                    poly_points_count = 8;
                    break;
                }
                case 1:
                {
                    poly_points[0] = ImVec2(poly_centre.x + 40.0f, poly_centre.y - 20.0f);
                    poly_points[1] = ImVec2(poly_centre.x, poly_centre.y + 32.0f);
                    poly_points[2] = ImVec2(poly_centre.x - 24.0f, poly_centre.y - 32.0f);
                    poly_points_count = 3;
                    break;
                }
                case 2:
                {
                    poly_points[0] = ImVec2(poly_centre.x - 32.0f, poly_centre.y);
                    poly_points[1] = ImVec2(poly_centre.x, poly_centre.y + 32.0f);
                    poly_points[2] = ImVec2(poly_centre.x + 32.0f, poly_centre.y);
                    poly_points[3] = ImVec2(poly_centre.x, poly_centre.y - 32.0f);
                    poly_points_count = 4;
                    break;
                }
                case 3:
                {
                    poly_points[0] = ImVec2(poly_centre.x - 4.0f, poly_centre.y - 20.0f);
                    poly_points[1] = ImVec2(poly_centre.x + 12.0f, poly_centre.y + 2.0f);
                    poly_points[2] = ImVec2(poly_centre.x + 8.0f, poly_centre.y + 16.0f);
                    poly_points[3] = ImVec2(poly_centre.x, poly_centre.y + 32.0f);
                    poly_points[4] = ImVec2(poly_centre.x - 16.0f, poly_centre.y - 32.0f);
                    poly_points_count = 5;
                    break;
                }
                case 4: // Same as test case 3 but with reversed winding
                {
                    poly_points[0] = ImVec2(poly_centre.x - 16.0f, poly_centre.y - 32.0f);
                    poly_points[1] = ImVec2(poly_centre.x, poly_centre.y + 32.0f);
                    poly_points[2] = ImVec2(poly_centre.x + 8.0f, poly_centre.y + 16.0f);
                    poly_points[3] = ImVec2(poly_centre.x + 12.0f, poly_centre.y + 2.0f);
                    poly_points[4] = ImVec2(poly_centre.x - 4.0f, poly_centre.y - 20.0f);
                    poly_points_count = 5;
                    break;
                }
                }

                // FIXME-SHADOWS: Offset forced to zero when shadow is not filled because it isn't supported
                ImDrawFlags draw_flags = shadow_filled ? ImDrawFlags_None : ImDrawFlags_ShadowCutOutShapeBackground;
                draw_list->AddShadowConvexPoly(poly_points, poly_points_count, ImGui::GetColorU32(shadow_color), shadow_thickness, shadow_filled ? shadow_offset : ImVec2(0.0f, 0.0f), draw_flags);

                if (wireframe)
                    draw_list->AddPolyline(poly_points, poly_points_count, ImGui::GetColorU32(shape_color), true, 1.0f);
                else
                    draw_list->AddConvexPolyFilled(poly_points, poly_points_count, ImGui::GetColorU32(shape_color));
            }

            draw_list->Flags = old_flags;

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(OBFUSCATE_STR("BG/FG draw lists")))
        {
            static bool draw_bg = true;
            static bool draw_fg = true;
            ImGui::Checkbox(OBFUSCATE_STR("Draw in Background draw list"), &draw_bg);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("The Background draw list will be rendered below every Dear ImGui windows."));
            ImGui::Checkbox(OBFUSCATE_STR("Draw in Foreground draw list"), &draw_fg);
            ImGui::SameLine(); HelpMarker(OBFUSCATE_STR("The Foreground draw list will be rendered over every Dear ImGui windows."));
            ImVec2 window_pos = ImGui::GetWindowPos();
            ImVec2 window_size = ImGui::GetWindowSize();
            ImVec2 window_center = ImVec2(window_pos.x + window_size.x * 0.5f, window_pos.y + window_size.y * 0.5f);
            if (draw_bg)
                ImGui::GetBackgroundDrawList()->AddCircle(window_center, window_size.x * 0.6f, IM_COL32(255, 0, 0, 200), 0, 10 + 4);
            if (draw_fg)
                ImGui::GetForegroundDrawList()->AddCircle(window_center, window_size.y * 0.6f, IM_COL32(0, 255, 0, 200), 0, 10);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

struct MyDocument
{
    const char* Name;       // Document title
    bool        Open;       // Set when open (we keep an array of all available documents to simplify demo code!)
    bool        OpenPrev;   // Copy of Open from last update.
    bool        Dirty;      // Set when the document has been modified
    bool        WantClose;  // Set when the document
    ImVec4      Color;      // An arbitrary variable associated to the document

    MyDocument(const char* name, bool open = true, const ImVec4& color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f))
    {
        Name = name;
        Open = OpenPrev = open;
        Dirty = false;
        WantClose = false;
        Color = color;
    }
    void DoOpen()       { Open = true; }
    void DoQueueClose() { WantClose = true; }
    void DoForceClose() { Open = false; Dirty = false; }
    void DoSave()       { Dirty = false; }

    // Display placeholder contents for the Document
    static void DisplayContents(MyDocument* doc)
    {
        rtx_spoof_func;
        ImGui::PushID(doc);
        ImGui::Text(OBFUSCATE_STR("Document \"%s\""), doc->Name);
        ImGui::PushStyleColor(ImGuiCol_Text, doc->Color);
        ImGui::TextWrapped(OBFUSCATE_STR("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua."));
        ImGui::PopStyleColor();
        if (ImGui::Button(OBFUSCATE_STR("Modify"), ImVec2(100, 0)))
            doc->Dirty = true;
        ImGui::SameLine();
        if (ImGui::Button(OBFUSCATE_STR("Save"), ImVec2(100, 0)))
            doc->DoSave();
        ImGui::ColorEdit3(OBFUSCATE_STR("color"), &doc->Color.x);  // Useful to test drag and drop and hold-dragged-to-open-tab behavior.
        ImGui::PopID();
    }

    // Display context menu for the Document
    static void DisplayContextMenu(MyDocument* doc)
    {
        rtx_spoof_func;
        if (!ImGui::BeginPopupContextItem())
            return;

        char buf[256];
        sprintf(buf, OBFUSCATE_STR("Save %s"), doc->Name);
        if (ImGui::MenuItem(buf, OBFUSCATE_STR("CTRL+S"), false, doc->Open))
            doc->DoSave();
        if (ImGui::MenuItem(OBFUSCATE_STR("Close"), OBFUSCATE_STR("CTRL+W"), false, doc->Open))
            doc->DoQueueClose();
        ImGui::EndPopup();
    }
};

struct ExampleAppDocuments
{
    ImVector<MyDocument> Documents;

    ExampleAppDocuments()
    {
        rtx_spoof_func;
        Documents.push_back(MyDocument(OBFUSCATE_STR("Lettuce"),             true,  ImVec4(0.4f, 0.8f, 0.4f, 1.0f)));
        Documents.push_back(MyDocument(OBFUSCATE_STR("Eggplant"),            true,  ImVec4(0.8f, 0.5f, 1.0f, 1.0f)));
        Documents.push_back(MyDocument(OBFUSCATE_STR("Carrot"),              true,  ImVec4(1.0f, 0.8f, 0.5f, 1.0f)));
        Documents.push_back(MyDocument(OBFUSCATE_STR("Tomato"),              false, ImVec4(1.0f, 0.3f, 0.4f, 1.0f)));
        Documents.push_back(MyDocument(OBFUSCATE_STR("A Rather Long Title"), false));
        Documents.push_back(MyDocument(OBFUSCATE_STR("Some Document"),       false));
    }
};

static void NotifyOfDocumentsClosedElsewhere(ExampleAppDocuments& app)
{
    rtx_spoof_func;
    for (int doc_n = 0; doc_n < app.Documents.Size; doc_n++)
    {
        MyDocument* doc = &app.Documents[doc_n];
        if (!doc->Open && doc->OpenPrev)
            ImGui::SetTabItemClosed(doc->Name);
        doc->OpenPrev = doc->Open;
    }
}

void ShowExampleAppDocuments(bool* p_open)
{
    rtx_spoof_func;
    static ExampleAppDocuments app;

    // Options
    static bool opt_reorderable = true;
    static ImGuiTabBarFlags opt_fitting_flags = ImGuiTabBarFlags_FittingPolicyDefault_;

    bool window_contents_visible = ImGui::Begin(OBFUSCATE_STR("Example: Documents"), p_open, ImGuiWindowFlags_MenuBar);
    if (!window_contents_visible)
    {
        ImGui::End();
        return;
    }

    // Menu
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu(OBFUSCATE_STR("File")))
        {
            int open_count = 0;
            for (int doc_n = 0; doc_n < app.Documents.Size; doc_n++)
                open_count += app.Documents[doc_n].Open ? 1 : 0;

            if (ImGui::BeginMenu(OBFUSCATE_STR("Open"), open_count < app.Documents.Size))
            {
                for (int doc_n = 0; doc_n < app.Documents.Size; doc_n++)
                {
                    MyDocument* doc = &app.Documents[doc_n];
                    if (!doc->Open)
                        if (ImGui::MenuItem(doc->Name))
                            doc->DoOpen();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(OBFUSCATE_STR("Close All Documents"), NULL, false, open_count > 0))
                for (int doc_n = 0; doc_n < app.Documents.Size; doc_n++)
                    app.Documents[doc_n].DoQueueClose();
            if (ImGui::MenuItem(OBFUSCATE_STR("Exit"), OBFUSCATE_STR("Ctrl+F4")) && p_open)
                *p_open = false;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // [Debug] List documents with one checkbox for each
    for (int doc_n = 0; doc_n < app.Documents.Size; doc_n++)
    {
        MyDocument* doc = &app.Documents[doc_n];
        if (doc_n > 0)
            ImGui::SameLine();
        ImGui::PushID(doc);
        if (ImGui::Checkbox(doc->Name, &doc->Open))
            if (!doc->Open)
                doc->DoForceClose();
        ImGui::PopID();
    }

    ImGui::Separator();

    // About the ImGuiWindowFlags_UnsavedDocument / ImGuiTabItemFlags_UnsavedDocument flags.
    // They have multiple effects:
    // - Display a dot next to the title.
    // - Tab is selected when clicking the X close button.
    // - Closure is not assumed (will wait for user to stop submitting the tab).
    //   Otherwise closure is assumed when pressing the X, so if you keep submitting the tab may reappear at end of tab bar.
    //   We need to assume closure by default otherwise waiting for "lack of submission" on the next frame would leave an empty
    //   hole for one-frame, both in the tab-bar and in tab-contents when closing a tab/window.
    //   The rarely used SetTabItemClosed() function is a way to notify of programmatic closure to avoid the one-frame hole.

    // Submit Tab Bar and Tabs
    {
        ImGuiTabBarFlags tab_bar_flags = (opt_fitting_flags) | (opt_reorderable ? ImGuiTabBarFlags_Reorderable : 0);
        if (ImGui::BeginTabBar(OBFUSCATE_STR("##tabs"), tab_bar_flags))
        {
            if (opt_reorderable)
                NotifyOfDocumentsClosedElsewhere(app);

            // [DEBUG] Stress tests
            //if ((ImGui::GetFrameCount() % 30) == 0) docs[1].Open ^= 1;            // [DEBUG] Automatically show/hide a tab. Test various interactions e.g. dragging with this on.
            //if (ImGui::GetIO().KeyCtrl) ImGui::SetTabItemSelected(docs[1].Name);  // [DEBUG] Test SetTabItemSelected(), probably not very useful as-is anyway..

            // Submit Tabs
            for (int doc_n = 0; doc_n < app.Documents.Size; doc_n++)
            {
                MyDocument* doc = &app.Documents[doc_n];
                if (!doc->Open)
                    continue;

                ImGuiTabItemFlags tab_flags = (doc->Dirty ? ImGuiTabItemFlags_UnsavedDocument : 0);
                bool visible = ImGui::BeginTabItem(doc->Name, &doc->Open, tab_flags);

                // Cancel attempt to close when unsaved add to save queue so we can display a popup.
                if (!doc->Open && doc->Dirty)
                {
                    doc->Open = true;
                    doc->DoQueueClose();
                }

                MyDocument::DisplayContextMenu(doc);
                if (visible)
                {
                    MyDocument::DisplayContents(doc);
                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }
    }

    // Update closing queue
    static ImVector<MyDocument*> close_queue;
    if (close_queue.empty())
    {
        // Close queue is locked once we started a popup
        for (int doc_n = 0; doc_n < app.Documents.Size; doc_n++)
        {
            MyDocument* doc = &app.Documents[doc_n];
            if (doc->WantClose)
            {
                doc->WantClose = false;
                close_queue.push_back(doc);
            }
        }
    }

    // Display closing confirmation UI
    if (!close_queue.empty())
    {
        int close_queue_unsaved_documents = 0;
        for (int n = 0; n < close_queue.Size; n++)
            if (close_queue[n]->Dirty)
                close_queue_unsaved_documents++;

        if (close_queue_unsaved_documents == 0)
        {
            // Close documents when all are unsaved
            for (int n = 0; n < close_queue.Size; n++)
                close_queue[n]->DoForceClose();
            close_queue.clear();
        }
        else
        {
            if (!ImGui::IsPopupOpen(OBFUSCATE_STR("Save?")))
                ImGui::OpenPopup(OBFUSCATE_STR("Save?"));
            if (ImGui::BeginPopupModal(OBFUSCATE_STR("Save?"), NULL, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text(OBFUSCATE_STR("Save change to the following items?"));
                float item_height = ImGui::GetTextLineHeightWithSpacing();
                if (ImGui::BeginChildFrame(ImGui::GetID(OBFUSCATE_STR("frame")), ImVec2(-FLT_MIN, 6.25f * item_height)))
                {
                    for (int n = 0; n < close_queue.Size; n++)
                        if (close_queue[n]->Dirty)
                            ImGui::Text(OBFUSCATE_STR("%s"), close_queue[n]->Name);
                    ImGui::EndChildFrame();
                }

                ImVec2 button_size(ImGui::GetFontSize() * 7.0f, 0.0f);
                if (ImGui::Button(OBFUSCATE_STR("Yes"), button_size))
                {
                    for (int n = 0; n < close_queue.Size; n++)
                    {
                        if (close_queue[n]->Dirty)
                            close_queue[n]->DoSave();
                        close_queue[n]->DoForceClose();
                    }
                    close_queue.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(OBFUSCATE_STR("No"), button_size))
                {
                    for (int n = 0; n < close_queue.Size; n++)
                        close_queue[n]->DoForceClose();
                    close_queue.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(OBFUSCATE_STR("Cancel"), button_size))
                {
                    close_queue.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }

    ImGui::End();
}

// End of Demo code
#else

void ImGui::ShowAboutWindow(bool*) {}
void ImGui::ShowDemoWindow(bool*) {}
void ImGui::ShowUserGuide() {}
void ImGui::ShowStyleEditor(ImGuiStyle*) {}

#endif

#endif // #ifndef IMGUI_DISABLE
