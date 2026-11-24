#include "atomic_string.hpp"

#include <benchmarksuite>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

using atomic_strings::std_alloc_atomic_string;
using atomic_strings::atomic_string;

static constexpr uint64_t text_length{ 1024 };
static constexpr uint64_t chunk_length{ 64 };
static constexpr uint64_t needle_length{ 16 };

static constexpr benchmarksuite::stage_config_data stage_config{
	.measured_iteration_count			= 100,
	.max_iteration_count				= 10000,
	.convergence_threshold				= 1.0,
	.max_time_in_s						= 5,
	.rse_threshold						= 2.5,
	.max_k								= 100000,
	.min_k								= 30,
	.clear_cpu_caches_before_iterations = true,
	.include_non_converged_results		= true,
	.benchmark_type						= benchmarksuite::benchmark_types::cpu,
};

using stage = benchmarksuite::benchmark_stage<"atomic_string-vs-std::string", stage_config>;

template<typename string_type> struct bench_inputs {
	string_type scratch{};
	std::string source{};
	std::string needle{};
	string_type value{};
	string_type twin{};

	bench_inputs(const std::string& source_new, const std::string& needle_new)
		: source{ source_new }, needle{ needle_new }, value{ source_new.data(), source_new.size() }, twin{ source_new.data(), source_new.size() } {
	}
};

template<typename string_type> struct construct_op {
	static uint64_t impl(bench_inputs<string_type>& inputs) {
		string_type constructed{ inputs.source.data(), inputs.source.size() };
		benchmarksuite::do_not_optimize_away(constructed);
		return inputs.source.size();
	}
};

template<typename string_type> struct push_back_op {
	static void before(bench_inputs<string_type>& inputs) {
		inputs.scratch.clear();
	}

	static uint64_t impl(bench_inputs<string_type>& inputs) {
		for (char value: inputs.source) {
			inputs.scratch.push_back(value);
		}
		benchmarksuite::do_not_optimize_away(inputs.scratch);
		return inputs.source.size();
	}
};

template<typename string_type> struct append_op {
	static void before(bench_inputs<string_type>& inputs) {
		inputs.scratch.clear();
	}

	static uint64_t impl(bench_inputs<string_type>& inputs) {
		const std::string_view source{ inputs.source };
		for (uint64_t offset = 0; offset < source.size(); offset += chunk_length) {
			inputs.scratch.append(source.substr(offset, chunk_length));
		}
		benchmarksuite::do_not_optimize_away(inputs.scratch);
		return source.size();
	}
};

template<typename string_type> struct copy_op {
	static uint64_t impl(bench_inputs<string_type>& inputs) {
		string_type copied{ inputs.value };
		benchmarksuite::do_not_optimize_away(copied);
		return inputs.source.size();
	}
};

template<typename string_type> struct find_op {
	static uint64_t impl(bench_inputs<string_type>& inputs) {
		benchmarksuite::do_not_optimize_away(inputs.value.find(std::string_view{ inputs.needle }));
		return inputs.source.size();
	}
};

template<typename string_type> struct index_sum_op {
	static uint64_t impl(bench_inputs<string_type>& inputs) {
		const string_type& value{ inputs.value };
		uint64_t sum{};
		for (uint64_t x = 0; x < value.size(); ++x) {
			sum += static_cast<unsigned char>(value[x]);
		}
		benchmarksuite::do_not_optimize_away(sum);
		return inputs.source.size();
	}
};

template<typename string_type> struct compare_op {
	static uint64_t impl(bench_inputs<string_type>& inputs) {
		benchmarksuite::do_not_optimize_away(inputs.value == inputs.twin);
		return inputs.source.size();
	}
};

template<typename string_type> struct insert_middle_op {
	static void before(bench_inputs<string_type>& inputs) {
		inputs.scratch = inputs.value;
	}

	static uint64_t impl(bench_inputs<string_type>& inputs) {
		inputs.scratch.insert(inputs.scratch.size() / 2, std::string_view{ inputs.needle });
		benchmarksuite::do_not_optimize_away(inputs.scratch);
		return inputs.source.size();
	}
};

