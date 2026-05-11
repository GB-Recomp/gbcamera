/* Main entry point */
#include "gbcamera.h"
#include "gbrt.h"
#include "audio.h"
#include "audio_stats.h"
#ifdef GB_HAS_SDL2
#include <SDL.h>
#include "platform_sdl.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define GB_DUP2 _dup2
#define GB_FILENO _fileno
#else
#include <unistd.h>
#define GB_DUP2 dup2
#define GB_FILENO fileno
#endif

/* === gb_asset_loader integration === */
#include "gb_asset_loader.h"
#include "assets_manifest_gbcamera.h"
extern uint8_t gbcamera__rom_data[];
extern const size_t gbcamera__rom_size;
static const GBGameAssets GB_GBCAMERA_GAME = {
    .game_id = "gbcamera",
    .rom_filename = "roms/gbcamera.gb",
    .rom_data = gbcamera__rom_data,
    .rom_size = 1048576u,
    .expected_sha1 = { 0x53, 0xa0, 0xdd, 0x6d, 0xe3, 0xeb, 0xd0, 0xd3, 0x49, 0x5f, 0x8b, 0xdc, 0x37, 0x7a, 0xb0, 0x8e, 0x50, 0x74, 0xc0, 0xef },
    .manifest = GBCAMERA_ASSETS_MANIFEST,
    .manifest_count = GBCAMERA_ASSETS_MANIFEST_COUNT,
};

static bool gb_redirect_logs(const char* path) {
    if (!path || !path[0]) {
        return true;
    }
    if (!freopen(path, "w", stdout)) {
        fprintf(stderr, "Failed to open log file '%s' for stdout redirection\n", path);
        return false;
    }
    if (GB_DUP2(GB_FILENO(stdout), GB_FILENO(stderr)) < 0) {
        fprintf(stdout, "Failed to open log file '%s' for stderr redirection\n", path);
        fflush(stdout);
        return false;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr, "[LOG] Redirecting runtime output to %s\n", path);
    return true;
}

#ifdef GB_HAS_SDL2
static double gb_profile_now_ms(void) {
    uint64_t ticks = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();
    return freq ? ((double)ticks * 1000.0) / (double)freq : 0.0;
}
#endif

static void gbrt_print_interpreter_summary(const GBContext* ctx, unsigned max_hotspots) {
    if (!ctx) {
        return;
    }
    if (ctx->total_dispatch_fallbacks == 0 &&
        ctx->total_interpreter_entries == 0 &&
        !ctx->has_unimplemented_interpreter_opcode) {
        fprintf(stderr, "[INTERP] No interpreter fallback recorded.\n");
        return;
    }
    fprintf(stderr,
            "[INTERP] Summary: fallbacks=%llu interpreter_entries=%llu interpreter_instructions=%llu interpreter_cycles=%llu\n",
            (unsigned long long)ctx->total_dispatch_fallbacks,
            (unsigned long long)ctx->total_interpreter_entries,
            (unsigned long long)ctx->total_interpreter_instructions,
            (unsigned long long)ctx->total_interpreter_cycles);
    unsigned printed = 0;
    for (size_t i = 0; i < GBRT_INTERPRETER_HOTSPOT_CAPACITY && printed < max_hotspots; i++) {
        const GBInterpreterHotspot* hotspot = &ctx->interpreter_hotspots[i];
        if (!hotspot->valid || hotspot->entries == 0) {
            continue;
        }
        fprintf(stderr,
                "[INTERP] Hotspot #%u %03X:%04X entries=%llu instructions=%llu cycles=%llu last_frame=%llu\n",
                printed + 1,
                hotspot->bank,
                hotspot->addr,
                (unsigned long long)hotspot->entries,
                (unsigned long long)hotspot->instructions,
                (unsigned long long)hotspot->cycles,
                (unsigned long long)hotspot->last_frame);
        printed++;
    }
    if (ctx->has_unimplemented_interpreter_opcode) {
        fprintf(stderr,
                "[INTERP] Coverage gap: opcode=%02X at %03X:%04X\n",
                ctx->last_unimplemented_opcode,
                ctx->last_unimplemented_bank,
                ctx->last_unimplemented_addr);
    }
}

