#!/usr/bin/env python3
"""Validate NUMA_PAR per-test calibration and whole-map progress on the host.

Actual domain ownership, wave binding, mapping, work ratios, calibration
storage, expected-work arithmetic and rendering are extracted from this
checkout. The pattern runner uses a deterministic tick cost per segment/window,
so its per-team totals serve as an independent oracle for the progress
arithmetic, including the low/high resident sweeps and the global fade stages.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

from c_extract import block, extract_function as function  # noqa: E402

HARNESS = r'''#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define VMEM_MAX_CONTEXTS 16
#define MAX_CPUS 256
#define MAX_PROXIMITY_DOMAINS 64
#define MAX_MEM_SEGMENTS 128
#define NUM_TEST_PATTERNS 12
#define NUM_PASS_TYPES 2
#define MAX_TEST_STAGES 12
#define TESTWORD_WIDTH 64
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define LOW_LOAD_LIMIT (4UL << 20)
#define VM_WINDOW_SIZE (1UL << 18)
#define SPIN_SIZE (1UL << 27)
#define NUMA_OFF 0
#define NUMA_ON 1
#define NUMA_PAR 2
#define ONE 0
#define PAR 2
#define TEST_WORK_WINDOW_FIXED 0
#define TEST_WORK_SEGMENT_FIXED 1
#define TEST_WORK_CLASSES 2
#define DISPLAY_MODE_NA 2
#define CONTEXT_NONE (-1)
#define CONTEXT_OK 0
#define CONTEXT_TOPOLOGY_FAILED 4
#define CONTEXT_MAP_OVERFLOW 3
#define BAR_LENGTH 40
#define MIN(a,b) ((a)<(b)?(a):(b))
#define round_down(x,a) ((x) & ~((uintptr_t)(a)-1))
#define round_up(x,a) round_down((x)+(a)-1,a)
/* SWEEP TYPE */
typedef enum {FAST_PASS,FULL_PASS} pass_type_t;
typedef uint64_t testword_t;
typedef struct {bool enabled;int cpu_mode,work_class,stages,iterations,errors;char description[80];} test_pattern_t;
typedef struct {
    unsigned int proximity_domain_idx;
    int master_cpu_num,team_cpu_count,active_cpu_count,status;
    uintptr_t window_index,window_start,window_end;
    bool windows_exhausted;
} test_context_t;
typedef struct {uintptr_t pm_base_addr;testword_t *start,*end;unsigned int proximity_domain_idx;} vm_map_t;
typedef struct {uintptr_t start,end;} pm_map_t;
typedef struct {uint64_t start,end;unsigned int proximity_domain_idx;} affinity_t;
typedef struct {int threads;} barrier_t;
static test_context_t test_contexts[16];
static vm_map_t vm_map[16][MAX_MEM_SEGMENTS];
static int vm_map_size[16];
static size_t num_mapped_pages[16];
static pm_map_t pm_map[128];
static int pm_map_size;
static uintptr_t pm_limit_lower,pm_limit_upper;
static affinity_t memory_affinity_ranges[128];
static int num_memory_affinity_ranges,num_proximity_domains,num_available_cpus,num_enabled_cpus;
static unsigned int cpu_domain[MAX_CPUS],current_context;
static bool cpu_is_global_participant[MAX_CPUS],numa_run_cpu_selected[MAX_CPUS],cpu_is_test_participant[MAX_CPUS];
static bool numa_boot_cpu_subset[MAX_CPUS];
static uint8_t cpu_execution_context[MAX_CPUS];
static int cpu_state[MAX_CPUS];
#define CPU_STATE_DISABLED 1
static uint16_t chunk_index[MAX_CPUS],test_team_chunk_index[MAX_CPUS],used_cpus_in_proximity_domain[64];
static uint16_t enabled_cpus_in_proximity_domain[64];
static uint32_t cpu_memory_domains[64];
static unsigned int num_cpu_memory_domains,num_execution_waves,current_wave,bsp_proximity_domain,num_bound_execution_contexts;
static int8_t domain_to_context[64],memory_owner_context[64];
static uint8_t context_to_domain[16];
static uint64_t work_units[TEST_WORK_CLASSES],work_denom[TEST_WORK_CLASSES];
static barrier_t context_barrier[16],legacy_barrier,*run_barrier=&legacy_barrier;
static bool dummy_run,numa_run_active,bail,restart;
static int numa_mode,display_mode,pass_num,test_num,test_stage,cpu_mode;
static char _start;
static test_memory_sweep_t test_memory_sweep;
static int ticks_per_test[2][12];
static uint64_t test_work_done,test_work_done_base,test_work_expected,pass_work_expected;
static unsigned int sleep_calls,wave_bind_calls,cases;
static int test_percentage,pass_percentage;
static test_context_t *test_context(void){return &test_contexts[current_context];}
static int test_context_index(void){return current_context;}
static int master_cpu;
static int num_active_cpus;
static unsigned int window_num;
static uintptr_t window_start,window_end;
static void run_at(uintptr_t addr,int cpu){(void)addr;(void)cpu;}
static uintptr_t low_load_addr,high_load_addr;
#define LONG_BARRIER do{}while(0)
#define SHORT_BARRIER do{}while(0)
#define trace(cpu,fmt,...) do{}while(0)
static unsigned int smp_get_proximity_domain_idx(int cpu){return cpu_domain[cpu];}
static int smp_my_cpu_num(void){return 0;}
static void stack_canary_disarm_all(void){}
static void display_reset_ui_owner(void){}
static void barrier_reset(barrier_t *barrier,int count){barrier->threads=count;}
static void display_all_active(void){}
static void display_active_cpu(int cpu){}
static void serial_log_wave_bind(unsigned int wave,unsigned int contexts){wave_bind_calls++;}
static void *first_word_mapping(uintptr_t page,int context){(void)context;return (void *)(page << PAGE_SHIFT);}
static void *last_word_mapping(uintptr_t page,size_t size,int context){(void)context;return (void *)((page << PAGE_SHIFT)+PAGE_SIZE-size);}
static bool map_window(int context,uintptr_t page){(void)context;(void)page;return true;}
static void check_input(void){}
static void update_timed_ui(void){}
static void display_test_stage_description(const char *description,int seconds){(void)description;(void)seconds;}
static void display_test_progress(int percentage){test_percentage=percentage;}
static void display_pass_progress(int percentage){pass_percentage=percentage;}
static void sleep(int seconds){sleep_calls+=seconds;}
static void fail(const char *why){fprintf(stderr,"progress pass=%d domains=%d selected=[%llu,%llu): %s\n",pass_num,num_proximity_domains,(unsigned long long)pm_limit_lower,(unsigned long long)pm_limit_upper,why);exit(1);}
static void context_record_failure(int context,int reason){test_contexts[context].status=reason;}
void calculate_chunk(testword_t **start,testword_t **end,int cpu,int segment,size_t alignment);
/* ACTUAL TEST TABLE */
static uint64_t pattern_cost(int test,int stage){return (pass_num==0?3:9)+(uint64_t)test*2+stage;}
static int run_test(int cpu,int test,int stage,int iterations) {
    int context=test_context_index();
    (void)iterations;
    if(num_mapped_pages[context]==0)return 0;
    uint64_t cost=pattern_cost(test,stage);
    uint64_t ticks=0;
    if(test_list[test].work_class==TEST_WORK_WINDOW_FIXED)ticks=cost;
    else for(int segment=0;segment<vm_map_size[context];segment++) {
        if(cpu>=0) {
            testword_t *start,*end;
            calculate_chunk(&start,&end,cpu,segment,sizeof(testword_t));
            if(end<start || start<vm_map[context][segment].start || end>vm_map[context][segment].end)fail("real context chunk outside mapped segment");
        }
        ticks+=cost;
    }
    // D counts one team tick per do_tick() from the context master,
    // independent of the team size.
    if(cpu>=0 && cpu==test_contexts[context].master_cpu_num)__sync_fetch_and_add(&test_work_done,ticks);
    return ticks;
}
/* SOURCE FUNCTIONS */
static bool window_is_in_sweep(unsigned int window) {
    return test_memory_sweep==TEST_SWEEP_ALL || (test_memory_sweep==TEST_SWEEP_LOW?window==0:window>0);
}
static void fixture(unsigned int domains,int selection,bool orphan,int pass,bool all_tests,const bool enabled[]) {
    pass_num=pass;num_proximity_domains=domains;numa_mode=NUMA_PAR;
    num_available_cpus=0;num_memory_affinity_ranges=0;
    memset(cpu_is_global_participant,0,sizeof(cpu_is_global_participant));
    memset(numa_run_cpu_selected,0,sizeof(numa_run_cpu_selected));
    memset(test_contexts,0,sizeof(test_contexts));
    uintptr_t low=LOW_LOAD_LIMIT >> PAGE_SHIFT,stride=low/domains;
    for(unsigned int domain=0;domain<domains;domain++) {
        unsigned int cores=orphan && domain==domains-1?0:1+domain%4;
        for(unsigned int core=0;core<cores;core++) {
            cpu_domain[num_available_cpus]=domain;
            cpu_is_global_participant[num_available_cpus]=true;
            numa_run_cpu_selected[num_available_cpus]=true;
            num_available_cpus++;
        }
        uintptr_t start=domain==0?1:domain*stride,end=domain==domains-1?low:(domain+1)*stride;
        memory_affinity_ranges[num_memory_affinity_ranges++]=(affinity_t){start << PAGE_SHIFT,end << PAGE_SHIFT,domain};
    }
    for(unsigned int domain=0;domain<domains;domain++) {
        uintptr_t start=domain==0?low:domain*VM_WINDOW_SIZE,end=(domain+1)*VM_WINDOW_SIZE;
        memory_affinity_ranges[num_memory_affinity_ranges++]=(affinity_t){start << PAGE_SHIFT,end << PAGE_SHIFT,domain};
    }
    num_enabled_cpus=num_available_cpus;
    pm_map_size=1;pm_map[0]=(pm_map_t){1,domains*VM_WINDOW_SIZE};
    pm_limit_lower=selection==2?low:0;
    pm_limit_upper=selection==1?low:pm_map[0].end;
    for(int test=0;test<12;test++)test_list[test].enabled=all_tests || enabled[test];
    build_numa_domain_list();
    build_work_ratios();
    if(num_execution_waves!=(num_cpu_memory_domains+15)/16)fail("domain list produced wrong wave count");
    dummy_run=true;numa_run_active=false;current_context=0;bail=restart=false;
    master_cpu=0;num_active_cpus=1;
    low_load_addr=high_load_addr=(uintptr_t)&_start;
    test_contexts[0].active_cpu_count=1;
    memset(ticks_per_test,0,sizeof(ticks_per_test));
    for(test_num=0;test_num<12;test_num++)if(test_list[test_num].enabled) {
        test_memory_sweep=test_first_sweep(test_num);
        for(test_stage=0;test_stage<test_list[test_num].stages;test_stage++) {
            if(test_stage_is_global_once(test_num,test_stage)) {
                ticks_per_test[pass_num][test_num]+=run_global_stage_once();
                continue;
            }
            current_context=0;window_num=0;window_start=window_end=0;
            test_all_windows(0);
        }
    }
    // The dummy must cover the whole selected map exactly once per test,
    // including window 0 for multi-stage tests.
    if(ticks_per_test[pass_num][2] <= 0)fail("dummy calibration produced no work");
}
static void verify_actual_work(int selection) {
    dummy_run=false;numa_run_active=true;current_context=0;bail=restart=false;
    test_work_done=0;sleep_calls=wave_bind_calls=0;
    pass_work_expected=pass_expected_work();
    unsigned int expected_sleep=0,expected_binds=0;
    uint64_t expected_total=0;
    for(test_num=0;test_num<12;test_num++)if(test_list[test_num].enabled) {
        test_memory_sweep=test_first_sweep(test_num);
        test_work_done_base=test_work_done;
        test_work_expected=test_expected_work(test_num);
        expected_total+=test_work_expected;
        do for(test_stage=0;test_stage<test_list[test_num].stages;test_stage++) {
            if(test_stage_is_global_once(test_num,test_stage)) {
                int seconds=test_list[test_num].iterations/(pass_num==0?3:1);
                int actual=run_global_stage_once();
                if(actual!=seconds)fail("global fade seconds scaled by domains or wrong sweep");
                expected_sleep+=seconds;
            } else {
                for(current_wave=0;current_wave<num_execution_waves;current_wave++) {
                    bind_execution_wave(current_wave,test_list[test_num].cpu_mode==PAR);
                    expected_binds++;
                    for(unsigned int window=0;;window++) {
                        uintptr_t start,end;window_page_bounds(window,&start,&end);
                        if(window_is_in_sweep(window))for(current_context=0;current_context<num_bound_execution_contexts;current_context++) {
                            setup_vm_map(current_context,start,end);
                            if(test_contexts[current_context].status!=CONTEXT_OK)fail("validated fixture failed mapping");
                            for(int cpu=0;cpu<num_available_cpus;cpu++)if(cpu_is_test_participant[cpu] && cpu_execution_context[cpu]==current_context)run_test(cpu,test_num,test_stage,test_list[test_num].iterations);
                        }
                        if(end>=pm_map[pm_map_size-1].end)break;
                    }
                }
            }
        } while(test_next_sweep(test_num));
        uint64_t actual=test_work_done-test_work_done_base;
        if(actual!=test_work_expected)fail("whole-test expected work differs from per-team mapped work");
        render_aggregate_progress(actual,test_work_expected,test_work_done,pass_work_expected);
        if(test_percentage!=(actual?100:0))fail("completed test progress did not reach its correct percentage");
    }
    if(test_work_done!=pass_work_expected || expected_total!=pass_work_expected)fail("per-test expected work differs from cumulative real work");
    if(pass_percentage!=100 || sleep_calls!=expected_sleep || wave_bind_calls!=expected_binds)fail("pass percentage, global fade count or wave count mismatch");
    int fade_seconds=test_list[10].iterations/(pass_num==0?3:1);
    if(test_list[10].enabled && expected_sleep!=(unsigned int)(4*fade_seconds*(selection==0?2:1)))fail("fade not once per round and selected resident sweep");
    cases++;
}
static void arithmetic_limits(void) {
    test_num=3;test_stage=0;test_list[3].stages=1;test_list[3].cpu_mode=PAR;
    test_list[3].work_class=TEST_WORK_SEGMENT_FIXED;
    numa_mode=NUMA_PAR;num_cpu_memory_domains=2;
    uint64_t weighted=(uint64_t)90000000*1024+(uint64_t)10000003*513;
    work_denom[TEST_WORK_SEGMENT_FIXED]=100000003;
    work_units[TEST_WORK_SEGMENT_FIXED]=weighted;
    ticks_per_test[0][3]=2000000000;pass_num=0;
    uint64_t oracle=(uint64_t)(((unsigned __int128)2000000000*weighted)/100000003);
    if(test_expected_work(3)!=oracle)fail("large work ratio overflowed or truncated incorrectly");
    work_denom[TEST_WORK_SEGMENT_FIXED]=0;
    if(test_expected_work(3)!=0)fail("empty work-ratio denominator did not return zero");

    // The global fade is calibrated once and expected once per sweep.
    test_num=10;test_list[10].stages=12;test_list[10].iterations=6;
    test_list[10].work_class=TEST_WORK_SEGMENT_FIXED;
    work_denom[TEST_WORK_SEGMENT_FIXED]=10;
    work_units[TEST_WORK_SEGMENT_FIXED]=20;
    ticks_per_test[0][10]=8+100;             // 4 fade stages x 2s + memory
    pm_map_size=1;pm_map[0]=(pm_map_t){1,4*VM_WINDOW_SIZE};
    pm_limit_lower=0;pm_limit_upper=pm_map[0].end;
    if(test_expected_work(10)!=200+16)fail("two-sweep fade expectation is wrong");
    pm_limit_upper=LOW_LOAD_LIMIT>>PAGE_SHIFT;
    if(test_expected_work(10)!=200+8)fail("low-only fade expectation is wrong");
    pm_limit_lower=LOW_LOAD_LIMIT>>PAGE_SHIFT;pm_limit_upper=pm_map[0].end;
    if(test_expected_work(10)!=200+8)fail("high-only fade expectation is wrong");

    render_aggregate_progress(101,100,200,100);
    if(test_percentage!=100 || pass_percentage!=100)fail("progress overrun did not clamp at 100");
    render_aggregate_progress(0,0,0,0);
    if(test_percentage || pass_percentage)fail("empty progress denominator did not render zero");
    cases+=5;
}
int main(void) {
    bool enabled[12];for(int test=0;test<12;test++)enabled[test]=test_list[test].enabled;
    const unsigned int domains[]={17,32,64};
    for(int pass=0;pass<2;pass++)for(unsigned int d=0;d<3;d++)for(int selection=0;selection<3;selection++)for(int orphan=0;orphan<2;orphan++)for(int all_tests=0;all_tests<2;all_tests++) {
        fixture(domains[d],selection,orphan,pass,all_tests,enabled);
        verify_actual_work(selection);
    }
    arithmetic_limits();
    printf("progress: %u calibrated per-test/work-ratio cases passed\n",cases);
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    main_source = (ROOT / "app/main.c").read_text()
    numa_source = (ROOT / "app/numa_par.c").read_text()
    smp_source = (ROOT / "system/smp.c").read_text()
    test_source = (ROOT / "tests/tests.c").read_text()
    chunk_source = (ROOT / "tests/test_helper.c").read_text()
    sweep = re.search(r"typedef enum \{\s*TEST_SWEEP_ALL,.*?\} test_memory_sweep_t;", main_source, re.S)
    table = re.search(r"test_pattern_t test_list\[NUM_TEST_PATTERNS\] =", test_source)
    if sweep is None or table is None:
        raise RuntimeError("Missing sweep enum or test table")
    definitions = []
    definitions += [function(smp_source,name) for name in ("smp_narrow_to_proximity_domain","smp_domain_memory_pieces_in_range","smp_domain_has_memory")]
    definitions += [function(numa_source,"numa_par_runs")]
    definitions += [function(main_source,name) for name in ("test_stage_is_global_once","window_page_bounds","selected_map_has_window_zero","selected_map_has_windows_1_plus","test_first_sweep","test_next_sweep")]
    definitions += [function(numa_source,name) for name in ("scale_work","test_expected_work","pass_expected_work","render_aggregate_progress","bind_execution_wave","build_numa_domain_list","build_work_ratios")]
    definitions += [function(chunk_source,name) for name in ("equalize_block_counts","calculate_chunk")]
    definitions += [function(main_source,name) for name in ("setup_vm_map","test_all_windows")]
    definitions += [function(numa_source,"run_global_stage_once")]
    # Core-subset picking later gives the scheduler its own selection array.
    # Use the name from the extracted source so this suite also runs at the
    # activation commit where per-test calibration first becomes available.
    selection_name = ("numa_run_cpu_selected" if re.search(r"\bnuma_run_cpu_selected\b",numa_source)
                      else "run_cpu_selected")
    harness = HARNESS.replace("numa_run_cpu_selected",selection_name)
    harness = harness.replace("/* SWEEP TYPE */",sweep.group())
    harness = harness.replace("/* ACTUAL TEST TABLE */",block(test_source,table.start())+";")
    harness = harness.replace("/* SOURCE FUNCTIONS */","\n\n".join(definitions))
    with tempfile.TemporaryDirectory(prefix="numa-progress-") as directory:
        cfile = Path(directory)/"progress.c"
        executable = Path(directory)/"progress"
        cfile.write_text(harness)
        subprocess.run([args.cc,"-std=c11","-O2",str(cfile),"-o",str(executable)],check=True)
        subprocess.run([str(executable)],check=True,timeout=30)
        # A regression that drops the whole-map ratio would make the expected
        # work equal to the dummy calibration, which differs from the per-team
        # actual work whenever a segment is split across domains.
        expression = "work_units[work_class]"
        if expression not in harness:
            raise RuntimeError("Missing work-ratio expression")
        negative = harness.replace(expression,"work_denom[work_class]",1)
        cfile.write_text(negative)
        subprocess.run([args.cc,"-std=c11","-O2",str(cfile),"-o",str(executable)],check=True)
        rejected = subprocess.run([str(executable)],capture_output=True,text=True,timeout=30)
        if rejected.returncode == 0 or "whole-test expected work differs" not in rejected.stderr:
            raise RuntimeError(f"Work-ratio negative control was not rejected correctly: {rejected.stderr}")
        print("progress negative control: missing work ratio rejected")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from None
    except subprocess.TimeoutExpired:
        raise SystemExit("progress executable exceeded its time limit") from None
