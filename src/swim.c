/**
  Implements a form of the SWIM gossip protocol
**/

#include "swim.h"

#define PING_TIMEOUT 1500
#define PING_LOOP_FREQ 1000
#define GOSSIP_COUNT 3

static void _swim_uv_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

static void _swim_uv_read_cb(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
  swim_state_t* state = (swim_state_t*)handle->data;

  if (nread <= 0) {
    return;
  }

  if (addr == NULL) {
    printf("[%s] null addr\n", state->self->node_id);
    return;
  }

  char sender[17] = {0};
  uv_ip4_name((struct sockaddr_in*) addr, sender, 16);

  uint16_t port = ntohs(((struct sockaddr_in*)addr)->sin_port);

  printf("[%s] read %d bytes (%02X %02X %02X)\n",
      state->self->node_id,
      nread,
      buf->base[0], buf->base[1], buf->base[2]);
  // printf("[%s] read data from %s:%d\n", state->self->node_id, sender, port);
  swim_state_handle(state, buf, nread);
  free(buf->base);
}

void _swim_uv_send_cb(uv_udp_send_t* req, int status) {
  free(req);
}

/**
swim_node_metadata_t* swim_node_metadata_create() {
  swim_node_metadata_t* meta = (swim_node_metadata_t*)malloc(sizeof(swim_node_metadata_t));
  meta->num_shards = 0;
  meta->shards = NULL;
  return meta;
}

void swim_node_metadata_destroy(swim_node_metadata_t* meta) {
  if (meta->num_shards && meta->shards) {
    free((void*)meta->shards);
  }

  if (meta->datacenter) {
    sdsfree(meta->datacenter);
  }

  if (meta->rack) {
    sdsfree(meta->rack);
  }

  free((void*)meta);
}
*/

void swim_node_metadata_init(swim_node_metadata_t* meta) {
  meta->num_shards = 0;
  meta->shards = NULL;
}

void swim_node_metadata_pack(swim_node_metadata_t* meta, mpack_writer_t* writer) {
  mpack_write_u8(writer, (uint8_t)meta->type);

  mpack_start_array(writer, meta->num_shards);
  for (size_t i = 0; i < meta->num_shards; i++) {
    mpack_write_u64(writer, meta->shards[i]);
  }
  mpack_finish_array(writer);

  mpack_write_cstr(writer, meta->datacenter);
  mpack_write_cstr(writer, meta->rack);
}

void swim_node_metadata_unpack(swim_node_metadata_t* meta, mpack_reader_t* reader) {
  meta->type = mpack_expect_u8(reader);
  meta->num_shards = mpack_expect_array(reader);
  assert(meta->shards == NULL);
  meta->shards = malloc(sizeof(uint64_t) * meta->num_shards);

  for (size_t i = 0; i < meta->num_shards; i++) {
    meta->shards[i] = mpack_expect_u64(reader);
  }
  mpack_done_array(reader);

  char* datacenter = mpack_expect_cstr_alloc(reader, 4096);
  mpack_done_str(reader);
  char* rack = mpack_expect_cstr_alloc(reader, 4096);
  mpack_done_str(reader);

  meta->datacenter = sdsnew(datacenter);
  meta->rack = sdsnew(rack);

  free(datacenter);
  free(rack);
}

swim_node_t* swim_node_create(sds node_id, sds host, uint16_t port) {
  swim_node_t* node = (swim_node_t*)malloc(sizeof(swim_node_t));
  assert(node);

  swim_node_metadata_init(&node->metadata);

  if (node_id) {
    node->node_id = sdsdup(node_id);
  }

  if (host) {
    node->host = sdsdup(host);
    node->port = port;
  }

  node->status = SWIM_NODE_STATUS_UNKNOWN;
  return node;
}

void swim_node_destroy(swim_node_t* node) {
  sdsfree(node->node_id);
  sdsfree(node->host);

  free((void *)node);
}

