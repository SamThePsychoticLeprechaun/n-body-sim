#include <algorithm>
#include <limits>
#include <random>

template <typename Precision>
Precision cluster::member_distance_2(const Member<Precision>& lhs, const Member<Precision>& rhs) {
    return (lhs.x - rhs.x) * (lhs.x - rhs.x)
            + (lhs.y - rhs.y) * (lhs.y - rhs.y)
            + (lhs.z - rhs.z) * (lhs.z - rhs.z);
}

template <typename Precision>
void cluster::kpp(Member<Precision>* members, ui32 member_count, Member<Precision>* centroids, ui32 centroid_count) {
    /************
       Set up metadata for k++ algorithm.
                                ************/

    ui32 number_of_members_chosen = 1;
    bool* member_chosen_as_centroid = new bool[member_count];

    Precision* cumulative_distance_2s = new Precision[member_count];
    Precision  total_distance_2 = 0.0;

    std::default_random_engine generator;

    /************
       Make initial choice of a centroid.
                                ************/

    {
        std::uniform_int_distribution<ui32> distribution(0, member_count - 1);
        ui32 initial_choice = distribution(generator);

        centroids[0] = members[initial_choice];
        member_chosen_as_centroid[initial_choice] = true;
    }

    /************
       Perform k++ algorithm for each subsequent centroid.
                                                 ************/

    for (ui32 centroid_idx = 1; centroid_idx < centroid_count; ++centroid_idx) {
        //
        // For each member of the dataset not so far chosen as a centroid, determine
        // the minimum distance to a chosen centroid and select one of those members
        // to be the next centroid with probability proportional to distance^2 from
        // nearest centroid.
        //

        // Calculate distances for each member not so far chosen as a centroid.
        for (ui32 member_idx = 0; member_idx < member_count; ++member_idx) {
            // Skip members already chosen as a centroid.
            if (member_chosen_as_centroid[member_idx]) {
                cumulative_distance_2s[member_idx] = cumulative_distance_2s[member_idx - 1];
                continue;
            }

            // Calculate distance to nearest chosen centroid.
            Precision minimum_distance_2_to_chosen_centroids = std::numeric_limits<Precision>::max();
            for (ui32 chosen_centroid_idx = 0; chosen_centroid_idx < centroid_idx; ++chosen_centroid_idx) {
                Precision distance_2_to_chosen_centroid = member_distance_2(members[member_idx], centroids[chosen_centroid_idx]);

                if (distance_2_to_chosen_centroid < minimum_distance_2_to_chosen_centroids) {
                    minimum_distance_2_to_chosen_centroids = distance_2_to_chosen_centroid;
                }
            }

            // Place the calculated distance into metadata.
            if (member_idx == 0) {
                cumulative_distance_2s[member_idx] = minimum_distance_2_to_chosen_centroids;
            } else {
                cumulative_distance_2s[member_idx] = cumulative_distance_2s[member_idx - 1] + minimum_distance_2_to_chosen_centroids;
            }
            total_distance_2 += minimum_distance_2_to_chosen_centroids;
        }

        // Select a member to be next chosen centroid with probability proportional to
        // distance^2 to nearest existing centroid.
        std::uniform_real_distribution<Precision> distribution(0.0, total_distance_2);
        Precision choice = distribution(generator);

        for (ui32 member_idx = 0; member_idx < member_count; ++member_idx) {
            if (cumulative_distance_2s[member_idx] > choice) {
                centroids[centroid_idx] = members[member_idx];
                member_chosen_as_centroid[member_idx] = true;
            }
        }
    }

    /************
       Clean-up.
       ************/

    delete[] member_chosen_as_centroid;
    delete[] cumulative_distance_2s;
}

