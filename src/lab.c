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
        fprintf(stderr, "buddy_calc: Buddy is NULL\n");
        return NULL;
    }

    uintptr_t addr = (int)((char *)buddy - (char *)pool->base);
    fprintf(stderr, "buddy_calc: addr = %p - %p = %d\n", buddy, pool->base, addr);
    int k = buddy->kval;

    fprintf(stderr, "buddy_calc: k = %d\n", k);
    uintptr_t buddy_addr = (addr ^ (UINT64_C(1) << k));
    if (buddy_addr < 0 || buddy_addr >= (uintptr_t)pool->numbytes)
    {
        fprintf(stderr, "buddy_calc: Buddy address out of range. Invalid adress: %p\tMax address: %p\n", buddy_addr, pool->numbytes);
        return NULL; // Ensure buddy address is within valid range
    }

    struct avail *buddy_block = (struct avail *)((char *)pool->base + buddy_addr);
    buddy_block->tag = BLOCK_AVAIL;
    buddy_block->kval = k;


    return buddy_block;
}

void *buddy_malloc(struct buddy_pool *pool, size_t size)
{
    //get the kval for the requested size with enough room for the tag
    size_t kval = btok(size + sizeof(struct avail)); //sizeof(struct avail) is the size of the metadata
    fprintf(stderr, "buddy_malloc: kval = %zu\n", kval);

    //R1 Find a block

    if(kval > pool->kval_m)
    {
        fprintf(stderr, "Requested size is too large\n");
        return NULL; //Not enough memory
    }

    //Find the first available block that is >= kval
    size_t j = -1;
    for (size_t i = kval; i <= pool->kval_m; i++)
    {
        if (pool->avail[i].next != &pool->avail[i])
        {
            j = i;
            break;
        }
    }

    //If we did not find a block then we need to return NULL
    if(j == -1 || j > pool->kval_m)
    {
        fprintf(stderr, "No available blocks\n");
        return NULL; //No available blocks
    }

    //R2 Remove from list;
    // remove the block from the list
    struct avail *l = pool->avail[j].next;
    struct avail *p = l->next;
    pool->avail[j].next = p;
    p->prev = &pool->avail[j];
    l->tag = BLOCK_RESERVED;
    
    while(j > kval){
        fprintf(stderr, "Splitting block\n");
        fprintf(stderr, "Block size: %zu\n", l->kval);
        fprintf(stderr, "kval size: %zu\n", kval);
        //R4 Split the block
        l->kval--;
        j--;
        struct avail *buddy = buddy_calc(pool, l);
        if (buddy == NULL)
        {
            fprintf(stderr, "Buddy calculation failed\n");
            // If buddy calculation fails, we need to restore the block to its original state
            l->prev->next = l;
            l->next->prev = l;
            l->next = l->prev = NULL;
            l->tag = BLOCK_AVAIL;
            return NULL;
        }

        // Update the buddy block's properties
        buddy->tag = BLOCK_AVAIL;
        buddy->kval = l->kval;
        buddy->next = buddy->prev = &pool->avail[buddy->kval];
        pool->avail[buddy->kval].next = pool->avail[buddy->kval].prev = buddy;
    }

    // Ensure the block is valid before returning
    if (l == NULL)
    {
        fprintf(stderr, "Block is NULL\n");
        return NULL; // Return NULL if block is invalid
    }

    // Return the memory address just after the block's metadata
    return (void *)((char *)l + sizeof(struct avail));
}

void buddy_free(struct buddy_pool *pool, void *ptr)
{
    if(ptr == NULL){
        fprintf(stderr, "Pointer is NULL\n");
        return; // Nothing to free
    }

    struct avail *block = (struct avail *)((char *)ptr - sizeof(struct avail));
    if (block->tag != BLOCK_RESERVED)
    {
        fprintf(stderr, "Block is not reserved\n");
        return; // Block is not reserved
    }
    
    // find the buddy
    struct avail *buddy = buddy_calc(pool, block);
    if (buddy == NULL)
    {
        fprintf(stderr, "Buddy calculation failed\n");
        return; // Buddy calculation failed
    }

    // check if we can merge with buddy
    if (block->tag == BLOCK_AVAIL && buddy->tag == BLOCK_AVAIL)
    {
        // merge the blocks
        block->kval++;
        buddy->tag = BLOCK_UNUSED;
        buddy->next->prev = buddy->prev;
        buddy->prev->next = buddy->next;
        buddy->next = buddy->prev = NULL;

        // add the merged block back into the free list
    }

    // add back into free list
    block->prev = pool->avail[block->kval].prev;
    block->next = &pool->avail[block->kval];
    pool->avail[block->kval].prev->next = block;
    pool->avail[block->kval].prev = block;
    block->tag = BLOCK_AVAIL;

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
