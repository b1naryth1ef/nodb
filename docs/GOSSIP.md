# Gossip

Gossip in nodb provides a system in which node health and metadata can be propagated sanely through a system. By default, clients and nodes have backoff and connection information for the actual control sockets and connections they keep. However, the health of a node as the member or provider of a shard must be stated and stored in a consistent, distributed form. The nodb gossip protocol builds on SWIM to provide a system for disementation of information:

- nodb gossip maintains a set of all nodes in the cluster
  - nodes are identified by a node\_id, generally we think of this as a UUID
  - nodes have a host and port which the gossip protocol can use
    - important to note this is _not_ the same as the control/data sockets, instead that information is kept within the gossip metadata
  - nodes have a set of metadata
    - node state (e.g. member (active in serving data) or listener (only cares about updates, will not serve data))
    - node shards (array of shards the node cares about)
    - node performance (latency? request load? etc?)
    - node location (dc, rack, etc)


## Protocol

At boot, a node considers itself in an UNKNOWN state, and is given a list of seeds. It connects to these seeds and issues an OP\_HELLO, which informs the seeds of the nodes host/port and id. Seeds will then respond with an OP\_STATE, containing the entire gossip ring state. At this point a node is considered connected. Every second, nodes randomly select a single gossip member to test, and emit a OP\_PING request to the member. If the member does not reply with a OP\_PONG in (duration?) the node then picks 3 other random ring members, and emits a OP\_PING\_REQ. If these nodes do not respond with an OP\_PONG (signaling the original test node is up for them), the initial testing node emits an OP\_NODE\_UPDATE with state set to BAD.

When a node is in a bad state, the member that initially pinged and marked it down (which could in theory be many members depending on rng) continues to send OP\_PING's forever. If the node begins to resopnd with OP\_PONGs again, a similar process begins wherein other members are used to check the state. It status is also upgraded to recovering. If the member successfuly passes all checks (in a window, maybe 30 seconds?) then it is promoted to OK.
