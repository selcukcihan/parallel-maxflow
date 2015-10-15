#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>

#include "common.h"
#include "fprf.h"
#include "fprf_queue.h"

/*
 * EXTERNAL defines are TBB_ATOMIC, NO_PARALLEL, etc. GCC_ATOMIC is not defined from outside
 */

#ifdef TBB_ATOMIC


#include <tbb/atomic.h>

#ifdef NO_PARALLEL
#undef NO_PARALLEL
#endif


#else


#ifndef NO_PARALLEL
#define GCC_ATOMIC
#endif


#endif

#include "linux_atomic.h"

#define PUSH_FRONT(id, node)    do \
                                { \
                                    if(NULL == g_threads[id].qhead) \
                                    { \
                                        g_threads[id].qhead = g_threads[id].qtail = g_threads[id].q; \
                                    } \
                                    else \
                                    { \
                                        if (g_threads[id].qhead == g_threads[id].q) \
                                            g_threads[id].qhead += g_n - 1; \
                                        else \
                                            g_threads[id].qhead--; \
                                    } \
                                    *(g_threads[id].qhead) = node; \
                                } while(0)

#define PUSH_BACK(id, node)     do \
                                { \
                                    if(NULL == g_threads[id].qhead) \
                                    { \
                                        g_threads[id].qhead = g_threads[id].qtail = g_threads[id].q; \
                                    } \
                                    else \
                                    { \
                                        g_threads[id].qtail++; \
                                        if (g_threads[id].qtail - g_threads[id].q == g_n) \
                                            g_threads[id].qtail = g_threads[id].q; \
                                    } \
                                    *(g_threads[id].qtail) = node; \
                                } while(0)

#define POP_FRONT(id)           do \
                                { \
                                    if (g_threads[(id)].qhead == g_threads[(id)].qtail) \
                                        g_threads[id].qhead = g_threads[id].qtail = NULL; \
                                    else \
                                    { \
                                        g_threads[(id)].qhead++; \
                                        if (g_threads[id].qhead - g_threads[id].q == g_n) \
                                            g_threads[id].qhead = g_threads[id].q; \
                                    } \
                                } while(0)

#define FRONT(id)               (*(g_threads[(id)].qhead))

typedef struct Arc_
{
    int32 w;
    int32 cap;
#ifdef TBB_ATOMIC
    tbb::atomic<int32> flow;
#else
    volatile int32 flow;
#endif

    struct Arc_ * next;
    struct Arc_ * other;
} Arc;

typedef struct Node_
{
#ifdef TBB_ATOMIC
    tbb::atomic<int32> e;
    tbb::atomic<int32> d;
#else
    volatile int32 e;
    volatile int32 d;
#endif

#ifdef DEBUG
#ifdef TBB_ATOMIC
    tbb::atomic<int32> u;
    tbb::atomic<int32> queued;
#else
    volatile int32 u;
    volatile int32 queued;
#endif
#endif

    Arc * cur;
    Arc * adj;
} Node;

typedef struct ThreadStat_
{
    int32 id;

    int32 loopCount;
    int32 innerLoopCount;
    int32 pushCount;
    int32 relabelCount;

    int32 busyWaitCount1;
    int32 busyWaitCount2;

    int32 noopCount;

    int32 globalRelabelingCount;
    int32 getTaskCount;
    int32 noTaskCount;
    int32 transferredCount;
    int32 dischargeCount;
#ifdef TIME_TASK_WAIT
    double taskWaitTime;
#endif
#ifdef TIME_TASK_TRANSFER
    double taskTransferTime;
#endif
} ThreadStat;

typedef struct fprf_thread_s
{
    int32 id;
    pthread_t pthread;
    ThreadStat stat;

    int32 * qhead;
    int32 * qtail;
    int32 * q;

#ifndef NO_PARALLEL
    int32 idle;
    // these two regulate sync. to q(transferring tasks)
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif
#ifdef PROFILE_WITH_GPROF
    struct itimerval itimer;
#endif
} fprf_thread_t;

static fprf_thread_t * g_threads = NULL;

#ifdef TBB_ATOMIC
static tbb::atomic<int32> g_idleCount;
static tbb::atomic<int32> g_globalRelabelingAwareCounter;
static tbb::atomic<int32> g_globalRelabelingFlag;
#else
static volatile int32 g_idleCount;
static volatile int32 g_globalRelabelingAwareCounter;
static volatile int32 g_globalRelabelingFlag;
#endif


static int32 g_n;
static int32 g_m;
static int32 g_s;
static int32 g_t;
static int32 g_nextGlobalRelabeling;
static int32 g_nextGlobalRelabelingIncrement;

static Node ** g_ppQueue = NULL;

#ifdef TBB_ATOMIC
static tbb::atomic<int32> g_taskCounter;
#else
static volatile int32 g_taskCounter;
#endif

#ifndef NO_PARALLEL
static pthread_barrier_t g_barrier;
static pthread_barrier_t g_globalRelabelingBarrier1;
static pthread_barrier_t g_globalRelabelingBarrier2;
#endif

static Node * nodes = NULL;
static Arc * arcs = NULL;
static int32 * arc_tail = NULL;
static int32 * arc_first = NULL;

static FILE * g_fpout = NULL;

static struct timeval g_begin, g_end;

static fprf_options_t g_options;

