#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>
#include <cstdio>
#include <memory>
#include <switch.h>
#include <language.hpp>

#include "windows.hpp"
#include "config.hpp"
#include "gui.hpp"
#include "imgui_impl_switch.hpp"
#include "log.hpp"

namespace GUI {
    static EGLDisplay s_display = EGL_NO_DISPLAY;
    static EGLContext s_context = EGL_NO_CONTEXT;
    static EGLSurface s_surface = EGL_NO_SURFACE;

    static bool InitEGL(NWindow* win) {
        s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

        if (!s_display) {
            Log::Error("Could not connect to display! error: %d", eglGetError());
            return false;
        }

        eglInitialize(s_display, nullptr, nullptr);
        if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
            Log::Error("Could not set API! error: %d", eglGetError());
            eglTerminate(s_display);
            s_display = nullptr;
        }

        EGLConfig config;
        EGLint num_configs;
        static const EGLint framebuffer_attr_list[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE,     8,
            EGL_GREEN_SIZE,   8,
            EGL_BLUE_SIZE,    8,
            EGL_ALPHA_SIZE,   8,
            EGL_DEPTH_SIZE,   24,
            EGL_STENCIL_SIZE, 8,
            EGL_NONE
        };

        eglChooseConfig(s_display, framebuffer_attr_list, std::addressof(config), 1, std::addressof(num_configs));
        if (num_configs == 0) {
            Log::Error("No config found! error: %d", eglGetError());
            eglTerminate(s_display);
            s_display = nullptr;
        }

        s_surface = eglCreateWindowSurface(s_display, config, win, nullptr);
        if (!s_surface) {
            Log::Error("Surface creation failed! error: %d", eglGetError());
            eglTerminate(s_display);
            s_display = nullptr;
        }

        static const EGLint context_attr_list[] = {
            EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
            EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
            EGL_CONTEXT_MINOR_VERSION_KHR, 3,
            EGL_NONE
        };

