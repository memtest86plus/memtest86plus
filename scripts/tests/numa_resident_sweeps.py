#!/usr/bin/env python3
"""Exercise actual relocation copies, own-address and bit-fade test patterns.

This complements numa_scheduler.py's actual scheduler/range recorder. Host
arrays represent both resident regions; the actual run_at copy is stopped at
its post-copy barrier before the machine-specific jump. No hardware is touched.
"""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

from c_extract import extract_function as function  # noqa: E402


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define LOW_LOAD_LIMIT (4UL << 20)
#define VM_WINDOW_SIZE (1UL << 18)
#define VM_PINNED_SIZE (1UL << 19)
#define VMEM_MAX_CONTEXTS 16
#define MAX_CPUS 4
#define SPIN_SIZE (1 << 27)
#define BSP_STACK_SIZE 128
#define AP_STACK_SIZE 128
#define LOCALS_SIZE 16
#define ARCH_BITS 64
#define TESTWORD_WIDTH 64
#define NUMA_ON 1
#define NUMA_PAR 2
#define NUMA_OFF 0
#define BAILOUT
#define SKIP_RANGE(n) { ticks += (n); continue; }
#define unlikely(x) (x)
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef uint64_t testword_t;
/* SWEEP TYPE */
static test_memory_sweep_t test_memory_sweep;
static testword_t memory[2][64][512];
static unsigned writes[2][64][512], reads[2][64][512];
static char *_start, *_stacks, *startup;
static int num_available_cpus, pass_num, test_num, num_active_cpus, numa_mode;
static bool dummy_run, numa_run_active, restart, bail, enable_trace;
static uint64_t test_work_done, test_work_done_base, test_work_expected, pass_work_expected;
static uintptr_t test_addr[MAX_CPUS];
static unsigned errors, ticks, copies, sleeps;
static jmp_buf copy_exit;
#define LONG_BARRIER longjmp(copy_exit,1)
static struct {testword_t *start,*end; uintptr_t pm_base_addr; unsigned proximity_domain_idx;} vm_map[1][1];
static int vm_map_size[1] = {1};
typedef struct {unsigned active_cpu_count;} test_context_t;
static test_context_t test_contexts[1];
static struct {uintptr_t start,end;} pm_map[4];
static int pm_map_size;
static uintptr_t pm_limit_lower, pm_limit_upper;
static struct {int stages,iterations;} test_list[12];
static bool numa_boot_cpu_subset[MAX_CPUS];
static uint16_t test_team_chunk_index[MAX_CPUS],chunk_index[MAX_CPUS],used_cpus_in_proximity_domain[64];
static bool numa_par_runs(void) {return numa_mode == NUMA_PAR;}
static int test_context_index(void) {return 0;}
static bool test_is_primary_context_master(int my_cpu) {return false;}
static unsigned smp_get_proximity_domain_idx(int cpu) {return 0;}
static uintptr_t round_down(uintptr_t n,size_t a) {return n & ~(a-1);}
static uintptr_t round_up(uintptr_t n,size_t a) {return (n+a-1) & ~(a-1);}
static void count_access(testword_t *p, bool write) {
    uintptr_t index=((uintptr_t)p-(uintptr_t)memory)/sizeof(testword_t);
    assert(index < 2*64*512);
    if(write) ((unsigned *)writes)[index]++; else ((unsigned *)reads)[index]++;
}
static void write_word(testword_t *p,testword_t v) {count_access(p,true);*p=v;}
static testword_t read_word(testword_t *p) {count_access(p,false);return *p;}
static void data_error(testword_t *p,testword_t expect,testword_t actual,bool data) {errors++;}
static void do_tick(int cpu) {ticks++;}
static void __attribute__((unused)) cache_sync_code_range(void *start, void *end) {}
static void flush_caches(int cpu) {}
static void flush_caches_all(int cpu) {}
static void display_test_pattern_name(const char *s) {}
static void display_test_pattern_value(testword_t v) {}
static void display_test_stage_description(const char *s,int secs) {}
static void render_aggregate_progress(uint64_t a,uint64_t b,uint64_t c,uint64_t d) {}
static void check_input(void) {}
static void update_timed_ui(void) {}
static void sleep(unsigned secs) {sleeps+=secs;}
#define master_cpu 0
#define trace(...) ((void)0)

