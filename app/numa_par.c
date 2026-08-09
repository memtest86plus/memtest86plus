// NUMA_PAR scheduler machinery. Included by main.c after the shared
// helpers and the domain state; dispatched only under numa_run_active.

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

static bool numa_window_step(int my_cpu, int context, int iterations);

// Master/CPU0-only: binds the current wave's CPU-backed domains to distinct
// execution contexts, configures their barriers, and publishes the dense
// chunk indexes. Runs only between two global barriers.
static void bind_execution_wave(int wave, bool parallel_test)
{
    if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
        // CPUs not taking part in this stage won't re-arm their stack
        // canaries, and the coming relocations will invalidate them.
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

            // The preceding global barrier guarantees the prior user of this
            // reusable context is quiescent. Status is an atomic object and
            // is not reset with a raw struct memset.
            test_contexts[context].proximity_domain_idx = domain;
            test_contexts[context].master_cpu_num = -1;
            test_contexts[context].team_cpu_count = 0;
            test_contexts[context].active_cpu_count = 0;
            test_contexts[context].window_index = 0;
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
                // In a parallel test every team CPU enters the test-internal
                // barrier; a sequential test only the context master does.
                cpu_is_test_participant[cpu] = parallel_test || cpu == test_contexts[context].master_cpu_num;
            } else {
                // Parked CPUs (later waves, CPU-only domains, disabled CPUs)
                // use context 0 only for identity-mapped control flow; they
                // never join context 0's test barrier.
                cpu_execution_context[cpu] = 0;
            }
        }

        // The wave ownership table: every domain's memory is owned by
        // exactly one context in exactly one wave. setup_vm_map() and the
        // work statistics both consult memory_owner_context[], so each
        // piece is mapped, tested and accounted exactly once per stage,
        // and no two teams ever alias the same physical range.
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

        // CPU 0 is the only reset owner, and every other CPU is still held
        // at the pre-publication global rendezvous.
        assert(!dummy_run);   // calibration never binds NUMA_PAR waves
        for (int context = 0; context < (int)num_contexts_in_wave; context++) {
            test_context_t *ctx = &test_contexts[context];
            assert(ctx->team_cpu_count > 0);
            assert(ctx->master_cpu_num >= 0);
            ctx->active_cpu_count = parallel_test ? ctx->team_cpu_count : 1;
            barrier_reset(&context_barrier[context], ctx->active_cpu_count);
        }

        num_bound_execution_contexts = num_contexts_in_wave;
        display_reset_ui_owner();

        // Wave-boundary invariant checks (debug-time validation); every
        // active context barrier was quiescent before barrier_reset().
        assert(num_contexts_in_wave >= 1 && num_contexts_in_wave <= VMEM_MAX_CONTEXTS);
        for (int context = 0; context < (int)num_contexts_in_wave; context++) {
            assert(test_contexts[context].master_cpu_num >= 0);
            assert(test_contexts[context].team_cpu_count > 0);
            assert(   test_contexts[context].active_cpu_count >= 1
                   && test_contexts[context].active_cpu_count <= test_contexts[context].team_cpu_count);
        }

        // The Using: field would otherwise stay blank in NUMA_PAR; fill it
        // like the legacy path does (IMC/SPD mode keeps its own display).
        if (display_mode == DISPLAY_MODE_NA) {
            display_all_active();
        }

        // Emit the lifecycle boundary once the binding and context count are
        // final, so concurrent multi-wave serial logs stay unambiguous.
        serial_log_wave_bind(current_wave, num_contexts_in_wave);

        return;
    }

    // Legacy modes: publish the active CPU count and reconfigure the legacy
    // run_barrier with it.
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

