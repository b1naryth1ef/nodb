# Gossip

nodb uses a form of SWIM to maintain node state and metadata, the core points of nodb's gossip are:

- Maintain a list of all nodes in the cluster
- Maintain a set of metadata per node
  - Node shard state
  - Node location (e.g. dc, rack)
  - Node reliability
- Maintain which nodes are available
  - By detecting failures, we remove nodes from the ring and can route requests to different members of a shard
