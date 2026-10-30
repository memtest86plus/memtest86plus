// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Lionel Debroux.
//
// NUMA_PAR scheduler, #included by app/main.c: not compiled standalone.

// Scheduler state, copied by the relocation and read back on re-entry.
static unsigned int current_wave = 0;
static unsigned int num_execution_waves = 1;

// True when this run uses the NUMA_PAR scheduler: NUMA_PAR mode (validated before AP startup),
// parallel CPU mode, and at least two CPU-backed memory domains.
static bool numa_par_runs(void)
{
    return VMEM_MAX_CONTEXTS > 1 && numa_mode == NUMA_PAR && cpu_mode == PAR && num_cpu_memory_domains >= 2;
}

// CPU 0 only, between two global barriers: binds the wave's CPU-backed domains to execution
// contexts and sets up their teams and barriers.
static void bind_execution_wave(int wave, bool parallel_test)
{
    // Idle CPUs won't re-arm their stack canaries, and the coming relocations invalidate them.
    stack_canary_disarm_all();

    memset(domain_to_context, CONTEXT_NONE, sizeof(domain_to_context));
    memset(cpu_is_test_participant, 0, sizeof(cpu_is_test_participant));

    unsigned int first = wave * VMEM_MAX_CONTEXTS;
    unsigned int limit = MIN(first + VMEM_MAX_CONTEXTS, num_cpu_memory_domains);
    unsigned int num_contexts_in_wave = 0;

    for (unsigned int i = first; i < limit; i++) {
        uint32_t domain = cpu_memory_domains[i];
        int context = num_contexts_in_wave++;
        domain_to_context[domain] = context;

        // The previous user is quiescent since the global barrier. Not memset: it holds atomics.
        test_contexts[context].proximity_domain_idx = domain;
        test_contexts[context].master_cpu_num = -1;
        test_contexts[context].team_cpu_count = 0;
        test_contexts[context].active_cpu_count = 0;
        test_contexts[context].window_index = (test_list[test_num].stages > 1) ? 1 : 0;
        test_contexts[context].window_start = 0;
        test_contexts[context].window_end = 0;
        __atomic_store_n(&test_contexts[context].windows_exhausted, false, __ATOMIC_RELAXED);
        __atomic_store_n(&test_contexts[context].status, CONTEXT_OK, __ATOMIC_RELAXED);
    }

    for (int cpu = 0; cpu < num_available_cpus; cpu++) {
        uint32_t domain = smp_get_proximity_domain_idx(cpu);
        int context = domain_to_context[domain];
        bool in_team = cpu_is_global_participant[cpu] && run_cpu_selected[cpu] && context != CONTEXT_NONE;
        if (in_team) {
            cpu_execution_context[cpu] = context;
            if (test_contexts[context].team_cpu_count == 0) {
                test_contexts[context].master_cpu_num = cpu;
            }
            test_team_chunk_index[cpu] = test_contexts[context].team_cpu_count++;
            // A parallel test runs the whole team, a sequential one the master only.
            cpu_is_test_participant[cpu] = parallel_test || cpu == test_contexts[context].master_cpu_num;
        } else {
            // Parked CPUs (later waves, CPU-only domains, disabled CPUs) use context 0's mapping
            // but never join its test barrier.
            cpu_execution_context[cpu] = 0;
        }
    }

    // Each domain's memory is owned by one context in one wave (see setup_vm_map()), so it is
    // tested once per stage. Memory-only domains go to the BSP context in wave 0.
    int bsp_context = domain_to_context[bsp_proximity_domain];
    for (int domain = 0; domain < num_proximity_domains; domain++) {
        if (domain_to_context[domain] != CONTEXT_NONE) {
            memory_owner_context[domain] = domain_to_context[domain];
        } else if (   wave == 0
                   && enabled_cpus_in_proximity_domain[domain] == 0
                   && smp_domain_has_memory(domain)) {
            memory_owner_context[domain] = bsp_context;
        } else {
            memory_owner_context[domain] = CONTEXT_NONE;
        }
    }

    for (int context = 0; context < (int)num_contexts_in_wave; context++) {
        test_context_t *ctx = &test_contexts[context];
        assert(ctx->team_cpu_count > 0);
        assert(ctx->master_cpu_num >= 0);
        ctx->active_cpu_count = parallel_test ? ctx->team_cpu_count : 1;
        barrier_reset(&context_barrier[context], ctx->active_cpu_count);
    }

    num_bound_execution_contexts = num_contexts_in_wave;
    display_reset_ui_owner();

    // Wave invariants.
    assert(num_contexts_in_wave >= 1 && num_contexts_in_wave <= VMEM_MAX_CONTEXTS);
    for (int context = 0; context < (int)num_contexts_in_wave; context++) {
        assert(test_contexts[context].master_cpu_num >= 0);
        assert(test_contexts[context].team_cpu_count > 0);
        assert(   test_contexts[context].active_cpu_count >= 1
               && test_contexts[context].active_cpu_count <= test_contexts[context].team_cpu_count);
    }

    // Fill the Using: field like the legacy path (IMC/SPD mode keeps its own display).
    if (display_mode == DISPLAY_MODE_NA) {
        display_all_active();
    }

    serial_log_wave_bind(current_wave, num_contexts_in_wave);
}

