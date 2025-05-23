#pragma once

#include "1d_stripes.hpp"
#include <math.h>
#include <omp.h>

#define USE_PARALLEL_PARTITION 1

run_result_t
stripe_join_partitoin_dim0_sweep_dim1_parallel(std::vector<std::pair<MBR *, MBR *>> &results, std::vector<MBR *> *R,
                                               std::vector<MBR *> *S,
                                               float mapLow0, float mapHigh0, int numPartitions) {

    float partition_side_len = (mapHigh0 - mapLow0) / numPartitions;
    int total_threads = omp_get_max_threads();

    auto start_partition = std::chrono::high_resolution_clock::now();
    // partitioning phase

    // map to store events to process for each partion: partition num -> vector of Events
    // map changed to vector so that can use openMP, worth investigating performance in single too
    std::vector<std::vector<Event *>> partitions;

    // initialize our vectors in map
    for (int i = 0; i < numPartitions; i++) {
        std::vector<Event *> vec;
        partitions.push_back(vec);
    }

    // do a full scan of input data, for each MBR create event and put in the right partition

#if USE_PARALLEL_PARTITION // multi-thread partition

    // std::cout << "Using parallel partition" << std::endl;
    int batch_num = total_threads;
    int batch_size_R = 1 + R->size() / batch_num;
    int batch_size_S = 1 + S->size() / batch_num;
    // std::cout << "Number of threads: " << total_threads << std::endl;

#pragma omp parallel for schedule(static) num_threads(total_threads)
    for (int batch_id = 0; batch_id < batch_num; batch_id++) {
        // std::cout << "batch id: " << batch_id << "thread id: " << omp_get_thread_num() << std::endl;

        int start_R = batch_id * batch_size_R;
        int end_R = (batch_id + 1) * batch_size_R > R->size() ? R->size() : (batch_id + 1) * batch_size_R;

        // local_partition
        std::vector<std::vector<Event *>> partitions_local;
        for (int i = 0; i < numPartitions; i++) {
            std::vector<Event *> vec;
            partitions_local.push_back(vec);
        }

        // first R
        for (int i = start_R; i < end_R; i++) {
            int lowPart, highPart;
            MBR *mbr = R->at(i);
            lowPart = int((mbr->get_low0() - mapLow0) / partition_side_len);
            highPart = int((mbr->get_high0() - mapLow0) / partition_side_len);
            highPart = highPart >= numPartitions ? numPartitions - 1 : highPart;
            Event *eventBottom = new Event(mbr->get_low1(), true, mbr, 0);
            Event *eventTop = new Event(mbr->get_high1(), false, mbr, 0);

            for (int j = lowPart; j <= highPart; j++) {
                partitions_local[j].push_back(eventBottom);
                partitions_local[j].push_back(eventTop);
            }
        }

        int start_S = batch_id * batch_size_S;
        int end_S = (batch_id + 1) * batch_size_S > S->size() ? S->size() : (batch_id + 1) * batch_size_S;
        // std::cout << "start_S: " << start_S << " end_S: " << end_S << std::endl;

        // repeat for S
        for (int j = start_S; j < end_S; j++) {
            int lowPart, highPart;
            MBR *mbr = S->at(j);
            lowPart = int((mbr->get_low0() - mapLow0) / partition_side_len);
            highPart = int((mbr->get_high0() - mapLow0) / partition_side_len);
            highPart = highPart >= numPartitions ? numPartitions - 1 : highPart;
            Event *eventBottom = new Event(mbr->get_low1(), true, mbr, 1);
            Event *eventTop = new Event(mbr->get_high1(), false, mbr, 1);

            // std::cout << "low part: " << lowPart << " high part: " << highPart << std::endl;
            // assert (lowPart >= 0 && lowPart < numPartitions);
            // assert (highPart >= 0 && highPart < numPartitions);

            for (int j = lowPart; j <= highPart; j++) {
                partitions_local[j].push_back(eventBottom);
                partitions_local[j].push_back(eventTop);
            }
        }

        // merge local_partition
#pragma omp critical
        {
            for (int i = 0; i < partitions_local.size(); i++) {
                std::vector<Event *> cp = partitions_local[i];
                partitions[i].insert(partitions[i].end(), cp.begin(), cp.end());
            }
        }
    }

#else // single-thread partition

    std::cout << "Using single-thread partition" << std::endl;
    // first R
    for(int i = 0; i < R->size(); i++)
    {
      int lowPart, highPart;
      MBR *mbr = R->at(i);
      lowPart = int((mbr->get_low0()-mapLow0)/partition_side_len);
      highPart = int((mbr->get_high0()-mapLow0)/partition_side_len);
      highPart = highPart >= numPartitions ? numPartitions - 1 : highPart;
      Event *eventBottom = new Event(mbr->get_low1(), true, mbr, 0);
      Event *eventTop = new Event(mbr->get_high1(), false, mbr, 0);

      for (int j = lowPart; j <= highPart; j++) {
        partitions[j].push_back(eventBottom);
        partitions[j].push_back(eventTop);
      }
    }

    // repeat for S
    for(int i = 0; i < S->size(); i++)
    {
      int lowPart, highPart;
      MBR *mbr = S->at(i);
      lowPart = int((mbr->get_low0()-mapLow0)/partition_side_len);
      highPart = int((mbr->get_high0()-mapLow0)/partition_side_len);
      highPart = highPart >= numPartitions ? numPartitions - 1 : highPart;
      Event *eventBottom = new Event(mbr->get_low1(), true, mbr, 1);
      Event *eventTop = new Event(mbr->get_high1(), false, mbr, 1);

      // std::cout << "low part: " << lowPart << " high part: " << highPart << std::endl;
      // assert (lowPart >= 0 && lowPart < numPartitions);
      // assert (highPart >= 0 && highPart < numPartitions);

      for (int j = lowPart; j <= highPart; j++) {
        partitions_local[j].push_back(eventBottom);
        partitions_local[j].push_back(eventTop);
      }
    }
#endif

    auto end_partition = std::chrono::high_resolution_clock::now();

    auto start_join = std::chrono::high_resolution_clock::now();

    // now we have our partitions and the events in them, we just join

    // method 1: all thread share the same result buffer
#pragma omp parallel for schedule(guided) num_threads(omp_get_max_threads())
    for (int i = 0; i < partitions.size(); i++) {
        std::vector<std::pair<MBR *, MBR *>> temp;
        std::vector<Event *> cp = partitions[i];
        float low = i * partition_side_len + mapLow0;
        // std::cout << "partition " << i << "size " << cp.size() << std::endl;

        sweep_line_join_dim1(temp, &cp, low);

#pragma omp critical
        results.insert(results.end(), temp.begin(), temp.end());
    }

    auto end_join = std::chrono::high_resolution_clock::now();

    double time_partition =
            (std::chrono::duration_cast<std::chrono::microseconds>(end_partition - start_partition).count()) / 1000.0;
    printf("Partition the datasets (partition dim 0, sweep dim 1): %.2f ms\n", time_partition);

    double time_join = (std::chrono::duration_cast<std::chrono::microseconds>(end_join - start_join).count()) / 1000.0;
    printf("Join the datasets (partition dim 0, sweep dim 1): %.2f ms\n", time_join);

    return (run_result_t) {
            0,
            time_partition,
            time_join
    };
}


