/* Standalone single-cart launcher for GB-Recomp/gbcamera.
 * Same launcher infrastructure the compilation builds use, but with only
 * one game registered — auto-start kicks in via g_game_count == 1. */
#define SDL_MAIN_HANDLED
extern "C" {
#include "gbcamera.h"
#include "gb_sha256.h"
}

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "platform_sdl.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#define IMGUI_IMPL_OPENGL_ES2
#include "backends/imgui_impl_opengl3.h"
#include <SDL_opengles2.h>

typedef int (*GBLauncherMainFn)(int argc, char* argv[]);

typedef struct {
    const char* id;
    const char* title;
    const char* rom_path;
    GBLauncherMainFn main_fn;
    /* Lowercase 64-char SHA-256 of the ROM revision the static
     * recompilation was built against. NULL skips the check. */
    const char* expected_sha256;
} GBLauncherGame;

static int launch_gbcamera(int argc, char* argv[]) {
    return gbcamera_main(argc, argv);
}

/* SHA-256 of the canonical gbcamera.gb the static
 * recompilation was built against. */
static const char GBCAMERA_EXPECTED_SHA256[] =
    "b772951adf58dff025e05722c2aa179e0c874b7cb0b844c5cda44f6194df6299";

static GBLauncherGame g_games[] = {
    {"gbcamera", "Game Boy Camera", "roms/gbcamera.gb", launch_gbcamera,
     GBCAMERA_EXPECTED_SHA256},
};

static const char* g_launcher_name = "gbcamera";
static const size_t g_game_count = sizeof(g_games) / sizeof(g_games[0]);

static char* trim_ascii(char* text) {
    while (text && *text && isspace((unsigned char)*text)) {
        text++;
    }
    if (!text || !*text) {
        return text;
    }
    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static void print_usage(const char* program) {
    fprintf(stderr, "Usage: %s [--list-games] [--game <id>] [game arguments...]\n", program);
    fprintf(stderr, "Run without --game to open the graphical launcher.\n");
}

static void print_games(void) {
    fprintf(stderr, "Available games in %s:\n", g_launcher_name);
    for (size_t i = 0; i < g_game_count; i++) {
        fprintf(stderr, "  %zu. %s [%s]\n", i + 1, g_games[i].title, g_games[i].id);
    }
}

/* A game is runnable iff one of:
 *   - assets/<id>/ exists (already extracted from a previous run), OR
 *   - <id>.gb / .gbc / .sgb exists in cwd (user-supplied ROM that
 *     the asset loader will extract from on first run).
 * Stat-only — doesn't validate manifest completeness or SHA. The
 * asset loader still gates on those at boot. */
static bool game_assets_available(const char* id) {
    if (!id || !*id) return false;
    char path[512];
    struct stat st;

    snprintf(path, sizeof(path), "assets/%s", id);
    if (stat(path, &st) == 0 && (st.st_mode & S_IFDIR)) return true;

    static const char* extensions[] = {".gb", ".gbc", ".sgb"};
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        snprintf(path, sizeof(path), "roms/%s%s", id, extensions[i]);
        if (stat(path, &st) == 0) return true;
    }
    return false;
}

static const GBLauncherGame* find_game_by_id(const char* id) {
    if (!id) {
        return NULL;
    }
    for (size_t i = 0; i < g_game_count; i++) {
        if (strcmp(g_games[i].id, id) == 0) {
            return &g_games[i];
        }
    }
    return NULL;
}

static const GBLauncherGame* prompt_for_game(void) {
    char line[256];
    print_games();
    fprintf(stderr, "Select a game by number or id: ");
    if (!fgets(line, sizeof(line), stdin)) {
        return NULL;
    }
    char* selection = trim_ascii(line);
    if (!selection || !*selection) {
        return NULL;
    }
    char* end = NULL;
    long index = strtol(selection, &end, 10);
    end = trim_ascii(end);
    if (selection != end && end && !*end) {
        if (index >= 1 && (size_t)index <= g_game_count) {
            return &g_games[index - 1];
        }
    }
    return find_game_by_id(selection);
}