void fprf_init(FILE * p_fpin, FILE * p_fpout, fprf_options_t p_options)
{
    int32 i;
    struct timeval begin;
    struct timeval end;
    char line[LINE_MAX];
    char * ptr = NULL;
    int32 tmp = 0;
    int32 current = 0;
    int32 from, to, cap;
    double tm = 0;
    Node * ndp = NULL;
    Arc * arc_current = NULL;

    g_options = p_options;
    g_fpout = p_fpout;

#ifdef NO_PARALLEL
    g_options.numproc = 1;
#endif

    gettimeofday(&g_begin, NULL);

    gettimeofday(&begin, NULL);

    while(fgets(line ,LINE_MAX, p_fpin) != NULL)
    {
        if(line[0] == 'c')
            continue;
        if(line[0] == 'p')
        {
            ptr = strtok(line, " ");
            ptr = strtok(NULL, " ");
            ptr = strtok(NULL, " ");
            g_n = atoi(ptr);
            ptr = strtok(NULL, " ");
            g_m = atoi(ptr);
            nodes = (Node *)calloc(g_n + 2, sizeof(Node));
            arcs = (Arc *)calloc(g_m * 2 + 1, sizeof(Arc));
            arc_tail = (int32 *)calloc(g_m * 2, sizeof(int32));
            arc_first = (int32 *)calloc(g_n + 2, sizeof(int32));
            fprintf(g_fpout, "num nodes: %ld\tnum arcs: %ld\n", g_n, g_m);
        }
        else if(line[0] == 'n')
        {
            ptr = strtok(line, " ");
            ptr = strtok(NULL, " ");
            tmp = atoi(ptr) - 1;
            ptr = strtok(NULL, " ");
            if(ptr[0] == 's')
                g_s = tmp;
            else
                g_t = tmp;
        }
        else if(line[0] == 'a')
        {
            ptr = strtok(line, " ");
            ptr = strtok(NULL, " ");
            from = atoi(ptr) - 1;

            ptr = strtok(NULL, " ");
            to = atoi(ptr) - 1;

            ptr = strtok(NULL, " ");
            cap = atoi(ptr);

            arc_first[from + 1]++;
            arc_first[to + 1]++;

            arc_tail[current] = from;
            arc_tail[current + 1] = to;

            arcs[current].cap += cap;
            arcs[current].w = to;
            arcs[current].other = arcs + current + 1;

            arcs[current + 1].w = from;
            arcs[current + 1].other = arcs + current;

            current += 2;
        }
    }

    nodes[0].adj = arcs + 0;
    for(i = 1; i < g_n; i++)
    {
        arc_first[i] += arc_first[i - 1];
        nodes[i].adj = arcs + arc_first[i];
    }

    for(i = 0; i < g_n - 1; i++)
    {
        int32 arc_num;
        for(arc_num = arc_first[i]; arc_num < nodes[i + 1].adj - arcs; arc_num++)
        {
            int32 tail = arc_tail[arc_num];
            while(tail != i)
            {
                int32 arc_new_num  = arc_first[tail];
                Arc * arc_current  = arcs + arc_num;
                Arc * arc_new      = arcs + arc_new_num;

                /* arc_current must be cited in the position arc_new    
                   swapping these arcs:                                 */

                int32 head_p = arc_new->w;
                arc_new->w = arc_current->w;
                arc_current->w  = head_p;

                int32 cap = arc_new->cap;
                arc_new->cap = arc_current->cap;
                arc_current->cap = cap;

                if(arc_new != arc_current->other)
                {
                    Arc * arc_tmp = arc_new->other;
                    arc_new->other = arc_current->other;
                    arc_current->other = arc_tmp;

                    arc_current->other->other = arc_current;
                    arc_new->other->other = arc_new;
                }

                arc_tail[arc_num] = arc_tail[arc_new_num];
                arc_tail[arc_new_num] = tail;

                /* we increase arc_first[tail]  */
                arc_first[tail] ++ ;

                tail = arc_tail[arc_num];
            }
        }
    }
    for(ndp = nodes; ndp <= nodes + g_n; ndp++)
    {
        ndp->adj = (Arc*)NULL;
        ndp->cur = (Arc*)NULL;
    }

    for(arc_current = arcs + (2*g_m-1); arc_current >= arcs; arc_current--)
    {
        int32 arc_num = arc_current - arcs;
        int32 tail = arc_tail[arc_num];
        Node * ndp = nodes + tail;
        arc_current->next = ndp->adj;
        ndp->adj = arc_current;
    }

    free(arc_tail);
    free(arc_first);

    g_ppQueue = (Node **)calloc(g_n, sizeof(Node *));

    fprf_queue_init(g_n, g_options.numproc);

    gettimeofday(&end, NULL);
    tm = (end.tv_sec - begin.tv_sec) + ((end.tv_usec - begin.tv_usec) / 1000000.0f);
    fprintf(g_fpout, "inittime: %f\n", tm);
}

//! this routine verifies whether the current preflow is actually a flow
static void fprf_verify_flow(void)
{
    int32 totalExcess = 0;
    int32 _v;
    for(_v = 0; _v < g_n; _v++)
    {
        Arc * a;
        Node * v = nodes + _v;
        totalExcess += v->e;
        int32 inTotal = 0;
        int32 outTotal = 0;
        for(a = v->adj; a != NULL; a = a->next)
        {
            assert(a->cap >= a->flow && "flow can not exceed capacity");
            assert(a->other->cap >= a->other->flow && "flow can not exceed capacity 2");
            assert(a->flow == -a->other->flow && "antisimetri should hold");

            outTotal += a->flow;
            inTotal += a->other->flow;
        }
#ifdef FULL_CALCULATION
        if(_v != g_s && _v != g_t)
            assert(v->e == 0 && "excess should be zero at transhipment nodes");
#endif
    }
    assert(totalExcess == 0 && "total excess should be consistent");
}

static pthread_barrier_t g_parallel_glb_barrier1;
static pthread_barrier_t g_parallel_glb_barrier2;

static int g_hasmore = 1;
static int32 fprf_calculate_labels_in_parallel(int p_id)
{
    int32 nextlevel = 0;
#ifdef DEBUG
    int32 level = -1; // current level. nodes being labeled at this level will get the label level + 1
#endif
    int32 retVal = 0; // will hold the # of active nodes

    pthread_barrier_wait(&g_parallel_glb_barrier1);

    while(g_hasmore)
    {
        Arc * a = NULL;
        int32 _v;

        nextlevel++;
#ifdef DEBUG
        level++;
#endif

        while((_v = fprf_queue_pop(p_id)) != -1)
        {
            Node * v = nodes + _v;
            assert(v->d == level);

            for(a = v->adj; a != NULL; a = a->next)
            {
                int32 original;
                int32 _w = a->w;
                Node * w = nodes + _w;
                if(a->other->cap - a->other->flow && w->d > nextlevel)
                {
                    SYNC_FETCH_AND_STORE(w->d, g_n, nextlevel, original);
                    assert(w->d == nextlevel);
                    assert(original == g_n || original == nextlevel);
                    if(original > nextlevel)
                    {
                        w->cur = w->adj;

                        if(w->e > 0)
                        {
                            PUSH_FRONT(p_id, _w);
#ifdef DEBUG
                            w->queued = 1;
#endif
                            retVal++;
                        }

                        // push to beginning of the queue
                        fprf_queue_push(p_id, _w);
                    }
                }
            }
        }

        pthread_barrier_wait(&g_parallel_glb_barrier1);
        if(p_id == 0)
            g_hasmore = fprf_queue_revert();
        pthread_barrier_wait(&g_parallel_glb_barrier2);
    }
#ifndef NO_DEBUG_MESSAGES
    g_threads[p_id].stat.globalRelabelingCount++;
#endif

    return retVal;
}