template <typename Precision>
cluster::impl::NearestCentroid<Precision> cluster::impl::nearest_centroid(const Member<Precision>& member, const MemberClusterMetadata<Precision>& member_metadata, const Cluster<Precision>* clusters, ui32 cluster_count) {
    // Optimisation by early back out of search if previous nearest centroid has got closer - in which case it is guaranteed to still be the nearest centroid.
    //     This is based on the paper "An Efficient Enhanced k-means Clustering Algorithm" by Fahim A.M., Salem A.M., Torkey F.A., and Ramadan M.A.
    Precision new_distance_2_to_current_cluster = member_distance_2(member, clusters[member_metadata.current_cluster.idx].centroid);
    if (new_distance_2_to_current_cluster < member_metadata.current_cluster.distance) {
        return NearestCentroid<Precision>{
            member_metadata.current_cluster.idx,
            new_distance_2_to_current_cluster
        };
    }

    NearestCentroid<Precision> nearest_centroid = {
        member_metadata.current_cluster.idx,
        new_distance_2_to_current_cluster
    };

    for (ui32 cluster_idx = 0; cluster_idx < cluster_count; ++cluster_idx) {
        if (cluster_idx == member_metadata.current_cluster.idx) continue;

        Precision centroid_distance_2 = member_distance_2(member, clusters[cluster_idx].centroid);

        if (centroid_distance_2 < nearest_centroid.distance) {
            nearest_centroid.idx = cluster_idx;
            nearest_centroid.distance = centroid_distance_2;
        }
    }

    return nearest_centroid;
}

template <typename Precision>
cluster::impl::NearestCentroid<Precision> cluster::impl::nearest_centroid_from_subset(const Member<Precision>& member, const MemberClusterMetadata<Precision>& member_metadata, const Cluster<Precision>* clusters, impl::NearestCentroidList cluster_subset, ui32 cluster_count) {
    // Optimisation by early back out of search if previous nearest centroid has got closer - in which case it is guaranteed to still be the nearest centroid.
    //     This is based on the paper "An Efficient Enhanced k-means Clustering Algorithm" by Fahim A.M., Salem A.M., Torkey F.A., and Ramadan M.A.
    Precision new_distance_2_to_current_cluster = member_distance_2(member, clusters[member_metadata.current_cluster.idx].centroid);
    if (new_distance_2_to_current_cluster < member_metadata.current_cluster.distance) {
        return NearestCentroid<Precision>{
            member_metadata.current_cluster.idx,
            new_distance_2_to_current_cluster
        }
    }

    NearestCentroid<Precision> nearest_centroid = {
        member_metadata.current_cluster.idx,
        new_distance_2_to_current_cluster
    };

    for (ui32 cluster_subset_idx = 0; cluster_subset_idx < cluster_count; ++cluster_subset_idx) {
        ui32 cluster_idx = cluster_subset.indices[cluster_subset_idx];

        if (cluster_idx == member_metadata.current_cluster.idx) continue;

        Precision centroid_distance_2 = member_distance_2(member, clusters[cluster_idx].centroid);

        if (centroid_distance_2 < nearest_centroid.distance) {
            nearest_centroid.idx = cluster_idx;
            nearest_centroid.distance = centroid_distance_2;
        }
    }

    return nearest_centroid;
}

#include <map>

template <typename Precision>
cluster::impl::NearestCentroidAndList<Precision> cluster::impl::nearest_centroid_and_build_list(const Member<Precision>& member, const MemberClusterMetadata<Precision>& member_metadata, const Cluster<Precision>* clusters, ui32 cluster_count, ui32 subset_count) {
    // Optimisation by making a subset of centroids to consider for a given particle.
    //     This is based on the paper "Faster k-means Cluster Estimation" by Khandelwal S., Awekar A.
    std::multimap<Precision, ui32> centroid_list;

    NearestCentroidAndList<Precision> nearest_centroid_and_list = {
        NearestCentroid<Precision>{
            member_metadata.current_cluster.idx,
            member_distance_2(member, clusters[member_metadata.current_cluster.idx].centroid)
        },
        NearestCentroidList{
            new ui32[subset_count]
        }
    };

    for (ui32 cluster_idx = 0; cluster_idx < cluster_count; ++cluster_idx) {
        if (cluster_idx == member_metadata.current_cluster.idx) continue;

        Precision centroid_distance_2 = member_distance_2(member, clusters[cluster_idx].centroid);

        centroid_list.insert(std::make_pair(centroid_distance_2, cluster_idx));

        if (centroid_distance_2 < nearest_centroid_and_list.centroid.distance) {
            nearest_centroid_and_list.centroid.idx = cluster_idx;
            nearest_centroid_and_list.centroid.distance = centroid_distance_2;
        }
    }

    for (ui32 idx = 0; idx < subset_count; ++idx) {
        auto it = centroid_list.begin() + idx;

        nearest_centroid_and_list.list.indices[idx] = *it;
    }

    return nearest_centroid_and_list;
}