void swim_node_pack(swim_node_t* node, mpack_writer_t* writer) {
  mpack_write_cstr(writer, node->node_id);
  mpack_write_cstr(writer, node->host);
  mpack_write_u16(writer, node->port);
  swim_node_metadata_pack(&node->metadata, writer);
  mpack_write_u8(writer, node->status);
}

void swim_node_unpack(swim_node_t* node, mpack_reader_t* reader) {
  char* node_id = mpack_expect_cstr_alloc(reader, 4096);
  node->node_id = sdsnew(node_id);
  mpack_done_str(reader);
  free(node_id);

  char* host = mpack_expect_cstr_alloc(reader, 4096);
  node->host = sdsnew(host);
  mpack_done_str(reader);
  free(host);

  node->port = mpack_expect_u16(reader);
  swim_node_metadata_unpack(&node->metadata, reader);
  node->status = mpack_expect_u8(reader);
}

uint64_t swim_node_hash(swim_node_t* node) {
  return farmhash_fingerprint64((void*)node, sizeof(swim_node_t));
}

swim_state_t* swim_state_create(uv_loop_t* loop, swim_node_t* self, sds host, uint16_t port) {
  swim_state_t* state = (swim_state_t*)malloc(sizeof(swim_state_t));
  state->loop = loop;
  state->self = self;
  state->members = array_create();
  state->ping_reqs = array_create();

  // Add ourselves to the members list
  array_append(state->members, (void*)self);

  // Setup the RECV socket
  uv_udp_init(loop, &state->recv_socket);
  state->recv_socket.data = (void*)state;
  struct sockaddr_in recv_addr;
  uv_ip4_addr(host, port, &recv_addr);
  uv_udp_bind(&state->recv_socket, (const struct sockaddr *)&recv_addr, UV_UDP_REUSEADDR);
  uv_udp_recv_start(&state->recv_socket, &_swim_uv_alloc_cb, _swim_uv_read_cb);

  // Setup the SEND socket
  uv_udp_init(loop, &state->send_socket);
  state->send_socket.data = (void*)state;
  struct sockaddr_in broadcast_addr;
  uv_ip4_addr("0.0.0.0", 0, &broadcast_addr);
  uv_udp_bind(&state->send_socket, (const struct sockaddr *)&broadcast_addr, 0);

  // Setup the PING loop which pings nodes
  uv_timer_init(state->loop, &state->ping_loop);
  state->ping_loop.data = (void*)state;
  // uv_timer_start(&state->ping_loop, &swim_state_ping_loop_callback, PING_LOOP_FREQ, PING_LOOP_FREQ);

  return state;
}

void swim_state_destroy(swim_state_t* state) {
  array_destroy(state->members);
  array_destroy(state->ping_reqs);
  free((void *)state);
}

void swim_state_seed_node(swim_state_t* state, swim_node_t* node) {
  printf("[%s] attempting to seed from node %s:%d\n",
      state->self->node_id,
      node->host,
      node->port);

  // Send HELLO
  swim_state_send_hello(state, node);
}

void swim_state_add_node(swim_state_t* state, swim_node_t* node) {
  printf("[%s] adding node %s\n",
      state->self->node_id,
      node->node_id);
  array_append(state->members, (void*)node);
}

void swim_state_remove_node(swim_state_t* state, swim_node_t* node) {
  array_pop(state->members, array_find(state->members, node));

  // TODO: clenaup ping reqs? etc?
}

void swim_state_update_node(swim_state_t* state, swim_node_t* node) {
  // TODO: make this random
  swim_node_t* other;
  for (size_t num = 0; num < state->members->length; num++) {
    other = array_index(state->members, (size_t)num);
    if (strcmp(other->node_id, state->self->node_id) != 0) {
      swim_state_send_node_update(state, other, node);
    }
  }
}

swim_node_t* swim_state_get_random_node(swim_state_t* state) {
  int r = rand() % ((state->members->length - 1) + 1 - 0);
  return array_index(state->members, (size_t)r);
}

