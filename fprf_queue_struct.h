#ifndef _FPRF_QUEUE_STRUCT_H_
#define _FPRF_QUEUE_STRUCT_H_

typedef struct fprf_logical_in_queue_s
{
    int32 *qhead; // pop from this position
    int32 *qtail; // one position further from the last element in the queue
    int direction; // +1 or -1. qhead will advance with this value
} fprf_logical_in_queue_t;

typedef struct fprf_logical_out_queue_s
{
    int32 *qhead; // push to this position
    int direction; // +1 or -1. qhead will advance with this value
    int32 count; // # of elements pushed
    int32 *qfixedhead; // points to g_internalQueue[i * g_n] for ith queue;
    int32 *qfixedtail; // points to g_internalQueue[(i + 1) * g_n - 1] for ith queue;
} fprf_logical_out_queue_t;

#endif