/* SOURCE FUNCTIONS */

static void relocate(unsigned region) {
    char *destination=(char *)&memory[region][0][160];
    if(_start==destination) return;
    copies++;
    if(!setjmp(copy_exit)) run_at((uintptr_t)destination,0);
    _start=destination;_stacks=_start+64;startup=_start;
}
static void map_domain(unsigned region,unsigned domain,unsigned domains) {
    vm_map[0][0].start=&memory[region][domain][region==0 && domain==0?160:0];
    vm_map[0][0].end=&memory[region][domain][region==1 && domain+1==domains?495:511];
    vm_map[0][0].pm_base_addr=region ? VM_PINNED_SIZE+3*VM_WINDOW_SIZE : 0;
}
static void assert_counts(unsigned domains,int selection,unsigned expected) {
    for(unsigned region=0;region<2;region++) for(unsigned d=0;d<64;d++) for(unsigned w=0;w<512;w++) {
        bool selected=d<domains && (selection==0 || region==(unsigned)(selection-1));
        if(region==0 && d==0 && w<160) selected=false; // USB low-memory guard
        if(region==1 && d+1==domains && w>=496) selected=false; // reserved tail
        unsigned count=selected?expected:0;
        assert(writes[region][d][w]==count);
        assert(reads[region][d][w]==count);
    }
}
static void pattern_case(unsigned domains,unsigned team,int selection,bool fade,int pass) {
    memset(memory,0x7e,sizeof(memory));memset(writes,0,sizeof(writes));memset(reads,0,sizeof(reads));
    errors=ticks=copies=sleeps=0;test_work_done=0;num_available_cpus=team;
    numa_mode=NUMA_PAR;numa_run_active=true;dummy_run=false;restart=bail=false;
    pass_num=pass;test_num=fade?10:2;
    test_list[2].stages=2;test_list[10].stages=12;test_list[10].iterations=6;
    pm_map_size=2;pm_map[0].start=0;pm_map[0].end=1024;pm_map[1].start=1024;pm_map[1].end=2048;
    pm_limit_lower=selection==2?1024:0;pm_limit_upper=selection==1?1024:2048;
    _start=(char *)&memory[0][0][160];_stacks=_start+64;startup=_start;
    test_memory_sweep=test_first_sweep(test_num);
    unsigned sweeps=0;
    do {
        unsigned region=test_memory_sweep==TEST_SWEEP_LOW?0:1;
        relocate(1-region); // matches actual phase-machine residency
        sweeps++;
        for(int stage=0;stage<test_list[test_num].stages;stage++) {
            if(fade && stage%3==1) {
                unsigned delay=pass?6:2;
                unsigned before=sleeps;
                int actual=run_global_stage_once();
                assert(actual==(int)delay && sleeps-before==delay);
                dummy_run=true;assert(run_global_stage_once()==actual);dummy_run=false;
                continue;
            }
            // Complete every wave before advancing the logical stage.
            for(unsigned wave=0;wave<(domains+15)/16;wave++) {
                for(unsigned d=wave*16;d<MIN((wave+1)*16,domains);d++) {
                    map_domain(region,d,domains);
                    test_contexts[0].active_cpu_count=fade?team:1;
                    for(unsigned cpu=0;cpu<(fade?team:1);cpu++) {
                        test_team_chunk_index[cpu]=cpu;
                        if(fade) test_bit_fade(cpu,stage,pass?6:2);
                        else test_own_addr2(cpu,stage);
                    }
                }
            }
        }
    } while(test_next_sweep(test_num));
    assert(sweeps==(selection?1U:2U));
    assert(copies==(selection==2?0U:selection==1?1U:2U));
    assert(errors==0);
    assert_counts(domains,selection,fade?4:1);
    assert(sleeps==(fade?4*sweeps*(pass?6:2):0));
    assert(test_work_done==sleeps);
}
static void negative_control(void) {
    errors=0;num_available_cpus=4;test_contexts[0].active_cpu_count=1;
    _start=(char *)&memory[0][0][160];_stacks=_start+64;startup=_start;
    // Original stage ordering: copy into both patterned load spans.
    relocate(1);map_domain(0,0,17);test_own_addr2(0,0);
    relocate(0);map_domain(1,0,17);test_own_addr2(0,0);
    relocate(1);map_domain(0,0,17);test_own_addr2(0,1);
    relocate(0);map_domain(1,0,17);test_own_addr2(0,1);
    assert(errors>0);
}
static void load_bounds(void) {
    uintptr_t addr;
    _start=(char *)(uintptr_t)0x10000000;
    pm_map_size=2;
    pm_map[0].start=(LOW_LOAD_LIMIT/PAGE_SIZE)-1;pm_map[0].end=(LOW_LOAD_LIMIT/PAGE_SIZE)+4;
    pm_map[1].start=(LOW_LOAD_LIMIT/PAGE_SIZE)+4;pm_map[1].end=(LOW_LOAD_LIMIT/PAGE_SIZE)+8;
    // The first candidate is usable RAM but would cross the sweep boundary.
    assert(!set_load_addr(&addr,2*PAGE_SIZE,0,LOW_LOAD_LIMIT));
    assert(set_load_addr(&addr,PAGE_SIZE,0,LOW_LOAD_LIMIT));
    assert(addr==LOW_LOAD_LIMIT-PAGE_SIZE);
    assert(set_load_addr(&addr,2*PAGE_SIZE,LOW_LOAD_LIMIT,LOW_LOAD_LIMIT+8*PAGE_SIZE));
    assert(addr==LOW_LOAD_LIMIT);
}
int main(void) {
    unsigned cases=0;
    for(unsigned domains=17;domains<=64;domains=domains==17?32:domains*2)
        for(unsigned team=1;team<=4;team+=team==1?2:1)
            for(int selection=0;selection<3;selection++) for(int fade=0;fade<2;fade++) for(int pass=0;pass<2;pass++) {
                pattern_case(domains,team,selection,fade,pass);cases++;
            }
    negative_control();load_bounds();
    printf("Resident sweeps: %u real-pattern cases, relocation negative control and load-boundary checks passed\n",cases);
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    sources = {path: (ROOT / path).read_text() for path in ('app/main.c', 'app/numa_par.c', 'tests/own_addr.c', 'tests/bit_fade.c', 'tests/test_helper.c')}
    own = '\n'.join(function(sources['tests/own_addr.c'], n) for n in ('pattern_fill', 'pattern_check', 'test_own_addr2'))
    own = own.replace('pattern_fill', 'own_pattern_fill').replace('pattern_check', 'own_pattern_check')
    fade = '\n'.join(function(sources['tests/bit_fade.c'], n) for n in ('fade_mix', 'fade_seed', 'fade_addr_offset', 'pattern_fill', 'pattern_check', 'fade_delay', 'test_bit_fade'))
    fade = fade.replace('pattern_fill', 'fade_pattern_fill').replace('pattern_check', 'fade_pattern_check')
    helpers = '\n'.join(function(sources['tests/test_helper.c'], n) for n in ('equalize_block_counts', 'calculate_chunk'))
    control = '\n'.join(function(sources['app/main.c'], n) for n in ('run_at', 'selected_map_has_window_zero', 'selected_map_has_windows_1_plus', 'test_first_sweep', 'test_next_sweep', 'addr_range_is_usable', 'set_load_addr'))
    control = '\n'.join((control, function(sources['app/numa_par.c'], 'run_global_stage_once')))
    sweep_type = re.search(r'typedef enum \{\s*TEST_SWEEP_ALL,.*?\} test_memory_sweep_t;', sources['app/main.c'], re.S).group()
    code = HARNESS.replace('/* SWEEP TYPE */', sweep_type).replace('/* SOURCE FUNCTIONS */', helpers + '\n' + own + '\n' + fade + '\n' + control)
    with tempfile.TemporaryDirectory(prefix='numa-resident-') as directory:
        source = Path(directory) / 'regression.c'
        binary = Path(directory) / 'regression'
        source.write_text(code)
        subprocess.run([args.cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-variable', '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    main()
