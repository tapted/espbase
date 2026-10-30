#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "perf_workloads.hpp"

using namespace sjson::perf;

static void print_usage(const char* prog) {
  std::cout << "Usage: " << prog << " [options]\n\n"
            << "Options:\n"
            << "  --mb <N>           Target corpus RAM size in megabytes (default: 50)\n"
            << "  --iterations <N>   Number of runs per workload to average (default: 3)\n"
            << "  --filter <name>    Run only matching workload (primitives, floats, strings, nested_paths, mixed_iot)\n"
            << "  --json             Output results as JSON\n"
            << "  --help, -h         Show this help message\n";
}

int main(int argc, char* argv[]) {
  std::size_t target_mb = 50;
  int iterations = 3;
  std::string filter;
  bool output_json = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--mb" && i + 1 < argc) {
      target_mb = std::strtoul(argv[++i], nullptr, 10);
      if (target_mb == 0) target_mb = 50;
    } else if (arg == "--iterations" && i + 1 < argc) {
      iterations = std::atoi(argv[++i]);
      if (iterations < 1) iterations = 1;
    } else if (arg == "--filter" && i + 1 < argc) {
      filter = argv[++i];
    } else if (arg == "--json") {
      output_json = true;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_usage(argv[0]);
      return 1;
    }
  }

  std::size_t target_bytes = target_mb * 1024 * 1024;

  std::vector<BenchmarkResult> results;

  auto execute_workload = [&](auto tag, const char* name) {
    using WorkloadT = decltype(tag);
    if (!filter.empty() && filter != name) return;

    if (!output_json) {
      std::cout << "Running [" << name << "] (" << target_mb << " MB, " << iterations << " iterations)..." << std::flush;
    }

    // Warm-up run (not included in metrics)
    run_benchmark<WorkloadT>(target_bytes / 5);

    BenchmarkResult best;
    best.name = name;
    best.description = WorkloadT::description;

    double sum_build_ms = 0.0;
    double sum_parse_ms = 0.0;
    bool all_verified = true;

    for (int it = 0; it < iterations; ++it) {
      auto run = run_benchmark<WorkloadT>(target_bytes);
      sum_build_ms += run.build_time_ms;
      sum_parse_ms += run.parse_time_ms;
      if (!run.verified) all_verified = false;

      best.target_bytes = run.target_bytes;
      best.actual_bytes = run.actual_bytes;
      best.num_objects = run.num_objects;
    }

    best.build_time_ms = sum_build_ms / iterations;
    best.parse_time_ms = sum_parse_ms / iterations;
    best.build_mbs = (static_cast<double>(best.actual_bytes) / (1024.0 * 1024.0)) / (best.build_time_ms / 1000.0);
    best.parse_mbs = (static_cast<double>(best.actual_bytes) / (1024.0 * 1024.0)) / (best.parse_time_ms / 1000.0);
    best.build_ops_sec = static_cast<double>(best.num_objects) / (best.build_time_ms / 1000.0);
    best.parse_ops_sec = static_cast<double>(best.num_objects) / (best.parse_time_ms / 1000.0);
    best.verified = all_verified;

    results.push_back(best);

    if (!output_json) {
      std::cout << " done.\n";
    }
  };

  execute_workload(PrimitivesWorkload{}, PrimitivesWorkload::name);
  execute_workload(FloatsWorkload{}, FloatsWorkload::name);
  execute_workload(StringsWorkload{}, StringsWorkload::name);
  execute_workload(NestedPathsWorkload{}, NestedPathsWorkload::name);
  execute_workload(MixedIotWorkload{}, MixedIotWorkload::name);

  if (output_json) {
    std::cout << "{\n  \"target_mb\": " << target_mb << ",\n  \"iterations\": " << iterations << ",\n  \"results\": [\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
      const auto& r = results[i];
      std::cout << "    {\n"
                << "      \"name\": \"" << r.name << "\",\n"
                << "      \"actual_bytes\": " << r.actual_bytes << ",\n"
                << "      \"num_objects\": " << r.num_objects << ",\n"
                << "      \"build_time_ms\": " << r.build_time_ms << ",\n"
                << "      \"parse_time_ms\": " << r.parse_time_ms << ",\n"
                << "      \"build_mbs\": " << r.build_mbs << ",\n"
                << "      \"parse_mbs\": " << r.parse_mbs << ",\n"
                << "      \"build_ops_sec\": " << r.build_ops_sec << ",\n"
                << "      \"parse_ops_sec\": " << r.parse_ops_sec << ",\n"
                << "      \"verified\": " << (r.verified ? "true" : "false") << "\n"
                << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
    }
    std::cout << "  ]\n}\n";
    return 0;
  }

  // Formatted ASCII Table Output
  std::cout << "\n"
            << "========================================================================================================================\n"
            << " stack_json In-Memory Performance Benchmark (Target: " << target_mb << " MB / workload, " << iterations << " iters)\n"
            << "========================================================================================================================\n";
  std::cout << std::left << std::setw(15) << "Workload"
            << std::right << std::setw(12) << "Size (MB)"
            << std::setw(12) << "Objects"
            << std::setw(13) << "Build Time"
            << std::setw(14) << "Build (MB/s)"
            << std::setw(13) << "Parse Time"
            << std::setw(14) << "Parse (MB/s)"
            << std::setw(14) << "Parse Ops/s"
            << std::setw(10) << "Status"
            << "\n";
  std::cout << "------------------------------------------------------------------------------------------------------------------------\n";

  double total_bytes = 0.0;
  std::size_t total_objects = 0;
  double total_build_ms = 0.0;
  double total_parse_ms = 0.0;
  bool all_pass = true;

  for (const auto& r : results) {
    double mb = static_cast<double>(r.actual_bytes) / (1024.0 * 1024.0);
    total_bytes += r.actual_bytes;
    total_objects += r.num_objects;
    total_build_ms += r.build_time_ms;
    total_parse_ms += r.parse_time_ms;
    if (!r.verified) all_pass = false;

    std::cout << std::left << std::setw(15) << r.name
              << std::right << std::fixed << std::setprecision(1) << std::setw(10) << mb << " MB"
              << std::setw(12) << r.num_objects
              << std::fixed << std::setprecision(1) << std::setw(10) << r.build_time_ms << " ms"
              << std::fixed << std::setprecision(1) << std::setw(12) << r.build_mbs << " MB/s"
              << std::fixed << std::setprecision(1) << std::setw(10) << r.parse_time_ms << " ms"
              << std::fixed << std::setprecision(1) << std::setw(12) << r.parse_mbs << " MB/s"
              << std::fixed << std::setprecision(0) << std::setw(12) << r.parse_ops_sec << " /s"
              << std::setw(10) << (r.verified ? "PASS" : "FAIL")
              << "\n";
  }

  std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
  double total_mb = total_bytes / (1024.0 * 1024.0);
  double overall_build_mbs = total_mb / (total_build_ms / 1000.0);
  double overall_parse_mbs = total_mb / (total_parse_ms / 1000.0);
  double overall_parse_ops = total_objects / (total_parse_ms / 1000.0);

  std::cout << std::left << std::setw(15) << "TOTAL / AVG"
            << std::right << std::fixed << std::setprecision(1) << std::setw(10) << total_mb << " MB"
            << std::setw(12) << total_objects
            << std::fixed << std::setprecision(1) << std::setw(10) << total_build_ms << " ms"
            << std::fixed << std::setprecision(1) << std::setw(12) << overall_build_mbs << " MB/s"
            << std::fixed << std::setprecision(1) << std::setw(10) << total_parse_ms << " ms"
            << std::fixed << std::setprecision(1) << std::setw(12) << overall_parse_mbs << " MB/s"
            << std::fixed << std::setprecision(0) << std::setw(12) << overall_parse_ops << " /s"
            << std::setw(10) << (all_pass ? "PASS" : "FAIL")
            << "\n";
  std::cout << "========================================================================================================================\n\n";

  return all_pass ? 0 : 1;
}

