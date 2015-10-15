#ifndef _FPRF_QUEUE_H_
#define _FPRF_QUEUE_H_

#include "common.h"
#include "fprf_queue_struct.h"

void fprf_queue_init(int32 p_n, int p_p);
void fprf_queue_uninit(void);

extern int g_fprf_queue_p;
extern int32 g_fprf_queue_n;
extern int32 * g_fprf_queue_internalQueue;

extern fprf_logical_in_queue_t * g_fprf_queue_in_queues;

extern fprf_logical_out_queue_t * g_fprf_queue_out_queues;

inline int32 fprf_queue_pop(int p_id)
{
    int32 v;
    if(g_fprf_queue_in_queues[p_id].qhead == g_fprf_queue_in_queues[p_id].qtail)
        return -1;
    v = *(g_fprf_queue_in_queues[p_id].qhead);
    //if(v < 0) // if we need to jump ahead to the next queue
    while(v < 0)
    {
        *(g_fprf_queue_in_queues[p_id].qhead) = 0;
        if(g_fprf_queue_in_queues[p_id].direction > 0)
            g_fprf_queue_in_queues[p_id].qhead -= v;
        else
            g_fprf_queue_in_queues[p_id].qhead += v;

        if(g_fprf_queue_in_queues[p_id].qhead == g_fprf_queue_in_queues[p_id].qtail)
            return -1;

        v = *(g_fprf_queue_in_queues[p_id].qhead);
    }

    *(g_fprf_queue_in_queues[p_id].qhead) = 0;
    g_fprf_queue_in_queues[p_id].qhead += g_fprf_queue_in_queues[p_id].direction;
    assert(v >= 0 && "we stored only nonnegative values, so now we should pop only nonnegative");
    //printf("%d GETS %d\n", p_id, v);
    return v;
}
inline void fprf_queue_push(int p_id, int32 p_v)
{
    assert(p_v >= 0 && "only push nonnegative values");
    *(g_fprf_queue_out_queues[p_id].qhead) = p_v;
    g_fprf_queue_out_queues[p_id].qhead += g_fprf_queue_out_queues[p_id].direction;
    g_fprf_queue_out_queues[p_id].count++;

    assert(g_fprf_queue_out_queues[p_id].count <= g_fprf_queue_n && "count must be < g_fprf_queue_n");
}
inline int fprf_queue_revert(void)
{
    int32 totalCount = 0, unfairshare, fairshare;
    int i;

    //printf("------reverting-------\n");
    for(i = 0; i < g_fprf_queue_p; i++)
    {
        totalCount += g_fprf_queue_out_queues[i].count;
    }
    if(totalCount == 0)
        return 0;

    fairshare = totalCount / g_fprf_queue_p;
    unfairshare = fairshare + (totalCount % g_fprf_queue_p);
    if(g_fprf_queue_out_queues[0].direction > 0)
    {
        fprf_logical_out_queue_t * cur_out_q = g_fprf_queue_out_queues;
        int32 *cur_buf = cur_out_q->qhead - cur_out_q->count;
        for(i = 0; i < g_fprf_queue_p; i++)
        {
            int32 moved = 0;
            int32 share = (i == g_fprf_queue_p - 1 ? unfairshare : fairshare);
            int32 curleft = (cur_out_q->qhead - cur_buf);

            g_fprf_queue_in_queues[i].direction = 1; // setup queue direction
            g_fprf_queue_in_queues[i].qhead = cur_buf;

            while(moved + curleft < share) // while out queue not enough
            {
                int32 * nextbeginning = cur_out_q[1].qhead - cur_out_q[1].count;
                *(cur_out_q->qhead) = (cur_out_q->qhead - nextbeginning); // links
                cur_out_q->direction = -1; // revert out queue direction
                cur_out_q->count = 0;
                cur_out_q->qhead = cur_out_q->qfixedtail;
                cur_out_q++;

                moved += curleft;

                cur_buf = nextbeginning;

                curleft = cur_out_q->count;
            }

            if(i == g_fprf_queue_p - 1)
            {
                for(; cur_out_q < g_fprf_queue_out_queues + g_fprf_queue_p; cur_out_q++)
                {
                    cur_out_q->direction = -1; // revert out queue direction
                    cur_out_q->count = 0;
                    cur_out_q->qhead = cur_out_q->qfixedtail;
                }
            }

            cur_buf += (share - moved);
            g_fprf_queue_in_queues[i].qtail = cur_buf;
        }
    }
    else // out queue was growing towards left
    {
        fprf_logical_out_queue_t * cur_out_q = g_fprf_queue_out_queues + g_fprf_queue_p - 1;
        int32 *cur_buf = cur_out_q->qhead + cur_out_q->count;
        for(i = g_fprf_queue_p - 1; i >= 0; i--)
        {
            int32 moved = 0;
            int32 share = (i == 0 ? unfairshare : fairshare);
            int32 curleft = (cur_buf - cur_out_q->qhead);

            g_fprf_queue_in_queues[i].direction = -1; // setup queue direction
            g_fprf_queue_in_queues[i].qhead = cur_buf;

            while(moved + curleft < share) // while out queue not enough
            {
                int32 * nextbeginning = cur_out_q[-1].qhead + cur_out_q[-1].count;
                *(cur_out_q->qhead) = (nextbeginning - cur_out_q->qhead); // links
                cur_out_q->direction = 1; // revert out queue direction
                cur_out_q->count = 0;
                cur_out_q->qhead = cur_out_q->qfixedhead;
                cur_out_q--;

                moved += curleft;

                cur_buf = nextbeginning;

                curleft = cur_out_q->count;
            }

            if(i == 0)
            {
                for(; cur_out_q >= g_fprf_queue_out_queues; cur_out_q--)
                {
                    cur_out_q->direction = 1; // revert out queue direction
                    cur_out_q->count = 0;
                    cur_out_q->qhead = cur_out_q->qfixedhead;
                }
            }

            cur_buf -= (share - moved);
            g_fprf_queue_in_queues[i].qtail = cur_buf;
        }
    }
    return (totalCount > 0);
}


#endif
