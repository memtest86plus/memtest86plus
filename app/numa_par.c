extern int pass_bar_length;
extern int test_bar_length;

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif


// Per-execution-owner test-work statistics over the actually tested map
// (pm_map ∩ selected range, split by the scheduler's windows), computed
// once per run boundary and used for the exact expected-work accounting.
// Memory-only domains are attributed to the BSP owner while the
// statistics are gathered, so every owner in cpu_memory_domains[]
// corresponds to one execution context. The whole-map totals are what the
// dummy calibration measures. The per-owner counts are bounded by the
// 64-domain SRAT cap times the 1 GiB windows (about 2^28 segments and
// 2^18 windows per owner even at 256 TB per domain), so 32 bits are ample.
static uint32_t owner_work_segments[MAX_PROXIMITY_DOMAINS];
static uint32_t owner_work_windows[MAX_PROXIMITY_DOMAINS];
static uint32_t whole_map_segments = 0;
static uint32_t whole_map_windows = 0;

// The tick-block accounting only applies where NUMA_PAR can run; on i586
// (32-bit testwords) the equality would not hold.
#if VMEM_MAX_CONTEXTS > 1
_Static_assert((VM_WINDOW_SIZE << PAGE_SHIFT) == (uintptr_t)SPIN_SIZE * sizeof(testword_t),
               "tick block must span at least a whole window");
#endif

// The expected aggregate work for one logical test stage: the stage's own
// dummy calibration, scaled by the per-context share of the tick structure
// over the actually tested map (pm_map ∩ selected range, split by the
// scheduler's windows). Every segment/window is counted exactly once in
// exactly one wave; memory-only domains were attributed to their wave-0
// owner, the BSP context, when the statistics were gathered.
static uint64_t stage_expected_work(int test, int stage)
{
    pass_type_t pass_type = (pass_num == 0) ? FAST_PASS : FULL_PASS;
    uint64_t cal = (uint64_t)ticks_per_stage[pass_type][test][stage];

    // The bit-fade fade delay is a global-once stage: its calibration is the
    // measured delay seconds, accounted one work unit per elapsed second on
    // CPU 0, independent of the memory partition.
    if (test_stage_is_global_once(test, stage)) {
        return cal;
    }

    bool parallel = test_list[test].cpu_mode == PAR;
    int work_class = test_list[test].work_class;

    // The class is constant for this stage, so the per-domain loop only ever
    // reads one of the two per-owner arrays; select it once.
    uint32_t *owner_work = (work_class == TEST_WORK_WINDOW_FIXED) ? owner_work_windows : owner_work_segments;

    uint64_t weighted = 0;
    for (unsigned int wave = 0; wave < num_execution_waves; wave++) {
        unsigned int first = wave * VMEM_MAX_CONTEXTS;
        unsigned int limit = MIN(first + VMEM_MAX_CONTEXTS, num_cpu_memory_domains);
        for (unsigned int i = first; i < limit; i++) {
            uint32_t domain = cpu_memory_domains[i];
            uint64_t active = parallel ? enabled_cpus_in_proximity_domain[domain] : 1;
            weighted += owner_work[domain] * active;
        }
    }

    uint64_t denom = (work_class == TEST_WORK_WINDOW_FIXED) ? whole_map_windows : whole_map_segments;
    if (denom == 0) {
        return 0;
    }
    // Exact 64-bit weighted average: the raw product cal * weighted can
    // reach about 2^66 on extreme hardware (2^18 windows x 128 segments,
    // up to 64 domain pieces per segment, 1024 active CPUs), so split the
    // division: weighted/denom <= 2^16 and weighted % denom < denom keep
    // both terms below 2^56. The 128-bit division helper is not linkable
    // in this freestanding build.
    uint64_t q = weighted / denom;
    uint64_t r = weighted % denom;
    return (uint64_t)cal * q + ((uint64_t)cal * r) / denom;
}

// The expected aggregate work for the whole pass: the sum over every
// enabled test and stage, so the pass percentage is whole-pass progress.
static uint64_t pass_expected_work(void)
{
    uint64_t expected = 0;
    for (int test = 0; test < NUM_TEST_PATTERNS; test++) {
        if (test_list[test].enabled) {
            for (int stage = 0; stage < test_list[test].stages; stage++) {
                expected += stage_expected_work(test, stage);
            }
        }
    }
    return expected;
}

// Renders NUMA_PAR aggregate progress from the shared work counters: the
// test fields use the current stage, the pass fields the cumulative bases.
void render_aggregate_progress(uint64_t done, uint64_t expected, uint64_t pass_done, uint64_t pass_expected)
{
    int pct = 0;
    if (expected > 0) {
        pct = (int)((done * 100) / expected);
        if (pct > 100) {
            pct = 100;
        }
    }
    display_test_percentage(pct);
    display_test_bar((BAR_LENGTH * pct) / 100);

    pct = 0;
    if (pass_expected > 0) {
        pct = (int)((pass_done * 100) / pass_expected);
        if (pct > 100) {
            pct = 100;
        }
    }
    display_pass_percentage(pct);
    display_pass_bar((BAR_LENGTH * pct) / 100);
}

// True when the current run executes (or, during the dummy calibration,
// will execute) the NUMA_PAR scheduler: NUMA_PAR is selected, the mapping
// model was validated (vmem_prepare_execution_contexts() before AP startup
// downgraded NUMA_PAR on failure), and enough selected CPU-backed memory
// domains exist to form independent teams.
static bool numa_par_runs(void)
{
    return VMEM_MAX_CONTEXTS > 1 && numa_mode == NUMA_PAR && num_cpu_memory_domains >= 2;
}