static const GBContext* gbrt_interpreter_summary_ctx = NULL;
static unsigned gbrt_interpreter_summary_limit = 8;
static int gbrt_interpreter_summary_enabled = 0;
static int gbrt_interpreter_summary_atexit_registered = 0;

static void gbrt_flush_interpreter_summary(void) {
    if (!gbrt_interpreter_summary_enabled || !gbrt_interpreter_summary_ctx) {
        return;
    }
    gbrt_print_interpreter_summary(gbrt_interpreter_summary_ctx, gbrt_interpreter_summary_limit);
    gbrt_interpreter_summary_ctx = NULL;
    gbrt_interpreter_summary_enabled = 0;
    if (gbrt_instruction_limit_callback == gbrt_flush_interpreter_summary) {
        gbrt_instruction_limit_callback = NULL;
    }
}

static void gbrt_enable_interpreter_summary(const GBContext* ctx, unsigned max_hotspots) {
    gbrt_interpreter_summary_ctx = ctx;
    gbrt_interpreter_summary_limit = max_hotspots;
    gbrt_interpreter_summary_enabled = 1;
    gbrt_instruction_limit_callback = gbrt_flush_interpreter_summary;
    if (!gbrt_interpreter_summary_atexit_registered) {
        atexit(gbrt_flush_interpreter_summary);
        gbrt_interpreter_summary_atexit_registered = 1;
    }
}

static void gbrt_disable_interpreter_summary(void) {
    gbrt_interpreter_summary_ctx = NULL;
    gbrt_interpreter_summary_enabled = 0;
    if (gbrt_instruction_limit_callback == gbrt_flush_interpreter_summary) {
        gbrt_instruction_limit_callback = NULL;
    }
}