static int32 fprf_calculate_labels(int32 p_everyone)
{
    Node ** ppQHead = NULL;
    Node ** ppQTail = NULL;

    int32 current = 0;
    int32 retVal = 0; // will hold the # of active nodes
    int32 i, v;

    for(i = 0; i < g_options.numproc; i++)
    {
        g_threads[i].qhead = g_threads[i].qtail = NULL;
    }
    g_taskCounter = 0;

    for(v = 0; v < g_n; v++)
    {
#ifdef FULL_CALCULATION
        nodes[v].d = g_n * 2;
#else
        nodes[v].d = g_n;
#endif
#ifdef DEBUG
        nodes[v].u = 0;
        nodes[v].queued = 0;
#endif
    }
    nodes[g_t].d = 0;

    *g_ppQueue = nodes + g_t;

    for(ppQHead = g_ppQueue, ppQTail = g_ppQueue + 1; ppQHead != ppQTail; ppQHead++)
    {
        Arc * a;
        Node * v = *ppQHead;
        int32 w_d = v->d + 1;
        for(a = v->adj; a != NULL; a = a->next)
        {
            int32 _w = a->w;
            Node * w = nodes + _w;
#ifdef FULL_CALCULATION
            if(w->d == g_n * 2)
#else
            if(w->d == g_n)
#endif
            {
                if(a->other->cap - a->other->flow > 0)
                {
                    w->cur = w->adj;
                    w->d = w_d;
                    if(w->e > 0)
                    {
                        PUSH_FRONT(current, _w);
#ifdef DEBUG
                        w->queued = 1;
#endif
                        if(p_everyone)
                            current++;
                        if(current == g_options.numproc)
                            current = 0;

                        retVal++;
                        g_taskCounter++;
                    }
                    *ppQTail = w;
                    ppQTail++;
                }
            }
        }
    }

#ifdef FULL_CALCULATION

    *g_ppQueue = nodes + g_s;

    nodes[g_s].d = g_n;

    for(ppQHead = g_ppQueue, ppQTail = g_ppQueue + 1; ppQHead != ppQTail; ppQHead++)
    {
        Arc * a;
        Node * v = *ppQHead;
        int32 w_d = v->d + 1;
        for(a = v->adj; a != NULL; a = a->next)
        {
            int32 _w = a->w;
            Node * w = nodes + _w;
            if(w->d == g_n * 2)
            {
                if(a->other->cap - a->other->flow > 0)
                {
                    w->cur = w->adj;
                    w->d = w_d;
                    if(w->e > 0)
                    {
                        PUSH_FRONT(current, _w);
#ifdef DEBUG
                        w->queued = 1;
#endif
                        if(p_everyone)
                            current++;
                        if(current == g_options.numproc)
                            current = 0;

                        retVal++;
                        g_taskCounter++;
                    }
                    *ppQTail = w;
                    ppQTail++;
                }
            }
        }
    }

#endif

#ifndef NO_DEBUG_MESSAGES
    g_threads[0].stat.globalRelabelingCount++;
#endif

    return retVal;
}

#ifndef NO_PARALLEL
static inline int32 fprf_push(int32 p_id, int32 p_v)
{
    Node  * w; /* sucsessor of i */
    int32  w_d; /* rank of the next layer */
    Arc * a; /* current arc (i,j) */
    int32 v_e;

    Node * v = nodes + p_v;

    ATOMIC_LOAD(w_d, v->d);
    w_d--;

    ATOMIC_LOAD(v_e, v->e);
    
    int32 totalFlowPushed = 0;

    assert(v_e > 0 && "there must be some excess here");

    for(a = v->cur; a != NULL; a = a->next) // iterate through adj list of v
    {
        int32 currentFlow, residual;

        ATOMIC_LOAD(currentFlow, a->flow);
        residual = a->cap - currentFlow;

        if(residual > 0) // arc a is not saturated
        {
            int32 _w_d;
            int32 _w = a->w;

            w = nodes + _w;

            ATOMIC_LOAD(_w_d, w->d);

            if(_w_d == w_d)
            {
                int32 flow, tmpe, oldE;

                flow = (v_e < residual ? v_e : residual);

                SYNC_FETCH_AND_ADD_NO_RETURN(a->flow, flow);
                SYNC_FETCH_AND_ADD_NO_RETURN(a->other->flow, -flow);

#ifndef NO_DEBUG_MESSAGES
                g_threads[p_id].stat.pushCount++; // statistics
#endif

                SYNC_FETCH_AND_ADD(w->e, flow, oldE);

                if(w_d > 0 && oldE == 0) // this is not target node and this above push caused w to become active from inactive state
                {
#ifdef DEBUG
                    int32 tmp;
                    SYNC_FETCH_AND_STORE(w->queued, 0, 1, tmp);
                    assert(tmp == 0 && "this node should not have been queued");
                    //assert(w->queued.fetch_and_store(1) == 0 && "this node should not have been queued");
#endif
                    SYNC_FETCH_AND_ADD_NO_RETURN(g_taskCounter, 1);
                    PUSH_BACK(p_id, _w);
                }

                totalFlowPushed += flow;
                v_e -= flow;


                SYNC_FETCH_AND_ADD(v->e, -flow, tmpe);

                if(tmpe == flow)
                    break;
                if(v_e == 0)
                {
                    v_e = tmpe - flow;
                }
            } // j belongs to the next layer
        } // a  is not saturated
    } // end of scanning arcs from  i

    v->cur = a;
    return ((a == NULL) ? 1 : 0);
}
#else // NO_PARALLEL
static inline int32 fprf_push(int32 p_id, int32 p_v)
{
    Node  * w; /* sucsessor of i */
    int32  w_d; /* rank of the next layer */
    Arc * a; /* current arc (i,j) */

    Node * v = nodes + p_v;
    w_d = v->d - 1;

    assert(v->e > 0 && "there must be some excess here");

    for(a = v->cur; a != NULL; a = a->next) // iterate through adj list of v
    {
        int32 residual = a->cap - a->flow;
        if(residual > 0) // arc a is not saturated
        {
            int32 _w = a->w;
            w = nodes + _w;
            if(w->d == w_d)
            {
                int32 flow = (v->e < residual ? v->e : residual);

                a->flow += flow;
                a->other->flow -= flow;

#ifndef NO_DEBUG_MESSAGES
                g_threads[p_id].stat.pushCount++; // statistics
#endif

                if(w_d > 0 && w->e == 0) // this is not target node and this above push caused w to become active from inactive state
                {
#ifdef DEBUG
                    assert(w->queued == 0 && "this node should not have been queued");
                    w->queued = 1;
#endif

                    g_taskCounter++;
                    PUSH_BACK(p_id, _w);
                }
                w->e += flow;

                v->e -= flow;

                if(v->e == 0)
                    break;
            } // j belongs to the next layer
        } // a  is not saturated
    } // end of scanning arcs from  i

    v->cur = a;
    return ((a == NULL) ? 1 : 0);
}
#endif // NO_PARALLEL