        s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, context_attr_list);
        if (!s_context) {
            Log::Error("Context creation failed! error: %d", eglGetError());
            eglDestroySurface(s_display, s_surface);
            s_surface = nullptr;
        }

        eglMakeCurrent(s_display, s_surface, s_surface, s_context);
        return true;
    }
    
    static void ExitEGL(void) {
        if (s_display) {
            eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

            if (s_context) {
                eglDestroyContext(s_display, s_context);
                s_context = nullptr;
            }

            if (s_surface) {
                eglDestroySurface(s_display, s_surface);
                s_surface = nullptr;
            }

            eglTerminate(s_display);
            s_display = nullptr;
        }
    }

    bool SwapBuffers(void) {
        return eglSwapBuffers(s_display, s_surface);
    }

    void SetDefaultTheme(void) {
        ImGui::GetStyle().FrameRounding = 4.0f;
        ImGui::GetStyle().GrabRounding = 4.0f;

        ImVec4 *colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
        colors[ImGuiCol_Border] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 0.65f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.18f, 0.22f, 0.25f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.09f, 0.21f, 0.31f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 0.50f, 0.50f, 1.0f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.00f, 0.50f, 0.50f, 1.0f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.50f, 0.50f, 1.0f);
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.50f, 0.50f, 1.0f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.50f, 0.50f, 1.0f);
        colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
        colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.00f, 0.50f, 0.50f, 1.0f);
        colors[ImGuiCol_TabActive] = ImVec4(0.00f, 0.50f, 0.50f, 1.0f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_PlotLines] = ImVec4(0.00f, 0.50f, 0.50f, 1.0f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.00f, 0.50f, 0.50f, 1.0f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
        colors[ImGuiCol_NavHighlight] = ImVec4(0.00f, 0.50f, 0.50f, 1.0f);
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    }

    bool Init(void) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        if (!GUI::InitEGL(nwindowGetDefault()))
            return false;
        gladLoadGL();
        ImGui_ImplSwitch_Init("#version 130");

        AppletType applet_type = appletGetAppletType();
        const bool is_applet = (applet_type != AppletType_Application && applet_type != AppletType_SystemApplication);

        PlFontData standard;
        if (R_FAILED(plGetSharedFontByType(&standard, PlSharedFontType_Standard))) {
            GUI::SetDefaultTheme();
            return true;
        }

        u8 *px = nullptr;
        int w = 0, h = 0, bpp = 0;
        ImFontConfig font_cfg;
        font_cfg.FontDataOwnedByAtlas = false;

        font_cfg.MergeMode = false;
        io.Fonts->AddFontFromMemoryTTF(standard.address, standard.size, 20.f, &font_cfg, io.Fonts->GetGlyphRangesDefault());

        font_cfg.MergeMode = true;
        static const ImWchar extended_range[] = {0xE000, 0xE152, 0};
        PlFontData extended;
        if (R_SUCCEEDED(plGetSharedFontByType(&extended, PlSharedFontType_NintendoExt))) {
            io.Fonts->AddFontFromMemoryTTF(extended.address, extended.size, 20.f, &font_cfg, extended_range);
        }

        // ----- Language-option list: every title must render under ANY active language. -----
        // Glyphs are requested explicitly as {start,end} pairs from the font that actually
        // contains each script (Standard already holds kana + kanji + Cyrillic; Simplified /
        // Traditional Chinese and Korean live in their dedicated shared fonts), so no title
        // glyph is ever dropped and no accidental huge range is baked.
        PlFontData kofont, cnfont, tcnfont;
        const bool hasKO = R_SUCCEEDED(plGetSharedFontByType(&kofont,  PlSharedFontType_KO));
        const bool hasCN = R_SUCCEEDED(plGetSharedFontByType(&cnfont,  PlSharedFontType_ChineseSimplified));
        const bool hasTC = R_SUCCEEDED(plGetSharedFontByType(&tcnfont, PlSharedFontType_ChineseTraditional));

        static const ImWchar titleStdGlyphs[] = {
            0x65E5,0x65E5, 0x672C,0x672C, 0x8A9E,0x8A9E,                 // 日本語
            0x0420,0x0420, 0x0438,0x043A, 0x0441,0x0441, 0x0443,0x0443,   // Русский
            0,
        };
        static const ImWchar titleSCGlyphs[] = {0x7B80,0x7B80, 0x4F53,0x4F53, 0x4E2D,0x4E2D, 0x6587,0x6587, 0}; // 简体中文
        static const ImWchar titleTCGlyphs[] = {0x7E41,0x7E41, 0x9AD4,0x9AD4, 0x4E2D,0x4E2D, 0x6587,0x6587, 0}; // 繁體中文
        static const ImWchar titleKOGlyphs[] = {0xD55C,0xD55C, 0xAD6D,0xAD6D, 0xC5B4,0xC5B4, 0};               // 한국어
        io.Fonts->AddFontFromMemoryTTF(standard.address, standard.size, 20.f, &font_cfg, titleStdGlyphs);
        if (hasCN) io.Fonts->AddFontFromMemoryTTF(cnfont.address,  cnfont.size,  20.f, &font_cfg, titleSCGlyphs);
        if (hasTC) io.Fonts->AddFontFromMemoryTTF(tcnfont.address, tcnfont.size, 20.f, &font_cfg, titleTCGlyphs);
        if (hasKO) io.Fonts->AddFontFromMemoryTTF(kofont.address,  kofont.size,  20.f, &font_cfg, titleKOGlyphs);

        io.Fonts->AddFontFromMemoryTTF(standard.address, standard.size, 20.f, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());
        io.Fonts->AddFontFromMemoryTTF(standard.address, standard.size, 20.f, &font_cfg, io.Fonts->GetGlyphRangesJapanese());
        if (hasTC) io.Fonts->AddFontFromMemoryTTF(tcnfont.address, tcnfont.size, 20.f, &font_cfg, io.Fonts->GetGlyphRangesChineseTraditionalCommon());

        if (is_applet)
        {
            if (hasKO) io.Fonts->AddFontFromMemoryTTF(kofont.address,  kofont.size,  20.f, &font_cfg, io.Fonts->GetGlyphRangesKoreanCommon());
            if (hasCN) io.Fonts->AddFontFromMemoryTTF(cnfont.address,  cnfont.size,  20.f, &font_cfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            io.Fonts->TexDesiredWidth = 1024;
        } else {
            // Application mode: generous atlas budget -> load every script for maximum coverage.
            if (hasKO) io.Fonts->AddFontFromMemoryTTF(kofont.address,  kofont.size,  20.f, &font_cfg, io.Fonts->GetGlyphRangesKorean());
            if (hasCN) io.Fonts->AddFontFromMemoryTTF(cnfont.address,  cnfont.size,  20.f, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());
            io.Fonts->TexDesiredWidth = 2048;
        }

        io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
        io.Fonts->GetTexDataAsAlpha8(&px, &w, &h, &bpp);
        io.Fonts->Build();

        // Temporary diagnostic (needs cfg.dev_options): report the real atlas footprint so the
        // applet multi-language load can be validated objectively from /switch/NX-Shell/debug.log.
        int total_glyphs = 0;
        for (int i = 0; i < io.Fonts->Fonts.Size; i++)
            total_glyphs += io.Fonts->Fonts[i]->Glyphs.Size;
        Log::Error("[fontdiag] mode=%s fonts=%d glyphs=%d tex=%dx%d alpha8=%.2fMB",
                   is_applet ? "applet" : "application",
                   io.Fonts->Fonts.Size, total_glyphs, w, h,
                   (double)w * (double)h / 1048576.0);

        io.Fonts->ClearInputData();

        GUI::SetDefaultTheme();
        return true;
    }

    bool Loop(u64 &key) {
        if (!appletMainLoop())
            return false;

        key = ImGui_ImplSwitch_NewFrame();
        ImGui::NewFrame();

        bool isEditorActive = TextEditor::IsActive();
        return isEditorActive || !(key & HidNpadButton_Plus);
    }

    void Render(void) {
        ImGui::Render();
        ImGuiIO &io = ImGui::GetIO(); (void)io;
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
        glClearColor(0.00f, 0.00f, 0.00f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplSwitch_RenderDrawData(ImGui::GetDrawData());
        GUI::SwapBuffers();
    }

    void Exit(void) {
        TextEditor::Shutdown();
        ImGui_ImplSwitch_Shutdown();
        GUI::ExitEGL();
    }
}
