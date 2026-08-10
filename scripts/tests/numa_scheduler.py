#!/usr/bin/env python3
"""Run host-side scheduler regressions against functions in this checkout.

The harness extracts the actual C implementations. Hardware mapping, barriers,
relocation and test patterns are replaced by bounded recorders, so it validates
scheduler termination and the exact physical intervals handed to the tests.
Run with: python3 scripts/tests/numa_scheduler.py [--cc clang]
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

from c_extract import extract_function as function  # noqa: E402


HARNESS = r'''#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>

/* SWEEP TYPE */
static test_memory_sweep_t test_memory_sweep;

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define VM_WINDOW_SIZE (1UL << 18)
#define LOW_LOAD_LIMIT (4UL << 20)
#define MAX_MEM_SEGMENTS 128
#define MAX_CPUS 4
#define PAR 2
#define ONE 0
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define CONTEXT_OK 0
#define CONTEXT_MAP_FAILED 2
#define CONTEXT_MAP_OVERFLOW 3
#define CONTEXT_TOPOLOGY_FAILED 4
#define CONTEXT_NONE (-1)

typedef uint64_t testword_t;
typedef struct {
    uint32_t proximity_domain_idx;
    uintptr_t window_index,window_start,window_end;
    int master_cpu_num,team_cpu_count,active_cpu_count;
    bool windows_exhausted;
    int status;
} test_context_t;
typedef struct {
    uintptr_t pm_base_addr;
    testword_t *start,*end;
    uint32_t proximity_domain_idx;
} vm_map_t;
typedef struct {uintptr_t start,end;} pm_map_t;
static test_context_t test_contexts[16];
static vm_map_t vm_map[16][MAX_MEM_SEGMENTS];
static int vm_map_size[16];
static size_t num_mapped_pages[16];
static uint64_t test_pages_done;
static pm_map_t pm_map[MAX_MEM_SEGMENTS];
static int pm_map_size;
static uintptr_t pm_limit_lower,pm_limit_upper;
static int ticks_per_test[2][16];
static struct {int stages,iterations,cpu_mode;} test_list[16];
static bool dummy_run,numa_run_active,bail,map_succeeds=true,par_available;
static int restart;
static int numa_mode,master_cpu,test_num,test_stage,pass_num,cpu_mode;
static int num_active_cpus=1,num_enabled_cpus;
static uintptr_t low_load_addr,high_load_addr;
static void run_at(uintptr_t addr,int cpu) {(void)addr;(void)cpu;}
static int window_num;                              // NUMA_PAR keeps its window state in the context
static uintptr_t window_start,window_end;           // the legacy window loop's plain state
static int display_mode;
#define DISPLAY_MODE_NA 7
#define NUMA_ON 1
#define NUMA_PAR 2
static char _start;
static uintptr_t low_load_addr,high_load_addr;
static unsigned int num_bound_execution_contexts;   // current_wave/num_execution_waves ship with the scheduler
static unsigned int current_wave,num_execution_waves=16;
static int memory_owner_context[64];
static bool cpu_is_test_participant[MAX_CPUS];
static int enabled_cpus_in_proximity_domain[64];
static unsigned int used_cpus_in_proximity_domain[64];
static int cpu_state[MAX_CPUS];
#define CPU_STATE_DISABLED 1
static uint64_t topology_prefix_end;
static uint32_t bsp_domain;
static unsigned int fake_domains,global_stage_calls;
static unsigned int barriers,test_calls,setup_calls,binding_calls;
static jmp_buf bounded_run;
static struct {uintptr_t start,end;} visits[1024];
static int visit_count;
static const char *case_name;

static void fail(const char *message) {
    fprintf(stderr,"%s (VMEM_MAX_CONTEXTS=%d): %s\n",case_name,VMEM_MAX_CONTEXTS,message);
    exit(1);
}
static void barrier(void) {
    if (++barriers > 1000) longjmp(bounded_run,1);
}
#define LONG_BARRIER barrier()
#define SHORT_BARRIER barrier()
static void barrier_spin_wait(void *unused) {barrier();}
static void *test_run_barrier(void) {return NULL;}
static int execution_context_for_cpu(int cpu) {return 0;}
static void context_record_failure(int context,int reason) {test_contexts[context].status=reason;}
static int load_test_status(int context) {return test_contexts[context].status;}
static void *first_word_mapping(uintptr_t page,int context) {(void)context;return (void *)(page << PAGE_SHIFT);}
static void *last_word_mapping(uintptr_t page,size_t size,int context) {(void)context;return (void *)((page << PAGE_SHIFT)+PAGE_SIZE-size);}
static uint32_t smp_get_proximity_domain_idx(int cpu) {return bsp_domain;}
static bool smp_narrow_to_proximity_domain(uint64_t start,uint64_t end,uint32_t *domain,uint64_t *new_start,uint64_t *new_end) {
    if(fake_domains) {
        uintptr_t page=start >> PAGE_SHIFT, low=LOW_LOAD_LIMIT >> PAGE_SHIFT, limit;
        if(page<low) {
            uintptr_t stride=low/fake_domains;
            *domain=page/stride;
            if(*domain>=fake_domains)*domain=fake_domains-1;
            limit=*domain==fake_domains-1?low:(*domain+1)*stride;
        } else {
            *domain=page/VM_WINDOW_SIZE;
            limit=(*domain+1)*VM_WINDOW_SIZE;
        }
        if(*domain>=fake_domains)return false;
        *new_start=start;*new_end=MIN(end,(uint64_t)limit << PAGE_SHIFT);return true;
    }
    if(topology_prefix_end && start>=topology_prefix_end)return false;
    *domain=topology_prefix_end?1:0;*new_start=start;*new_end=topology_prefix_end?MIN(end,topology_prefix_end):end;return true;
}
static bool map_window(int context,uintptr_t page) {return map_succeeds;}
static bool numa_par_runs(void) {return VMEM_MAX_CONTEXTS>1 && par_available;}
static int run_test(int cpu,int test,int stage,int iterations) {
    test_calls++;
    for(int i=0;i<vm_map_size[0];i++) {
        if(visit_count >= 1024) fail("too many ranges");
        visits[visit_count].start=vm_map[0][i].pm_base_addr;
        visits[visit_count].end=((uintptr_t)vm_map[0][i].end+sizeof(testword_t)) >> PAGE_SHIFT;
        visit_count++;
    }
    return 1;
}
static void stack_canary_disarm_all(void) {}
static void display_all_active(void) {}
static void display_active_cpu(int cpu) {}
static unsigned int display_stage_calls;
static void display_test_stage_description(const char *fmt,int seconds) {(void)fmt;(void)seconds; display_stage_calls++;}
static int barrier_slot;
static void barrier_reset(void *barrier,int count) {(void)barrier;(void)count;}
static void *const run_barrier = &barrier_slot;
static void bind_execution_wave(int wave,bool parallel) {
    binding_calls++;
    test_contexts[0].active_cpu_count=parallel?MAX_CPUS:1;
    if(numa_run_active) {
        // Collapse this wave's domains into one host team. Other regression
        // suites validate real per-context CPU binding; here the actual
        // scheduler still enumerates every wave and its exact owned spans.
        memset(test_contexts,0,sizeof(test_contexts));
        test_contexts[0].master_cpu_num=0;
        test_contexts[0].team_cpu_count=1;test_contexts[0].active_cpu_count=1;
        test_contexts[0].window_index=test_memory_sweep==TEST_SWEEP_HIGH?1:0;
        cpu_is_test_participant[0]=true;
        for(unsigned int domain=0;domain<64;domain++)memory_owner_context[domain]=CONTEXT_NONE;
        for(unsigned int domain=wave*VMEM_MAX_CONTEXTS;domain<MIN((wave+1)*VMEM_MAX_CONTEXTS,fake_domains);domain++)memory_owner_context[domain]=0;
    }
}
static void relocate_all_and_resume(uintptr_t addr,int cpu) {fail("unexpected host relocation");}
static void serial_log_context_end(unsigned wave,int context,int domain,int status) {}
static uint64_t test_work_done,test_work_done_base,test_work_expected,pass_work_expected;
static void render_aggregate_progress(uint64_t done,uint64_t expected,uint64_t pass_done,uint64_t pass_expected) {}
static void update_timed_ui(void) {}
static void check_input(void) {}
static void sleep_(int seconds) {(void)seconds;}
#define sleep sleep_

/* SOURCE DEFINITIONS */

static void reset_case(const char *name,bool dummy,int mode,int stages,uintptr_t lower,uintptr_t upper) {
    case_name=name;
    memset(test_contexts,0,sizeof(test_contexts));
    memset(vm_map,0,sizeof(vm_map));
    memset(vm_map_size,0,sizeof(vm_map_size));
    memset(num_mapped_pages,0,sizeof(num_mapped_pages));
    memset(ticks_per_test,0,sizeof(ticks_per_test));
    test_memory_sweep=TEST_SWEEP_ALL;par_available=false;fake_domains=0;global_stage_calls=0;
    display_stage_calls=0;
    dummy_run=dummy;numa_run_active=false;numa_stage_failure=false;bail=false;
    numa_mode=0;master_cpu=0;test_num=0;test_stage=0;pass_num=0;cpu_mode=mode;
    test_list[0].stages=stages;test_list[0].iterations=6;test_list[0].cpu_mode=mode;
    pm_limit_lower=lower;pm_limit_upper=upper;
    low_load_addr=high_load_addr=(uintptr_t)&_start;
    barriers=test_calls=setup_calls=binding_calls=0;visit_count=0;
    current_wave=0;num_execution_waves=1;num_bound_execution_contexts=1;
    scheduler_phase=WAVE_BIND;map_succeeds=true;topology_prefix_end=0;bsp_domain=0;
    window_num=window_start=window_end=0;
}

static void verify_ranges(void) {
    int count=0;
    struct {uintptr_t start,end;} expected[MAX_MEM_SEGMENTS];
    // Legacy multi-stage tests intentionally omit window0 to avoid image copies.
    bool skip_window_zero = test_memory_sweep==TEST_SWEEP_HIGH;
    bool sweep_limited = numa_run_active;
    if (test_memory_sweep==TEST_SWEEP_ALL) {
        // The NUMA_PAR dummy calibration deliberately includes window 0 for
        // multi-stage tests, unlike the legacy multi-stage window loop.
        bool numa_dummy = dummy_run && numa_par_runs();
        skip_window_zero = numa_run_active ? !selected_map_has_window_zero()
                                           : (test_list[test_num].stages>1 && !numa_dummy);
    } else if (test_memory_sweep==TEST_SWEEP_LOW && !numa_run_active) {
        // Legacy dummy runs execute the plain legacy window loop, which does
        // not implement sweep selection: multi-stage tests skip window 0 and
        // keep walking every window in the selected map.
        skip_window_zero = test_list[test_num].stages > 1;
        sweep_limited = false;
    }
    uintptr_t minimum = skip_window_zero ? LOW_LOAD_LIMIT >> PAGE_SHIFT : 0;
    uintptr_t maximum = (sweep_limited && test_memory_sweep==TEST_SWEEP_LOW) ? MIN(pm_limit_upper,LOW_LOAD_LIMIT >> PAGE_SHIFT) : pm_limit_upper;
    if (minimum < pm_limit_lower) minimum=pm_limit_lower;
    for(int i=0;i<pm_map_size;i++) {
        uintptr_t s=pm_map[i].start,e=pm_map[i].end;
        if(s<minimum)s=minimum;
        if(e>maximum)e=maximum;
        if(s<e) {expected[count].start=s;expected[count].end=e;count++;}
    }
    int at=0;uintptr_t cursor=count?expected[0].start:0;
    for(int i=0;i<visit_count;i++) {
        if(at>=count)fail("test visited extra physical memory");
        if(visits[i].start!=cursor)fail("gap, duplicate or unsorted test range");
        if(visits[i].end<=visits[i].start || visits[i].end>expected[at].end)fail("test interval extends outside eligible map");
        cursor=visits[i].end;
        if(cursor==expected[at].end){at++;if(at<count)cursor=expected[at].start;}
    }
    if(at!=count)fail("eligible memory omitted");
    if(numa_run_active) {
        if(scheduler_phase!=WAVE_DONE)fail("scheduler did not complete");
        if(binding_calls!=num_execution_waves)fail("unexpected execution wave count");
    } else if(binding_calls!=0)fail("legacy path must not bind execution waves");
}

static void run_case(const char *name,bool dummy,int mode,int stages,uintptr_t lower,uintptr_t upper,int pass) {
    reset_case(name,dummy,mode,stages,lower,upper);
    pass_num=pass;
    if(setjmp(bounded_run)!=0)fail("scheduler exceeded bounded barrier count");
    if(numa_run_active) test_all_windows_numa(0); else test_all_windows(0);
    verify_ranges();
    if(dummy_run && ticks_per_test[pass_num][test_num]!=(int)test_calls)fail("dummy calibration tick count differs from tested windows");
}

static void direct_window_regressions(void) {
    reset_case("map failure",false,PAR,1,0,128);
    pm_map_size=1;pm_map[0]=(pm_map_t){1,128};map_succeeds=false;
    test_all_windows(0);
    if(test_calls || visit_count || window_num!=0) fail("failed mapping tested or advanced memory");
    reset_case("map recovery",false,PAR,1,0,128);
    pm_map_size=1;pm_map[0]=(pm_map_t){1,128};
    test_all_windows(0);
    if(!test_calls || visit_count==0 || window_num==0) fail("successful mapping skipped testing");
}

static void topology_gap_regressions(void) {
    if(VMEM_MAX_CONTEXTS<=1)return;
    reset_case("NUMA_ON covered prefix and gap",false,PAR,1,0,8);
    pm_map_size=1;pm_map[0]=(pm_map_t){1,8};numa_mode=NUMA_ON;
    topology_prefix_end=4*PAGE_SIZE;bsp_domain=3;used_cpus_in_proximity_domain[1]=1;
    setup_vm_map(0,0,8);
    if(vm_map_size[0]!=2 || num_mapped_pages[0]!=7)fail("gap fallback duplicated or omitted pages");
    if(vm_map[0][0].pm_base_addr!=1 || ((uintptr_t)vm_map[0][0].end+sizeof(testword_t))>>PAGE_SHIFT!=4 || vm_map[0][0].proximity_domain_idx!=1)fail("covered prefix changed");
    if(vm_map[0][1].pm_base_addr!=4 || ((uintptr_t)vm_map[0][1].end+sizeof(testword_t))>>PAGE_SHIFT!=8 || vm_map[0][1].proximity_domain_idx!=3)fail("remaining gap not assigned once to BSP");
    reset_case("NUMA_PAR rejects uncovered topology",false,PAR,1,0,8);
    pm_map_size=1;pm_map[0]=(pm_map_t){1,8};numa_run_active=true;
    topology_prefix_end=4*PAGE_SIZE;memory_owner_context[1]=0;
    setup_vm_map(0,0,8);
    if(test_contexts[0].status!=CONTEXT_TOPOLOGY_FAILED || num_mapped_pages[0]!=3)fail("NUMA_PAR topology gap did not fail explicitly");
}

static unsigned int numa_sweep_regressions(void) {
    if(VMEM_MAX_CONTEXTS<=1)return 0;
    unsigned int cases=0;
    const unsigned int domains[]={17,32,64};
    for(int pass=0;pass<2;pass++)for(unsigned int d=0;d<3;d++)for(int dummy=0;dummy<2;dummy++)for(int mode=ONE;mode<=PAR;mode+=PAR-ONE)for(int pattern=0;pattern<2;pattern++)for(int region=0;region<3;region++) {
        pm_map_size=1;pm_map[0]=(pm_map_t){1,domains[d]*VM_WINDOW_SIZE};
        uintptr_t low=LOW_LOAD_LIMIT >> PAGE_SHIFT;
        uintptr_t lower=region==2?low:0,upper=region==1?low:pm_map[0].end;
        reset_case("NUMA multi-stage resident sweeps",dummy,mode,pattern?12:2,lower,upper);
        pass_num=pass;test_num=pattern?10:2;
        test_list[test_num]=test_list[0];
        par_available=true;numa_mode=NUMA_PAR;numa_run_active=!dummy;
        fake_domains=domains[d];
        test_memory_sweep=test_first_sweep(test_num);
        unsigned int sweeps=0,total_calls=0;
        do {
            sweeps++;
            for(test_stage=0;test_stage<test_list[test_num].stages;test_stage++) {
                current_wave=0;num_execution_waves=dummy?1:(fake_domains+VMEM_MAX_CONTEXTS-1)/VMEM_MAX_CONTEXTS;
                barriers=test_calls=binding_calls=0;visit_count=0;
                memset(test_contexts,0,sizeof(test_contexts));
                window_num=window_start=window_end=0;
                numa_run_active=!dummy;
                scheduler_phase=test_stage_is_global_once(test_num,test_stage)?GLOBAL_STAGE_ONCE:WAVE_BIND;
                if(setjmp(bounded_run)!=0)fail("NUMA sweep exceeded bounded barrier count");
                if(numa_run_active) test_all_windows_numa(0); else test_all_windows(0);
                if(dummy) total_calls+=test_calls;
                else if(test_stage_is_global_once(test_num,test_stage)) {
                    if(visit_count || scheduler_phase!=WAVE_DONE || binding_calls)fail("fade stage touched memory or bound a wave");
                } else verify_ranges();
            }
        } while(test_next_sweep(test_num));
        if(sweeps!=(dummy?1U:(region==0?2U:1U)))fail("incorrect number of selected memory sweeps");
        if(dummy && ticks_per_test[pass_num][test_num]!=(int)total_calls)fail("dummy calibration tick count differs from tested windows");
        if(display_stage_calls!=(pattern && !dummy?4*sweeps:0))fail("fade did not run once per round and resident sweep");
        cases++;
    }
    return cases;
}

int main(void) {
    direct_window_regressions();
    topology_gap_regressions();
    unsigned cases=4+(VMEM_MAX_CONTEXTS>1?2:0);
    for(int pass=0;pass<2;pass++)for(int dummy=0;dummy<2;dummy++)for(int mode=ONE;mode<=PAR;mode+=PAR-ONE)for(int stages=1;stages<=2;stages++) {
        pm_map_size=1;pm_map[0]=(pm_map_t){1,128};
        run_case("small map",dummy,mode,stages,0,128,pass);cases++;
        pm_map[0]=(pm_map_t){1,3*VM_WINDOW_SIZE+123};
        run_case("3 GiB+ map",dummy,mode,stages,0,pm_map[0].end,pass);cases++;
        run_case("selected upper",dummy,mode,stages,0,VM_WINDOW_SIZE+17,pass);cases++;
        run_case("selected lower above low region",dummy,mode,stages,LOW_LOAD_LIMIT>>PAGE_SHIFT,pm_map[0].end,pass);cases++;
        run_case("selected interior window",dummy,mode,stages,VM_WINDOW_SIZE+9,2*VM_WINDOW_SIZE+11,pass);cases++;
        run_case("empty selected map",dummy,mode,stages,4*VM_WINDOW_SIZE,5*VM_WINDOW_SIZE,pass);cases++;
        pm_map_size=3;pm_map[0]=(pm_map_t){1,100};pm_map[1]=(pm_map_t){VM_WINDOW_SIZE+17,VM_WINDOW_SIZE+23};pm_map[2]=(pm_map_t){3*VM_WINDOW_SIZE+4,3*VM_WINDOW_SIZE+32};
        run_case("empty windows and physical gaps",dummy,mode,stages,0,pm_map[2].end,pass);cases++;
    }
    cases+=numa_sweep_regressions();
    printf("scheduler VMEM_MAX_CONTEXTS=%d: %u cases passed\n",VMEM_MAX_CONTEXTS,cases);
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    main_source = (ROOT / "app/main.c").read_text()
    numa_source = (ROOT / "app/numa_par.c").read_text()
    enum = re.search(r"typedef enum \{\s*GLOBAL_STAGE_ONCE,.*?\} test_scheduler_phase_t;", numa_source, re.S)
    if enum is None:
        raise RuntimeError("Missing scheduler phase enum")
    names_main = [
        "test_stage_is_global_once", "setup_vm_map", "window_page_bounds",
        "selected_map_has_window_zero", "selected_map_has_windows_1_plus",
        "test_first_sweep", "test_next_sweep", "test_all_windows",
    ]
    names_numa = [
        "wave_has_window_zero", "run_global_stage_once", "numa_window_step",
        "run_owned_window_zero", "run_owned_windows_1_plus", "test_all_windows_numa",
    ]
    definitions = (enum.group() + "\nstatic test_scheduler_phase_t scheduler_phase;\nstatic bool numa_stage_failure;\n"
                   + "\n\n".join(function(main_source, name) for name in names_main)
                   + "\n\n".join(function(numa_source, name) for name in names_numa))
    sweep_enum = re.search(r"typedef enum \{\s*TEST_SWEEP_ALL,.*?\} test_memory_sweep_t;", main_source, re.S)
    if sweep_enum is None:
        raise RuntimeError("Missing memory sweep enum")
    harness = HARNESS.replace("/* SWEEP TYPE */", sweep_enum.group()).replace("/* SOURCE DEFINITIONS */", definitions)
    with tempfile.TemporaryDirectory(prefix="numa-scheduler-") as temporary:
        temporary = Path(temporary)
        cfile = temporary / "scheduler.c"
        cfile.write_text(harness)
        for contexts in (1, 16):
            executable = temporary / f"scheduler-{contexts}"
            subprocess.run([args.cc, "-std=c11", "-O2", f"-DVMEM_MAX_CONTEXTS={contexts}", str(cfile), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from None
    except subprocess.TimeoutExpired:
        raise SystemExit("scheduler executable exceeded its time limit") from None
