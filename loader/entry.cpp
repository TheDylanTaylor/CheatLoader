#include "entry.h"
#include "uxtheme.h"
#include "dwmapi.h"
#include <filesystem>
static int dnd_counter = 0;
static bool remember_me = false;
char login[64];
char password[64];
static int iProduct = 0;
static float inject_prgoress = 0;
ImVec2 menu_size = ImVec2(700, 450);
HWND hwnd;
RECT rc;
inline WNDCLASSEXW wc;
auto main() -> std::uint32_t
{
    rtx_spoof_func;
    std::filesystem::create_directories(OBFUSCATE_STR("C:\\codeine\\configs"));
    std::filesystem::create_directories(OBFUSCATE_STR("C:\\codeine\\img"));
    std::filesystem::create_directories(OBFUSCATE_STR("C:\\codeine\\log"));
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = NULL;
    wc.cbWndExtra = NULL;
    wc.hInstance = nullptr;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszMenuName = reinterpret_cast<LPCWSTR>(OBFUSCATE_STR("rawr"));
    wc.lpszClassName = reinterpret_cast<LPCWSTR>(OBFUSCATE_STR("Class01"));
    wc.hIconSm = LoadIcon(0, IDI_APPLICATION);

    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(NULL, wc.lpszClassName, wc.lpszMenuName, WS_POPUP, (GetSystemMetrics(SM_CXSCREEN) / 2) - (menu_size.x / 2), (GetSystemMetrics(SM_CYSCREEN) / 2) - (menu_size.y / 2), menu_size.x, menu_size.y, 0, 0, 0, 0);

    SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);

    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    POINT mouse;
    rc = { 0 };
    GetWindowRect(hwnd, &rc);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    io.Fonts->AddFontFromMemoryTTF(&RobotoRegular, sizeof RobotoRegular, 18, NULL, io.Fonts->GetGlyphRangesCyrillic());

    big_font = io.Fonts->AddFontFromMemoryTTF(&NotoSansMiaoRegular, sizeof NotoSansMiaoRegular, 38, NULL, io.Fonts->GetGlyphRangesCyrillic());

    medium_font = io.Fonts->AddFontFromMemoryTTF(&NotoSansMiaoRegular, sizeof NotoSansMiaoRegular, 30, NULL, io.Fonts->GetGlyphRangesCyrillic());

    small_font = io.Fonts->AddFontFromMemoryTTF(&NotoSansMiaoRegular, sizeof NotoSansMiaoRegular, 20, NULL, io.Fonts->GetGlyphRangesCyrillic());

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ImVec4 clear_color = ImVec4(0.1f, 0.1f, 0.1f, 1.00f);

    ImGuiStyle& s = ImGui::GetStyle();

    s.FramePadding = ImVec2(5, 3);
    s.WindowPadding = ImVec2(0, 0);
    s.FrameRounding = 5.f;
    s.WindowRounding = 4.f;
    s.WindowBorderSize = 0.f;
    s.PopupBorderSize = 0.f;
    s.WindowPadding = ImVec2(0, 0);
    s.ChildBorderSize = 10;
    s.ItemSpacing = ImVec2(0, 5);

    static float anim_speed = ImGui::GetIO().DeltaTime * 6.f;

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        static DWORD dwTickStart = GetTickCount();
        if (GetTickCount() - dwTickStart > 3500)
        {
            if (current_news < 2)
                current_news++;
            else
                current_news = 0;
            dwTickStart = GetTickCount();
        }
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int windowWidth = menu_size.x;
        int windowHeight = menu_size.y;
        int posX = (screenWidth - windowWidth) / 2;
        int posY = (screenHeight - windowHeight) / 2;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        {
            LoadImages();
            ImGui::SetNextWindowPos(ImVec2((windowWidth - menu_size.x) * 0.5f, (windowHeight - menu_size.y) * 0.5f));
            ImGui::SetNextWindowSize(ImVec2(menu_size.x, menu_size.y));
            ImGui::Begin(OBFUSCATE_STR("General"), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
            {
                auto draw = ImGui::GetWindowDrawList();
                const auto& p = ImGui::GetWindowPos();

                menu.if_auth_offset = ImLerp(menu.if_auth_offset, menu_state != 0 ? 400.f : 0.f, anim_speed / 6);

                menu.main_loading_offset = ImLerp(menu.main_loading_offset, menu.if_auth_offset > 390 ? 0.f : 600.f, anim_speed / 6);

                Trinage_background();

                if (menu_state == 0 || menu.if_auth_offset < 390)
                {

                    ImGui::PushClipRect(p + ImVec2(11, 11), p + ImVec2(689, 70), true);
                    draw->AddRectFilled(p - ImVec2(0, 150), p + ImVec2(690, 70), ImColor(0.f, 0.f, 0.f, 0.4f), 4.f);
                    draw->AddShadowRect(p + ImVec2(10, 120), p + ImVec2(690, 70), ImColor(0.f, 0.f, 0.f, 1.f), 495.f, ImVec2(0, 0));
                    ImGui::PushFont(medium_font);

                    draw->AddText(p + ImVec2(320, 40) - ImGui::CalcTextSize(OBFUSCATE_STR("CODEINE.WTF")) / 2, ImColor(1.f, 1.f, 1.f, 1.f), OBFUSCATE_STR("CODEINE.WTF"));

                    ImGui::PopFont();
                    ImGui::PopClipRect();

                    draw->AddRect(p + ImVec2(10, 10), p + ImVec2(690, 70), second_color, 4.f, 0, 2.f);

                    ImGui::SetCursorPos(ImVec2(180 - menu.if_auth_offset, 130));
                    ImGui::BeginGroup();
                    ImGui::InputTextEx(OBFUSCATE_STR("Username"), NULL, login, 64, ImVec2(300, 35), 0);
                    ImGui::InputTextEx(OBFUSCATE_STR("Password"), NULL, password, 64, ImVec2(300, 35), ImGuiInputTextFlags_Password);


                    ImGui::Checkbox(OBFUSCATE_STR("Remember Me"), &remember_me);
                    if (ImGui::Button(OBFUSCATE_STR("LOGIN"), ImVec2(300, 35)))
                        menu_state = 1;
                    ImGui::EndGroup();
                }

                if (menu_state == 1)
                {
                    LoadingScreen(p - ImVec2(0, menu.main_loading_offset));
                }

                if (menu_state == 2)
                {
                    ImGui::PushClipRect(p + ImVec2(11, 11), p + ImVec2(689, 70), true);
                    draw->AddRectFilled(p - ImVec2(0, 150), p + ImVec2(690, 70), ImColor(0.f, 0.f, 0.f, 0.4f), 4.f);
                    draw->AddShadowRect(p + ImVec2(10, 120), p + ImVec2(690, 70), ImColor(0.f, 0.f, 0.f, 1.f), 495.f, ImVec2(0, 0));
                    ImGui::PushFont(medium_font);

                    draw->AddText(p + ImVec2(320, 40) - ImGui::CalcTextSize(OBFUSCATE_STR("CODEINE.WTF")) / 2, ImColor(1.f, 1.f, 1.f, 1.f), OBFUSCATE_STR("CODEINE.WTF"));

                    ImGui::PopFont();
                    ImGui::PopClipRect();

                    draw->AddRect(p + ImVec2(10, 10), p + ImVec2(690, 70), second_color, 4.f, 0, 2.f);
                    ImGui::SetCursorPos(ImVec2(10, 90));
                    ImGui::BeginGroup();
                    ImGui::Product(OBFUSCATE_STR("Rust DMA"), &iProduct, 0, OBFUSCATE_STR("Last update: 25.12.22"));
                    ImGui::Product(OBFUSCATE_STR("Rust External"), &iProduct, 1, OBFUSCATE_STR("Last update: 06.03.23"));
                    ImGui::EndGroup();
                    ImGui::SetCursorPos(ImVec2(390, 150));
                    ImGui::BeginGroup();
                    ImGui::InvisibleButton(OBFUSCATE_STR("CSGO_IMAGE"), ImVec2(300, 180));
                    if (iProduct == 0 || iProduct == 1) {draw->AddImageRounded(Product[0], p + ImVec2(390, 90), p + ImVec2(390, 90) + ImVec2(300, 180), ImVec2(0, 0), ImVec2(1, 1), ImColor(1.f, 1.f, 1.f, 1.f), 5.f);}
                    if (ImGui::Button(OBFUSCATE_STR("LAUNCH"), ImVec2(300, 35))) {
                        menu.inject_alpha = 0.f;
                        menu.inject_progress = 0.f;
                        menu_state = 4;
                    }
                    ImGui::EndGroup();
                }
                if (menu_state == 4)
                {
                    const char* inject_names[7] = { "Checking Status!", "Validating With Server!", "Checking HWID!", "Loading Bypass", "Loading driver!", "Obtaining Trarget Process Id!", "Injected!" };
                    menu.inject_alpha = ImLerp(menu.inject_alpha, 1.00f, ImGui::GetIO().DeltaTime);
                    menu.inject_progress = ImLinearSweep(menu.inject_progress, 680.f, ImGui::GetIO().DeltaTime * 30.f);
                    draw->AddText(small_font, 20, p + ImVec2(10, 410), ImColor(1.f, 1.f, 1.f, menu.inject_alpha), OBFUSCATE_STR("LAUNCHING"));
                    draw->AddRect(p + ImVec2(9, 429), p + ImVec2(681, 441), ImColor(0.09f, 0.09f, 0.10f, menu.inject_alpha), 4.f);
                    draw->AddRectFilled(p + ImVec2(10, 430), p + ImVec2(menu.inject_progress, 440), main_color, 4.f);
                    draw->AddText(p + ImVec2(680 - ImGui::CalcTextSize(inject_names[int(menu.inject_progress / 100)]).x, 410), ImColor(1.f, 1.f, 1.f, menu.inject_alpha), inject_names[int(menu.inject_progress / 100)]);
                    if (menu.inject_progress == 680.f)
                        menu_state = 2;
                }
            }
            ImGui::End();
        }

        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(0, 0); // without vsync
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