void swim_state_send(swim_state_t* state, swim_node_t* other, uv_buf_t* buf) {
  printf("[%s] sending %d bytes to %s (%02X %02X %02X)\n",
      state->self->node_id,
      buf->len,
      other->host,
      buf->base[0], buf->base[1], buf->base[2]);

  uv_udp_send_t* send_req = malloc(sizeof(uv_udp_send_t));
  struct sockaddr_in send_addr;
  uv_ip4_addr(other->host, other->port, &send_addr);
  uv_udp_send(
    send_req,
    &state->send_socket,
    buf,
    1,
    (const struct sockaddr *)&send_addr,
    &_swim_uv_send_cb);
}

void swim_state_send_hello(swim_state_t* state, swim_node_t* other) {
  SWIM_STATE_PACKET(state->self, SWIM_OP_HELLO);

  mpack_write_cstr(&writer, state->self->host);
  mpack_write_u16(&writer, state->self->port);
  swim_node_metadata_pack(&state->self->metadata, &writer);

  assert(mpack_writer_destroy(&writer) == mpack_ok);
  swim_state_send(state, other, &buf);
  free(buf.base);
}

void swim_state_send_state(swim_state_t* state, swim_node_t* other) {
  SWIM_STATE_PACKET(state->self, SWIM_OP_STATE);

  mpack_start_array(&writer, state->members->length);
  for (size_t i = 0; i < state->members->length; i++) {
    swim_node_t* node = (swim_node_t*)array_index(state->members, i);
    swim_node_pack(node, &writer);
  }
  mpack_finish_array(&writer);

  assert(mpack_writer_destroy(&writer) == mpack_ok);
  swim_state_send(state, other, &buf);
  free(buf.base);
}

void swim_state_send_node_update(swim_state_t* state, swim_node_t* other, swim_node_t* node) {
  SWIM_STATE_PACKET(state->self, SWIM_OP_NODE_UPDATE);

  mpack_write_u64(&writer, swim_node_hash(node));
  swim_node_pack(node, &writer);

  assert(mpack_writer_destroy(&writer) == mpack_ok);
  printf("[%s] sending node update for %s to %s (%d)\n",
      state->self->node_id,
      node->node_id,
      other->node_id,
      buf.len);

  mpack_reader_t reader;
  mpack_reader_init_data(&reader, buf.base, buf.len);

  swim_state_send(state, other, &buf);
  free(buf.base);
}

void swim_state_send_ping(swim_state_t* state, swim_node_t* other, int64_t salt) {
  SWIM_STATE_PACKET(state->self, SWIM_OP_PING);
  mpack_write_i64(&writer, salt);

  assert(mpack_writer_destroy(&writer) == mpack_ok);
  swim_state_send(state, other, &buf);
  free(buf.base);
}

void swim_state_send_pong(swim_state_t* state, swim_node_t* other, int64_t salt) {
  SWIM_STATE_PACKET(state->self, SWIM_OP_PONG);
  mpack_write_i64(&writer, salt);

  assert(mpack_writer_destroy(&writer) == mpack_ok);
  swim_state_send(state, other, &buf);
  free(buf.base);
}

bool _remote_node_finder(void* ctx, void* node) {
  return strcmp((sds)ctx, ((swim_node_t*)node)->node_id) == 0;
}

bool _swim_state_ping_req_finder(void* ctx, void* req) {
  return *(int64_t*)ctx == ((swim_state_ping_req_t*)req)->salt;
}