template<benchmarksuite::string_literal test_name, template<typename> typename op_type> struct run_test {
	static void impl(bench_inputs<std::string>& std_inputs, bench_inputs<std_alloc_atomic_string>& std_alloc_inputs, bench_inputs<atomic_string>& atomic_inputs) {
		std::cout << "Starting benchmark: " << test_name.operator std::string_view() << std::endl;
		stage::run_benchmark<test_name, "std::string", op_type<std::string>>(std_inputs);
		stage::run_benchmark<test_name, "atomic_string (std alloc)", op_type<std_alloc_atomic_string>>(std_alloc_inputs);
		stage::run_benchmark<test_name, "atomic_string (jsonifier alloc)", op_type<atomic_string>>(atomic_inputs);
		stage::get_test_results(test_name.operator std::string_view()).print(false);
	}
};

template<typename string_type> struct verify_parity {
	static bool impl(const std::string& source, const std::string& needle) {
		string_type value{ source.data(), source.size() };
		bool passed{ value.snapshot() == source };
		passed &= value.find(std::string_view{ needle }) == source.find(needle);
		passed &= value.rfind('a') == source.rfind('a');
		passed &= value.find_first_of("xyz") == source.find_first_of("xyz");
		passed &= value.find_last_not_of("abc") == source.find_last_not_of("abc");
		passed &= value.substr(10, 20).snapshot() == source.substr(10, 20);
		passed &= (value + "tail").snapshot() == source + "tail";
		passed &= value.ends_with(std::string_view{ needle }) && value.starts_with(source.substr(0, 4).c_str());
		value.replace(5, 10, "swapped");
		std::string expected{ source };
		expected.replace(5, 10, "swapped");
		passed &= value.snapshot() == expected && std::string_view{ value.c_str() } == expected;
		value[0] = '!';
		expected[0] = '!';
		passed &= value == std::string_view{ expected };
		return passed;
	}
};

static std::string generate_source() {
	std::mt19937_64 engine{ 0x5eed };
	std::uniform_int_distribution<int32_t> distribution{ 'a', 'z' };
	std::string source(text_length, char{});
	for (char& value: source) {
		value = static_cast<char>(distribution(engine));
	}
	return source;
}

int32_t main() {
	benchmarksuite::pin_for_benchmark();

	const std::string source{ generate_source() };
	const std::string needle{ source.substr(source.size() - needle_length) };

	if (!verify_parity<std_alloc_atomic_string>::impl(source, needle) || !verify_parity<atomic_string>::impl(source, needle)) {
		std::cerr << "atomic_string diverged from std::string, refusing to benchmark a liar." << std::endl;
		return 1;
	}

	bench_inputs<std::string> std_inputs{ source, needle };
	bench_inputs<std_alloc_atomic_string> std_alloc_inputs{ source, needle };
	bench_inputs<atomic_string> atomic_inputs{ source, needle };

	run_test<"construct-1024", construct_op>::impl(std_inputs, std_alloc_inputs, atomic_inputs);
	run_test<"push-back-1024", push_back_op>::impl(std_inputs, std_alloc_inputs, atomic_inputs);
	run_test<"append-64-byte-chunks", append_op>::impl(std_inputs, std_alloc_inputs, atomic_inputs);
	run_test<"copy-construct", copy_op>::impl(std_inputs, std_alloc_inputs, atomic_inputs);
	run_test<"find-needle-at-end", find_op>::impl(std_inputs, std_alloc_inputs, atomic_inputs);
	run_test<"index-sum", index_sum_op>::impl(std_inputs, std_alloc_inputs, atomic_inputs);
	run_test<"compare-equal", compare_op>::impl(std_inputs, std_alloc_inputs, atomic_inputs);
	run_test<"insert-middle", insert_middle_op>::impl(std_inputs, std_alloc_inputs, atomic_inputs);
	return 0;
}