int gbcamera_main(int argc, char* argv[]) {
    bool debug_audio = false;
    bool debug_audio_trace = false;
    bool audio_stats_console = false;
    unsigned debug_audio_seconds = 10;
    bool differential_mode = false;
    unsigned long long differential_steps = 10000;
    bool differential_steps_explicit = false;
    unsigned long long differential_frames = 0;
    unsigned long long differential_log_interval = 1000;
    bool differential_compare_memory = true;
    bool differential_log_fallbacks = false;
    bool differential_fail_on_fallback = false;
    bool debug_performance = false;
    const char* input_script = NULL;
    const char* log_file = NULL;
    unsigned long long frame_limit = 0;
    double slow_frame_ms = 0.0;
    double slow_vsync_ms = 0.0;
    bool log_frame_fallbacks = false;
    bool log_lcd_transitions = false;
    bool report_interpreter_hotspots = false;
    unsigned long interpreter_hotspot_limit = 8;
    int smooth_lcd_transitions_override = -1;
    bool benchmark_mode = false;
    const char* model_override = "auto";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        }
    }
    if (!gb_redirect_logs(log_file)) {
        return 1;
    }
    // Parse args
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0) {
            gbrt_trace_enabled = true;
            printf("Trace enabled\n");
        } else if (strcmp(argv[i], "--trace-entries") == 0 && i + 1 < argc) {
            gbrt_set_trace_file(argv[++i]);
        } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            gbrt_instruction_limit = strtoull(argv[++i], NULL, 10);
            printf("Instruction limit: %llu\n", (unsigned long long)gbrt_instruction_limit);
        } else if (strcmp(argv[i], "--limit-frames") == 0 && i + 1 < argc) {
            frame_limit = strtoull(argv[++i], NULL, 10);
            printf("Frame limit: %llu\n", (unsigned long long)frame_limit);
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_script = argv[++i];
            gb_platform_set_input_script(input_script);
        } else if (strcmp(argv[i], "--record-input") == 0 && i + 1 < argc) {
            gb_platform_set_input_record_file(argv[++i]);
        } else if (strcmp(argv[i], "--dump-frames") == 0 && i + 1 < argc) {
            gb_platform_set_dump_frames(argv[++i]);
        } else if (strcmp(argv[i], "--dump-present-frames") == 0 && i + 1 < argc) {
            gb_platform_set_dump_present_frames(argv[++i]);
        } else if (strcmp(argv[i], "--screenshot-prefix") == 0 && i + 1 < argc) {
            gb_platform_set_screenshot_prefix(argv[++i]);
        } else if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            i++;
        } else if (strcmp(argv[i], "--debug-performance") == 0) {
            debug_performance = true;
        } else if (strcmp(argv[i], "--debug-audio") == 0) {
            debug_audio = true;
        } else if (strcmp(argv[i], "--debug-audio-seconds") == 0 && i + 1 < argc) {
            debug_audio_seconds = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--debug-audio-trace") == 0) {
            debug_audio_trace = true;
        } else if (strcmp(argv[i], "--audio-stats") == 0) {
            audio_stats_console = true;
        } else if (strcmp(argv[i], "--log-slow-frames") == 0 && i + 1 < argc) {
            slow_frame_ms = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--log-slow-vsync") == 0 && i + 1 < argc) {
            slow_vsync_ms = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--log-frame-fallbacks") == 0) {
            log_frame_fallbacks = true;
        } else if (strcmp(argv[i], "--log-lcd-transitions") == 0) {
            log_lcd_transitions = true;
        } else if (strcmp(argv[i], "--report-interpreter-hotspots") == 0) {
            report_interpreter_hotspots = true;
        } else if (strcmp(argv[i], "--interpreter-hotspot-limit") == 0 && i + 1 < argc) {
            interpreter_hotspot_limit = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--smooth-lcd-transitions") == 0) {
            smooth_lcd_transitions_override = 1;
        } else if (strcmp(argv[i], "--no-smooth-lcd-transitions") == 0) {
            smooth_lcd_transitions_override = 0;
        } else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_override = argv[++i];
        } else if (strcmp(argv[i], "--differential") == 0) {
            differential_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                differential_steps = strtoull(argv[++i], NULL, 10);
                differential_steps_explicit = true;
            }
        } else if (strcmp(argv[i], "--differential-frames") == 0 && i + 1 < argc) {
            differential_frames = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--differential-log") == 0 && i + 1 < argc) {
            differential_log_interval = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--differential-no-memory") == 0) {
            differential_compare_memory = false;
        } else if (strcmp(argv[i], "--differential-log-fallbacks") == 0) {
            differential_log_fallbacks = true;
        } else if (strcmp(argv[i], "--differential-fail-on-fallback") == 0) {
            differential_fail_on_fallback = true;
        } else if (strcmp(argv[i], "--benchmark") == 0) {
            benchmark_mode = true;
        }
    }

    if (debug_performance) {
        audio_stats_console = true;
        log_frame_fallbacks = true;
        log_lcd_transitions = true;
        report_interpreter_hotspots = true;
        if (slow_frame_ms <= 0.0) slow_frame_ms = 1.0;
        if (slow_vsync_ms <= 0.0) slow_vsync_ms = 0.1;
        fprintf(stderr,
                "[PERF] Enabled performance debug logging (frame>=%.3fms, vsync>=%.3fms, fallbacks, LCD transitions, audio stats)\n",
                slow_frame_ms,
                slow_vsync_ms);
    }

    GBConfig runtime_config = *gbcamera_default_config();
    if (strcmp(model_override, "auto") == 0) {
        runtime_config.model = runtime_config.cartridge_supports_cgb ? GB_MODEL_CGB : GB_MODEL_DMG;
        runtime_config.cgb_compatibility_mode = false;
    } else if (strcmp(model_override, "dmg") == 0) {
        if (runtime_config.cartridge_requires_cgb) {
            fprintf(stderr, "CGB-only ROMs cannot run with --model dmg\n");
            return 1;
        }
        runtime_config.model = GB_MODEL_DMG;
        runtime_config.cgb_compatibility_mode = false;
    } else if (strcmp(model_override, "cgb") == 0) {
        runtime_config.model = GB_MODEL_CGB;
        runtime_config.cgb_compatibility_mode = !runtime_config.cartridge_supports_cgb;
    } else {
        fprintf(stderr, "Unknown model '%s' (expected auto, dmg, or cgb)\n", model_override);
        return 1;
    }

    if (differential_mode && differential_frames > 0 && !differential_steps_explicit) {
        differential_steps = 0;
    }

    if (differential_mode) {
        GBContext* generated_ctx = gb_context_create(&runtime_config);
        GBContext* interpreted_ctx = gb_context_create(&runtime_config);
        if (!generated_ctx || !interpreted_ctx) {
            fprintf(stderr, "Failed to create differential contexts\n");
            gb_context_destroy(generated_ctx);
            gb_context_destroy(interpreted_ctx);
            return 1;
        }
        gb_context_set_save_id(generated_ctx, "gbcamera");
        gb_context_set_save_id(interpreted_ctx, "gbcamera");
        gbcamera_init(generated_ctx);
        gbcamera_init(interpreted_ctx);
        GBDifferentialOptions diff_options = {
            .max_steps = differential_steps,
            .max_frames = differential_frames,
            .log_interval = differential_log_interval,
            .compare_memory = differential_compare_memory,
            .log_fallbacks = differential_log_fallbacks,
            .fail_on_fallback = differential_fail_on_fallback,
            .input_script = input_script,
        };
        GBDifferentialResult diff_result;
        bool matched = gb_run_differential(generated_ctx, interpreted_ctx, &diff_options, &diff_result);
        gb_context_destroy(generated_ctx);
        gb_context_destroy(interpreted_ctx);
        return matched ? 0 : 1;
    }

    GBContext* ctx = gb_context_create(&runtime_config);
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    gb_context_set_save_id(ctx, "gbcamera");
    gbrt_log_lcd_transitions = log_lcd_transitions;
    if (debug_audio) gb_audio_set_debug(true);
    gb_audio_set_debug_capture_seconds(debug_audio_seconds);
    if (debug_audio_trace) gb_audio_set_debug_trace(true);
    audio_stats_set_log_to_console(audio_stats_console);