#ifndef NO_PARALLEL
static inline int32 fprf_relabel(int32 p_id, int32 p_v)
{
    int32 w_d; // minimal rank of a node available from j
    Arc * new_cur = NULL;
    Arc * a;

    Node * v = nodes + p_v;

    g_threads[p_id].stat.relabelCount++;

#ifdef FULL_CALCULATION
    w_d = g_n * 2;
#else
    w_d = g_n;
#endif
    ATOMIC_STORE(v->d, w_d);

    for(a = v->adj; a != NULL; a = a->next) // search for the node with lowest distance label, within the adjacent nodes to v to which an arc with residual capacity exists
    {
        int32 flow;
        ATOMIC_LOAD(flow, a->flow);

        if(a->cap - flow > 0 )
        {
            int32 _w_d;
            int32 _w = a->w;

            Node * w = nodes + _w;

            ATOMIC_LOAD(_w_d, w->d);
            if(_w_d < w_d)
            {
                new_cur = a;
                w_d = _w_d;
            }
        }
    }

    w_d++;
#ifdef FULL_CALCULATION
    if(w_d < g_n * 2)
#else
    if(w_d < g_n)
#endif
    {
        ATOMIC_STORE(v->d, w_d);
        v->cur = new_cur;
    }

    return w_d;
}
#else // NO_PARALLEL
static inline int32 fprf_relabel(int32 p_id, int32 p_v)
{
    int32 w_d; // minimal rank of a node available from j
    Arc * new_cur = NULL;
    Arc * a;

    Node * v = nodes + p_v;

    g_threads[p_id].stat.relabelCount++;

#ifdef FULL_CALCULATION
    v->d = w_d = g_n * 2;
#else
    v->d = w_d = g_n;
#endif

    for(a = v->adj; a != NULL; a = a->next) // search for the node with lowest distance label, within the adjacent nodes to v to which an arc with residual capacity exists
    {
        int32 flow = a->flow;

        if(a->cap - flow > 0 )
        {
            int32 _w = a->w;

            Node * w = nodes + _w;
            if(w->d < w_d)
            {
                new_cur = a;
                w_d = w->d;
            }
        }
    }

    w_d++;
#ifdef FULL_CALCULATION
    if(w_d < g_n * 2)
#else
    if(w_d < g_n)
#endif
    {
        v->d = w_d;
        v->cur = new_cur;
    }

    return w_d;
}
#endif // NO_PARALLEL

double g_glbWaiting = 0;
#ifndef NO_PARALLEL
static int32 fprf_thread_routine_perform_global_relabeling(int32 p_id)
{
    int32 globalRelabelingAwareCounter;
    int32 tmp;
    int32 taskCount = 0;
    struct timeval begin;
    struct timeval end;
    double tm;

    //fprintf(g_fpout, "thread %d reached global relabeling barrier\n", p_id);

    SYNC_FETCH_AND_ADD(g_globalRelabelingAwareCounter, 1, globalRelabelingAwareCounter);

    pthread_barrier_wait(&g_globalRelabelingBarrier1);
    if(p_id == 0)
    {
        int i, v;
        gettimeofday(&begin, NULL);
        g_nextGlobalRelabeling += g_nextGlobalRelabelingIncrement;

        SYNC_FETCH_AND_STORE(g_globalRelabelingAwareCounter, g_options.numproc, 0, tmp);
        assert(tmp == g_options.numproc && "everyone should be aware at this point");

        SYNC_FETCH_AND_STORE(g_globalRelabelingFlag, 1, 0, tmp);
        assert(tmp == 1 && "global relabeling flag should be set at this point");


        for(i = 0; i < g_options.numproc; i++)
        {
            g_threads[i].qhead = g_threads[i].qtail = NULL;
        }
        g_taskCounter = 0;

        for(v = 0; v < g_n; v++)
        {
#ifdef FULL_CALCULATION
            nodes[v].d = g_n * 2;
#else
            nodes[v].d = g_n;
#endif
#ifdef DEBUG
            nodes[v].u = 0;
            nodes[v].queued = 0;
#endif
        }
        nodes[g_t].d = 0;

        fprf_queue_push(0, g_t);
        g_hasmore = fprf_queue_revert();

        g_taskCounter = 0;
    }

    taskCount = fprf_calculate_labels_in_parallel(p_id);
    SYNC_FETCH_AND_ADD_NO_RETURN(g_taskCounter, taskCount);

    pthread_barrier_wait(&g_globalRelabelingBarrier2);

    if(p_id == 0)
    {
        gettimeofday(&end, NULL);
        tm = (end.tv_sec - begin.tv_sec) + ((end.tv_usec - begin.tv_usec) / 1000000.0f);
        g_glbWaiting += tm;
    }
    //fprintf(g_fpout, "thread %d leaving global relabeling barrier\n", p_id);

    return (g_taskCounter > 0);
}
#else // SERIAL
static int32 fprf_thread_routine_perform_global_relabeling(int32 p_id)
{
    assert(g_globalRelabelingAwareCounter == 1 && "i should be aware at this point");
    g_globalRelabelingAwareCounter = 0;

    assert(g_globalRelabelingFlag == 1 && "global relabeling flag should be set at this point");
    g_globalRelabelingFlag = 0;

    return 1;
}
#endif

