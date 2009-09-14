/*
 * Copyright 2008, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Binary implementation of the original opcontrol script due to missing tools
 * like awk, test, etc.
 */

#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#include "op_config.h"

#if 0
#define verbose(fmt...) printf(fmt)
#else
#define verbose(fmt...)
#endif

/* Experiments found that using a small interval may hang the device, and the
 * more events tracked simultaneously, the longer the interval has to be.
 */

#if !defined(WITH_ARM_V7_A)
#define MAX_EVENTS 3
int min_count[3] = {150000, 200000, 250000};
#else
#define MAX_EVENTS 4
int min_count[4] = {150000, 200000, 250000, 300000};
#endif

int list_events; 
int show_usage;
int setup;
int quick;
int num_events;
int start;
int stop;
int reset;

int selected_events[MAX_EVENTS];
int selected_counts[MAX_EVENTS];

char kernel_range[512];
char vmlinux[512];

struct option long_options[] = {
    {"help", 0, &show_usage, 1},
    {"list-events", 0, &list_events, 1},
    {"reset", 0, &reset, 1},
    {"setup", 0, &setup, 1},
    {"quick", 0, &quick, 1},
    {"event", 1, 0, 'e'},
    {"vmlinux", 1, 0, 'v'},
    {"kernel-range", 1, 0, 'r'},
    {"start", 0, &start, 1},
    {"stop", 0, &stop, 1},
    {"shutdown", 0, 0, 'h'},
    {"status", 0, 0, 't'},
    {0, 0, 0, 0},
};