// Materializes the domain metadata after the CPU selection snapshot has been
// published at a run boundary. Produces cpu_memory_domains[] (BSP first),
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

    // Build the ordered list of domains that need an execution team, BSP
    // first: the BSP domain heads the list and owns context 0 in wave 0.
    // The BSP itself cannot be disabled, so the BSP domain always has at
    // least one enabled CPU. A memory-only domain (no enabled CPU) is
    // deliberately omitted here: its memory is owned by the BSP context in
    // wave 0. A CPU-only domain (no memory) is parked entirely.
    bsp_proximity_domain = smp_get_proximity_domain_idx(0);
    num_cpu_memory_domains = 0;

    // If any orphaned memory domain exists, the BSP domain is included in
    // wave 0 even when the BSP domain itself reports no local memory,
    // otherwise there would be no live BSP context to own those ranges.
    bool have_orphan_memory = false;
    for (int domain = 0; domain < num_proximity_domains; domain++) {
        if (domain != (int)bsp_proximity_domain && smp_domain_has_memory(domain) && enabled_cpus_in_proximity_domain[domain] == 0) {
            have_orphan_memory = true;
            break;
        }
    }
    if (smp_domain_has_memory(bsp_proximity_domain) || have_orphan_memory) {
        cpu_memory_domains[num_cpu_memory_domains++] = bsp_proximity_domain;
    }

    for (int domain = 0; domain < num_proximity_domains; domain++) {
        // Orphaned memory domains are covered by the BSP context in wave 0
        // and must not get their own execution context.
        if (domain != (int)bsp_proximity_domain && smp_domain_has_memory(domain) && enabled_cpus_in_proximity_domain[domain] != 0) {
            cpu_memory_domains[num_cpu_memory_domains++] = domain;
        }
    }

    num_execution_waves = (num_cpu_memory_domains + VMEM_MAX_CONTEXTS - 1) / VMEM_MAX_CONTEXTS;
}


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
                ctx->window_end  += VM_WINDOW_SIZE;
            }
            if (ctx->window_start >= pm_map[pm_map_size - 1].end) {
                // The window enumeration is exhausted. The sticky flag is
                // published by the context barrier below and never rewritten
                // during this binding, so every participant reads the same
                // end-of-enumeration decision (a per-window value would race
                // the master's next write).
                __atomic_store_n(&ctx->windows_exhausted, true, __ATOMIC_RELEASE);
            }
            setup_vm_map(context, ctx->window_start, ctx->window_end);
        }
    }
    barrier_spin_wait(test_run_barrier());

    // Finish reading this publication before a faster peer can enter
    // map_window and publish a failure for the next phase. Otherwise a
    // delayed CPU could leave here while that peer waits at the map barrier.
    stop = stop || load_test_status(context) != CONTEXT_OK;
    bool windows_exhausted = __atomic_load_n(&ctx->windows_exhausted, __ATOMIC_ACQUIRE);
    bool window_empty = (num_mapped_pages[context] == 0);
    barrier_spin_wait(test_run_barrier());

    if (stop) {
        // Every team member evaluates the same published state after the
        // same barrier generation, so the whole context exits together; a
        // lone early exit would strand the others in the next tick barrier.
        return false;
    }
    if (windows_exhausted) {
        // No more windows for this context.
        return false;
    }
    if (window_empty) {
        // No memory to test in this window.
        if (my_cpu == ctx->master_cpu_num) {
            ctx->window_index++;
        }
        // Keep the empty-window path at a common step boundary before any
        // CPU starts the next iteration.
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

    // The last test tick only synchronises the memory accesses before it.
    // Peers may still read vm_map or vm_map_size while leaving their test
    // loops, so wait for every run_test call to return before the master
    // publishes the next window's map.
    barrier_spin_wait(test_run_barrier());

    if (my_cpu == ctx->master_cpu_num) {
        ctx->window_index++;
    }
    return true;
}

static bool run_owned_window_zero(int my_cpu, bool i_am_master, bool i_am_active, int iterations)
{
    if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
        // The context master computes window 0 and its map; the context
        // barrier publishes them. Parked CPUs wait only at the phase-end
        // global barrier.
        if (cpu_is_test_participant[my_cpu]) {
            numa_window_step(my_cpu, execution_context_for_cpu(my_cpu), iterations);
        }
        return true;
    }

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
    if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
        if (!cpu_is_test_participant[my_cpu]) {
            return;
        }
        int context = execution_context_for_cpu(my_cpu);
        while (numa_window_step(my_cpu, context, iterations)) { }
        return;
    }

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

        running = run_test_window(my_cpu, i_am_master, i_am_active, dummy_run, iterations);
    } while (running && test_contexts[0].window_end < pm_map[pm_map_size - 1].end);
}

static bool wave_has_window_zero(void)
{
    if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
        return selected_map_has_window_zero();
    }

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

static void test_all_windows_numa(int my_cpu)
{
    bool parallel_test = false;
    bool i_am_master = (my_cpu == master_cpu);
    bool i_am_active = i_am_master;
    if (!dummy_run) {
        // In NUMA_PAR the test-pattern CPU mode decides: PAR patterns use
        // every CPU of each context team, ONE patterns use the fixed context
        // master, independent of the (hidden) legacy CPU sequencing mode.
        if (test_list[test_num].cpu_mode == PAR && ((VMEM_MAX_CONTEXTS > 1 && numa_run_active) || cpu_mode == PAR)) {
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
            // runs once on CPU 0 between memory-touching phases; the other
            // global participants wait at the surrounding barriers. No
            // context is bound and no context barrier is entered here.
            LONG_BARRIER;
            if (my_cpu == 0) {
                run_global_stage_once();
                scheduler_phase = WAVE_DONE;
            }
            LONG_BARRIER;
            continue;

          case WAVE_BIND:
            LONG_BARRIER;
            if (VMEM_MAX_CONTEXTS > 1 && numa_run_active) {
                // CPU 0 is the binding and reset owner, also when parked.
                if (my_cpu == 0) {
                    bind_execution_wave(current_wave, parallel_test);
                    scheduler_phase = wave_has_window_zero() ? WAVE_WINDOW_0 : WAVE_WINDOWS_1_PLUS;
                }
            } else if (i_am_master) {
                bind_execution_wave(0, parallel_test);
                scheduler_phase = wave_has_window_zero() ? WAVE_WINDOW_0 : WAVE_WINDOWS_1_PLUS;
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
                // Promote any fatal context status to the stage-global
                // failure latch before the next wave rebinds these contexts,
                // and report every terminal status with its wave/domain
                // identity while it is still available.
                for (int context = 0; context < (int)num_bound_execution_contexts;
                     context++) {
                    int status = load_test_status(context);
                    if (status == CONTEXT_MAP_FAILED || status == CONTEXT_MAP_OVERFLOW || status == CONTEXT_TOPOLOGY_FAILED) {
                        numa_stage_failure = true;
                    }
                    if (status != CONTEXT_OK) {
                        serial_log_context_end(current_wave, context, test_contexts[context].proximity_domain_idx, status);
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
