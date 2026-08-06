// NUMA_PAR scheduler machinery. Included by main.c after the shared
// helpers; only dispatched while numa_run_active is set.

typedef enum {
    GLOBAL_STAGE_ONCE,   // test-specific stage run once by CPU 0 (e.g. bit-fade delay)
    WAVE_BIND,           // bind the wave: publish counts/barriers, then first window
    WAVE_WINDOW_0,       // test window 0 (below LOW_LOAD_LIMIT), relocated high
    WAVE_WINDOWS_1_PLUS, // test windows 1 and above, relocated low
    WAVE_FINISH,         // advance to the next wave, or finish the stage
    WAVE_DONE            // the only state that lets main() advance the stage
} test_scheduler_phase_t;

static test_scheduler_phase_t scheduler_phase;
static unsigned int current_wave = 0;
static unsigned int num_execution_waves = 1;

static bool legacy_window_running = true;

static bool run_test_window(int my_cpu, bool i_am_master, bool i_am_active, bool dummy_run_active, int iterations)
{
    if (!i_am_active) {
        return true;
    }

    if (num_mapped_pages[0] == 0) {
        // No memory to test in this window.
        if (i_am_master) {
            window_num++;
        }
        return true;
    }

    if (dummy_run_active) {
        if (i_am_master) {
            ticks_per_test[pass_num][test_num] += run_test(-1, test_num, test_stage, iterations);
            window_num++;
        }
        return true;
    }

    if (!map_window(0, vm_map[0][0].pm_base_addr)) {
        // Either there is no PAE or we are at the PAE limit.
        return false;
    }
    run_test(my_cpu, test_num, test_stage, iterations);

    if (i_am_master) {
        window_num++;
    }
    return true;
}

static bool run_owned_window_zero(int my_cpu, bool i_am_master, bool i_am_active, int iterations)
{
    if (i_am_master) {
        test_context_t *ctx = &test_contexts[0];
        window_page_bounds(0, &ctx->window_start, &ctx->window_end);
        setup_vm_map(0, ctx->window_start, ctx->window_end);
    }
    SHORT_BARRIER;

    return run_test_window(my_cpu, i_am_master, i_am_active, dummy_run, iterations);
}

static void run_owned_windows_1_plus(int my_cpu, bool i_am_master, bool i_am_active, int iterations)
{
    bool running = true;
    do {
        LONG_BARRIER;
        if (bail) {
            break;
        }

        if (i_am_master) {
            test_context_t *ctx = &test_contexts[0];
            if (window_num == 1) {
                window_page_bounds(1, &ctx->window_start, &ctx->window_end);
            } else {
                ctx->window_start = ctx->window_end;
                ctx->window_end  += VM_WINDOW_SIZE;
            }
            setup_vm_map(0, ctx->window_start, ctx->window_end);
        }
        SHORT_BARRIER;

        bool my_result = run_test_window(my_cpu, i_am_master, i_am_active, dummy_run, iterations);
        if (i_am_master) {
            // The map-failure decision must be shared: the inactive CPUs
            // would otherwise return true here (they take no part in the
            // window) and diverge from the master through different barrier
            // generations, deadlocking at the addressability limit.
            legacy_window_running = my_result;
        }
        SHORT_BARRIER;
        running = legacy_window_running;
    } while (running && test_contexts[0].window_end < pm_map[pm_map_size - 1].end);
}

static bool wave_has_window_zero(void)
{
    if (window_num == 0 && test_list[test_num].stages > 1) {
        // A multi-stage test runs through all the windows at each stage.
        // Relocation may disrupt the test.
        window_num = 1;
    }
    if (window_num == 0 && pm_limit_lower >= LOW_LOAD_LIMIT) {
        // Avoid unnecessary relocation.
        window_num = 1;
    }
    return window_num == 0;
}

static void bind_execution_wave(bool parallel_test)
{
    // CPUs not taking part in this test won't re-arm their stack
    // canaries, and the coming relocations will invalidate them.
    stack_canary_disarm_all();

    test_contexts[0].active_cpu_count = 1;
    if (!dummy_run) {
        if (parallel_test) {
            test_contexts[0].active_cpu_count = num_enabled_cpus;
            if(display_mode == DISPLAY_MODE_NA) {
                display_all_active();
            }
        } else {
            if (display_mode == 0) {
                display_active_cpu(smp_my_cpu_num());
            }
        }
    }
    barrier_reset(run_barrier, test_contexts[0].active_cpu_count);
}

static void test_all_windows_numa(int my_cpu)
{
    bool parallel_test = false;
    bool i_am_master = (my_cpu == master_cpu);
    bool i_am_active = i_am_master;
    if (!dummy_run) {
        if (cpu_mode == PAR && test_list[test_num].cpu_mode == PAR) {
            parallel_test = true;
            i_am_active = true;
        }
    }

    int iterations = test_list[test_num].iterations;
    if (pass_num == 0) {
        // Reduce iterations for a faster first pass.
        iterations /= 3;
    }

    for (;;) {
        switch (scheduler_phase) {
          case GLOBAL_STAGE_ONCE:
            // A test-specific global-once stage (e.g. the bit-fade delay)
            // runs once on CPU 0 between memory-touching phases.
            LONG_BARRIER;
            if (my_cpu == 0) {
                scheduler_phase = WAVE_DONE;
            }
            LONG_BARRIER;
            continue;

          case WAVE_BIND:
            LONG_BARRIER;
            if (i_am_master) {
                bind_execution_wave(parallel_test);
                scheduler_phase = wave_has_window_zero()
                                ? WAVE_WINDOW_0 : WAVE_WINDOWS_1_PLUS;
            }
            SHORT_BARRIER;
            continue;

          case WAVE_WINDOW_0: {
            bool window_zero_ok;
            if (!dummy_run && (uintptr_t)&_start != high_load_addr) {
                relocate_all_and_resume(high_load_addr, my_cpu);
            }
            window_zero_ok = run_owned_window_zero(my_cpu, i_am_master, i_am_active, iterations);
            LONG_BARRIER;
            if (my_cpu == 0) {
                // A window-0 map failure (the PAE limit was reached) aborts
                // the stage: the following windows would fail identically,
                // and the run boundary falls back to legacy placement.
                scheduler_phase = window_zero_ok ? WAVE_WINDOWS_1_PLUS : WAVE_FINISH;
            }
            SHORT_BARRIER;
            continue;
          }

          case WAVE_WINDOWS_1_PLUS:
            if (!dummy_run && (uintptr_t)&_start != low_load_addr) {
                relocate_all_and_resume(low_load_addr, my_cpu);
            }
            run_owned_windows_1_plus(my_cpu, i_am_master, i_am_active, iterations);
            LONG_BARRIER;
            if (my_cpu == 0) {
                scheduler_phase = WAVE_FINISH;
            }
            SHORT_BARRIER;
            continue;

          case WAVE_FINISH:
            LONG_BARRIER;
            if (my_cpu == 0) {
                if (++current_wave < num_execution_waves) {
                    scheduler_phase = WAVE_BIND;
                } else {
                    scheduler_phase = WAVE_DONE;
                }
            }
            SHORT_BARRIER;
            continue;

          case WAVE_DONE:
            return;
        }
    }
}