// Master/CPU0-only: binds the current wave's CPU-backed domains to distinct
// execution contexts, configures their barriers, and publishes the dense
// chunk indexes. Runs only between two global barriers.
static void bind_execution_wave(int wave, bool parallel_test)
{
    if (numa_run_active) {
        // CPUs not taking part in this stage won't re-arm their stack
        // canaries, and the coming relocations will invalidate them.
        stack_canary_disarm_all();

        memset(domain_to_context, CONTEXT_NONE, sizeof(domain_to_context));
        memset(context_to_domain, 0, sizeof(context_to_domain));
        memset(cpu_is_test_participant, 0, sizeof(cpu_is_test_participant));

        unsigned int first = wave * VMEM_MAX_CONTEXTS;
        unsigned int limit = MIN(first + VMEM_MAX_CONTEXTS, num_cpu_memory_domains);
        unsigned int num_contexts_in_wave = 0;

        for (unsigned int i = first; i < limit; i++) {
            uint32_t domain = cpu_memory_domains[i];
            int context = num_contexts_in_wave++;
            domain_to_context[domain] = context;
            context_to_domain[context] = domain;

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
            test_contexts[context].windows_exhausted = false;
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
            // Re-point this context's translation roots at the current
            // image: after a relocation, a context that was not bound to a
            // running CPU still points at the other image copy, which
            // memory testing overwrites.
            vmem_rebase_execution_context(context);
        }

        num_bound_execution_contexts = num_contexts_in_wave;
        for (int cpu = 0; cpu < num_available_cpus; cpu++) {
            uint32_t domain = smp_get_proximity_domain_idx(cpu);
            int context = domain_to_context[domain];
            bool in_team =    cpu_is_global_participant[cpu]
                           && run_cpu_selected[cpu]
                           && context != CONTEXT_NONE;
            if (in_team) {
                cpu_is_test_participant[cpu] = parallel_test || cpu == test_contexts[context].master_cpu_num;
            }
        }

        // Wave-boundary invariant checks (debug-time validation); every
        // active context barrier was quiescent before barrier_reset().
        assert(num_contexts_in_wave >= 1 && num_contexts_in_wave <= VMEM_MAX_CONTEXTS);
        for (int context = 0; context < (int)num_contexts_in_wave; context++) {
            assert(test_contexts[context].master_cpu_num >= 0);
            assert(test_contexts[context].team_cpu_count > 0);
            assert(   test_contexts[context].active_cpu_count >= 1
                   && test_contexts[context].active_cpu_count <= test_contexts[context].team_cpu_count);
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

    // Compute the per-owner test-work statistics over the actually tested
    // map (pm_map ∩ selected range, split by the scheduler's windows), used
    // by the exact expected-work accounting. The whole-map totals are what
    // the dummy calibration measures: the dummy's legacy map clips every
    // span to the same windows and limits, and every window is at most one
    // tick block (see the _Static_assert above), so the whole-map segment
    // total equals the per-stage calibration.
    memset(owner_work_segments, 0, sizeof(owner_work_segments));
    memset(owner_work_windows, 0, sizeof(owner_work_windows));
    whole_map_segments = 0;
    whole_map_windows = 0;
    bool owner_in_window[MAX_PROXIMITY_DOMAINS] = { false };

    for (uint64_t window_index = 0; ; window_index++) {
        uintptr_t win_start, win_end;
        window_page_bounds(window_index, &win_start, &win_end);

        // Reduce the window to the selected range, like setup_vm_map().
        // The unclipped end still drives the termination check below, so
        // the iteration always covers the whole map.
        uintptr_t window_end = win_end;
        if (win_start < pm_limit_lower) {
            win_start = pm_limit_lower;
        }
        if (win_end > pm_limit_upper) {
            win_end = pm_limit_upper;
        }

        if (win_start < win_end) {
            bool window_has_memory = false;
            for (int seg = 0; seg < pm_map_size; seg++) {
                uintptr_t s = pm_map[seg].start;
                uintptr_t e = pm_map[seg].end;
                if (s <= win_start) {
                    s = win_start;
                }
                if (e >= win_end) {
                    e = win_end;
                }
                if (s < e) {
                    whole_map_segments++;
                    for (int d = 0; d < num_proximity_domains; d++) {
                        uint32_t pieces =
                            smp_domain_memory_pieces_in_range(d, s, e);
                        if (pieces == 0) {
                            continue;
                        }
                        window_has_memory = true;
                        // Attribute to the execution owner: a memory-only
                        // domain belongs to the BSP context in wave 0.
                        int owner = (enabled_cpus_in_proximity_domain[d] > 0) ? d : (int)bsp_proximity_domain;
                        owner_work_segments[owner] += pieces;
                        owner_in_window[owner] = true;
                    }
                }
            }
            if (window_has_memory) {
                whole_map_windows++;
            }
        }

        // Window-fixed tests tick once per nonempty owner/window pair, so
        // each owner is marked at most once per window, even when several
        // of its owned domains overlap there.
        for (int d = 0; d < num_proximity_domains; d++) {
            if (owner_in_window[d]) {
                owner_work_windows[d]++;
                owner_in_window[d] = false;
            }
        }
        if (window_end >= pm_map[pm_map_size - 1].end) {
            break;
        }
    }
}