#ifndef NO_PARALLEL
static inline int32 fprf_thread_routine_inform_global_relabeling(int32 p_id) // return false if needed to call once again
{
    int32 idleCount;
    int32 globalRelabelingFlag;
    int32 globalRelabelingAwareCounter;
    int32 j;
#ifdef TIME_TASK_TRANSFER
    struct timeval begin;
    struct timeval end;
#endif

    ATOMIC_LOAD(globalRelabelingFlag, g_globalRelabelingFlag);
    assert(globalRelabelingFlag == 1 && "at this point global relabeling flag should be set");

    ATOMIC_LOAD(idleCount, g_idleCount);
    ATOMIC_LOAD(globalRelabelingAwareCounter, g_globalRelabelingAwareCounter);
    if(idleCount == 0)
    {
        pthread_yield();
        return (globalRelabelingAwareCounter == g_options.numproc - 1);
    }

#ifdef TIME_TASK_TRANSFER
    gettimeofday(&begin, NULL);
#endif
    for(j = 1; j < g_options.numproc; j++)
    {
        int32 i = p_id + j;
        if(i >= g_options.numproc)
            i -= g_options.numproc;
        //fprintf(g_fpout, "trying to wake up %d\n", i);
        if(pthread_mutex_trylock(&g_threads[i].mutex) == 0)
        {
            if(g_threads[i].idle)
            {
                SYNC_FETCH_AND_ADD_NO_RETURN(g_idleCount, -1);
                g_threads[i].idle = 0;
            }
            pthread_cond_signal(&g_threads[i].cond);
            pthread_mutex_unlock(&g_threads[i].mutex);
        }
    }
#ifdef TIME_TASK_TRANSFER
    gettimeofday(&end, NULL);
    g_threads[p_id].stat.taskTransferTime += (end.tv_sec - begin.tv_sec) + ((end.tv_usec - begin.tv_usec) / 1000000.0f);
#endif

    pthread_yield();
    return 0;
}
#else
#define fprf_thread_routine_inform_global_relabeling(id) 0
#endif

#ifndef NO_PARALLEL
static int32 fprf_thread_routine_transfer_task(int32 p_id)
{
    int32 task_count = 0;
    int32 * qhead = g_threads[p_id].qhead;
    int32 * qtail = g_threads[p_id].qtail;
    int32 idleCount;

    assert(g_threads[p_id].qhead && "at this point the threads should have at least one task in its queue");
    //assert(!g_threads[p_id].idle && "this thread should not be idle at this point");

    ATOMIC_LOAD(idleCount, g_idleCount);
    if(idleCount == 0)
        return 0;

    task_count = (qtail >= qhead ? (qtail - qhead + 1) : (qtail - qhead + 1 + g_n));

    if(task_count >= g_options.xfer_treshold)
    {
        int32 j;
#ifdef TIME_TASK_TRANSFER
        struct timeval begin;
        struct timeval end;
        gettimeofday(&begin, NULL);
#endif
        for(j = 1; j < g_options.numproc; j++)
        {
            int32 i = p_id + j;
            if(i >= g_options.numproc)
                i -= g_options.numproc;
            if(pthread_mutex_trylock(&g_threads[i].mutex) == 0)
            {
                if(g_threads[i].idle)
                {
                    int32 xferred;
                    task_count = task_count / 2;
                    for(xferred = 0; xferred < task_count; xferred++)
                    {
                        int32 h = FRONT(p_id);
                        POP_FRONT(p_id);
                        PUSH_BACK(i, h);
                    }
#ifndef NO_DEBUG_MESSAGES
                    g_threads[i].stat.transferredCount += task_count;
#endif
                    SYNC_FETCH_AND_ADD_NO_RETURN(g_idleCount, -1);
                    g_threads[i].idle = 0;
                    pthread_cond_signal(&g_threads[i].cond);
                    pthread_mutex_unlock(&g_threads[i].mutex);

#ifdef TIME_TASK_TRANSFER
                    gettimeofday(&end, NULL);
                    g_threads[p_id].stat.taskTransferTime += (end.tv_sec - begin.tv_sec) + ((end.tv_usec - begin.tv_usec) / 1000000.0f);
#endif
                    return 1;
                }
                pthread_mutex_unlock(&g_threads[i].mutex);
            }
        }
#ifdef TIME_TASK_TRANSFER
        gettimeofday(&end, NULL);
        g_threads[p_id].stat.taskTransferTime += (end.tv_sec - begin.tv_sec) + ((end.tv_usec - begin.tv_usec) / 1000000.0f);
#endif

    }
    return 0;
}
#else
#define fprf_thread_routine_transfer_task(id) (void)(id)
#endif

#ifndef NO_PARALLEL
static int32 fprf_thread_routine_wait_for_task(int32 p_id)
{
    int32 globalRelabelingFlag;
#ifdef TIME_TASK_WAIT
    struct timeval begin;
    struct timeval end;
    gettimeofday(&begin, NULL);
#endif

#ifndef NO_DEBUG_MESSAGES
    g_threads[p_id].stat.noTaskCount++;
#endif
    SYNC_FETCH_AND_ADD_NO_RETURN(g_idleCount, 1);

    pthread_mutex_lock(&g_threads[p_id].mutex);
    g_threads[p_id].idle = 1; // only we set idle to 1. and only another thread will reset our idle to 0
    pthread_cond_wait(&g_threads[p_id].cond, &g_threads[p_id].mutex);
    pthread_mutex_unlock(&g_threads[p_id].mutex);
#ifdef TIME_TASK_WAIT
    gettimeofday(&end, NULL);
    g_threads[p_id].stat.taskWaitTime += (end.tv_sec - begin.tv_sec) + ((end.tv_usec - begin.tv_usec) / 1000000.0f);
#endif

    ATOMIC_LOAD(globalRelabelingFlag, g_globalRelabelingFlag);
    if(globalRelabelingFlag) // global relabeling seen
        return 2;
    else
        return (g_threads[p_id].qhead != NULL ? 0 : 1);
}
#else
#define fprf_thread_routine_wait_for_task(id) (void)(id)
#endif

