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
}