static void apply_launcher_style(void) {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 12.0f;
    style.GrabRounding = 12.0f;
    style.ScrollbarRounding = 12.0f;
    style.WindowPadding = ImVec2(20.0f, 20.0f);
    style.FramePadding = ImVec2(14.0f, 10.0f);
    style.ItemSpacing = ImVec2(12.0f, 12.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.08f, 0.11f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.12f, 0.16f, 0.95f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.17f, 0.21f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.27f, 0.33f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.11f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.13f, 0.18f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.14f, 0.45f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.56f, 0.50f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.12f, 0.37f, 0.33f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.18f, 0.24f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.31f, 0.41f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.22f, 0.31f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.24f, 0.31f, 0.40f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.24f, 0.31f, 0.40f, 0.55f);
}

static int init_graphical_launcher(SDL_Window** out_window, SDL_GLContext* out_ctx) {
    *out_window = NULL;
    *out_ctx = NULL;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "[LAUNCH] SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow(
        "GameBoy Recompiled Launcher",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        480,
        540,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) {
        fprintf(stderr, "[LAUNCH] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx) {
        fprintf(stderr, "[LAUNCH] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
    SDL_GL_MakeCurrent(window, ctx);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    /* Manual selection tracking owns the arrow keys / D-pad / face buttons.
     * Leaving ImGui nav off keeps the first arrow press from being consumed
     * by ImGui's focus system and removes the focus rectangle around the
     * child window. */
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = NULL;

    apply_launcher_style();
    ImGui_ImplSDL2_InitForOpenGL(window, ctx);
    ImGui_ImplOpenGL3_Init("#version 100");

    *out_window = window;
    *out_ctx = ctx;
    return 1;
}

static void shutdown_graphical_launcher(SDL_Window* window, SDL_GLContext ctx) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (ctx) {
        SDL_GL_DeleteContext(ctx);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}

static int run_graphical_launcher(const GBLauncherGame** out_selected) {
    *out_selected = NULL;
    if (g_game_count == 0) {
        return 1;
    }

    SDL_Window* window = NULL;
    SDL_GLContext ctx = NULL;
    if (!init_graphical_launcher(&window, &ctx)) {
        return 0;
    }

    int selected_index = 0;
    bool running = true;
    bool accepted = false;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Launcher", NULL, window_flags)) {
            /* Keyboard / D-pad navigation. */
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown) ||
                ImGui::IsKeyPressed(ImGuiKey_S)) {
                selected_index = (selected_index + 1) % (int)g_game_count;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp) ||
                ImGui::IsKeyPressed(ImGuiKey_W)) {
                selected_index = (selected_index - 1 + (int)g_game_count) % (int)g_game_count;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown) ||
                ImGui::IsKeyPressed(ImGuiKey_Z)) {
                /* Silently no-op if the selected game's ROM is missing —
                 * the "(missing ROM)" label tells the user what's needed.
                 * Mouse clicks are already gated by ImGuiSelectableFlags_
                 * Disabled below; this covers the keyboard/gamepad path. */
                if (selected_index >= 0 &&
                    (size_t)selected_index < g_game_count &&
                    game_assets_available(g_games[selected_index].id)) {
                    accepted = true;
                    running = false;
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
                running = false;
            }

            const float content_w = ImGui::GetContentRegionAvail().x;
            const float content_h = ImGui::GetContentRegionAvail().y;

            ImGui::TextDisabled("GameBoy Recompiled  ·  %s", g_launcher_name);
            ImGui::Spacing();

            const float list_width = content_w > 480.0f ? 420.0f : content_w * 0.9f;
            const float list_x = (content_w - list_width) * 0.5f;
            const float footer_h = ImGui::GetTextLineHeightWithSpacing() * 1.6f;

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + list_x);
            ImGui::BeginChild("games", ImVec2(list_width, content_h - footer_h - 32.0f), false);
            for (size_t i = 0; i < g_game_count; i++) {
                bool selected = (int)i == selected_index;
                bool available = game_assets_available(g_games[i].id);
                char label[160];
                if (available) {
                    snprintf(label, sizeof(label), "%s", g_games[i].title);
                } else {
                    snprintf(label, sizeof(label), "%s  (missing ROM)",
                             g_games[i].title);
                }
                ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
                if (!available) flags |= ImGuiSelectableFlags_Disabled;
                ImGui::PushID((int)i);
                if (ImGui::Selectable(label, selected, flags,
                                      ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 1.4f))) {
                    selected_index = (int)i;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        accepted = true;
                        running = false;
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::SetCursorPosY(content_h + ImGui::GetStyle().WindowPadding.y - footer_h);
            const char* hint = "↑ ↓ select   ·   Enter / A launch   ·   Esc / B quit";
            const float hint_w = ImGui::CalcTextSize(hint).x;
            ImGui::SetCursorPosX((content_w - hint_w) * 0.5f);
            ImGui::TextDisabled("%s", hint);
        }
        ImGui::End();

        ImGui::Render();
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(11.0f/255.0f, 14.0f/255.0f, 19.0f/255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    if (accepted && selected_index >= 0 && (size_t)selected_index < g_game_count) {
        *out_selected = &g_games[selected_index];
    }

    shutdown_graphical_launcher(window, ctx);
    return 1;
}

