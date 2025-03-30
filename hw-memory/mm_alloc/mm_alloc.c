/*
 * mm_alloc.c
 */

#include "mm_alloc.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

struct block {
  size_t size;          
  int free;              
  struct block *prev;   
  struct block *next;   
};

static struct block *head = NULL;

#define ALIGN8(x) (((((x) - 1) >> 3) << 3) + 8)

#define BLOCK_SIZE (sizeof(struct block))

#define MIN_SPLIT_SIZE 8

static void split_block(struct block *b, size_t size) {
  if (b->size >= size + BLOCK_SIZE + MIN_SPLIT_SIZE) {
    struct block *new_b = (struct block *)((char *)(b + 1) + size);
    new_b->size = b->size - size - BLOCK_SIZE;
    new_b->free = 1;
    new_b->prev = b;
    new_b->next = b->next;
    if (b->next)
      b->next->prev = new_b;
    b->next = new_b;
    b->size = size;
  }
}

static void coalesce(struct block *b) {
  if (b->next && b->next->free) {
    b->size += BLOCK_SIZE + b->next->size;
    b->next = b->next->next;
    if (b->next)
      b->next->prev = b;
  }

  if (b->prev && b->prev->free) {
    b->prev->size += BLOCK_SIZE + b->size;
    b->prev->next = b->next;
    if (b->next)
      b->next->prev = b->prev;
  }
}

static struct block *find_free_block(size_t size) {
  struct block *curr = head;
  while (curr) {
    if (curr->free && curr->size >= size)
      return curr;
    curr = curr->next;
  }
  return NULL;
}

static struct block *extend_heap(size_t size) {
  size_t total_size = BLOCK_SIZE + size;
  void *ptr = sbrk(total_size);
  if (ptr == (void *) -1)
    return NULL;

  struct block *new_block = (struct block *)ptr;
  new_block->size = size;
  new_block->free = 0;
  new_block->prev = NULL;
  new_block->next = NULL;

  if (head == NULL) {
    head = new_block;
  } else {
    struct block *curr = head;
    while (curr->next)
      curr = curr->next;
    new_block->prev = curr;
    curr->next = new_block;
  }
  return new_block;
}

void* mm_malloc(size_t size) {
  //TODO: Implement malloc
  if (size == 0)
    return NULL;

  size = ALIGN8(size);

  struct block *b = find_free_block(size);
  if (b) {
    split_block(b, size);
    b->free = 0;
    memset((char *)(b + 1), 0, b->size);
    return (void *)(b + 1);
  }

  b = extend_heap(size);
  if (!b)
    return NULL;
  memset((char *)(b + 1), 0, b->size);
  return (void *)(b + 1);

}

void* mm_realloc(void* ptr, size_t size) {
  //TODO: Implement realloc
  if (ptr == NULL)
    return mm_malloc(size);

  if (size == 0) {
    mm_free(ptr);
    return NULL;
  }

  size = ALIGN8(size);
  struct block *b = (struct block *)ptr - 1;

  if (b->size >= size) {
    split_block(b, size);
    return ptr;
  }

  if (b->next && b->next->free && (b->size + BLOCK_SIZE + b->next->size) >= size) {
    b->size += BLOCK_SIZE + b->next->size;
    b->next = b->next->next;
    if (b->next)
      b->next->prev = b;
    split_block(b, size);
    return ptr;
  }

  void *new_ptr = mm_malloc(size);
  if (!new_ptr)
    return NULL;
  memcpy(new_ptr, ptr, b->size);
  mm_free(ptr);
  return new_ptr;
}

void mm_free(void* ptr) {
  //TODO: Implement free
  if (ptr == NULL)
    return;
  struct block *b = (struct block *)ptr - 1;
  b->free = 1;
  coalesce(b);
}
