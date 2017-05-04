#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdlib.h>
#include <uv.h>

#include "mpack/mpack.h"
#include "sds/sds.h"
#include "array.h"

typedef enum {
  // The node is in an unknown state, generally this means we just added the node
  //  and are waiting to ping-verify it.
  SWIM_NODE_STATUS_UNKNOWN,
  // The node is in an OK state
  SWIM_NODE_STATUS_OK,
  // The node is in a BAD state, which means it has failed ping checks
  SWIM_NODE_STATUS_BAD,
  // The node is in a RECOVERING state, coming back from BAD
  SWIM_NODE_STATUS_RECOVERING,
} swim_node_status_t;

typedef enum {
  // Used when first conneting to a seed node
  SWIM_OP_HELLO = 0x01,
  // Sent to a node who is initially connecting, contains state information
  SWIM_OP_STATE = 0x02,
  // Sent when a nodes status changes, usually due to it becoming bad/recovering
  SWIM_OP_NODE_UPDATE = 0x03,
  // Sent when a node is testing connectivity between a peer
  SWIM_OP_PING = 0x03,
  // Sent when a node wants wants a peer to test connectvitiy between another peer
  SWIM_OP_PING_REQ = 0x04,
  // Sent as the response to a ping
  SWIM_OP_PONG = 0x05,
} swim_opcode_t;

typedef enum {
	SWIM_NODE_TYPE_MEMBER,
	SWIM_NODE_TYPE_LISTENER,
} swim_node_type_t;

typedef struct swim_node_metadata_s {
  // The nodes type
	swim_node_type_t type;

  // Information on what shards this node subscribes too
  size_t num_shards;
  uint64_t* shards;

  // Location information
  sds datacenter;
  sds rack;
} swim_node_metadata_t;

/**
  Represents a single SWIM node within our ring. This contains state and information
  about our perspective of the node, as well as the metadata and information that
  comes alongside it.
*/
typedef struct swim_node_s {
  // The node ID, generally a UUID
  sds node_id;

  // The node host and port
  sds host;
  uint16_t port;

  // TODO: metadata
  // hashmap?

  // The node status
  swim_node_status_t status;
} swim_node_t;

swim_node_t* swim_node_create(sds, sds, uint16_t);
void swim_node_destroy(swim_node_t*);

/**
  Represents a stateful observer of the SWIM ring. The state is used as the base
  abstraction for a node interacting with the SWIM ring, and maintains connections
  and state required for processing ring gossip.
**/
typedef struct swim_state_s {
  // The uv loop for this state
  uv_loop_t* loop;

  // The node for this state, can be null
  swim_node_t* self;

  // All ring members this state is aware of
  array_t* members;

  // Sockets for gossip
  uv_udp_t send_socket;
  uv_udp_t recv_socket;
} swim_state_t;

swim_state_t* swim_state_create(uv_loop_t*, swim_node_t*, sds, uint16_t);
void swim_state_destroy(swim_state_t*);

void swim_state_seed_node(swim_state_t*, swim_node_t*);
void swim_state_add_node(swim_state_t*, swim_node_t*);
void swim_state_remove_node(swim_state_t*, swim_node_t*);
swim_node_t* swim_state_get_random_node(swim_state_t*);

// Networking functions
void swim_state_send(swim_state_t*, swim_node_t*, uv_buf_t*);
void swim_state_send_hello(swim_state_t*, swim_node_t*);
void swim_state_send_state(swim_state_t*, swim_node_t*);
void swim_state_send_node_update(swim_state_t*, swim_node_t*);
void swim_state_send_ping(swim_state_t*, swim_node_t*);
void swim_state_send_ping_req(swim_state_t*, swim_node_t*);
void swim_state_send_pong(swim_state_t*, swim_node_t*);

void swim_state_handle(swim_state_t*, const uv_buf_t*);