static void * fprf_thread_routine_parallel_phase(int p_id)
{
    double tm = 0;
    int32 xfer_skipped = 0;

    struct timeval begin;
    struct timeval end;

    gettimeofday(&begin, NULL);

    while(1) /* main loop */
    {
        int32 globalRelabelingFlag;
#ifndef NO_DEBUG_MESSAGES
        g_threads[p_id].stat.loopCount++;
#endif
        ATOMIC_LOAD(globalRelabelingFlag, g_globalRelabelingFlag);
        if(globalRelabelingFlag == 1)
        {
            if(!fprf_thread_routine_perform_global_relabeling(p_id))
            {
#ifdef DEBUG
                int32 taskCounter;
                ATOMIC_LOAD(taskCounter, g_taskCounter);
                assert(taskCounter == 0 && "there should not be any tasks since after global relabeling we decided to exit main loop");
#endif
                break;
            }
        }

        if(g_threads[p_id].qhead == NULL)
        {
#ifndef NO_PARALLEL
            int32 waitStatus;
            waitStatus = fprf_thread_routine_wait_for_task(p_id);
            if(waitStatus == 1)
            {
#ifdef DEBUG
                int32 taskCounter;
                ATOMIC_LOAD(taskCounter, g_taskCounter);
                assert(taskCounter == 0 && "there should not be any tasks since we woke up and saw that our queue is empty");
#endif
                break;
            }
            else if(waitStatus == 2) // global relabeling seen
            {
                continue;
            }
#endif
        }
        else
        {
            int32 taskCounter;
            int32 i_d;
            // at this point we must have at least one task, we will get that node and discharge from it as much as we can
            int32 i = FRONT(p_id);
            POP_FRONT(p_id);

#ifdef DEBUG
            {
                int32 tmp;

                SYNC_FETCH_AND_STORE(nodes[i].queued, 1, 0, tmp);
                assert(tmp == 1 && "this node should have been queued");

                SYNC_FETCH_AND_STORE(nodes[i].u, 0, 1, tmp);
                assert(tmp == 0 && "this node should be free from locks");
            }
#endif

#ifndef NO_DEBUG_MESSAGES
            g_threads[p_id].stat.getTaskCount++;
#endif

            ATOMIC_LOAD(i_d, nodes[i].d);
#ifndef NO_DEBUG_MESSAGES
            g_threads[p_id].stat.dischargeCount++;
#endif

#ifdef FULL_CALCULATION
            while(i_d < g_n * 2)
#else
            while(i_d < g_n)
#endif
            {
#ifndef NO_DEBUG_MESSAGES
                g_threads[p_id].stat.innerLoopCount++;
#endif
                int32 cc = fprf_push(p_id, i);
                if(cc == 0)
                    break;

                // we should relabel i at this point
                i_d = fprf_relabel(p_id, i);
            }
            if(p_id == 0 && (g_threads[p_id].stat.relabelCount) > g_nextGlobalRelabeling) // only thread # 0 decides global relabeling
            {
                int32 tmp;
                SYNC_FETCH_AND_STORE(g_globalRelabelingFlag, 0, 1, tmp);
                assert(tmp == 0 && "global relabeling should not have been decided");
                while(!fprf_thread_routine_inform_global_relabeling(p_id));
            }
#ifdef DEBUG
            {
                int32 tmp;
                SYNC_FETCH_AND_STORE(nodes[i].u, 1, 0, tmp);
                assert(tmp == 1 && "this node should have been locked previously, we have unlocked it");
                //nodes[i].u = 0;
            }
#endif
            SYNC_FETCH_AND_ADD(g_taskCounter, -1, taskCounter);
            if(taskCounter == 1) // previous value was 1, so g_taskCounter is actually 0 at this point
            {
#ifdef DEBUG
                int32 idleCount;
                ATOMIC_LOAD(idleCount, g_idleCount);
                assert(idleCount == (g_options.numproc - 1) && "only this thread should not be idle");
#endif
                ATOMIC_LOAD(globalRelabelingFlag, g_globalRelabelingFlag);
                if(globalRelabelingFlag == 1)
                {
                    fprintf(g_fpout, "thread %d detected there are no tasks but global relabeling flag is set\n", p_id);
                    continue;
                }

                fprintf(g_fpout, "end of first phase detected by %d\n", p_id);
                // this thread detected end of first phase, now wake up any sleepers and give control to thread #0
#ifndef NO_PARALLEL
                {
                    int32 j;
                    for(j = 0; j < g_options.numproc; j++)
                    {
                        if(j != p_id)
                        {
                            pthread_mutex_lock(&g_threads[j].mutex);
                            pthread_cond_signal(&g_threads[j].cond);
                            pthread_mutex_unlock(&g_threads[j].mutex);
                        }
                    }
                }
#endif
                break;
            }
            else
            {
                xfer_skipped++;
                if(xfer_skipped == g_options.xfer_check_interval)
                {
                    xfer_skipped = 0;
                    if(g_threads[p_id].qhead)
                        fprf_thread_routine_transfer_task(p_id);
                }
            }
        }
    } /* end of the main loop */
#ifndef NO_PARALLEL
    fprintf(g_fpout, "thread %d reached barrier\n", p_id);
    pthread_barrier_wait(&g_barrier);
#endif
    if(p_id == 0)
    {
        gettimeofday(&end, NULL);
        tm = (end.tv_sec - begin.tv_sec) + ((end.tv_usec - begin.tv_usec) / 1000000.0f);
        fprintf(g_fpout, "paralleltime: %f\n", tm);
    }
    return NULL;
}

