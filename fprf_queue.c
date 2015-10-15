#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "common.h"
#include "fprf_queue_struct.h"

int g_fprf_queue_p;
int32 g_fprf_queue_n;
int32 * g_fprf_queue_internalQueue;


fprf_logical_in_queue_t * g_fprf_queue_in_queues;
fprf_logical_out_queue_t * g_fprf_queue_out_queues;

void fprf_queue_init(int32 p_n, int p_p)
{
    int i;

    //g_fprf_queue_internalQueue = (int32 *)malloc(p_n * p_p * sizeof(int32));
    g_fprf_queue_internalQueue = (int32 *)calloc(p_n * p_p, sizeof(int32));
    g_fprf_queue_in_queues = (fprf_logical_in_queue_t *)malloc(p_p * sizeof(fprf_logical_in_queue_t));
    g_fprf_queue_out_queues = (fprf_logical_out_queue_t *)malloc(p_p * sizeof(fprf_logical_out_queue_t));

    g_fprf_queue_p = p_p;
    g_fprf_queue_n = p_n;

    for(i = 0; i < p_p; i++)
    {
        g_fprf_queue_in_queues[i].qhead = (g_fprf_queue_internalQueue + p_n * i);
        g_fprf_queue_in_queues[i].direction = 1;
        g_fprf_queue_in_queues[i].qtail = g_fprf_queue_in_queues[i].qhead;

        g_fprf_queue_out_queues[i].qhead = (g_fprf_queue_internalQueue + p_n * (i + 1) - 1);
        g_fprf_queue_out_queues[i].direction = -1;
        g_fprf_queue_out_queues[i].count = 0;
        g_fprf_queue_out_queues[i].qfixedhead = (g_fprf_queue_internalQueue + p_n * i);
        g_fprf_queue_out_queues[i].qfixedtail = (g_fprf_queue_internalQueue + p_n * (i + 1) - 1);
    }
}

void fprf_queue_uninit(void)
{
    free(g_fprf_queue_internalQueue);
    free(g_fprf_queue_in_queues);
    free(g_fprf_queue_out_queues);
}