struct event_info {
    int id;
    const char *name;
    const char *explanation;
} event_info[] = {
#if !defined(WITH_ARM_V7_A)
    /* ARM V6 events */
    {0x00, "IFU_IFETCH_MISS", 
     "number of instruction fetch misses"},
    {0x01, "CYCLES_IFU_MEM_STALL", 
     "cycles instruction fetch pipe is stalled"},
    {0x02, "CYCLES_DATA_STALL", 
     "cycles stall occurs for due to data dependency"},
    {0x03, "ITLB_MISS", 
     "number of Instruction MicroTLB misses"},
    {0x04, "DTLB_MISS", 
     "number of Data MicroTLB misses"},
    {0x05, "BR_INST_EXECUTED", 
     "branch instruction executed w/ or w/o program flow change"},
    {0x06, "BR_INST_MISS_PRED", 
     "branch mispredicted"},
    {0x07, "INSN_EXECUTED", 
     "instructions executed"},
    {0x09, "DCACHE_ACCESS", 
     "data cache access, cacheable locations"},
    {0x0a, "DCACHE_ACCESS_ALL", 
     "data cache access, all locations"},
    {0x0b, "DCACHE_MISS", 
     "data cache miss"},
    {0x0c, "DCACHE_WB", 
     "data cache writeback, 1 event for every half cacheline"},
    {0x0d, "PC_CHANGE", 
     "number of times the program counter was changed without a mode switch"},
    {0x0f, "TLB_MISS", 
     "Main TLB miss"},
    {0x10, "EXP_EXTERNAL", 
     "Explicit external data access"},
    {0x11, "LSU_STALL", 
     "cycles stalled because Load Store request queue is full"},
    {0x12, "WRITE_DRAIN", 
     "Times write buffer was drained"},
    {0xff, "CPU_CYCLES", 
     "clock cycles counter"}, 
#else
    /* ARM V7 events */
    {0x00, "PMNC_SW_INCR",
     "Software increment of PMNC registers"},
    {0x01, "IFETCH_MISS",
     "Instruction fetch misses from cache or normal cacheable memory"},
    {0x02, "ITLB_MISS",
     "Instruction fetch misses from TLB"},
    {0x03, "DCACHE_REFILL",
     "Data R/W operation that causes a refill from cache or normal cacheable"
     "memory"},
    {0x04, "DCACHE_ACCESS",
     "Data R/W from cache"},
    {0x05, "DTLB_REFILL",
     "Data R/W that causes a TLB refill"},
    {0x06, "DREAD",
     "Data read architecturally executed (note: architecturally executed = for"
     "instructions that are unconditional or that pass the condition code)"},
    {0x07, "DWRITE",
     "Data write architecturally executed"},
    {0x08, "INSTR_EXECUTED",
     "All executed instructions"},
    {0x09, "EXC_TAKEN",
     "Exception taken"},
    {0x0A, "EXC_EXECUTED",
     "Exception return architecturally executed"},
    {0x0B, "CID_WRITE",
     "Instruction that writes to the Context ID Register architecturally"
     "executed"},
    {0x0C, "PC_WRITE",
     "SW change of PC, architecturally executed (not by exceptions)"},
    {0x0D, "PC_IMM_BRANCH",
     "Immediate branch instruction executed (taken or not)"},
    {0x0E, "PC_PROC_RETURN",
     "Procedure return architecturally executed (not by exceptions)"},
    {0x0F, "UNALIGNED_ACCESS",
     "Unaligned access architecturally executed"},
    {0x10, "PC_BRANCH_MIS_PRED",
     "Branch mispredicted or not predicted. Counts pipeline flushes because of"
     "misprediction"},
    {0x12, "PC_BRANCH_MIS_USED",
    "Branch or change in program flow that could have been predicted"},
    {0x40, "WRITE_BUFFER_FULL",
     "Any write buffer full cycle"},
    {0x41, "L2_STORE_MERGED",
     "Any store that is merged in L2 cache"},
    {0x42, "L2_STORE_BUFF",
     "Any bufferable store from load/store to L2 cache"},
    {0x43, "L2_ACCESS",
     "Any access to L2 cache"},
    {0x44, "L2_CACH_MISS",
     "Any cacheable miss in L2 cache"},
    {0x45, "AXI_READ_CYCLES",
     "Number of cycles for an active AXI read"},
    {0x46, "AXI_WRITE_CYCLES",
     "Number of cycles for an active AXI write"},
    {0x47, "MEMORY_REPLAY",
     "Any replay event in the memory subsystem"},
    {0x48, "UNALIGNED_ACCESS_REPLAY",
     "Unaligned access that causes a replay"},
    {0x49, "L1_DATA_MISS",
     "L1 data cache miss as a result of the hashing algorithm"},
    {0x4A, "L1_INST_MISS",
     "L1 instruction cache miss as a result of the hashing algorithm"},
    {0x4B, "L1_DATA_COLORING",
     "L1 data access in which a page coloring alias occurs"},
    {0x4C, "L1_NEON_DATA",
     "NEON data access that hits L1 cache"},
    {0x4D, "L1_NEON_CACH_DATA",
     "NEON cacheable data access that hits L1 cache"},
    {0x4E, "L2_NEON",
     "L2 access as a result of NEON memory access"},
    {0x4F, "L2_NEON_HIT",
     "Any NEON hit in L2 cache"},
    {0x50, "L1_INST",
     "Any L1 instruction cache access, excluding CP15 cache accesses"},
    {0x51, "PC_RETURN_MIS_PRED",
     "Return stack misprediction at return stack pop"
     "(incorrect target address)"},
    {0x52, "PC_BRANCH_FAILED",
     "Branch prediction misprediction"},
    {0x53, "PC_BRANCH_TAKEN",
     "Any predicted branch that is taken"},
    {0x54, "PC_BRANCH_EXECUTED",
     "Any taken branch that is executed"},
    {0x55, "OP_EXECUTED",
     "Number of operations executed"
     "(in instruction or mutli-cycle instruction)"},
    {0x56, "CYCLES_INST_STALL",
     "Cycles where no instruction available"},
    {0x57, "CYCLES_INST",
     "Number of instructions issued in a cycle"},
    {0x58, "CYCLES_NEON_DATA_STALL",
     "Number of cycles the processor waits on MRC data from NEON"},
    {0x59, "CYCLES_NEON_INST_STALL",
     "Number of cycles the processor waits on NEON instruction queue or"
     "NEON load queue"},
    {0x5A, "NEON_CYCLES",
     "Number of cycles NEON and integer processors are not idle"},
    {0x70, "PMU0_EVENTS",
     "Number of events from external input source PMUEXTIN[0]"},
    {0x71, "PMU1_EVENTS",
     "Number of events from external input source PMUEXTIN[1]"},
    {0x72, "PMU_EVENTS",
     "Number of events from both external input sources PMUEXTIN[0]"
     "and PMUEXTIN[1]"},
    {0xFF, "CPU_CYCLES",
     "Number of CPU cycles"},
#endif
};