#ifndef NO_PARALLEL
static void * fprf_thread_routine_serial_phase(int p_id)
{
    double tm;

    struct timeval begin;
    struct timeval end;

    gettimeofday(&begin, NULL);

    if(fprf_calculate_labels(0) == 0)
        goto serial_end;

    while(1) /* main loop */
    {
#ifndef NO_DEBUG_MESSAGES
        g_threads[p_id].stat.loopCount++;
#endif
        if((g_threads[p_id].stat.relabelCount) > g_nextGlobalRelabeling)
        {
            if(!fprf_calculate_labels(0))
            {
#ifdef DEBUG
                int32 taskCounter;
                ATOMIC_LOAD(taskCounter, g_taskCounter);
                assert(taskCounter == 0 && "there should not be any tasks since after global relabeling we decided to exit main loop");
#endif
                break;
            }
            g_nextGlobalRelabeling += g_nextGlobalRelabelingIncrement;
        }
        if(g_threads[p_id].qhead == NULL)
        {
            break;
        }
        else
        {
            // at this point we must have at least one task, we will get that node and discharge from it as much as we can
            int32 i = FRONT(p_id);
            POP_FRONT(p_id);

#ifdef DEBUG
            {
                int32 tmp;

                ATOMIC_LOAD(tmp, nodes[i].queued);
                assert(tmp == 1 && "this node should be queued");
                //assert(nodes[i].queued == 1 && "this node should be queued");

                SYNC_FETCH_AND_STORE(nodes[i].u, 0, 1, tmp);
                assert(tmp == 0 && "this node should be free from locks");
                //assert(nodes[i].u.fetch_and_store(1) == 0 && "this node should be free from locks");

                SYNC_FETCH_AND_STORE(nodes[i].queued, 1, 0, tmp);
                assert(tmp == 1 && "this node should have been queued");
                //assert(nodes[i].queued.fetch_and_store(0) == 1 && "this node should have been queued");
            }
#endif

#ifndef NO_DEBUG_MESSAGES
            g_threads[p_id].stat.getTaskCount++;
#endif

            int32 i_d = nodes[i].d;
#ifndef NO_DEBUG_MESSAGES
            g_threads[p_id].stat.dischargeCount++;
#endif

#ifdef FULL_CALCULATION
            while(i_d < g_n * 2)
#else
            while(i_d < g_n)
#endif
            {
#ifndef NO_DEBUG_MESSAGES
                g_threads[p_id].stat.innerLoopCount++;
#endif
                int32 cc = fprf_push(p_id, i);
                if(cc == 0)
                    break;

                // we should relabel i at this point
                i_d = fprf_relabel(p_id, i);
            }
#ifdef DEBUG
            {
                int32 tmp;
                SYNC_FETCH_AND_STORE(nodes[i].u, 1, 0, tmp);
                assert(tmp == 1 && "this node should have been locked previously, we have unlocked it");
                //nodes[i].u = 0;
            }
#endif

            g_taskCounter--;
        }
    } /* end of the main loop */

serial_end:
    gettimeofday(&end, NULL);
    tm = (end.tv_sec - begin.tv_sec) + ((end.tv_usec - begin.tv_usec) / 1000000.0f);

    fprintf(g_fpout, "serialtime: %f\n", tm);

    return NULL;
}
#else
#define fprf_thread_routine_serial_phase(id) (void)(id)
#endif

static void * fprf_thread_routine(void * p_pArg)
{
    int id = *((int32 *)p_pArg);

#ifdef PROFILE_WITH_GPROF
    setitimer(ITIMER_PROF, &g_threads[id].itimer, NULL);
#endif

    fprf_thread_routine_parallel_phase(id);

    if(id == 0)
        fprf_thread_routine_serial_phase(id);

    fprintf(g_fpout, "thread #%d is exiting\n", id);
    pthread_exit(0);
    return NULL;
}

static void fprf_initialize_thread_local_storage(void)
{
    int32 i;
    g_threads = (fprf_thread_t *)malloc(sizeof(fprf_thread_t) * g_options.numproc);

    for(i = 0; i < g_options.numproc; i++)
    {
        g_threads[i].qhead = g_threads[i].qtail = NULL;
        g_threads[i].q = (int32 *)malloc(sizeof(int32) * g_n);
        g_threads[i].id = i;
#ifndef NO_PARALLEL
        g_threads[i].idle = 0;
        pthread_mutex_init(&g_threads[i].mutex, NULL);
        pthread_cond_init(&g_threads[i].cond, NULL);
#endif

        memset(&g_threads[i].stat, 0, sizeof(g_threads[i].stat));
    }

#ifndef NO_PARALLEL
    if(pthread_barrier_init(&g_barrier, NULL, g_options.numproc) != 0)
    {
        fprintf(g_fpout, "error in pthread_barrier_init\n");
        exit(1);
    }
    if(pthread_barrier_init(&g_globalRelabelingBarrier1, NULL, g_options.numproc) != 0)
    {
        fprintf(g_fpout, "error in pthread_barrier_init\n");
        exit(1);
    }
    if(pthread_barrier_init(&g_globalRelabelingBarrier2, NULL, g_options.numproc) != 0)
    {
        fprintf(g_fpout, "error in pthread_barrier_init\n");
        exit(1);
    }
    if(pthread_barrier_init(&g_parallel_glb_barrier1, NULL, g_options.numproc) != 0)
    {
        fprintf(g_fpout, "error in pthread_barrier_init\n");
        exit(1);
    }
    if(pthread_barrier_init(&g_parallel_glb_barrier2, NULL, g_options.numproc) != 0)
    {
        fprintf(g_fpout, "error in pthread_barrier_init\n");
        exit(1);
    }
#endif

#ifndef NO_PARALLEL
    g_idleCount = 0;
    g_globalRelabelingAwareCounter = 0;
#endif
    g_globalRelabelingFlag = 0;
    g_taskCounter = 0;

}

static void fprf_create_threads(void)
{
    int32 i;
    for(i = 0; i < g_options.numproc; i++)
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

#ifdef PROFILE_WITH_GPROF
        getitimer(ITIMER_PROF, &g_threads[i].itimer);
#endif
        if(pthread_create(&g_threads[i].pthread, &attr, fprf_thread_routine, (void *)(&g_threads[i].id)))
        {
            fprintf(g_fpout, "error in pthread_create\n");
            exit(1);
        }
        pthread_attr_destroy(&attr);
    }
}

void fprf_join_threads(void)
{
    int32 i;
    for(i = 0; i < g_options.numproc; i++)
    {
        void * pStatus = 0;
        if(pthread_join(g_threads[i].pthread, &pStatus))
        {
            fprintf(g_fpout, "error joining thread\n");
            exit(1);
        }
    }
}

static void fprf_establish_initial_preflow(void)
{
    Arc * a;
    Node * v = nodes + g_s;
    for(a = v->adj; a != NULL; a = a->next)
    {
        int32 flow = a->cap;
        if(flow > 0)
        {
            a->flow += flow;
            a->other->flow -= flow;

#ifndef NO_DEBUG_MESSAGES
            g_threads[0].stat.pushCount++; // statistics
#endif

            nodes[a->w].e += flow;
            v->e -= flow;
        } // a  is not saturated
    } // end of scanning arcs from  i
}

int32 fprf_run(void)
{
    double tm;

    g_nextGlobalRelabelingIncrement = (int32)(g_n * g_options.c / g_options.numproc);
    g_nextGlobalRelabeling = g_nextGlobalRelabelingIncrement;

    fprf_initialize_thread_local_storage();

    fprf_establish_initial_preflow();

    g_globalRelabelingFlag = 1; // so that at the beginning the threads perform a global relabeling together

    //tmp = fprf_calculate_labels(1);
    //assert(tmp > 0 && "we need to have at least one active node at this point");

    fprf_create_threads();
    fprf_join_threads();

    gettimeofday(&g_end, NULL);
    tm = (g_end.tv_sec - g_begin.tv_sec) + ((g_end.tv_usec - g_begin.tv_usec) / 1000000.0f);
    fprintf(g_fpout, "time: %f\n", tm);

    fprf_verify_flow();

    return nodes[g_t].e;
}