// At a run boundary, from the CPU selection: builds cpu_memory_domains[] (BSP domain first),
// num_cpu_memory_domains and num_execution_waves.
static void build_numa_domain_list(void)
{
    memset(enabled_cpus_in_proximity_domain, 0, sizeof(enabled_cpus_in_proximity_domain));

    for (int cpu = 0; cpu < num_available_cpus; cpu++) {
        if (!cpu_is_global_participant[cpu] || !run_cpu_selected[cpu]) {
            continue;
        }
        uint32_t domain = smp_get_proximity_domain_idx(cpu);
        enabled_cpus_in_proximity_domain[domain]++;
    }

    // Domains needing a team, the BSP domain first (context 0 of wave 0). Memory-only domains
    // are owned by the BSP context in wave 0; CPU-only domains are parked.
    bsp_proximity_domain = smp_get_proximity_domain_idx(0);
    num_cpu_memory_domains = 0;

    // The BSP domain is listed even without memory when it must own memory-only domains.
    bool have_orphan_memory = false;
    for (int domain = 0; domain < num_proximity_domains; domain++) {
        if (   domain != (int)bsp_proximity_domain && smp_domain_has_memory(domain)
            && enabled_cpus_in_proximity_domain[domain] == 0) {
            have_orphan_memory = true;
            break;
        }
    }
    if (smp_domain_has_memory(bsp_proximity_domain) || have_orphan_memory) {
        cpu_memory_domains[num_cpu_memory_domains++] = bsp_proximity_domain;
    }

    for (int domain = 0; domain < num_proximity_domains; domain++) {
        if (   domain != (int)bsp_proximity_domain && smp_domain_has_memory(domain)
            && enabled_cpus_in_proximity_domain[domain] != 0) {
            cpu_memory_domains[num_cpu_memory_domains++] = domain;
        }
    }

    num_execution_waves = (num_cpu_memory_domains + VMEM_MAX_CONTEXTS - 1) / VMEM_MAX_CONTEXTS;
}

//------------------------------------------------------------------------------
// NUMA_PAR scheduler.
//
// A relocation-resumable state machine: run_at() never returns, so after a relocation main()
// re-enters test_all_windows_numa(), which resumes from scheduler_phase and current_wave.
// Each stage runs waves 0..N-1. Contexts and barriers are only reconfigured between the
// LONG/SHORT_BARRIER pairs bracketing each phase.
//------------------------------------------------------------------------------

// Fatal context failures, latched by CPU 0 at each wave end before the next rebind. Read after
// the stage to fall back to legacy mode.
static bool numa_stage_failure = false;

