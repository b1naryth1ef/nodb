/**
  Implements a form of the SWIM gossip protocol
**/

#include "swim.h"

static void _swim_uv_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

static void _swim_uv_read_cb(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
  swim_state_t* state = (swim_state_t*)handle->data;

  if (nread <= 0) {
    printf("[%s] read error\n", state->self->node_id);
    return;
  }

  if (addr == NULL) {
    printf("[%s] null addr\n", state->self->node_id);
    return;
  }

  char sender[17] = {0};
  uv_ip4_name((struct sockaddr_in*) addr, sender, 16);

  printf("[%s] read data from %s\n", state->self->node_id, sender);
  swim_state_handle(state, buf);
  free(buf->base);
}

void _swim_uv_send_cb(uv_udp_send_t* req, int status) {
  free(req);
}


swim_node_t* swim_node_create(sds node_id, sds host, uint16_t port) {
  swim_node_t* node = (swim_node_t*)malloc(sizeof(swim_node_t));
  assert(node);

  if (node_id) {
    node->node_id = sdsdup(node_id);
  }

  node->host = sdsdup(host);
  node->port = port;
  node->status = SWIM_NODE_UNKNOWN;

  return node;
}

void swim_node_destroy(swim_node_t* node) {
  sdsfree(node->node_id);
  sdsfree(node->host);
  free((void *)node);
}

swim_state_t* swim_state_create(uv_loop_t* loop, swim_node_t* self, sds host, uint16_t port) {
  swim_state_t* state = (swim_state_t*)malloc(sizeof(swim_state_t));
  state->loop = loop;
  state->self = self;
  state->members = array_create();

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

  return state;
}

void swim_state_destroy(swim_state_t* state) {
  array_destroy(state->members);
  free((void *)state);
}

void swim_state_seed_node(swim_state_t* state, swim_node_t* node) {
  swim_state_add_node(state, node);

  // Send HELLO
  swim_state_send_hello(state, node);
}

void swim_state_add_node(swim_state_t* state, swim_node_t* node) {
  array_append(state->members, (void*)node);
}

void swim_state_remove_node(swim_state_t* state, swim_node_t* node) {
  array_pop(state->members, array_find(state->members, node));
}

swim_node_t* swim_state_get_random_node(swim_state_t* state) {
  int r = rand() % ((state->members->length - 1) + 1 - 0);
  return array_index(state->members, (size_t)r);
}

void swim_state_send(swim_state_t* state, swim_node_t* other, uv_buf_t* buf) {
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
  uv_buf_t buf;
  mpack_writer_t writer;
  mpack_writer_init_growable(&writer, &buf.base, &buf.len);

  mpack_write_u8(&writer, (uint8_t)SWIM_OP_HELLO);
  mpack_write_cstr(&writer, state->self->node_id);
  mpack_write_cstr(&writer, state->self->host);
  mpack_write_u16(&writer, state->self->port);

  assert(mpack_writer_destroy(&writer) == mpack_ok);

  swim_state_send(state, other, &buf);
  free(buf.base);
}

void swim_state_send_state(swim_state_t* state, swim_node_t* other) {
  uv_buf_t buf;
  mpack_writer_t writer;
  mpack_writer_init_growable(&writer, &buf.base, &buf.len);

  mpack_write_u8(&writer, (uint8_t)SWIM_OP_STATE);

  // Array of all nodes within

  assert(mpack_writer_destroy(&writer) == mpack_ok);

  swim_state_send(state, other, &buf);
  free(buf.base);
}

void swim_state_handle(swim_state_t* state, const uv_buf_t* buf) {
  // First item in every payload should be our opcode
  mpack_reader_t reader;
  mpack_reader_init_data(&reader, buf->base, buf->len);

  swim_opcode_t op = (swim_opcode_t)mpack_expect_u8(&reader);
  printf("got op code %d\n", op);

  switch (op) {
    // If we recieve an OP HELLO, add the remote node to our ring (in unknown),
    //  then send back state, and finally request ping checks to promote the node
    //  to healthy...
    case SWIM_OP_HELLO: {
      // Read node_id and host strings
      char* node_id = mpack_expect_cstr_alloc(&reader, 4096);
      mpack_done_str(&reader);
      char * host = mpack_expect_cstr_alloc(&reader, 4096);
      mpack_done_str(&reader);

      // Construct the node structure
      swim_node_t* node = swim_node_create(
        sdsnew(host),
        sdsnew(node_id),
        mpack_expect_u16(&reader)
      );

      // Cleanup msgpack strings
      free(host);
      free(node_id);

      // Add the node to our swim
      node->status = SWIM_NODE_UNKNOWN;
      swim_state_add_node(state, node);

      // Send state to the node
      swim_state_send_state(state, node);
    }
  }

  size_t remaining = mpack_reader_remaining(&reader, NULL);
  mpack_error_t error = mpack_reader_destroy(&reader);

  if (error != mpack_ok) {
    printf("reader error %s had %d bytes left\n",
        mpack_error_to_string(error),
        (int)remaining);
  }

  // assert(mpack_reader_destroy(&reader) == mpack_ok);
}
