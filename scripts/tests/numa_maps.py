#!/usr/bin/env python3
"""Verify current NUMA memory-map ownership across 20,000 stable cases.

Both x86 window placement and AArch64 RAM bases are exercised. Test maps
include 2-64 domains, selected bounds, reserved gaps, a memory-only domain,
and 1-4 execution waves. The expected union comes from the physical map;
each interval must appear once, without crossing a reserved gap.
"""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]

from c_extract import extract_function  # noqa: E402


PREFIX = r"""
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#define VMEM_MAX_CONTEXTS 16
#define MAX_MEM_SEGMENTS 128
#define PAGE_SHIFT 12
#define PAGE_SIZE 4096
#define VM_WINDOW_SIZE (1UL<<18)
#define LOW_LOAD_LIMIT low_limit_bytes
#define NUMA_ON 1
#define CONTEXT_MAP_OVERFLOW 2
#define CONTEXT_TOPOLOGY_FAILED 3
#define do_trace(...) ((void)0)
typedef uintptr_t testword_t;
typedef struct {uintptr_t start,end;} pm_map_t;
typedef struct {uintptr_t pm_base_addr; testword_t *start,*end; uint32_t proximity_domain_idx;} vm_map_t;
typedef struct {uint64_t start,end; uint32_t proximity_domain_idx;} affinity_t;
static pm_map_t pm_map[128];
static int pm_map_size;
static vm_map_t vm_map[16][128];
static int vm_map_size[16];
static size_t num_mapped_pages[16];
static uintptr_t pm_limit_lower,pm_limit_upper,low_limit_bytes;
static int memory_owner_context[64],numa_mode,failures;
static unsigned int used_cpus_in_proximity_domain[64];
static bool numa_run_active;
static affinity_t memory_affinity_ranges[128];
static int num_memory_affinity_ranges;
static uint32_t smp_get_proximity_domain_idx(int cpu){(void)cpu;return 0;}
static void context_record_failure(int ctx,int reason){(void)ctx;(void)reason;failures++;}
static void *first_word_mapping(uintptr_t page,int context){(void)context;return (void*)(page<<PAGE_SHIFT);}
static void *last_word_mapping(uintptr_t page,size_t word,int context){(void)context;return (void*)((page<<PAGE_SHIFT)+PAGE_SIZE-word);}
"""

MAIN = r"""
typedef struct {uintptr_t start,end;} interval_t;
static int cmp(const void*a,const void*b){const interval_t*x=a,*y=b;return (x->start>y->start)-(x->start<y->start);}
static uint32_t rng=0x12345678;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void exercise(int domains,uintptr_t ram_start,uintptr_t ram_end,int config){
    num_memory_affinity_ranges=domains;
    for(int d=0;d<domains;d++){
        uintptr_t s=ram_start+(ram_end-ram_start)*d/domains;
        uintptr_t e=ram_start+(ram_end-ram_start)*(d+1)/domains;
        memory_affinity_ranges[d]=(affinity_t){s<<PAGE_SHIFT,e<<PAGE_SHIFT,(uint32_t)d};
    }
    pm_map_size=12;
    for(int p=0;p<pm_map_size;p++){
        uintptr_t lo=ram_start+(ram_end-ram_start)*p/pm_map_size;
        uintptr_t hi=ram_start+(ram_end-ram_start)*(p+1)/pm_map_size;
        uintptr_t inset=next()%16;
        pm_map[p]=(pm_map_t){lo+inset,hi-(next()%16)};
    }
    // These small gaps represent pinned/reserved allocations removed from pm_map.
    pm_limit_lower=(config%3)?ram_start:ram_start+next()%1024;
    pm_limit_upper=(config%5)?ram_end:ram_end-next()%1024;
    numa_run_active=true; numa_mode=2; failures=0;
    interval_t actual[8192]; int count=0;
    // Last domain is memory-only: BSP owns it in wave 0.
    int teams=domains-1;
    for(int wave=0;wave<(teams+15)/16;wave++){
        for(int d=0;d<domains;d++)memory_owner_context[d]=-1;
        int contexts=teams-wave*16;if(contexts>16)contexts=16;
        for(int c=0;c<contexts;c++)memory_owner_context[wave*16+c]=c;
        if(wave==0)memory_owner_context[domains-1]=0;
        for(uint64_t w=0;;w++){
            uintptr_t lo,hi;window_page_bounds(w,&lo,&hi);
            for(int c=0;c<contexts;c++){
                setup_vm_map(c,lo,hi);
                for(int s=0;s<vm_map_size[c];s++){
                    assert(count<8192);
                    uintptr_t first=(uintptr_t)vm_map[c][s].start>>PAGE_SHIFT;
                    uintptr_t end=((uintptr_t)vm_map[c][s].end+sizeof(testword_t))>>PAGE_SHIFT;
                    actual[count++]=(interval_t){first,end};
                }
            }
            if(hi>=ram_end)break;
        }
    }
    assert(failures==0);qsort(actual,count,sizeof(*actual),cmp);
    int j=0;
    for(int p=0;p<pm_map_size;p++){
        uintptr_t lo=pm_map[p].start>pm_limit_lower?pm_map[p].start:pm_limit_lower;
        uintptr_t hi=pm_map[p].end<pm_limit_upper?pm_map[p].end:pm_limit_upper;
        if(lo>=hi)continue;
        uintptr_t cursor=lo;
        while(j<count && actual[j].start<hi){
            if(actual[j].start!=cursor || actual[j].end>hi){
                fprintf(stderr,"coverage mismatch config=%d domains=%d actual=%lx-%lx cursor=%lx expected_end=%lx\n",config,domains,actual[j].start,actual[j].end,cursor,hi);exit(1);
            }
            cursor=actual[j++].end;
        }
        assert(cursor==hi);
    }
    assert(j==count);
}
int main(void){
    for(int config=0;config<10000;config++){
        uintptr_t ram_start=TEST_ARM ? ((config%2)?(1UL<<18):(1UL<<20)):0;
        uintptr_t ram_end=ram_start+(5UL<<18)+next()%(1UL<<18);
        low_limit_bytes=(ram_start<<PAGE_SHIFT)+(4UL<<20);
        exercise(2+next()%63,ram_start,ram_end,config);
    }
    printf("PASS: 10000 map/window/ownership cases, architecture=%s, 2-64 domains, 1-4 waves, orphan domain, reserved gaps, selected bounds\n",TEST_ARM?"AArch64":"x86-64");
}
"""


def main():
    functions = "\n".join(
        extract_function((ROOT / path).read_text(), name)
        for path, name in (
            ("system/smp.c", "smp_narrow_to_proximity_domain"),
            ("app/main.c", "setup_vm_map"),
            ("app/main.c", "window_page_bounds"),
        )
    ).replace("#if defined(__aarch64__)", "#if TEST_ARM")
    with tempfile.TemporaryDirectory(prefix="numa-maps-") as directory:
        source = Path(directory) / "regression.c"
        source.write_text(PREFIX + "\n" + functions + "\n" + MAIN)
        compiler = shlex.split(os.environ.get("CC", "cc"))
        for architecture in (0, 1):
            binary = Path(directory) / f"regression-{architecture}"
            subprocess.run(
                compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            f"-DTEST_ARM={architecture}", str(source), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
