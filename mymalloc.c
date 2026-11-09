#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include <malloc.h>
#include <stdio.h>

// Include any other headers we need here
#include <assert.h>
#include <stdint.h>  // intptr_t, INTPTR_MAX, SIZE_MAX
#include <string.h>  // memset
#include <unistd.h>  // sbrk

#include <debug.h> // definition of debug_printf

/**
 * Header stored in front of every block of memory obtained from sbrk.
 *
 * Heap layout:  |block_t|---user memory---|block_t|---user memory---| ...
 *
 * All blocks, free and in use, are linked through next in the order they
 * were obtained from sbrk.
 */
typedef struct block {
  size_t size;         // Number of usable bytes after this header
  struct block *next;  // Next block in sbrk order (NULL for the last one)
  int free;            // Nonzero if the block is free, 0 if in use
} block_t;

/** Head of the block list (the first block obtained from sbrk), or NULL
 *  if no memory has been requested yet. */
static block_t *head = NULL;

/**
 * Return the address of the user memory that follows a block's header.
 *
 * block: a block in the list (must not be NULL)
 * returns: pointer to the first usable byte of the block
 */
static void *block_to_data(block_t *block) {
  assert(block != NULL);
  return (void *) (block + 1);
}

/**
 * Return the header of the block that holds the given user memory.
 *
 * ptr: a pointer previously returned by mymalloc (must not be NULL)
 * returns: pointer to the block's header, located just before ptr
 */
static block_t *data_to_block(void *ptr) {
  assert(ptr != NULL);
  return ((block_t *) ptr) - 1;
}

/**
 * First-fit search: walk the list from the head and return the first free
 * block with at least size usable bytes.
 *
 * size: the number of bytes requested (> 0)
 * last: out-parameter; set to the last block visited, so a new block can be
 *       appended at the tail without a second walk (NULL if the list is empty)
 * returns: the first fitting free block, or NULL if there is none
 */
static block_t *find_free_block(size_t size, block_t **last) {
  assert(size > 0);
  assert(last != NULL);

  block_t *current = head;
  *last = NULL;

  while (current != NULL) {
    if (current->free && current->size >= size) {
      return current;
    }
    *last = current;
    current = current->next;
  }
  return NULL;
}

/**
 * Request a new block from the OS with sbrk and append it at the tail of
 * the list. Requests exactly sizeof(block_t) + size bytes.
 *
 * size: the number of usable bytes needed (> 0)
 * last: the current tail of the list, or NULL if the list is empty
 * returns: the new block (marked in use), or NULL if sbrk fails or the
 *          request is too large for sbrk
 */
static block_t *request_block(size_t size, block_t *last) {
  assert(size > 0);

  // sbrk takes a signed intptr_t, so make sure the total fits.
  if (size > (size_t) INTPTR_MAX - sizeof(block_t)) {
    return NULL;
  }

  void *mem = sbrk((intptr_t) (sizeof(block_t) + size));
  if (mem == (void *) -1) {
    return NULL;
  }

  block_t *block = (block_t *) mem;
  block->size = size;
  block->next = NULL;
  block->free = 0;

  if (last == NULL) {
    assert(head == NULL);
    head = block;
  } else {
    assert(last->next == NULL);
    last->next = block;
  }
  return block;
}

/**
 * Allocate s bytes of memory using a first-fit strategy. A freed block is
 * reused if one is large enough; otherwise a new block is requested with sbrk.
 *
 * s: the number of bytes to allocate
 * returns: pointer to the memory, or NULL if s is 0 or memory runs out
 */
void *mymalloc(size_t s) {
  if (s == 0) {
    return NULL;  // malloc(0) returns NULL and prints nothing
  }

  void *p = NULL;
  block_t *last = NULL;
  block_t *block = find_free_block(s, &last);

  if (block != NULL) {
    block->free = 0;  // reuse a freed block
  } else {
    block = request_block(s, last);
  }

  if (block != NULL) {
    p = block_to_data(block);
  }
  // Otherwise we are out of memory (sbrk failed) and p stays NULL.

  debug_printf("Malloc %zu bytes\n", s);

  return p;
}

/**
 * Allocate zero-initialized memory for an array of nmemb elements of s bytes
 * each. The memory is obtained from mymalloc.
 *
 * nmemb: number of elements
 * s: size of each element in bytes
 * returns: pointer to the zeroed memory, or NULL if nmemb * s overflows,
 *          is 0, or memory runs out
 */
void *mycalloc(size_t nmemb, size_t s) {
  if (s != 0 && nmemb > SIZE_MAX / s) {
    return NULL;  // nmemb * s would overflow
  }

  size_t total = nmemb * s;
  void *p = mymalloc(total);

  if (p != NULL) {
    memset(p, 0, total);
  }
  // Otherwise we are out of memory (or total is 0) and p stays NULL.

  debug_printf("Calloc %zu bytes\n", total);

  return p;
}

/**
 * Free memory previously returned by mymalloc or mycalloc by marking its
 * block as free, so later allocations can reuse it. Memory is never
 * returned to the OS.
 *
 * ptr: pointer returned by mymalloc/mycalloc, or NULL (does nothing)
 */
void myfree(void *ptr) {
  if (ptr == NULL) {
    return;  // free(NULL) does nothing and prints nothing
  }

  block_t *block = data_to_block(ptr);
  assert(!block->free);  // catch double frees

  block->free = 1;

  // Report the size stored in the header (may exceed the latest request).
  debug_printf("Freed %zu bytes\n", block->size);
}