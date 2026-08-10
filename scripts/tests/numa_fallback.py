#!/usr/bin/env python3
"""Exercise the checkout's fatal NUMA fallback and subsequent chunking.

Every fallback span must be covered exactly once. The last CPU absorbs the
rounding remainder, so nondivisible spans have no omitted tail.
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
#define MAX_CPUS 64
#define NUMA_OFF 0
#define NUMA_ON 1
#define NUMA_PAR 2
#define SPIN_SIZE (1UL << 27)
#define PAGE_SIZE 4096
#define round_down(x,a) ((x) & ~((uintptr_t)(a)-1))
#define round_up(x,a) round_down((x)+(a)-1,a)
typedef uint64_t testword_t;
typedef struct {int active_cpu_count;} test_context_t;
static test_context_t test_contexts[16];
static struct {testword_t *start,*end;unsigned int proximity_domain_idx;} vm_map[16][1];
static uint16_t chunk_index[MAX_CPUS],test_team_chunk_index[MAX_CPUS],used_cpus_in_proximity_domain[64];
static bool numa_boot_cpu_subset[MAX_CPUS];
static int numa_mode,master_cpu;
static bool numa_run_active,numa_stage_failure,restart,start_run,dummy_run;
#define num_active_cpus (test_contexts[0].active_cpu_count)
static int test_context_index(void){return 0;}
static unsigned int smp_get_proximity_domain_idx(int cpu){return cpu%3;}
static bool numa_par_runs(void){return numa_mode==NUMA_PAR;}
static testword_t memory[MAX_CPUS*8192];
static unsigned int cases;
static void fail(const char *why){fprintf(stderr,"fallback contexts=%d: %s\n",VMEM_MAX_CONTEXTS,why);exit(1);}
/* SOURCE FUNCTIONS */
static void apply_failure_boundary(void) {
    /* ACTUAL FAILURE BRANCH */
    for (;;) {
        /* ACTUAL RESTART BRANCH */
        break;
    }
    /* ACTUAL REENTRY ASSIGNMENT */
}
static void reset(bool active,bool failed) {
    memset(test_contexts,0,sizeof(test_contexts));
    memset(used_cpus_in_proximity_domain,0,sizeof(used_cpus_in_proximity_domain));
    numa_mode=NUMA_PAR;numa_run_active=active;numa_stage_failure=failed;
    restart=start_run=dummy_run=false;master_cpu=9;
    for(int cpu=0;cpu<MAX_CPUS;cpu++) {
        chunk_index[cpu]=cpu;test_team_chunk_index[cpu]=cpu;
        // NUMA_PAR never initialized NUMA_ON's counts; retained/excluded
        // boot-subset CPUs must both use global fallback chunk ordinals.
        numa_boot_cpu_subset[cpu]=(cpu%2)==0;
    }
}
static void check_chunks(unsigned int cpus,size_t bytes,size_t alignment,bool divisible) {
    reset(true,true);
    apply_failure_boundary();
    if(numa_mode!=NUMA_OFF || numa_run_active || !start_run || !dummy_run || restart || master_cpu!=0)fail("fatal failure did not enter a safe legacy restart");
    num_active_cpus=cpus;
    vm_map[0][0].start=memory;
    vm_map[0][0].end=memory+bytes/sizeof(testword_t)-1;
    vm_map[0][0].proximity_domain_idx=0;
    uintptr_t cursor=(uintptr_t)memory;
    for(unsigned int cpu=0;cpu<cpus;cpu++) {
        testword_t *start,*end;
        calculate_chunk(&start,&end,cpu,0,alignment);
        if((uintptr_t)start!=cursor || end<start || end>vm_map[0][0].end)fail("fallback chunks overlap, contain a gap, or leave the eligible span");
        cursor=(uintptr_t)end+sizeof(testword_t);
    }
    size_t covered=cursor-(uintptr_t)memory;
    (void)divisible;
    if(covered!=bytes)fail("fallback segment not completely covered");
    for(unsigned int domain=0;domain<64;domain++)if(used_cpus_in_proximity_domain[domain])fail("test unexpectedly populated NUMA_ON counts");
    cases++;
}
int main(void) {
    for(int active=0;active<2;active++)for(int failed=0;failed<2;failed++) {
        reset(active,failed);apply_failure_boundary();
        bool must_fallback=VMEM_MAX_CONTEXTS>1 && active && failed;
        if((numa_mode==NUMA_OFF)!=must_fallback || start_run!=must_fallback)fail("failure-boundary condition changed");
        cases++;
    }
    if(VMEM_MAX_CONTEXTS>1) {
        const unsigned int cpus[]={1,2,3,4,17,32};
        const size_t alignments[]={8,64};
        for(unsigned int c=0;c<sizeof(cpus)/sizeof(cpus[0]);c++)for(unsigned int a=0;a<2;a++) {
            check_chunks(cpus[c],cpus[c]*PAGE_SIZE,alignments[a],true);
            check_chunks(cpus[c],cpus[c]*PAGE_SIZE+PAGE_SIZE,alignments[a],false);
        }
    }
    printf("fallback VMEM_MAX_CONTEXTS=%d: %u cases passed\n",VMEM_MAX_CONTEXTS,cases);
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    source = (ROOT / "app/main.c").read_text()
    helper = (ROOT / "tests/test_helper.c").read_text()
    failure = re.search(r"if \(VMEM_MAX_CONTEXTS > 1 && numa_run_active && numa_stage_failure\)\s*\{", source)
    restart = re.search(r"if \(restart\)\s*\{\s*// The configuration has been changed", source)
    reentry = re.search(r"numa_run_active = !dummy_run && VMEM_MAX_CONTEXTS > 1 && numa_par_runs\(\);", source)
    if failure is None or restart is None or reentry is None:
        raise RuntimeError("Missing fatal-failure/restart source")
    harness = HARNESS.replace("/* SOURCE FUNCTIONS */", function(helper,"equalize_block_counts")+"\n"+function(helper,"calculate_chunk"))
    harness = harness.replace("/* ACTUAL FAILURE BRANCH */", block(source,failure.start()))
    harness = harness.replace("/* ACTUAL RESTART BRANCH */", block(source,restart.start()))
    harness = harness.replace("/* ACTUAL REENTRY ASSIGNMENT */", reentry.group())
    with tempfile.TemporaryDirectory(prefix="numa-fallback-") as directory:
        cfile = Path(directory)/"fallback.c"
        cfile.write_text(harness)
        for contexts in (1,16):
            executable = Path(directory)/f"fallback-{contexts}"
            subprocess.run([args.cc,"-std=c11","-O2","-fsanitize=undefined","-fno-sanitize-recover=all",f"-DVMEM_MAX_CONTEXTS={contexts}",str(cfile),"-o",str(executable)],check=True)
            subprocess.run([str(executable)],check=True,timeout=10)
        # Reintroducing NUMA_ON is unsafe with NUMA_PAR's global ordinals and
        # untouched per-domain counts; reject it before using that state.
        if "numa_mode = NUMA_OFF;" not in harness:
            raise RuntimeError("Missing topology-agnostic fallback assignment")
        cfile.write_text(harness.replace("numa_mode = NUMA_OFF;","numa_mode = NUMA_ON;",1))
        subprocess.run([args.cc,"-std=c11","-O2","-fsanitize=undefined","-fno-sanitize-recover=all","-DVMEM_MAX_CONTEXTS=16",str(cfile),"-o",str(executable)],check=True)
        rejected = subprocess.run([str(executable)],capture_output=True,text=True,timeout=10)
        if rejected.returncode == 0 or "failure-boundary condition changed" not in rejected.stderr:
            raise RuntimeError(f"NUMA_ON negative control was not rejected correctly: {rejected.stderr}")
        print("fallback negative control: NUMA_ON restart rejected")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from None
    except subprocess.TimeoutExpired:
        raise SystemExit("fallback executable exceeded its time limit") from None
