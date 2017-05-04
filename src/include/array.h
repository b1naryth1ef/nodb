#pragma once

/**
  Array implements a linked list
*/

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct array_member_s {
  void* data;
  struct array_member_s* next;
} array_member_t;

typedef struct array_s {
  size_t length;
  array_member_t* head;
  array_member_t* tail;
} array_t;

// Array Setup
array_t* array_create();
void array_destroy(array_t*);

// Array Operations
void array_prepend(array_t*, void*);
void array_append(array_t*, void*);
array_member_t* array_index_m(array_t*, size_t);
void* array_index(array_t*, size_t);
void* array_pop(array_t*, size_t);
size_t array_find(array_t*, void*);

// Array Member Setup
array_member_t* array_member_create(void*, array_member_t*);
void array_member_destroy(array_member_t*);