int main(int argc, char* argv[]) {
    const GBLauncherGame* selected = NULL;
    char** forwarded_argv = (char**)calloc((size_t)argc + 1, sizeof(char*));
    int forwarded_argc = 1;
    if (!forwarded_argv) {
        fprintf(stderr, "Failed to allocate launcher argument buffer\n");
        return 1;
    }

    forwarded_argv[0] = argv[0];
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list-games") == 0) {
            print_games();
            free(forwarded_argv);
            return 0;
        }
        if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
            print_usage(argv[0]);
            print_games();
            free(forwarded_argv);
            return 0;
        }
        if (strcmp(argv[i], "--game") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --game\n");
                print_usage(argv[0]);
                free(forwarded_argv);
                return 1;
            }
            selected = find_game_by_id(argv[++i]);
            if (!selected) {
                fprintf(stderr, "Unknown game id '%s'\n", argv[i]);
                print_games();
                free(forwarded_argv);
                return 1;
            }
            continue;
        }
        forwarded_argv[forwarded_argc++] = argv[i];
    }
    forwarded_argv[forwarded_argc] = NULL;

    /* Auto-launch when this build only has one game wired up (typical for
     * a single-cart repo using the multi-game launcher infra). Falls back
     * to the menu if the cart's ROM/assets aren't actually present. */
    const bool single_game_mode = (g_game_count == 1) &&
                                  game_assets_available(g_games[0].id);
    if (single_game_mode && !selected) {
        selected = &g_games[0];
    }

    for (;;) {
        if (!selected) {
            if (run_graphical_launcher(&selected)) {
                if (!selected) {
                    free(forwarded_argv);
                    return 0;
                }
            } else {
                fprintf(stderr, "[LAUNCH] Graphical launcher unavailable, falling back to terminal selection.\n");
                selected = prompt_for_game();
            }
            if (!selected) {
                fprintf(stderr, "No game selected\n");
                free(forwarded_argv);
                return 1;
            }
        }

        fprintf(stderr, "[LAUNCH] Starting %s [%s]\n", selected->title, selected->id);
        /* Hash-verify the ROM matches the build the cart was
         * compiled against. Mismatch is a warning, not fatal --
         * the asset loader still gates on its own checks. */
        if (selected->expected_sha256 && selected->rom_path) {
            gb_sha256_verify_file(selected->rom_path,
                                  selected->expected_sha256,
                                  selected->rom_path);
        }
        /* In single-game mode there's no launcher to return to, so hide
         * the in-game "Return to Launcher" Esc-menu entry (the menu shows
         * "Restart Game" in its place — handled below by the consume call). */
        gb_platform_set_launcher_return_enabled(!single_game_mode);
        int rc = selected->main_fn(forwarded_argc, forwarded_argv);
        /* "Restart Game" Esc-menu button: rerun the same cart from scratch.
         * The cart's main_fn already tore down SDL and freed the context;
         * looping back re-invokes it for a clean reboot. */
        if (gb_platform_consume_restart_requested()) {
            continue;
        }
        if (rc == GB_PLATFORM_RETURN_TO_LAUNCHER_EXIT_CODE && !single_game_mode) {
            selected = NULL;
            continue;
        }
        free(forwarded_argv);
        return rc;
    }
}
