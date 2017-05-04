# Concepts

- Custom Gossip Protocol
	- Maintains state and health information
- Data is sharded into a hash ring
	- Nodes can select which shards they would like to serve
	- Users can put constraints on tables and queries to ensure N+1
- Actual data is stored within rocksdb
- Secondary indexes can be built
	- Indexes require more data to be synced
	- Indexes can be marked as stale-ok, and won't block insert/update
- Cluster can be weighted based on location
	- Reads/Writes can be forced to local DC, or remote DC
	- Entire tables or shards can be gravitated towards regions

## Scratch

- Each shard splits ownership based on a hash of the sorted node\_ids who are members, this way ownership of the data gets split evenly?

# ETC

- Protocol is simple, built over msgpack
- On disk encrpytion
