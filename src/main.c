#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "sds/sds.h"
#include "swim.h"


void initialize_weak_prng() {
  srand(time(NULL));
}

typedef struct node_thread_args_s {
  swim_node_t* node;

  size_t num_seeds;
  sds* seed_hosts;
  uint16_t* seed_ports;
} node_thread_args_t;

void* node_thread(void* data) {
  node_thread_args_t* args = (node_thread_args_t*)data;
  swim_node_t* node = (swim_node_t*)args->node;

  printf("Running node %s\n", node->node_id);
  uv_loop_t* loop = (uv_loop_t*)malloc(sizeof(uv_loop_t));
  uv_loop_init(loop);
  swim_state_t* state = swim_state_create(loop, node, node->host, node->port);

  swim_node_t* seed;
  for (size_t i = 0; i < args->num_seeds; i++) {
    seed = swim_node_create(NULL, args->seed_hosts[i], args->seed_ports[i]);
    swim_state_seed_node(state, seed);
  }

  uv_run(loop, UV_RUN_DEFAULT);

  swim_state_destroy(state);
  swim_node_destroy(node);
  printf("Exiting node %s\n", node->node_id);
  return NULL;
}

int main(int argc, char** argv) {
  size_t num_threads = 6;

  char buf[256];
  size_t sz;
  pthread_t* thread = malloc(sizeof(pthread_t) * num_threads);

  node_thread_args_t* args;
  for (int i = 0; i < num_threads; i++) {

    args = malloc(sizeof(node_thread_args_t));
    args->num_seeds = 1;
    args->seed_hosts = malloc(sizeof(sds) * args->num_seeds);
    args->seed_ports = malloc(sizeof(uint16_t) * args->num_seeds);
    sz = snprintf(buf, 256, "node-%d", i + 1);
    args->node = swim_node_create(sdsnewlen(buf, sz), sdsnew("localhost"), 12001 + i);

    args->node->metadata.type = SWIM_NODE_TYPE_MEMBER;
    args->node->metadata.num_shards = 1;
    args->node->metadata.shards = malloc(sizeof(uint64_t) * 1);
    args->node->metadata.shards[0] = 0x01;
    args->node->metadata.datacenter = sdsnew("dc1");
    args->node->metadata.rack = sdsnew("rack1");

    args->num_seeds = 1;
    args->seed_hosts[0] = sdsnew("localhost");
    args->seed_ports[0] = 12001;

    pthread_create(&thread[i], NULL, &node_thread, (void*)args);
    sleep(2);
  }

  for (int i = 0; i < num_threads; i++) {
    pthread_join(thread[i], NULL);
  }

  return 0;
}