void usage() {
    printf("\nopcontrol: usage:\n"
           "   --list-events    list event types\n"
           "   --help           this message\n"
           "   --setup          setup directories\n"
           "   --quick          setup and select CPU_CYCLES:150000\n"
           "   --status         show configuration\n"
           "   --start          start data collection\n"
           "   --stop           stop data collection\n"
           "   --reset          clears out data from current session\n"
           "   --shutdown       kill the oprofile daeman\n"
           "   --event=eventspec\n"
           "      Choose an event. May be specified multiple times.\n"
           "      eventspec is in the form of name[:count], where :\n"
           "        name:  event name, see \"opcontrol --list-events\"\n"
           "        count: reset counter value\n" 
           "   --vmlinux=file   vmlinux kernel image\n"
           "   --kernel-range=start,end\n"
           "                    kernel range vma address in hexadecimal\n"
          );
}

void setup_session_dir() {
    int fd;

    fd = open(OP_DATA_DIR, O_RDONLY);
    if (fd != -1) {
        system("rm -r "OP_DATA_DIR);
        close(fd);
    }

    if (mkdir(OP_DATA_DIR, 755)) {
        fprintf(stderr, "Cannot create directory \"%s\": %s\n",
                OP_DATA_DIR, strerror(errno));
    }
    if (mkdir(OP_DATA_DIR"/samples", 644)) {
        fprintf(stderr, "Cannot create directory \"%s\": %s\n",
                OP_DATA_DIR"/samples", strerror(errno));
    }
}

int do_setup() {
    char dir[1024];

    setup_session_dir();

    if (mkdir(OP_DRIVER_BASE, 644)) {
        fprintf(stderr, "Cannot create directory "OP_DRIVER_BASE": %s\n",
                strerror(errno));
        return -1;
    }
    if (system("mount -t oprofilefs nodev "OP_DRIVER_BASE)) {
        return -1;
    }
    return 0;
}

void do_list_events()
{
    unsigned int i;

    printf("%-20s: %s\n", "name", "meaning");
    printf("----------------------------------------"
           "--------------------------------------\n");
    for (i = 0; i < sizeof(event_info)/sizeof(struct event_info); i++) {
        printf("%-20s: %s\n", event_info[i].name, event_info[i].explanation);
    }
}

int find_event_id_from_name(const char *name) {
    unsigned int i;

    for (i = 0; i < sizeof(event_info)/sizeof(struct event_info); i++) {
        if (!strcmp(name, event_info[i].name)) {
            return i;
        }
    }
    return -1;
}

const char * find_event_name_from_id(int id) {
    unsigned int i;

    for (i = 0; i < sizeof(event_info)/sizeof(struct event_info); i++) {
        if (event_info[i].id == id) {
            return event_info[i].name;
        }
    }
    return NULL;
}

int process_event(const char *event_spec) {
    char event_name[512];
    char count_name[512];
    unsigned int i;
    int event_id;
    int count_val;

    strncpy(event_name, event_spec, 512);
    count_name[0] = 0;

    /* First, check if the name is followed by ":" */
    for (i = 0; i < strlen(event_name); i++) {
        if (event_name[i] == 0) {
            break;
        }
        if (event_name[i] == ':') {
            strncpy(count_name, event_name+i+1, 512);
            event_name[i] = 0;
            break;
        }
    }
    event_id = find_event_id_from_name(event_name);
    if (event_id == -1) {
        fprintf(stderr, "Unknown event name: %s\n", event_name);
        return -1;
    }

    /* Use defualt count */
    if (count_name[0] == 0) {
        count_val = min_count[0];
    } else {
        count_val = atoi(count_name);
    }

    selected_events[num_events] = event_id;
    selected_counts[num_events++] = count_val;
    verbose("event_id is %d\n", event_id);
    verbose("count_val is %d\n", count_val);
    return 0;
}