void fprf_uninit(void)
{
    int32 i;

    free(nodes);
    nodes = NULL;

    free(arcs);
    arcs = NULL;

    free(g_ppQueue);
    g_ppQueue = NULL;

    for(i = 0; i < g_options.numproc; i++)
    {
        g_threads[i].qhead = g_threads[i].qtail = NULL;
        free(g_threads[i].q);
        memset(&g_threads[i].stat, 0, sizeof(g_threads[i].stat));

#ifndef NO_PARALLEL
        pthread_mutex_destroy(&g_threads[i].mutex);
        pthread_cond_destroy(&g_threads[i].cond);
#endif
    }
    free(g_threads);

#ifndef NO_PARALLEL
    pthread_barrier_destroy(&g_barrier);
    pthread_barrier_destroy(&g_globalRelabelingBarrier1);
    pthread_barrier_destroy(&g_globalRelabelingBarrier2);
    pthread_barrier_destroy(&g_parallel_glb_barrier1);
    pthread_barrier_destroy(&g_parallel_glb_barrier2);
#endif

    fprf_queue_uninit();

    g_fpout = NULL;
}

void fprf_output(void)
{
    int32 i;

    ThreadStat stat;
    memset(&stat, 0, sizeof(ThreadStat));

    fprintf(g_fpout, "\n\n");
    fprintf(g_fpout, "-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
    fprintf(g_fpout, "thread | push         | relabel      | loop       | inner loop   | busywait1    | busywait2    | noop       | glbl rlbl | gettask    | notask     | xferred    | waittime   | xfertime   | discharge \n");
    fprintf(g_fpout, "-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");

    for(i = 0; i < g_options.numproc; i++)
    {
        fprintf(g_fpout, "%-6ld | ", i);
        fprintf(g_fpout, "%-12ld | ", g_threads[i].stat.pushCount);
        fprintf(g_fpout, "%-12ld | ", g_threads[i].stat.relabelCount);
        fprintf(g_fpout, "%-10ld | ", g_threads[i].stat.loopCount);
        fprintf(g_fpout, "%-12ld | ", g_threads[i].stat.innerLoopCount);
        fprintf(g_fpout, "%-12ld | ", g_threads[i].stat.busyWaitCount1);
        fprintf(g_fpout, "%-12ld | ", g_threads[i].stat.busyWaitCount2);
        fprintf(g_fpout, "%-10ld | ", g_threads[i].stat.noopCount);
        fprintf(g_fpout, "%-9ld | ", g_threads[i].stat.globalRelabelingCount);
        fprintf(g_fpout, "%-10ld | ", g_threads[i].stat.getTaskCount);
        fprintf(g_fpout, "%-10ld | ", g_threads[i].stat.noTaskCount);
        fprintf(g_fpout, "%-10ld | ", g_threads[i].stat.transferredCount);

#ifdef TIME_TASK_WAIT
        fprintf(g_fpout, "%-10f | ", g_threads[i].stat.taskWaitTime);
#else
        fprintf(g_fpout, "%-10s | ", "undefined");
#endif

#ifdef TIME_TASK_TRANSFER
        fprintf(g_fpout, "%-10f | ", g_threads[i].stat.taskTransferTime);
#else
        fprintf(g_fpout, "%-10s | ", "undefined");
#endif

        fprintf(g_fpout, "%-10ld\n", g_threads[i].stat.dischargeCount);

        stat.pushCount += g_threads[i].stat.pushCount;
        stat.relabelCount += g_threads[i].stat.relabelCount;
        stat.loopCount += g_threads[i].stat.loopCount;
        stat.innerLoopCount += g_threads[i].stat.innerLoopCount;
        stat.busyWaitCount1 += g_threads[i].stat.busyWaitCount1;
        stat.busyWaitCount2 += g_threads[i].stat.busyWaitCount2;
        stat.noopCount += g_threads[i].stat.noopCount;
        stat.globalRelabelingCount += g_threads[i].stat.globalRelabelingCount;
        stat.getTaskCount += g_threads[i].stat.getTaskCount;
        stat.noTaskCount += g_threads[i].stat.noTaskCount;
        stat.transferredCount += g_threads[i].stat.transferredCount;
        stat.dischargeCount += g_threads[i].stat.dischargeCount;
#ifdef TIME_TASK_WAIT
        stat.taskWaitTime += g_threads[i].stat.taskWaitTime;
#endif
#ifdef TIME_TASK_TRANSFER
        stat.taskTransferTime += g_threads[i].stat.taskTransferTime;
#endif
    }
    fprintf(g_fpout, "-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");

    fprintf(g_fpout, "%-6ld | ", g_options.numproc);
    fprintf(g_fpout, "%-12ld | ", stat.pushCount);
    fprintf(g_fpout, "%-12ld | ", stat.relabelCount);
    fprintf(g_fpout, "%-10ld | ", stat.loopCount);
    fprintf(g_fpout, "%-12ld | ", stat.innerLoopCount);
    fprintf(g_fpout, "%-12ld | ", stat.busyWaitCount1);
    fprintf(g_fpout, "%-12ld | ", stat.busyWaitCount2);
    fprintf(g_fpout, "%-10ld | ", stat.noopCount);
    fprintf(g_fpout, "%-9ld | ", stat.globalRelabelingCount);
    fprintf(g_fpout, "%-10ld | ", stat.getTaskCount);
    fprintf(g_fpout, "%-10ld | ", stat.noTaskCount);
    fprintf(g_fpout, "%-10ld | ", stat.transferredCount);
#ifdef TIME_TASK_WAIT
    fprintf(g_fpout, "%-10f | ", stat.taskWaitTime);
#else
    fprintf(g_fpout, "%-10s | ", "undefined");
#endif
#ifdef TIME_TASK_TRANSFER
    fprintf(g_fpout, "%-10f | ", stat.taskTransferTime);
#else
    fprintf(g_fpout, "%-10s | ", "undefined");
#endif
    fprintf(g_fpout, "%-10ld\n", stat.dischargeCount);
    fprintf(g_fpout, "-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n");

    fprintf(g_fpout, "GLOBAL RLBL TIME: %f\n", g_glbWaiting);
}

