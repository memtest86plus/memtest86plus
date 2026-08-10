#!/usr/bin/env python3
"""Validate current NUMA sweep calibration and aggregate progress on the host.

Actual domain ownership, wave binding, mapping, calibration storage, chunking,
expected-work arithmetic and rendering are extracted from this checkout. The
pattern runner uses a deterministic tick cost per segment/window, allowing its
per-CPU totals to serve as an independent oracle for the progress arithmetic.
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
static uint16_t chunk_index[MAX_CPUS],test_team_chunk_index[MAX_CPUS],used_cpus_in_proximity_domain[64];
static uint16_t enabled_cpus_in_proximity_domain[64];
static uint32_t cpu_memory_domains[64];
static unsigned int num_cpu_memory_domains,num_execution_waves,current_wave,bsp_proximity_domain,num_bound_execution_contexts;
static int8_t domain_to_context[64],memory_owner_context[64];
static uint8_t context_to_domain[16];
static uint64_t owner_weighted_par[TEST_WORK_CLASSES][NUM_TEST_SWEEPS],owner_weighted_seq[TEST_WORK_CLASSES][NUM_TEST_SWEEPS];
static uint32_t whole_map_denom[TEST_WORK_CLASSES][NUM_TEST_SWEEPS];
static barrier_t context_barrier[16],legacy_barrier,*run_barrier=&legacy_barrier;
static bool dummy_run,numa_run_active,bail,restart;
static int numa_mode,display_mode,pass_num,test_num,test_stage;
static test_memory_sweep_t test_memory_sweep;
static int ticks_per_test[2][12],ticks_per_sweep_stage[2][12][NUM_TEST_SWEEPS][12];
static uint64_t test_work_done,test_work_done_base,test_work_expected,pass_work_expected;
static unsigned int sleep_calls,wave_bind_calls,cases;
static int test_percentage,pass_percentage;
static test_context_t *test_context(void){return &test_contexts[current_context];}
static int test_context_index(void){return current_context;}
#define master_cpu (test_context()->master_cpu_num)
#define num_active_cpus (test_context()->active_cpu_count)
#define window_num (test_context()->window_index)
static unsigned int smp_get_proximity_domain_idx(int cpu){return cpu_domain[cpu];}
static int smp_my_cpu_num(void){return 0;}
static void stack_canary_disarm_all(void){}
static void display_reset_ui_owner(void){}
static void vmem_rebase_execution_context(int context){}
static void barrier_reset(barrier_t *barrier,int count){barrier->threads=count;}
static void display_all_active(void){}
static void display_active_cpu(int cpu){}
static void serial_log_wave_bind(unsigned int wave,unsigned int contexts){wave_bind_calls++;}
static void *first_word_mapping(uintptr_t page){return (void *)(page << PAGE_SHIFT);}
static void *last_word_mapping(uintptr_t page,size_t size){return (void *)((page << PAGE_SHIFT)+PAGE_SIZE-size);}
static bool map_window(int context,uintptr_t page){return true;}
static void check_input(void){}
static void update_timed_ui(void){}
static void display_test_stage_description(const char *description,int seconds){}
static void display_test_percentage(int percentage){test_percentage=percentage;}
static void display_pass_percentage(int percentage){pass_percentage=percentage;}
static void display_test_bar(int length){}
static void display_pass_bar(int length){}
static void display_test_progress(int percentage){test_percentage=percentage;}
static void display_pass_progress(int percentage){pass_percentage=percentage;}
static void sleep(int seconds){sleep_calls+=seconds;}
static void fail(const char *why){fprintf(stderr,"accounting pass=%d domains=%d selected=[%llu,%llu): %s\n",pass_num,num_proximity_domains,(unsigned long long)pm_limit_lower,(unsigned long long)pm_limit_upper,why);exit(1);}
static void context_record_failure(int context,int reason){test_contexts[context].status=reason;}
void calculate_chunk(testword_t **start,testword_t **end,int cpu,int segment,size_t alignment);
/* ACTUAL TEST TABLE */
static uint64_t pattern_cost(int test,int stage){return (pass_num==0?3:9)+(uint64_t)test*2+stage;}
static int run_test(int cpu,int test,int stage,int iterations) {
    int context=test_context_index();
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
    if(cpu>=0)__sync_fetch_and_add(&test_work_done,ticks);
    return ticks;
}
/* SOURCE FUNCTIONS */
static void calibrate_global_stage(void) {
    int ticks=run_global_stage_once();
    /* ACTUAL GLOBAL CALIBRATION GUARD */
}
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
    pm_limit_lower=selection==2?low:0;pm_limit_upper=selection==1?low:pm_map[0].end;
    dummy_run=true;numa_run_active=false;current_context=0;bail=restart=false;
    test_contexts[0].active_cpu_count=1;
    memset(ticks_per_test,0,sizeof(ticks_per_test));
    memset(ticks_per_sweep_stage,0,sizeof(ticks_per_sweep_stage));
    for(int test=0;test<12;test++)test_list[test].enabled=all_tests || enabled[test];
    build_numa_domain_list();
    if(num_execution_waves!=(num_cpu_memory_domains+15)/16)fail("domain list produced wrong wave count");
    for(test_num=0;test_num<12;test_num++)if(test_list[test_num].enabled) {
        test_memory_sweep=test_first_sweep(test_num);
        do for(test_stage=0;test_stage<test_list[test_num].stages;test_stage++) {
            if(test_stage_is_global_once(test_num,test_stage)){calibrate_global_stage();continue;}
            for(unsigned int window=0;;window++) {
                uintptr_t start,end;window_page_bounds(window,&start,&end);
                if(window_is_in_sweep(window)) {
                    current_context=0;test_contexts[0].window_index=window;
                    setup_vm_map(0,start,end);
                    run_test_window(0,true,true,test_list[test_num].iterations);
                }
                if(end>=pm_map[pm_map_size-1].end)break;
            }
        } while(test_next_sweep(test_num));
    }
}
static void verify_actual_work(int selection) {
    dummy_run=false;numa_run_active=true;current_context=0;
    test_work_done=0;sleep_calls=wave_bind_calls=0;
    pass_work_expected=pass_expected_work();
    unsigned int expected_sleep=0,expected_binds=0;
    uint64_t expected_total=0;
    for(test_num=0;test_num<12;test_num++)if(test_list[test_num].enabled) {
        test_memory_sweep=test_first_sweep(test_num);
        do for(test_stage=0;test_stage<test_list[test_num].stages;test_stage++) {
            test_work_done_base=test_work_done;
            test_work_expected=stage_expected_work(test_num,test_stage,test_memory_sweep);
            expected_total+=test_work_expected;
            if(test_stage_is_global_once(test_num,test_stage)) {
                int seconds=test_list[test_num].iterations/(pass_num==0?3:1);
                int actual=run_global_stage_once();
                if(actual!=seconds || test_work_expected!=(uint64_t)seconds)fail("global fade seconds scaled by domains or wrong sweep");
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
            uint64_t actual=test_work_done-test_work_done_base;
            if(actual!=test_work_expected)fail("calibrated stage expected work differs from per-CPU mapped work");
            render_aggregate_progress(actual,test_work_expected,test_work_done,pass_work_expected);
            if(test_percentage!=(actual?100:0))fail("completed stage progress did not reach its correct percentage");
        } while(test_next_sweep(test_num));
    }
    if(test_work_done!=pass_work_expected || expected_total!=pass_work_expected)fail("calibrated pass expected work differs from cumulative real work");
    if(pass_percentage!=100 || sleep_calls!=expected_sleep || wave_bind_calls!=expected_binds)fail("pass percentage, global fade count or wave count mismatch");
    int fade_seconds=test_list[10].iterations/(pass_num==0?3:1);
    if(test_list[10].enabled && expected_sleep!=(unsigned int)(4*fade_seconds*(selection==0?2:1)))fail("fade not once per round and selected resident sweep");
    cases++;
}
static void arithmetic_limits(void) {
    test_num=3;test_stage=0;test_list[3].cpu_mode=PAR;test_list[3].work_class=TEST_WORK_SEGMENT_FIXED;
    num_cpu_memory_domains=2;num_execution_waves=1;cpu_memory_domains[0]=0;cpu_memory_domains[1]=1;
    uint64_t weighted=(uint64_t)90000000*1024+(uint64_t)10000003*513;
    whole_map_denom[TEST_WORK_SEGMENT_FIXED][TEST_SWEEP_ALL]=100000003;
    owner_weighted_par[TEST_WORK_SEGMENT_FIXED][TEST_SWEEP_ALL]=weighted;
    ticks_per_sweep_stage[0][3][TEST_SWEEP_ALL][0]=2000000000;pass_num=0;
    uint64_t oracle=(uint64_t)(((unsigned __int128)2000000000*weighted)/100000003);
    if(stage_expected_work(3,0,TEST_SWEEP_ALL)!=oracle)fail("large weighted calibration overflowed or truncated incorrectly");
    whole_map_denom[TEST_WORK_SEGMENT_FIXED][TEST_SWEEP_ALL]=0;
    if(stage_expected_work(3,0,TEST_SWEEP_ALL)!=0)fail("empty sweep denominator did not return zero");
    render_aggregate_progress(101,100,200,100);
    if(test_percentage!=100 || pass_percentage!=100)fail("progress overrun did not clamp at 100");
    render_aggregate_progress(0,0,0,0);
    if(test_percentage || pass_percentage)fail("empty progress denominator did not render zero");
    cases+=4;
}
int main(void) {
    bool enabled[12];for(int test=0;test<12;test++)enabled[test]=test_list[test].enabled;
    const unsigned int domains[]={17,32,64};
    for(int pass=0;pass<2;pass++)for(unsigned int d=0;d<3;d++)for(int selection=0;selection<3;selection++)for(int orphan=0;orphan<2;orphan++)for(int all_tests=0;all_tests<2;all_tests++) {
        fixture(domains[d],selection,orphan,pass,all_tests,enabled);
        verify_actual_work(selection);
    }
    arithmetic_limits();
    printf("accounting: %u calibrated pass/progress cases passed\n",cases);
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
    definitions += [function(numa_source,name) for name in ("stage_expected_work","test_expected_work","pass_expected_work","render_aggregate_progress","bind_execution_wave","build_numa_domain_list")]
    definitions += [function(chunk_source,name) for name in ("equalize_block_counts","calculate_chunk")]
    definitions += [function(main_source,name) for name in ("setup_vm_map","run_test_window","run_global_stage_once")]
    global_case = main_source.index("case GLOBAL_STAGE_ONCE:")
    guard = main_source.index("if (dummy_run)",global_case)
    # Core-subset picking later gives the scheduler its own selection array.
    # Use the name from the extracted source so this suite also runs at the
    # activation commit where sweep accounting first becomes available.
    selection_name = ("numa_run_cpu_selected" if re.search(r"\bnuma_run_cpu_selected\b",numa_source)
                      else "run_cpu_selected")
    harness = HARNESS.replace("numa_run_cpu_selected",selection_name)
    harness = harness.replace("/* SWEEP TYPE */",sweep.group())
    harness = harness.replace("/* ACTUAL TEST TABLE */",block(test_source,table.start())+";")
    harness = harness.replace("/* SOURCE FUNCTIONS */","\n\n".join(definitions))
    harness = harness.replace("/* ACTUAL GLOBAL CALIBRATION GUARD */",block(main_source,guard))
    with tempfile.TemporaryDirectory(prefix="numa-accounting-") as directory:
        cfile = Path(directory)/"accounting.c"
        executable = Path(directory)/"accounting"
        cfile.write_text(harness)
        subprocess.run([args.cc,"-std=c11","-O2",str(cfile),"-o",str(executable)],check=True)
        subprocess.run([str(executable)],check=True,timeout=30)
        # A regression that reuses one sweep's calibration for every sweep
        # must fail when LOW and HIGH have different segment counts (the
        # ALL entry is never calibrated for multi-stage tests).
        expression = "ticks_per_sweep_stage[pass_type][test][sweep][stage]"
        if expression not in harness:
            raise RuntimeError("Missing sweep-specific calibration expression")
        negative = harness.replace(expression,"ticks_per_sweep_stage[pass_type][test][TEST_SWEEP_ALL][stage]",1)
        cfile.write_text(negative)
        subprocess.run([args.cc,"-std=c11","-O2",str(cfile),"-o",str(executable)],check=True)
        rejected = subprocess.run([str(executable)],capture_output=True,text=True,timeout=30)
        if rejected.returncode == 0 or "calibrated stage expected work differs" not in rejected.stderr:
            raise RuntimeError(f"Aggregate-calibration negative control was not rejected correctly: {rejected.stderr}")
        print("accounting negative control: fixed-sweep calibration rejected")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from None
    except subprocess.TimeoutExpired:
        raise SystemExit("accounting executable exceeded its time limit") from None