int echo_dev(const char* str, int val, const char* file, int counter)
{
    char fullname[512];
    char content[128];
    int fd;
    
    if (counter >= 0) {
        snprintf(fullname, 512, OP_DRIVER_BASE"/%d/%s", counter, file);
    }
    else {
        snprintf(fullname, 512, OP_DRIVER_BASE"/%s", file);
    }
    fd = open(fullname, O_WRONLY);
    if (fd<0) {
        fprintf(stderr, "Cannot open %s: %s\n", fullname, strerror(errno));
        return fd;
    }
    if (str == 0) {
        sprintf(content, "%d", val);
    }
    else {
        strncpy(content, str, 128);
    }
    verbose("Configure %s (%s)\n", fullname, content);
    write(fd, content, strlen(content));
    close(fd);
    return 0;
}

int read_num(const char* file)
{
    char buffer[256];
    int fd = open(file, O_RDONLY);
    if (fd<0) return -1;
    int rd = read(fd, buffer, sizeof(buffer)-1);
    buffer[rd] = 0;
    return atoi(buffer);
}

void do_status()
{
    int num;
    char fullname[512];
    int i;

    printf("Driver directory: %s\n", OP_DRIVER_BASE);
    printf("Session directory: %s\n", OP_DATA_DIR);
    for (i = 0; i < MAX_EVENTS; i++) {
        sprintf(fullname, OP_DRIVER_BASE"/%d/enabled", i);
        num = read_num(fullname);
        if (num > 0) {
            printf("Counter %d:\n", i);

            /* event name */
            sprintf(fullname, OP_DRIVER_BASE"/%d/event", i);
            num = read_num(fullname);
            printf("    name: %s\n", find_event_name_from_id(num));

            /* profile interval */
            sprintf(fullname, OP_DRIVER_BASE"/%d/count", i);
            num = read_num(fullname);
            printf("    count: %d\n", num);
        }
        else {
            printf("Counter %d disabled\n", i);
        }
    }

    num = read_num(OP_DATA_DIR"/lock");
    if (num >= 0) {
        int fd;
        /* Still needs to check if this lock is left-over */
        sprintf(fullname, "/proc/%d", num);
        fd = open(fullname, O_RDONLY);
        if (fd == -1) {
            printf("Session directory is not clean - do \"opcontrol --setup\""
                   " before you continue\n");
            return;
        }
        else {
            close(fd);
            printf("oprofiled pid: %d\n", num);
            num = read_num(OP_DRIVER_BASE"/enable");
            printf("profiler is%s running\n", num == 0 ? " not" : "");
            num = read_num(OP_DRIVER_BASE"/stats/cpu0/sample_received");
            printf("  %9u samples received\n", num);
            num = read_num(OP_DRIVER_BASE"/stats/cpu0/sample_lost_overflow");
            printf("  %9u samples lost overflow\n", num);
#if 0
            /* FIXME - backtrace seems broken */
            num = read_num(OP_DRIVER_BASE"/stats/cpu0/backtrace_aborted");
            printf("  %9u backtrace aborted\n", num);
            num = read_num(OP_DRIVER_BASE"/backtrace_depth");
            printf("  %9u backtrace_depth\n", num);
#endif
        }
    }
    else {
        printf("oprofiled is not running\n");
    }
}

void do_reset() 
{
    int fd;

    fd = open(OP_DATA_DIR"/samples/current", O_RDONLY);
    if (fd == -1) {
        return;
    }
    close(fd);
    system("rm -r "OP_DATA_DIR"/samples/current");
}

