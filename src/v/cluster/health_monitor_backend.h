
/*
 * Copyright 2020 Vectorized, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */
#pragma once
#include "cluster/fwd.h"
#include "cluster/health_monitor_types.h"
#include "cluster/members_table.h"
#include "cluster/partition_manager.h"
#include "model/metadata.h"
#include "raft/consensus.h"

#include <seastar/core/semaphore.hh>
#include <seastar/core/sharded.hh>

#include <absl/container/node_hash_map.h>

#include <chrono>
#include <vector>
namespace cluster {
/**
 * Health monitor backend is responsible for collecting cluster health status
 * and caching cluster health information.
 *
 * Health monitor status collection is active only on the node which is a
 * controller partition leader. When any other node is requesting a cluster
 * report it either uses locally cached state or asks controller leader for
 * new report.
 */
class health_monitor_backend {
public:
    static constexpr ss::shard_id shard{0};

    health_monitor_backend(
      ss::lw_shared_ptr<raft::consensus>,
      ss::sharded<members_table>&,
      ss::sharded<rpc::connection_cache>&,
      ss::sharded<partition_manager>&,
      ss::sharded<ss::abort_source>&);

    ss::future<> stop();

    ss::future<result<cluster_health_report>> get_cluster_health(
      cluster_report_filter, force_refresh, model::timeout_clock::time_point);

    cluster_health_report
    get_current_cluster_health_snapshot(const cluster_report_filter&);

    ss::future<result<node_health_report>>
      collect_current_node_health(node_report_filter);

private:
    struct reply_status {
        ss::lowres_clock::time_point last_reply_timestamp
          = ss::lowres_clock::time_point::min();
        alive is_alive = alive::no;
    };

    using status_cache_t = absl::node_hash_map<model::node_id, node_state>;
    using report_cache_t
      = absl::node_hash_map<model::node_id, node_health_report>;

    using last_reply_cache_t
      = absl::node_hash_map<model::node_id, reply_status>;

    void tick();
    ss::future<> collect_cluster_health();
    ss::future<result<node_health_report>>
      collect_remote_node_health(model::node_id);

    ss::future<std::error_code> refresh_cluster_health_cache(force_refresh);

    cluster_health_report build_cluster_report(const cluster_report_filter&);

    std::optional<node_health_report>
    build_node_report(model::node_id, const node_report_filter&);

    ss::future<std::vector<topic_status>>
      collect_topic_status(partitions_filter);

    std::vector<node_disk_space> get_disk_space();

    void refresh_nodes_status();

    result<node_health_report>
      process_node_reply(model::node_id, result<get_node_health_reply>);

    std::chrono::milliseconds tick_interval();
    std::chrono::milliseconds max_metadata_age();

    ss::lw_shared_ptr<raft::consensus> _raft0;
    ss::sharded<members_table>& _members;
    ss::sharded<rpc::connection_cache>& _connections;
    ss::sharded<partition_manager>& _partition_manager;
    ss::sharded<ss::abort_source>& _as;

    ss::lowres_clock::time_point _last_refresh;

    status_cache_t _status;
    report_cache_t _reports;
    last_reply_cache_t _last_replies;

    ss::timer<ss::lowres_clock> _tick_timer;
    ss::gate _gate;
    mutex _refresh_mutex;
};
} // namespace cluster
