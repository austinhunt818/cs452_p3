#include <assert.h>
#include <stdlib.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include "harness/unity.h"
#include "../src/lab.h"


void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}



/**
 * Check the pool to ensure it is full.
 */
void check_buddy_pool_full(struct buddy_pool *pool)
{
  //A full pool should have all values 0-(kval-1) as empty
  for (size_t i = 0; i < pool->kval_m; i++)
    {
      assert(pool->avail[i].next == &pool->avail[i]);
      assert(pool->avail[i].prev == &pool->avail[i]);
      assert(pool->avail[i].tag == BLOCK_UNUSED);
      assert(pool->avail[i].kval == i);
    }

  //The avail array at kval should have the base block
  assert(pool->avail[pool->kval_m].next->tag == BLOCK_AVAIL);
  assert(pool->avail[pool->kval_m].next->next == &pool->avail[pool->kval_m]);
  assert(pool->avail[pool->kval_m].prev->prev == &pool->avail[pool->kval_m]);

  //Check to make sure the base address points to the starting pool
  //If this fails either buddy_init is wrong or we have corrupted the
  //buddy_pool struct.
  assert(pool->avail[pool->kval_m].next == pool->base);
}

/**
 * Check the pool to ensure it is empty.
 */
void check_buddy_pool_empty(struct buddy_pool *pool)
{
  //An empty pool should have all values 0-(kval) as empty
  for (size_t i = 0; i <= pool->kval_m; i++)
    {
      assert(pool->avail[i].next == &pool->avail[i]);
      assert(pool->avail[i].prev == &pool->avail[i]);
      assert(pool->avail[i].tag == BLOCK_UNUSED);
      assert(pool->avail[i].kval == i);
    }
}

/**
 * Test allocating 1 byte to make sure we split the blocks all the way down
 * to MIN_K size. Then free the block and ensure we end up with a full
 * memory pool again
 */
void test_buddy_malloc_one_byte(void)
{
  fprintf(stderr, "->Test allocating and freeing 1 byte\n");
  struct buddy_pool pool;
  int kval = MIN_K;
  size_t size = UINT64_C(1) << kval;
  buddy_init(&pool, size);
  void *mem = buddy_malloc(&pool, 1);
  //Make sure correct kval was allocated
  buddy_free(&pool, mem);
  check_buddy_pool_full(&pool);
  buddy_destroy(&pool);
}

/**
 * Tests the allocation of one massive block that should consume the entire memory
 * pool and makes sure that after the pool is empty we correctly fail subsequent calls.
 */
void test_buddy_malloc_one_large(void)
{
  fprintf(stderr, "->Testing size that will consume entire memory pool\n");
  struct buddy_pool pool;
  size_t bytes = UINT64_C(1) << MIN_K;
  buddy_init(&pool, bytes);

  //Ask for an exact K value to be allocated. This test makes assumptions on
  //the internal details of buddy_init.
  size_t ask = bytes - sizeof(struct avail);
  void *mem = buddy_malloc(&pool, ask);
  assert(mem != NULL);

  //Move the pointer back and make sure we got what we expected
  struct avail *tmp = (struct avail *)mem - 1;
  assert(tmp->kval == MIN_K);
  assert(tmp->tag == BLOCK_RESERVED);
  check_buddy_pool_empty(&pool);

  //Verify that a call on an empty tool fails as expected and errno is set to ENOMEM.
  void *fail = buddy_malloc(&pool, 5);
  assert(fail == NULL);
  assert(errno = ENOMEM);

  //Free the memory and then check to make sure everything is OK
  buddy_free(&pool, mem);
  check_buddy_pool_full(&pool);
  buddy_destroy(&pool);
}

/**
 * Tests to make sure that the struct buddy_pool is correct and all fields
 * have been properly set kval_m, avail[kval_m], and base pointer after a
 * call to init
 */
void test_buddy_init(void)
{
  fprintf(stderr, "->Testing buddy init\n");
  //Loop through all kval MIN_k-DEFAULT_K and make sure we get the correct amount allocated.
  //We will check all the pointer offsets to ensure the pool is all configured correctly
  for (size_t i = MIN_K; i <= DEFAULT_K; i++)
    {
      size_t size = UINT64_C(1) << i;
      struct buddy_pool pool;
      buddy_init(&pool, size);
      check_buddy_pool_full(&pool);
      buddy_destroy(&pool);
    }
}

void test_btok(void){
  fprintf(stderr, "->Testing btok\n");

  int bytes = 16;
  int k = btok(bytes);
  fprintf(stderr, "\tbytes = %d\n\tk = %d\n", bytes, k);
  assert(btok(bytes) == 4);

   bytes = 1024;
   k = btok(bytes);
  fprintf(stderr, "\tbytes = %d\n\tk = %d\n", bytes, k);
  assert(btok(bytes) == 10);

   bytes = 1048576;
   k = btok(bytes);
  fprintf(stderr, "\tbytes = %d\n\tk = %d\n", bytes, k);
  assert(btok(bytes) == 20);
}

void test_buddy_calc(void){
  fprintf(stderr, "->Testing buddy_calc\n");
  struct buddy_pool pool;
  buddy_init(&pool, 1024);
  struct avail *block = (struct avail *)pool.base;
  block->tag = BLOCK_AVAIL;
  block->kval = 10;

  struct avail *buddy = buddy_calc(&pool, block);
  fprintf(stderr, "\tblock = %p\n\tbuddy = %p\n\n", block, buddy);
  assert(buddy != NULL);
  assert(buddy = block + (1 << block->kval));

  struct avail *block_buddy = buddy_calc(&pool, buddy);
  fprintf(stderr, "\tblock_buddy = \t%p\n\tbuddy = \t%p\n", block_buddy, block);
  assert(block_buddy != NULL);
  assert(block_buddy = block);

  buddy_destroy(&pool);
}


int main(void) {
  time_t t;
  unsigned seed = (unsigned)time(&t);
  fprintf(stderr, "Random seed:%d\n", seed);
  srand(seed);
  printf("Running memory tests.\n");

  UNITY_BEGIN();
  RUN_TEST(test_buddy_init);
  RUN_TEST(test_buddy_malloc_one_byte);
  RUN_TEST(test_buddy_malloc_one_large);
  RUN_TEST(test_btok);
  RUN_TEST(test_buddy_calc);
return UNITY_END();
}