int main(int argc, char * const argv[])
{
    int option_index;
    char command[1024];

    /* Initialize default strings */
    strcpy(vmlinux, "--no-vmlinux");
    strcpy(kernel_range, "");

    while (1) {
        int c = getopt_long(argc, argv, "", long_options, &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
            case 0:
                break;
            /* --event */
            case 'e':   
                if (num_events == MAX_EVENTS) {
                    fprintf(stderr, "More than %d events specified\n",
                            MAX_EVENTS);
                    exit(1);
                }
                if (process_event(optarg)) {
                    exit(1);
                }
                break;
            /* --vmlinux */
            case 'v':
                sprintf(vmlinux, "-k %s", optarg);
                break;
            /* --kernel-range */
            case 'r':
                sprintf(kernel_range, "-r %s", optarg);
                break;
            /* --shutdown */
            case 'h': {
                int pid = read_num(OP_DATA_DIR"/lock");
                if (pid >= 0) {
                    kill(pid, SIGKILL);
                }   
                setup_session_dir();
                break;
            }
            /* --status */
            case 't':
                do_status();
                break;
            default:
                usage();
                exit(1);
        }
    }
    verbose("list_events = %d\n", list_events);
    verbose("setup = %d\n", setup);

    if (list_events) {
        do_list_events();
    }

    if (quick) {
        process_event("CPU_CYCLES");
        setup = 1;
    }

    if (reset) {
        do_reset();
    }

    if (show_usage) {
        usage();
    }

    if (setup) {
        if (do_setup()) {
            fprintf(stderr, "do_setup failed");
            exit(1);
        }
    }

    if (num_events != 0) {
        int i;

        strcpy(command, "oprofiled --session-dir="OP_DATA_DIR);

#if !defined(WITH_ARM_V7_A)
        /* Since counter #3 can only handle CPU_CYCLES, check and shuffle the 
         * order a bit so that the maximal number of events can be profiled
         * simultaneously
         */
        if (num_events == 3) {
            for (i = 0; i < num_events; i++) {
                int event_idx = selected_events[i];

                if (event_info[event_idx].id == 0xff) {
                    break;
                }
            }

            /* No CPU_CYCLES is found */
            if (i == 3) {
                fprintf(stderr, "You can only specify three events if one of "
                                "them is CPU_CYCLES\n");
                exit(1);
            }
            /* Swap CPU_CYCLES to counter #2 (starting from #0)*/
            else if (i != 2) {
                int temp;

                temp = selected_events[2];
                selected_events[2] = selected_events[i];
                selected_events[i] = temp;

                temp = selected_counts[2];
                selected_counts[2] = selected_counts[i];
                selected_counts[i] = temp;
            }
        }
#endif


        /* Configure the counters and enable them */
        for (i = 0; i < num_events; i++) {
            int event_idx = selected_events[i];
            int setup_result = 0;

            if (i == 0) {
                snprintf(command+strlen(command), 1024 - strlen(command), 
                         " --events=");
            }
            else {
                snprintf(command+strlen(command), 1024 - strlen(command), 
                         ",");
            }
            /* Compose name:id:count:unit_mask:kernel:user, something like
             * --events=CYCLES_DATA_STALL:2:0:200000:0:1:1,....
             */
            snprintf(command+strlen(command), 1024 - strlen(command), 
                     "%s:%d:%d:%d:0:1:1",
                     event_info[event_idx].name,
                     event_info[event_idx].id,
                     i,
                     selected_counts[i]);

            setup_result |= echo_dev("1", 0, "user", i);
            setup_result |= echo_dev("1", 0, "kernel", i);
            setup_result |= echo_dev("0", 0, "unit_mask", i);
            setup_result |= echo_dev("1", 0, "enabled", i);
            setup_result |= echo_dev(NULL, selected_counts[i], "count", i);
            setup_result |= echo_dev(NULL, event_info[event_idx].id, 
                                     "event", i);
            if (setup_result) {
                fprintf(stderr, "Counter configuration failed for %s\n",
                        event_info[event_idx].name);
                fprintf(stderr, "Did you do \"opcontrol --setup\" first?\n");
                exit(1);
            }
        }

        /* Disable the unused counters */
        for (i = num_events; i < MAX_EVENTS; i++) {
            echo_dev("0", 0, "enabled", i);
        }

        snprintf(command+strlen(command), 1024 - strlen(command), " %s",
                 vmlinux);
        if (kernel_range[0]) {
            snprintf(command+strlen(command), 1024 - strlen(command), " %s",
                     kernel_range);
        }
        verbose("command: %s\n", command);
        system(command);
    }

    if (start) {
        echo_dev("1", 0, "enable", -1);
    }

    if (stop) {
        echo_dev("0", 0, "enable", -1);
    }
}