typedef enum {
    GLOBAL_STAGE_ONCE,   // test-specific stage run once by CPU 0 (e.g. bit-fade delay)
    WAVE_BIND,           // bind the wave: publish counts/barriers, then first window
    WAVE_WINDOW_0,       // test window 0 (below LOW_LOAD_LIMIT), relocated high
    WAVE_WINDOWS_1_PLUS, // test windows 1 and above, relocated low
    WAVE_FINISH,         // advance to the next wave, or finish the stage
    WAVE_DONE            // the only state that lets main() advance the stage
} test_scheduler_phase_t;

static test_scheduler_phase_t scheduler_phase;

// As in the legacy loop, multi-stage tests skip window 0: relocating between stages would overwrite it.
static bool wave_has_window_zero(void)
{
    return test_list[test_num].stages == 1 && selected_map_has_window_zero();
}

// CPU 0 only: the bit-fade delay. The other CPUs wait at the global barriers, so no context
// barrier or test helper may be used here.
static void run_global_stage_once(void)
{
    int sleep_secs = test_list[test_num].iterations;
    if (pass_num == 0) {
        // Reduced fast pass, matching the window stages.
        sleep_secs /= 3;
    }
    // Does do_tick()'s work each second. Only CPU 0 runs, so no UI lock is needed.
    display_test_stage_description("fade over %i seconds", sleep_secs);
    while (sleep_secs > 0) {
        check_input();
        if (restart || bail) {
            // Stop early; the run boundary handles the request.
            break;
        }
        display_progress_tick();
        update_timed_ui();
        error_update();
        sleep_secs--;
        sleep(1);
    }
}

// One window iteration of the calling CPU's context: the master computes the window and map,
// the context barrier publishes them. Returns false when the whole team must stop.
static bool numa_window_step(int my_cpu, int context, int iterations)
{
    test_context_t *ctx = &test_contexts[context];
    bool stop = false;

    if (my_cpu == ctx->master_cpu_num) {
        if (load_test_status(context) != CONTEXT_OK) {
            stop = true;
        } else {
            if (ctx->window_index <= 1) {
                window_page_bounds(ctx->window_index, &ctx->window_start, &ctx->window_end);
            } else {
                ctx->window_start = ctx->window_end;
                // Skip address space holes spanning whole windows.
                for (int i = 0; i < pm_map_size; i++) {
                    if (pm_map[i].end > ctx->window_start) {
                        uintptr_t seg_window = pm_map[i].start & ~(VM_WINDOW_SIZE - 1);
                        if (seg_window > ctx->window_start) {
                            ctx->window_start = seg_window;
                        }
                        break;
                    }
                }
                ctx->window_end   = ctx->window_start + VM_WINDOW_SIZE;
            }
            if (ctx->window_start >= pm_map[pm_map_size - 1].end) {
                // Sticky for this binding: a per-window flag would race the master's next write
                // before every peer has read it.
                __atomic_store_n(&ctx->windows_exhausted, true, __ATOMIC_RELEASE);
            }
            setup_vm_map(context, ctx->window_start, ctx->window_end);
        }
    }
    barrier_spin_wait(test_run_barrier());

    // Read the publication before a faster peer can publish a map_window() failure: a delayed
    // CPU could otherwise exit while that peer waits at the map barrier.
    stop = stop || load_test_status(context) != CONTEXT_OK;
    bool windows_exhausted = __atomic_load_n(&ctx->windows_exhausted, __ATOMIC_ACQUIRE);
    bool window_empty = (num_mapped_pages[context] == 0);
    barrier_spin_wait(test_run_barrier());

    if (stop) {
        // The whole team read the same state, so it exits together.
        return false;
    }
    if (windows_exhausted) {
        return false;
    }
    if (window_empty) {
        if (my_cpu == ctx->master_cpu_num) {
            ctx->window_index++;
        }
        // Keep the team in step before the next iteration.
        barrier_spin_wait(test_run_barrier());
        return true;
    }

    if (!map_window(context, vm_map[context][0].pm_base_addr)) {
        // Either there is no PAE or we are at the PAE limit.
        context_record_failure(context, CONTEXT_MAP_FAILED);
    }
    barrier_spin_wait(test_run_barrier());

    if (load_test_status(context) != CONTEXT_OK) {
        return false;
    }
    run_test(my_cpu, test_num, test_stage, iterations);

    // Peers may still read vm_map while leaving run_test(): wait for all of them before the
    // master publishes the next window's map.
    barrier_spin_wait(test_run_barrier());

    if (my_cpu == ctx->master_cpu_num) {
        // For the Testing line's progress field.
        __sync_fetch_and_add(&test_pages_done, num_mapped_pages[context]);
        ctx->window_index++;
    }
    return true;
}

