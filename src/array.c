#include "array.h"

array_t* array_create() {
  array_t* array = (array_t*)malloc(sizeof(array_t));

  array->length = 0;
  array->head = NULL;
  array->tail = NULL;

  return array;
}

void array_destroy(array_t* array) {
  for (size_t i = 0; i < array->length; i++) {
    array_member_destroy(array_pop(array, i));
  }

  free((void *)array);
}

array_member_t* array_member_create(void* data, array_member_t* next) {
  array_member_t* member = (array_member_t*)malloc(sizeof(array_member_t));
  member->data = data;
  member->next = next;
  return member;
}

void array_member_destroy(array_member_t* member) {
  free((void *)member);
}

void array_prepend(array_t* array, void* data) {
  array->length += 1;
  array->head = array_member_create(data, array->head);

  if (array->length == 1) {
    array->tail = NULL;
  }
}

void array_append(array_t* array, void* data) {
  array->length += 1;
  array_member_t* member = array_member_create(data, NULL);

  if (array->length == 1) {
    array->head = member;
    array->tail = member;
  } else {
    array->tail->next = member;
    array->tail = member;
  }
}

array_member_t* array_index_m(array_t* array, size_t idx) {
  array_member_t* member = array->head;

  while (idx) {
    idx--;
    member = member->next;
  }

  return member;
}

void* array_index(array_t* array, size_t idx) {
  return array_index_m(array, idx)->data;
}

void* array_pop(array_t* array, size_t idx) {
  array_member_t* member = array_index_m(array, idx);

  array->length -= 1;

  // This is the first item in the array
  if (idx == 0) {
    if (array->length == 0) {
      array->head = NULL;
      array->tail = NULL;
    } else {
      array->head = member->next;
    }
  } else {
    array_member_t* before = array_index_m(array, idx - 1);
    before->next = member->next;
  }

  return member->data;
}

size_t array_find(array_t* array, void* data) {
  array_member_t* member = array->head;

  size_t idx = 0;
  while (true) {
    if (member->data == data) {
      return idx;
    }

    member = member->next;
    if (member == NULL) {
      return -1;
    }
    idx++;
  }
}

void* array_search(array_t* array, array_search_f func, void* ctx) {
  array_member_t* member = array->head;

  while (true) {
    if (!member) {
      break;
    }

    if (func(ctx, member->data)) {
      return member->data;
    }

    member = member->next;
  }

  return NULL;
}
