#ifndef _MY_LINUX_ATOMIC_H_
#define _MY_LINUX_ATOMIC_H_



#if __i386__


#define RELEASE_CONSISTENCY_HELPER() __asm__ __volatile__("": : :"memory")
//inline void REL_ACQ_FENCE() { __asm__ __volatile__("mfence": : :"memory"); }
#define REL_ACQ_FENCE() __asm__ __volatile__("mfence": : :"memory")


#elif __ia64__


#define RELEASE_CONSISTENCY_HELPER() __asm__ __volatile__("": : :"memory")
#define REL_ACQ_FENCE() __asm__ __volatile__("mf": : :"memory")


#elif __x86_64__


#define RELEASE_CONSISTENCY_HELPER() __asm__ __volatile__("": : :"memory")
//#ifndef REL_ACQ_FENCE
//inline void REL_ACQ_FENCE() { __asm__ __volatile__("mfence": : :"memory"); }
//#endif 
#ifndef REL_ACQ_FENCE
#define REL_ACQ_FENCE() __asm__ __volatile__("mfence": : :"memory")
#endif 


#endif



#if defined(GCC_ATOMIC)

// lhs is the shared variable
#define ATOMIC_STORE(lhs, rhs) \
    do { \
        /*RELEASE_CONSISTENCY_HELPER(); */\
        (lhs) = (rhs); \
    } while(0)

// rhs is the shared variable
#define ATOMIC_LOAD(lhs, rhs) \
    do { \
        (lhs) = (rhs); \
        /*RELEASE_CONSISTENCY_HELPER(); */\
    } while(0)

#define SYNC_FETCH_AND_STORE(base, condition, newval, original) \
    do { \
        (original) = __sync_val_compare_and_swap(&(base), (condition), (newval)); \
    } while(0)
#define SYNC_FETCH_AND_ADD_NO_RETURN(base, addend) \
    do { \
        (void)__sync_fetch_and_add(&(base), (addend)); \
    } \
    while(0)
#define SYNC_FETCH_AND_ADD(base, addend, prev) \
    do { \
        (prev) = __sync_fetch_and_add(&(base), (addend)); \
    } \
    while(0)

#elif defined(TBB_ATOMIC)

#define ATOMIC_STORE(lhs, rhs) \
    do { \
        (lhs).operator=((rhs)); \
    } while(0)
#define ATOMIC_LOAD(lhs, rhs) \
    do { \
        (lhs) = (rhs); \
    } while(0)
#define SYNC_FETCH_AND_STORE(base, condition, newval, original) \
    do { \
        (original) = (base).fetch_and_store((newval)); \
    } while(0)
#define SYNC_FETCH_AND_ADD_NO_RETURN(base, addend) \
    do { \
        (void)(base).fetch_and_add((addend)); \
    } \
    while(0)
#define SYNC_FETCH_AND_ADD(base, addend, prev) \
    do { \
        (prev) = (base).fetch_and_add((addend)); \
    } \
    while(0)

#else

#define ATOMIC_STORE(lhs, rhs) \
    do { \
        (lhs) = (rhs); \
    } while(0)
#define ATOMIC_LOAD(lhs, rhs) \
    do { \
        (lhs) = (rhs); \
    } while(0)
#define SYNC_FETCH_AND_STORE(base, condition, newval, original) \
    do { \
        (original) = (base); \
        (base) = newval; \
    } while(0)
#define SYNC_FETCH_AND_ADD_NO_RETURN(base, addend) \
    do { \
        (void)(base) += (addend); \
    } \
    while(0)
#define SYNC_FETCH_AND_ADD(base, addend, prev) \
    do { \
        (prev) = (base); \
        (base) += (addend); \
    } \
    while(0)

#endif



#endif //_MY_LINUX_ATOMIC_H_