void swim_state_handle(swim_state_t* state, const uv_buf_t* buf, ssize_t nread) {
  // First item in every payload should be our opcode
  mpack_reader_t reader;
  mpack_reader_init_data(&reader, buf->base, nread);

  mpack_tag_t tag;

  // Read the opcode
  tag = mpack_peek_tag(&reader);
  // assert(tag.type == mpack_type_uint);
  swim_opcode_t op = (swim_opcode_t)mpack_expect_u8(&reader);

  // Read the node id from the header
  tag = mpack_peek_tag(&reader);
  // assert(tag.type == mpack_type_str);
  char* _node_id = mpack_expect_cstr_alloc(&reader, 4096);
  sds node_id = sdsnew(_node_id);
  mpack_done_str(&reader);
  free(_node_id);

  if (op == 0x00) {
    printf("[%s] ERROR: (%s) bad op in %d bytes\n", state->self->node_id, node_id, nread);
    goto cleanup;
  }

  switch (op) {
    // If we recieve an OP HELLO, add the remote node to our ring (in unknown),
    //  then send back state, and finally request ping checks to promote the node
    //  to healthy...
    case SWIM_OP_HELLO: {
      // Read the remote host
      char* host = mpack_expect_cstr_alloc(&reader, 4096);
      mpack_done_str(&reader);

      // Construct the node structure
      swim_node_t* node = swim_node_create(
        node_id,
        sdsnew(host),
        mpack_expect_u16(&reader)
      );

      swim_node_metadata_unpack(&node->metadata, &reader);

      printf("[%s] got OP_HELLO from node %s (%s / %s)\n",
          state->self->node_id,
          node->node_id,
          node->metadata.datacenter,
          node->metadata.rack);

      // Cleanup msgpack strings
      free(host);

      if (strcmp(state->self->node_id, node_id) == 0) {
        swim_node_destroy(node);
        goto cleanup;
      }

      // Add the node to our swim, force it to be unknown so we ping it and check
      node->status = SWIM_NODE_STATUS_UNKNOWN;
      swim_state_add_node(state, node);

      // Ping the node in 5 seconds
      swim_state_ping_later(state, node, 5000);

      // Send state to the node
      swim_state_send_state(state, node);
      goto cleanup;
    }
    // OP_STATE holds the ring information as seen from one of our seeds,
    //  so for this we simply want to merge it in to our ring.
    case SWIM_OP_STATE: {
      size_t nodes = mpack_expect_array(&reader);

      for (size_t i = 0; i < nodes; i++) {
        swim_node_t* node = swim_node_create(NULL, NULL, 0);
        swim_node_unpack(node, &reader);

        if (strcmp(node->node_id, state->self->node_id) == 0) {
          continue;
        }

        // node->status = SWIM_NODE_STATUS_UNKNOWN;
        swim_state_add_node(state, node);
        // swim_state_ping(state, node);
      }

      printf("[%s] got %d ring members from seed state\n",
          state->self->node_id,
          (int)state->members->length);
      goto cleanup;
    }
    default: break;
  }

  swim_node_t* remote = (swim_node_t*)array_search(state->members, &_remote_node_finder, (void*)node_id);
  if (remote == NULL) {
    printf("[%s] could not find node in ring for %s (%d)\n", state->self->node_id, node_id, op);
    goto cleanup;
  }

  switch (op) {
    case SWIM_OP_PING: {
      int64_t salt = mpack_expect_i64(&reader);
      printf("[%s] got ping request from %s (%lld)\n",
        state->self->node_id,
        remote->node_id,
        salt);
      swim_state_send_pong(state, remote, salt);
      break;
    }
    case SWIM_OP_PONG: {
      int64_t salt = mpack_expect_i64(&reader);
      swim_state_ping_req_t* req = (swim_state_ping_req_t*)array_search(
        state->ping_reqs,
        &_swim_state_ping_req_finder,
        &salt
      );

      // If the status was unknown, this is an initial ping which was successful,
      //  we now want to inform the rest of the cluster about this node.
      if (remote->status == SWIM_NODE_STATUS_UNKNOWN) {
        remote->status = SWIM_NODE_STATUS_OK;
        swim_state_update_node(state, remote);
      }

      if (req == NULL) {
        printf("[%s] got unexpected pong from %s\n", state->self->node_id, remote->node_id);
        break;
      }

      req->pong = true;
      printf("[%s] got ok pong from %s\n", state->self->node_id, remote->node_id);
      break;
    }
    case SWIM_OP_NODE_UPDATE: {
      uint64_t hash = mpack_expect_u64(&reader);
      swim_node_t* node = swim_node_create(NULL, NULL, 0);
      swim_node_unpack(node, &reader);

      swim_node_t* current = (swim_node_t*)array_search(
          state->members,
          &_remote_node_finder,
          node->node_id);

      bool prop = false;

      // TODO: random propagate
      if (current == NULL) {
        printf("[%s] got new node from update\n", state->self->node_id);
        swim_state_add_node(state, node);
        prop = true;
      } else if (strcmp(state->self->node_id, node->node_id) == 0) {

      } else {
        if (hash != swim_node_hash(current)) {
          if (node->status != current->status) {
            printf("[%s] node %s status changed from %d to %d\n",
              state->self->node_id,
              current->node_id,
              current->status,
              node->status);

            current->status = node->status;
            prop = true;
          }

          // TODO: check for metadata change
        }
      }

      if (prop) {
        printf("[%s] propagating changes to node %s\n",
          state->self->node_id,
          node->node_id);
          swim_state_update_node(state, node);
      }

      free(node);
      break;
    }
    default: {
      printf("[%s] got unhandled opcode %d\n", state->self->node_id, op);
    }
  }

  cleanup: {
    size_t remaining = mpack_reader_remaining(&reader, NULL);
    mpack_error_t error = mpack_reader_destroy(&reader);

    if (error != mpack_ok) {
      printf("reader error %s had %d bytes left\n",
          mpack_error_to_string(error),
          (int)remaining);
    }
  }
}

