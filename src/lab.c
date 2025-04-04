#include <stdio.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/errno.h>
#else
#include <errno.h>
#endif

#include "lab.h"

#define handle_error_and_die(msg) \
    do                            \
    {                             \
        perror(msg);              \
        raise(SIGKILL);          \
    } while (0)

/**
 * @brief Convert bytes to the correct K value
 *
 * @param bytes the number of bytes
 * @return size_t the K value that will fit bytes
 */
size_t btok(size_t bytes)
{
    if(bytes <= 1) return 0;

    size_t k = 0;
    k+=(btok(bytes/2) + 1);
    return k;
}

struct avail *buddy_calc(struct buddy_pool *pool, struct avail *buddy)
{   
    if (buddy == NULL)
    {
        return NULL;
    }

    int addr = (int)((char *)buddy - (char *)pool->base);
    int k = buddy->kval;

    int buddy_addr = (addr ^ (1 << k));
    struct avail *buddy_block = (struct avail *)((char *)pool->base + buddy_addr);
    buddy_block->kval = buddy->kval;
    buddy_block->tag = buddy->tag;
    return buddy_block;
}

void *buddy_malloc(struct buddy_pool *pool, size_t size)
{
    
    //get the kval for the requested size with enough room for the tag and kval fields
    size_t kval = btok(size + sizeof(struct avail));

    //R1 Find a block
    if(kval > pool->kval_m)
    {
        return NULL; //Not enough memory
    }

    //Find the first available block that is >= kval
    struct avail *block = NULL;
    for (size_t i = kval; i <= pool->kval_m; i++)
    {
        if (pool->avail[i].next != &pool->avail[i])
        {
            block = pool->avail[i].next;
            break;
        }
    }

    //There was not enough memory to satisfy the request thus we need to set error and return NULL
    if (block == NULL)
    {
        return NULL;
    }

    //R2 Remove from list;
    //remove the block from the list
    // block->prev->next = block->next;


    // //R3 Split required?
    // if(block->kval > kval){
    //     struct avail *buddy = buddy_calc(pool, block);
    //     if (buddy == NULL)
    //     {
    //         return NULL;
    //     }
    // }

    // //R4 Split the block
    // //Set the tag and kval for the block
    // block->tag = BLOCK_RESERVED;
    // block->kval = kval;
    // block->next = block->prev = block;
    // //Set the block to point to the base address
    // block->next = (struct avail *)((char *)pool->base + ((char *)block - (char *)pool->base));
    // block->prev = (struct avail *)((char *)pool->base + ((char *)block - (char *)pool->base));

    return ((char *)block + sizeof(struct avail));

}

void buddy_free(struct buddy_pool *pool, void *ptr)
{

}

// /**
//  * @brief This is a simple version of realloc.
//  *
//  * @param poolThe memory pool
//  * @param ptr  The user memory
//  * @param size the new size requested
//  * @return void* pointer to the new user memory
//  */
// void *buddy_realloc(struct buddy_pool *pool, void *ptr, size_t size)
// {
//     //Required for Grad Students
//     //Optional for Undergrad Students
// }

void buddy_init(struct buddy_pool *pool, size_t size)
{
    size_t kval = 0;
    if (size == 0)
        kval = DEFAULT_K;
    else
        kval = btok(size);

    if (kval < MIN_K)
        kval = MIN_K;
    if (kval > MAX_K)
        kval = MAX_K - 1;

    //make sure pool struct is cleared out
    memset(pool,0,sizeof(struct buddy_pool));
    pool->kval_m = kval;
    pool->numbytes = (UINT64_C(1) << pool->kval_m);
    //Memory map a block of raw memory to manage
    pool->base = mmap(
        NULL,                               /*addr to map to*/
        pool->numbytes,                     /*length*/
        PROT_READ | PROT_WRITE,             /*prot*/
        MAP_PRIVATE | MAP_ANONYMOUS,        /*flags*/
        -1,                                 /*fd -1 when using MAP_ANONYMOUS*/
        0                                   /* offset 0 when using MAP_ANONYMOUS*/
    );
    if (MAP_FAILED == pool->base)
    {
        handle_error_and_die("buddy_init avail array mmap failed");
    }

    //Set all blocks to empty. We are using circular lists so the first elements just point
    //to an available block. Thus the tag, and kval feild are unused burning a small bit of
    //memory but making the code more readable. We mark these blocks as UNUSED to aid in debugging.
    for (size_t i = 0; i <= kval; i++)
    {
        pool->avail[i].next = pool->avail[i].prev = &pool->avail[i];
        pool->avail[i].kval = i;
        pool->avail[i].tag = BLOCK_UNUSED;
    }

    //Add in the first block
    pool->avail[kval].next = pool->avail[kval].prev = (struct avail *)pool->base;
    struct avail *m = pool->avail[kval].next;
    m->tag = BLOCK_AVAIL;
    m->kval = kval;
    m->next = m->prev = &pool->avail[kval];
}

void buddy_destroy(struct buddy_pool *pool)
{
    int rval = munmap(pool->base, pool->numbytes);
    if (-1 == rval)
    {
        handle_error_and_die("buddy_destroy avail array");
    }
    //Zero out the array so it can be reused it needed
    memset(pool,0,sizeof(struct buddy_pool));
}

#define UNUSED(x) (void)x

/**
 * This function can be useful to visualize the bits in a block. This can
 * help when figuring out the buddy_calc function!
 */
static void printb(unsigned long int b)
{
     size_t bits = sizeof(b) * 8;
     unsigned long int curr = UINT64_C(1) << (bits - 1);
     for (size_t i = 0; i < bits; i++)
     {
          if (b & curr)
          {
               printf("1");
          }
          else
          {
               printf("0");
          }
          curr >>= 1L;
     }
}