#ifdef GB_HAS_SDL2
    // Initialize SDL2 platform with 5x scaling
    if (benchmark_mode) {
        gb_platform_set_benchmark_mode(true);
    }
    if (!gb_platform_init(5)) {
        fprintf(stderr, "Failed to initialize platform\n");
        gb_context_destroy(ctx);
        return 1;
    }
    gb_platform_register_context(ctx);
    if (smooth_lcd_transitions_override >= 0) {
        gb_platform_set_smooth_lcd_transitions(smooth_lcd_transitions_override != 0);
    }
#endif
    if (!gb_chdir_to_exe_dir()) {
        fprintf(stderr, "[gbcamera] chdir to exe dir failed\n");
        return 1;
    }
    if (!gb_load_assets(&GB_GBCAMERA_GAME)) {
        fprintf(stderr, "[gbcamera] failed to load ROM/assets\n");
        return 1;
    }
#ifdef GB_HAS_SDL2
    gb_platform_set_game_id(ctx, GB_GBCAMERA_GAME.game_id);
#endif
    gbcamera_init(ctx);
    int exit_code = 0;

#ifdef GB_HAS_SDL2
    if (report_interpreter_hotspots) {
        gbrt_enable_interpreter_summary(ctx, (unsigned)interpreter_hotspot_limit);
    }

    // Run the game loop
    unsigned long long frame_index = 0;
    const uint32_t lcd_smooth_slice_cycles = 70224u;
    bool running = true;
    while (running) {
        double emu_ms = 0.0;
        double render_ms = 0.0;
        double upload_ms = 0.0;
        double compose_ms = 0.0;
        double present_ms = 0.0;
        double vsync_ms = 0.0;
        uint32_t paced_cycles = 0;
        GBPlatformTimingInfo timing_info = {0};
        gb_reset_frame(ctx);
        ctx->stopped = 0;
        while (!ctx->frame_done) {
            bool smooth_lcd_transitions = gb_platform_get_smooth_lcd_transitions();
            uint32_t slice_budget = smooth_lcd_transitions ? lcd_smooth_slice_cycles : 0xFFFFFFFFu;
            uint32_t slice_start_cycles = ctx->frame_cycles;
            double slice_start_ms = gb_profile_now_ms();
            gb_run_cycles(ctx, slice_budget);
            emu_ms += gb_profile_now_ms() - slice_start_ms;
            uint32_t slice_cycles = ctx->frame_cycles - slice_start_cycles;
            if (!gb_platform_poll_events(ctx)) {
                running = false;
                break;
            }
            if (smooth_lcd_transitions && !ctx->frame_done && slice_cycles >= lcd_smooth_slice_cycles) {
                if (ctx->lcd_off_active || !(ctx->io[0x40] & 0x80)) {
                    gb_platform_render_lcd_off_frame();
                } else {
                    const uint32_t* slice_fb = gb_get_framebuffer(ctx);
                    if (slice_fb) gb_platform_present_framebuffer(slice_fb);
                }
                gb_platform_get_timing_info(&timing_info);
                render_ms += timing_info.total_render_ms;
                upload_ms += timing_info.upload_ms;
                compose_ms += timing_info.compose_ms;
                present_ms += timing_info.present_ms;
                gb_platform_vsync(slice_cycles);
                paced_cycles += slice_cycles;
            }
        }
        if (!running) break;
        if (ctx->frame_done) {
            uint32_t completed_frame_cycles = ctx->frame_cycles;
            uint32_t final_pacing_cycles = (completed_frame_cycles > paced_cycles) ? (completed_frame_cycles - paced_cycles) : 0;
            frame_index++;
            const uint32_t* fb = gb_get_framebuffer(ctx);
            if (fb) gb_platform_render_frame(fb);
            gb_platform_get_timing_info(&timing_info);
            render_ms += timing_info.total_render_ms;
            upload_ms += timing_info.upload_ms;
            compose_ms += timing_info.compose_ms;
            present_ms += timing_info.present_ms;
            if ((slow_frame_ms > 0.0 && (emu_ms + render_ms) >= slow_frame_ms) ||
                (log_frame_fallbacks && ctx->frame_dispatch_fallbacks > 0)) {
                fprintf(stderr,
                        "[FRAME] #%llu emu=%.3fms render=%.3fms upload=%.3fms compose=%.3fms present=%.3fms cycles=%u fallbacks=%u interp_instr=%llu interp_cycles=%llu first=%03X:%04X last=%03X:%04X total_fallbacks=%llu lcd_off_cycles=%u lcd_transitions=%u lcd_spans=%u last_lcd_off_span=%u\n",
                        frame_index,
                        emu_ms,
                        render_ms,
                        upload_ms,
                        compose_ms,
                        present_ms,
                        completed_frame_cycles,
                        ctx->frame_dispatch_fallbacks,
                        (unsigned long long)ctx->frame_interpreter_instructions,
                        (unsigned long long)ctx->frame_interpreter_cycles,
                        (unsigned)ctx->frame_first_fallback_bank,
                        ctx->frame_first_fallback_addr,
                        (unsigned)ctx->frame_last_fallback_bank,
                        ctx->frame_last_fallback_addr,
                        (unsigned long long)ctx->total_dispatch_fallbacks,
                        ctx->frame_lcd_off_cycles,
                        ctx->frame_lcd_transition_count,
                        ctx->frame_lcd_off_span_count,
                        ctx->last_lcd_off_span_cycles);
            }
            if (final_pacing_cycles > 0) {
                gb_platform_vsync(final_pacing_cycles);
                gb_platform_get_timing_info(&timing_info);
                vsync_ms = timing_info.pacing_ms;
            }
            if (slow_vsync_ms > 0.0 && vsync_ms >= slow_vsync_ms) {
                fprintf(stderr,
                        "[VSYNC] #%llu wait=%.3fms cycles=%u\n",
                        frame_index,
                        vsync_ms,
                        final_pacing_cycles);
            }
            if (frame_limit > 0 && frame_index >= frame_limit) {
                fprintf(stderr,
                        "[LIMIT] Reached frame limit %llu\n",
                        (unsigned long long)frame_limit);
                break;
            }
        }
    }
    if (gb_platform_get_exit_action() == GB_PLATFORM_EXIT_RETURN_TO_LAUNCHER) {
        exit_code = GB_PLATFORM_RETURN_TO_LAUNCHER_EXIT_CODE;
    }
    gb_platform_shutdown();
#else
    // No SDL2 - just run for testing
    if (report_interpreter_hotspots) {
        gbrt_enable_interpreter_summary(ctx, (unsigned)interpreter_hotspot_limit);
    }
    gbcamera_run(ctx);
    printf("Recompiled code executed successfully!\n");
    printf("Registers: A=%02X B=%02X C=%02X\n", ctx->a, ctx->b, ctx->c);
#endif

    if (report_interpreter_hotspots) {
        gbrt_flush_interpreter_summary();
    }
    gbrt_disable_interpreter_summary();
    gb_context_destroy(ctx);
    return exit_code;
}
