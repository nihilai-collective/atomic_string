#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>

#if defined(_WIN32)
	#if !defined(NOMINMAX)
		#define NOMINMAX
	#endif
	#if !defined(WIN32_LEAN_AND_MEAN)
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <Windows.h>
	#include <malloc.h>
#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)
	#include <sys/mman.h>
#endif

#if defined(__AVX__) || defined(__AVX2__) || defined(__AVX512F__)
	#include <immintrin.h>
	#define ATOMIC_STRING_ANY_AVX 1
#else
	#define ATOMIC_STRING_ANY_AVX 0
#endif

namespace atomic_strings {

#if defined(__AVX512F__)
	inline constexpr uint64_t simd_bytes_per_register{ 64 };
#elif ATOMIC_STRING_ANY_AVX
	inline constexpr uint64_t simd_bytes_per_register{ 32 };
#else
	inline constexpr uint64_t simd_bytes_per_register{ 16 };
#endif

	template<uint64_t multiple> constexpr uint64_t round_up_to_multiple(uint64_t value) noexcept {
		return ((value + multiple - 1) / multiple) * multiple;
	}

	enum class allocated_memory_types : uint8_t {
		huge_page,
		mmap,
		standard_page,
		standard,
	};

	template<typename value_type_new> struct aligned_allocator {
		using value_type	  = value_type_new;
		using size_type		  = uint64_t;
		using difference_type = std::ptrdiff_t;
		using pointer		  = value_type*;
		using const_pointer	  = const value_type*;

		static constexpr uint64_t alignment{ (alignof(value_type) > simd_bytes_per_register) ? alignof(value_type) : simd_bytes_per_register };

		struct allocation_header {
			size_type total_bytes{};
			allocated_memory_types allocated_memory_type{};
		};

		static constexpr uint64_t header_size{ round_up_to_multiple<alignment>(sizeof(allocation_header)) };
		static constexpr uint64_t huge_page_size{ 2 * 1024 * 1024ULL };
		static constexpr uint64_t huge_page_threshold{ huge_page_size * 2 };

		template<typename other_type> struct rebind {
			using other = aligned_allocator<other_type>;
		};

		constexpr aligned_allocator() noexcept = default;

		template<typename other_type> constexpr aligned_allocator(const aligned_allocator<other_type>&) noexcept {
		}

		static pointer finalize(void* base, allocated_memory_types allocated_memory_type, size_type total_bytes) noexcept {
			allocation_header* header{ static_cast<allocation_header*>(base) };
			header->total_bytes			  = total_bytes;
			header->allocated_memory_type = allocated_memory_type;
			return std::bit_cast<pointer>(static_cast<std::byte*>(base) + header_size);
		}

		static pointer allocate(size_type count) {
			if (count == 0) [[unlikely]] {
				return nullptr;
			}
			if (count > max_size()) [[unlikely]] {
				throw std::bad_alloc{};
			}
			const size_type bytes{ count * sizeof(value_type) + header_size };
			const size_type aligned_bytes{ round_up_to_multiple<alignment>(bytes) };
			if (aligned_bytes >= huge_page_threshold) {
				[[maybe_unused]] const size_type huge_page_bytes{ round_up_to_multiple<huge_page_size>(aligned_bytes) };
#if defined(_WIN32)
				void* huge{ VirtualAlloc(nullptr, huge_page_bytes, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE) };
				if (huge != nullptr) {
					return finalize(huge, allocated_memory_types::huge_page, huge_page_bytes);
				}
				huge = VirtualAlloc(nullptr, huge_page_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
				if (huge != nullptr) {
					return finalize(huge, allocated_memory_types::standard_page, huge_page_bytes);
				}
#elif defined(__linux__) && !defined(__ANDROID__)
				void* huge{ mmap(nullptr, huge_page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0) };
				if (huge != MAP_FAILED) {
					return finalize(huge, allocated_memory_types::huge_page, huge_page_bytes);
				}
				huge = mmap(nullptr, huge_page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (huge != MAP_FAILED) {
					madvise(huge, huge_page_bytes, MADV_HUGEPAGE);
					return finalize(huge, allocated_memory_types::mmap, huge_page_bytes);
				}
#elif defined(__APPLE__) || defined(__ANDROID__)
				void* huge{ mmap(nullptr, huge_page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) };
				if (huge != MAP_FAILED) {
					return finalize(huge, allocated_memory_types::mmap, huge_page_bytes);
				}
#endif
			}
#if (defined(_WIN32) || defined(__linux__)) && ATOMIC_STRING_ANY_AVX
			void* base{ _mm_malloc(aligned_bytes, alignment) };
#elif defined(_WIN32)
			void* base{ _aligned_malloc(aligned_bytes, alignment) };
#else
			void* base{ std::aligned_alloc(alignment, aligned_bytes) };
#endif
			if (base == nullptr) [[unlikely]] {
				throw std::bad_alloc{};
			}
			return finalize(base, allocated_memory_types::standard, aligned_bytes);
		}

		static constexpr size_type max_size() noexcept {
			return (static_cast<size_type>(-1) - header_size - (alignment - 1)) / sizeof(value_type);
		}

		static void deallocate(pointer value, size_type) noexcept {
			if (value == nullptr) [[unlikely]] {
				return;
			}
			void* base{ std::bit_cast<std::byte*>(value) - header_size };
			[[maybe_unused]] const allocation_header* header{ static_cast<allocation_header*>(base) };
			switch (header->allocated_memory_type) {
				case allocated_memory_types::huge_page:
				case allocated_memory_types::standard_page: {
#if defined(_WIN32)
					VirtualFree(base, 0, MEM_RELEASE);
#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)
					munmap(base, header->total_bytes);
#endif
					break;
				}
				case allocated_memory_types::mmap: {
#if defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)
					munmap(base, header->total_bytes);
#endif
					break;
				}
				case allocated_memory_types::standard: {
#if (defined(_WIN32) || defined(__linux__)) && ATOMIC_STRING_ANY_AVX
					_mm_free(base);
#elif defined(_WIN32)
					_aligned_free(base);
#else
					std::free(base);
#endif
					break;
				}
			}
		}

		template<typename other_type> constexpr bool operator==(const aligned_allocator<other_type>&) const noexcept {
			return true;
		}

		static_assert(alignment >= alignof(allocation_header), "alignment must cover allocation_header's alignment requirement.");
	};

	struct aligned_delete {
		template<typename value_type> void operator()(value_type* value) const noexcept {
			value->~value_type();
			aligned_allocator<value_type>::deallocate(value, 1);
		}
	};

}
