#ifndef _FPRF_IMPLEMENTATION_H_
#define _FPRF_IMPLEMENTATION_H_

#include "common.h"

typedef struct fprf_options_s
{
	float c; // global relabeling freq. factor (gets multiplied with # of nodes)
	int32 numproc;

	//int32 idle_thread_busy_wait;

    int32 xfer_treshold; // attempt to transfer tasks only if you have more than that many tasks
    int32 xfer_check_interval; // attempt to transfer tasks after this many discharges
} fprf_options_t;

void fprf_init(FILE * p_fpin, FILE * p_fpout, fprf_options_t p_options);
int32 fprf_run(void);
void fprf_uninit(void);
void fprf_output(void);

#endif