void swim_state_ping(swim_state_t* state, swim_node_t* node) {
  swim_state_ping_req_t* req = (swim_state_ping_req_t*)malloc(sizeof(swim_state_ping_req_t));

  req->state = state;
  req->node = node;
  req->timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
  req->salt = rand();
  req->pong = false;

  array_append(state->ping_reqs, (void*)req);

  // Create the timer and point its data to our request
  uv_timer_init(state->loop, req->timer);
  req->timer->data = (void*)req;
  uv_timer_start(req->timer, &swim_state_ping_callback, PING_TIMEOUT, 0);

  swim_state_send_ping(state, node, req->salt);
}

void swim_state_ping_later(swim_state_t* state, swim_node_t* node, uint64_t delay) {
  uv_timer_t* timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
  uv_timer_init(state->loop, timer);

  void** data = malloc(sizeof(void*) * 2);
  data[0] = (void*)state;
  data[1] = (void*)node;
  timer->data = (void*)data;

  uv_timer_start(timer, &swim_state_ping_later_callback, delay, 0);
}

void swim_state_ping_later_callback(uv_timer_t* timer) {
  void** data = (void**)timer->data;
  swim_state_t* state = (swim_state_t*)data[0];
  swim_node_t* node = (swim_node_t*)data[1];

  swim_state_ping(state, node);
  free(timer->data);
  free(timer);
}

void swim_state_ping_callback(uv_timer_t* timer) {
  swim_state_ping_req_t* req = (swim_state_ping_req_t*)timer->data;

  if (!req->pong) {
    printf("[%s] node %s did not answer ping request, checking from other nodes\n",
        req->state->self->node_id,
        req->node->node_id);

    // TODO: request other nodes ping check
    return;
  } else {
    printf("[%s] node %s pinged OK\n",
          req->state->self->node_id,
          req->node->node_id);
  }

  size_t idx = array_find(req->state->ping_reqs, timer->data);
  array_pop(req->state->ping_reqs, idx);

  free(req);
  free(timer);
}

void swim_state_ping_loop_callback(uv_timer_t* timer) {
  swim_state_t* state = (swim_state_t*)timer->data;
  swim_node_t* node = swim_state_get_random_node(state);

  if (node->status == SWIM_NODE_STATUS_UNKNOWN) return;
  swim_state_ping(state, node);
}