#include <cstring>

template <typename Precision>
void cluster::k_means(const Cluster<Precision>* initial_clusters, ui32 cluster_count, ui32 member_count, const KMeansOptions& options, OUT Cluster<Precision>*& clusters) {
    /************
       Set up buffer of new clusters produced.
                                     ************/

    clusters = new Cluster<Precision>[cluster_count];
    std::memcpy(clusters, initial_clusters, sizeof(Cluster<Precision>) * cluster_count);

    /************
       Set up metadata for k-means algorithm.
                                    ************/
    
    impl::MemberClusterMetadata<Precision>* member_cluster_metadata = new impl::MemberClusterMetadata<Precision>[member_count];
    // TODO(Matthew): Do we template parameterise the centroid_subset_optimisation flag to reduce memory usage if it is disabled?
    [[maybe_unused]] impl::NearestCentroidList* nearest_centroids_lists = new impl::NearestCentroidList[member_count];
    bool* cluster_modified_in_iteration = new bool[cluster_count](false);

    /************
       Perform k-means algorithm.
                        ************/

    ui32 iterations = 0;
    ui32 changes_in_iteration = 0;
    do {
        // Complete if max iterations has been reached.
        if (++iterations > options.max_iterations) break;

        // Each iteration starts at zero changes!
        changes_in_iteration = 0;
        std::fill_n(cluster_modified_in_iteration, cluster_count, false);

        //
        // Iterate each member of the population on which the clusters are being built.
        // For each member, determine which centroid it is nearest to and add its position
        // to a new centroid which will then take its position as the average position of
        // the associated members.
        //

        ui32 global_member_idx = 0;
        // Iterate each initial cluster and then iterate members held by that cluster.
        for (ui32 initial_cluster_idx = 0; initial_cluster_idx < cluster_count; ++initial_cluster_idx) {
            // Get handle on cluster we're looking at.
            const Cluster<Precision>& cluster = clusters[initial_cluster_idx];

            for (ui32 in_cluster_member_idx = 0; in_cluster_member_idx < initial_clusters[initial_cluster_idx].member_count; ++in_cluster_member_idx) {
                // Set metadata at start of k_means algorithm.
                if (iterations == 0) {
                    member_cluster_metadata[global_member_idx].initial_member_idx = in_cluster_member_idx;
                    member_cluster_metadata[global_member_idx].initial_cluster_idx = initial_cluster_idx;
                    member_cluster_metadata[global_member_idx].current_cluster.idx = initial_cluster_idx;

                    // If we're front loaded, that means we're starting from no known clusters so just set distance to minimum possible.
                    //     This will result in the right behaviour when we search for the nearest centroid, with calculation
                    //     performed the first time over all centroids.
                    // Additionally, if we want to avoid the approaching centroid optimisation, then we should likewise set the distance
                    // to the minimum possible to force distance calculations for all centroids.
                    if (options.front_loaded || options.no_approaching_centroid_optimisation) {
                        member_cluster_metadata[global_member_idx].current_cluster.distance = std::numeric_limits<Precision>::min();
                    } else {
                        member_cluster_metadata[global_member_idx].current_cluster.distance = member_distance_2(cluster.members[in_cluster_member_idx], cluster.centroid);
                    }
                }

                impl::NearestCentroid<Precision> nearest_centroid;
                if (options.no_centroid_subset_optimisation || iterations == 0) {
                    nearest_centroid = impl::nearest_centroid(cluster.members[in_cluster_member_idx], member_cluster_metadata[global_member_idx], clusters, cluster_count);
                } else {
                    if (iterations == 1) {
                        impl::NearestCentroidAndList<Precision> nearest_centroid_and_list = impl::nearest_centroid_and_build_list(
                            cluster.members[in_cluster_member_idx],
                            member_cluster_metadata[global_member_idx],
                            clusters,
                            cluster_count,
                            options.centroid_subset.k_prime
                        );

                        nearest_centroid = nearest_centroid_and_list.centroid;
                        nearest_centroids_lists[global_member_idx] = nearest_centroid_and_list.list;
                    } else {
                        nearest_centroid = impl::nearest_centroid_from_subset(
                            cluster.members[in_cluster_member_idx],
                            member_cluster_metadata[global_member_idx],
                            clusters,
                            nearest_centroids_lists[global_member_idx],
                            cluster_count
                        );
                    }
                }

                // If this is the first member to join a cluster this round, then set values,
                // otherwise add the new values in.
                if (!cluster_modified_in_iteration[initial_cluster_idx]) {
                    clusters[nearest_centroid.idx].centroid.x = cluster.members[in_cluster_member_idx].x;
                    clusters[nearest_centroid.idx].centroid.y = cluster.members[in_cluster_member_idx].y;
                    clusters[nearest_centroid.idx].centroid.z = cluster.members[in_cluster_member_idx].z;

                    clusters[nearest_centroid.idx].member_count = 0;

                    cluster_modified_in_iteration[initial_cluster_idx] = true;
                } else {
                    clusters[nearest_centroid.idx].centroid.x += cluster.members[in_cluster_member_idx].x;
                    clusters[nearest_centroid.idx].centroid.y += cluster.members[in_cluster_member_idx].y;
                    clusters[nearest_centroid.idx].centroid.z += cluster.members[in_cluster_member_idx].z;

                    ++(clusters[nearest_centroid.idx].member_count);
                }

                // If the member has changed cluster membership, then update changes in iteration and its
                // current cluster index.
                if (member_cluster_metadata[global_member_idx].current_cluster.idx != nearest_centroid.idx) {
                    ++changes_in_iteration;
                    member_cluster_metadata[global_member_idx].current_cluster.idx = nearest_centroid.idx;
                }

                // No matter what, the distance to the centroid has very likely changed, so we should update it!
                // However, if we want to avoid the approaching centroid optimisation, then we should set the
                // distance to the minimum possible to force distance calculations for all centroids.
                if (options.no_approaching_centroid_optimisation) {
                    member_cluster_metadata[global_member_idx].current_cluster.distance = std::numeric_limits<Precision>::min();
                } else {
                    member_cluster_metadata[global_member_idx].current_cluster.distance = nearest_centroid.distance;
                }

                // Incremement global member index.
                ++global_member_idx;
            }

            // Only look at first cluster in buffer if front loaded.
            if (options.front_loaded) break;
        }

        // Using total members associated with each centroid, calculate the new
        // centroid for that group by taking the average of their positions.
        for (ui32 cluster_idx = 0; cluster_idx < cluster_count; ++cluster_idx) {
            clusters[cluster_idx].centroid.x /= clusters[cluster_idx].member_count;
            clusters[cluster_idx].centroid.y /= clusters[cluster_idx].member_count;
            clusters[cluster_idx].centroid.z /= clusters[cluster_idx].member_count;
        }
    } while (changes_in_iteration > options.acceptable_changes_per_iteration);

    /************
       Assign members to their final clusters.
                                     ************/

    // TODO(Matthew): Speed up by minimising copies by considering non-moving and moving members separately.
    //                    Implementation options include:
    //                        - sorted member buffers (on distance to centroid, e.g.); or,
    //                        - on-demand sorting, taking ownership of previous member buffers and extracting changed members and placing in others.

    // Allocate necessary buffers for clusters.
    for (ui32 cluster_idx = 0; cluster_idx < cluster_count; ++cluster_idx) {
        clusters[cluster_idx].members = new Member<Precision>[clusters[cluster_idx].member_count];
    }

    // For each member, copy it into the appropriate cluster buffer.
    ui32* member_cursors = new ui32[cluster_count](0);
    for (ui32 member_idx = 0; member_idx < member_count; ++member_idx) {
        ui32 new_cluster_offset = member_cursors[member_cluster_metadata[member_idx].current_cluster_idx]++;

        const Cluster<Precision>& old_cluster = initial_clusters[member_cluster_metadata[member_idx].initial_cluster_idx];
        const Cluster<Precision>& new_cluster = clusters[member_cluster_metadata[member_idx].current_cluster_idx];

        new_cluster.members[new_cluster_offset] = old_cluster.members[member_cluster_metadata[member_idx].initial_member_idx];
    }

    /************
       Clean-up.
       ************/

    delete[] member_cluster_metadata;
    delete[] cluster_modified_in_iteration;
}