// Window 0 covers [0, LOW_LOAD_LIMIT). Parked CPUs wait only at the phase-end global barrier.
static void run_owned_window_zero(int my_cpu, int iterations)
{
    if (cpu_is_test_participant[my_cpu]) {
        numa_window_step(my_cpu, execution_context_for_cpu(my_cpu), iterations);
    }
}

// Windows 1 and above, until the context's memory is exhausted.
static void run_owned_windows_1_plus(int my_cpu, int iterations)
{
    if (!cpu_is_test_participant[my_cpu]) {
        return;
    }
    int context = execution_context_for_cpu(my_cpu);
    while (numa_window_step(my_cpu, context, iterations)) { }
}

// NUMA_PAR counterpart of test_all_windows(): runs the current stage with one context per
// CPU-backed domain, in waves of VMEM_MAX_CONTEXTS.
static void test_all_windows_numa(int my_cpu)
{
    int iterations = test_list[test_num].iterations;
    if (pass_num == 0) {
        // Reduce iterations for a faster first pass.
        iterations /= 3;
    }

    for (;;) {
        switch (scheduler_phase) {
          case GLOBAL_STAGE_ONCE:
            LONG_BARRIER;
            if (my_cpu == 0) {
                run_global_stage_once();
                scheduler_phase = WAVE_DONE;
            }
            LONG_BARRIER;
            continue;

          case WAVE_BIND:
            LONG_BARRIER;
            // CPU 0 is the binding and reset owner, also when parked.
            if (my_cpu == 0) {
                bind_execution_wave(current_wave, test_list[test_num].cpu_mode == PAR);
                scheduler_phase = wave_has_window_zero() ? WAVE_WINDOW_0 : WAVE_WINDOWS_1_PLUS;
            }
            SHORT_BARRIER;
            continue;

          case WAVE_WINDOW_0: {
            // A map failure is latched at WAVE_FINISH; the run then falls back to legacy placement.
            if ((uintptr_t)&_start != high_load_addr) {
                relocate_all_and_resume(high_load_addr, my_cpu);
            }
            run_owned_window_zero(my_cpu, iterations);
            LONG_BARRIER;
            if (my_cpu == 0) {
                scheduler_phase = WAVE_WINDOWS_1_PLUS;
            }
            SHORT_BARRIER;
            continue;
          }

          case WAVE_WINDOWS_1_PLUS:
            if ((uintptr_t)&_start != low_load_addr) {
                relocate_all_and_resume(low_load_addr, my_cpu);
            }
            run_owned_windows_1_plus(my_cpu, iterations);
            LONG_BARRIER;
            if (my_cpu == 0) {
                scheduler_phase = WAVE_FINISH;
            }
            SHORT_BARRIER;
            continue;

          case WAVE_FINISH:
            LONG_BARRIER;
            if (my_cpu == 0) {
                // Latch fatal statuses and log terminal ones before the next wave rebinds the contexts.
                for (int context = 0; context < (int)num_bound_execution_contexts;
                     context++) {
                    int status = load_test_status(context);
                    if (   status == CONTEXT_MAP_FAILED || status == CONTEXT_MAP_OVERFLOW
                        || status == CONTEXT_TOPOLOGY_FAILED) {
                        numa_stage_failure = true;
                    }
                    if (status != CONTEXT_OK) {
                        serial_log_context_end(current_wave, context, test_contexts[context].proximity_domain_idx,
                                               status);
                    }
                }
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