run_result_t
stripe_join_partitoin_dim1_sweep_dim0_parallel(std::vector<std::pair<MBR *, MBR *>> &results, std::vector<MBR *> *R,
                                               std::vector<MBR *> *S,
                                               float mapLow1, float mapHigh1, int numPartitions) {

    float partition_side_len = (mapHigh1 - mapLow1) / numPartitions;
    int total_threads = omp_get_max_threads();

    auto start_partition = std::chrono::high_resolution_clock::now();

    // partitioning phase
    // map to store events to process for each partion: partition num -> vector of Events
    // map changed to vector so that can use openMP, worth investigating performance in single too
    std::vector<std::vector<Event *>> partitions;

    // initialize our vectors in map
    for (int i = 0; i < numPartitions; i++) {
        std::vector<Event *> vec;
        partitions.push_back(vec);
    }

    // do a full scan of input data, for each MBR create event and put in the right partition

#if USE_PARALLEL_PARTITION // multi-thread partition

    // std::cout << "Using parallel partition" << std::endl;
    int batch_num = total_threads;
    int batch_size_R = 1 + R->size() / batch_num;
    int batch_size_S = 1 + S->size() / batch_num;
    // std::cout << "Number of threads: " << total_threads << std::endl;

#pragma omp parallel for schedule(static) num_threads(total_threads)
    for (int batch_id = 0; batch_id < batch_num; batch_id++) {
        // std::cout << "batch id: " << batch_id << "thread id: " << omp_get_thread_num() << std::endl;

        int start_R = batch_id * batch_size_R;
        int end_R = (batch_id + 1) * batch_size_R > R->size() ? R->size() : (batch_id + 1) * batch_size_R;

        // local_partition
        std::vector<std::vector<Event *>> partitions_local;
        for (int i = 0; i < numPartitions; i++) {
            std::vector<Event *> vec;
            partitions_local.push_back(vec);
        }

        // first R
        for (int i = start_R; i < end_R; i++) {
            int lowPart, highPart;
            MBR *mbr = R->at(i);
            lowPart = int((mbr->get_low1() - mapLow1) / partition_side_len);
            highPart = int((mbr->get_high1() - mapLow1) / partition_side_len);
            highPart = highPart >= numPartitions ? numPartitions - 1 : highPart;
            Event *eventBottom = new Event(mbr->get_low0(), true, mbr, 0);
            Event *eventTop = new Event(mbr->get_high0(), false, mbr, 0);

            for (int j = lowPart; j <= highPart; j++) {
                partitions_local[j].push_back(eventBottom);
                partitions_local[j].push_back(eventTop);
            }
        }

        int start_S = batch_id * batch_size_S;
        int end_S = (batch_id + 1) * batch_size_S > S->size() ? S->size() : (batch_id + 1) * batch_size_S;
        // std::cout << "start_S: " << start_S << " end_S: " << end_S << std::endl;

        // repeat for S
        for (int j = start_S; j < end_S; j++) {
            int lowPart, highPart;
            MBR *mbr = S->at(j);
            lowPart = int((mbr->get_low1() - mapLow1) / partition_side_len);
            highPart = int((mbr->get_high1() - mapLow1) / partition_side_len);
            highPart = highPart >= numPartitions ? numPartitions - 1 : highPart;
            Event *eventBottom = new Event(mbr->get_low0(), true, mbr, 1);
            Event *eventTop = new Event(mbr->get_high0(), false, mbr, 1);

            // std::cout << "low part: " << lowPart << " high part: " << highPart << std::endl;
            // assert (lowPart >= 0 && lowPart < numPartitions);
            // assert (highPart >= 0 && highPart < numPartitions);

            for (int j = lowPart; j <= highPart; j++) {
                partitions_local[j].push_back(eventBottom);
                partitions_local[j].push_back(eventTop);
            }
        }

        // merge local_partition
#pragma omp critical
        {
            for (int i = 0; i < partitions_local.size(); i++) {
                std::vector<Event *> cp = partitions_local[i];
                partitions[i].insert(partitions[i].end(), cp.begin(), cp.end());
            }
        }
    }

#else // single-thread partition

    std::cout << "Using single-thread partition" << std::endl;
    // first R
    for(int i = 0; i < R->size(); i++)
    {
      int lowPart, highPart;
      MBR *mbr = R->at(i);
      lowPart = int((mbr->get_low1()-mapLow1)/partition_side_len);
      highPart = int((mbr->get_high1()-mapLow1)/partition_side_len);
      highPart = highPart >= numPartitions ? numPartitions - 1 : highPart;
      Event *eventBottom = new Event(mbr->get_low0(), true, mbr, 0);
      Event *eventTop = new Event(mbr->get_high0(), false, mbr, 0);


      // if diff, also push to highPart
      for (int j = lowPart; j <= highPart; j++) {
        partitions[j].push_back(eventBottom);
        partitions[j].push_back(eventTop);
      }
    }

    // repeat for S
    for(int i = 0; i < S->size(); i++)
    {
      int lowPart, highPart;
      MBR *mbr = S->at(i);
      lowPart = int((mbr->get_low1()-mapLow1)/partition_side_len);
      highPart = int((mbr->get_high1()-mapLow1)/partition_side_len);
      highPart = highPart >= numPartitions ? numPartitions - 1 : highPart;
      Event *eventBottom = new Event(mbr->get_low0(), true, mbr, 1);
      Event *eventTop = new Event(mbr->get_high0(), false, mbr, 1);

      // std::cout << "low part: " << lowPart << " high part: " << highPart << std::endl;
      // assert (lowPart >= 0 && lowPart < numPartitions);
      // assert (highPart >= 0 && highPart < numPartitions);

      for (int j = lowPart; j <= highPart; j++) {
        partitions_local[j].push_back(eventBottom);
        partitions_local[j].push_back(eventTop);
      }
    }
#endif

    auto end_partition = std::chrono::high_resolution_clock::now();

    auto start_join = std::chrono::high_resolution_clock::now();

    // now we have our partitions and the events in them, we just join

    // method 1: all thread share the same result buffer
#pragma omp parallel for schedule(guided) num_threads(omp_get_max_threads())
    for (int i = 0; i < partitions.size(); i++) {
        std::vector<std::pair<MBR *, MBR *>> temp;
        std::vector<Event *> cp = partitions[i];
        float low = i * partition_side_len + mapLow1;

        sweep_line_join_dim0(temp, &cp, low);

#pragma omp critical
        results.insert(results.end(), temp.begin(), temp.end());
    }

    auto end_join = std::chrono::high_resolution_clock::now();

    double time_partition =
            (std::chrono::duration_cast<std::chrono::microseconds>(end_partition - start_partition).count()) / 1000.0;
    printf("Partition the datasets (partition dim 1, sweep dim 0): %.2f ms\n", time_partition);

    double time_join = (std::chrono::duration_cast<std::chrono::microseconds>(end_join - start_join).count()) / 1000.0;
    printf("Join the datasets (partition dim 1, sweep dim 0): %.2f ms\n", time_join);

    return (run_result_t) {
            0,
            time_partition,
            time_join
    };
}