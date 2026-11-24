# atomic_string

### The `std::string` you never asked for, now with 1000x less throughput.

`atomic_string` is a header-only C++23 string with the same API as `std::string`, built on one simple principle: **every character deserves its own heap allocation, its own reference count, its own atomic, and its own mutex acquisition.** Two of each, where possible.

```cpp
using char_cell = std::shared_ptr<std::unique_ptr<std::atomic<char>>>;
```

That is one character. Yes, really.

---

## Features

- **Same API as `std::string`**: constructors, `assign`, `at`, `operator[]`, `front`, `back`, `data`, `c_str`, iterators and reverse iterators, `reserve`, `shrink_to_fit`, `insert`, `erase`, `push_back`, `pop_back`, `append`, `operator+=`, `replace`, `resize`, `swap`, `copy`, `find`, `rfind`, all four `find_*_of` / `find_*_not_of` families, `compare`, `starts_with`, `ends_with`, `contains`, `substr`, `operator+`, `==`, `<=>`, `<<`, `>>`, `getline`, and `std::hash`.
- **Thread-safe in the least useful way possible**: every member function takes a `std::mutex`, guarding atomics, which guard characters nobody else can reach. Belt, suspenders, and a second pair of pants.
- **Per-character heap allocation**: two `malloc` calls per character (the `unique_ptr` target and the `shared_ptr` control block), plus a 16-byte handle in the vector. A 1 KB string performs 2048 allocations to exist.
- **Small String Optimization**: we have heard of it.
- **Proxy references**: non-const `operator[]` returns an `atomic_char_reference`, which acquires the lock again on every read *and* every write.
- **Contiguous storage, technically**: `data()` and `c_str()` lovingly rebuild a full shadow copy of the string on every call. Writes through `data()` go into the copy and are discarded with honor.
- **Deadlock-free self-operations**: `s += s`, `s.find(s)` and `s == s` all work, because the other string is copied out before the lock is taken. `swap` and move assignment use `std::scoped_lock` on both mutexes.
- **Pluggable allocation policies**: as of now, you can allocate slowly *with SIMD-aligned, header-prefixed, huge-page-aware precision*.

## Allocation Policies

`basic_atomic_string<cell_policy>` is templated over how it allocates its suffering:

| Alias | Policy | Each character costs |
|---|---|---|
| `std_alloc_atomic_string` | `std_cell_policy` | `make_shared` + `make_unique`, the classic |
| `atomic_string` | `jsonifier_cell_policy` | the atomic, its control block, *and* the cell vector all go through a port of Jsonifier's `aligned_allocator` |

The Jsonifier port ([aligned_allocator.hpp](atomic_string/aligned_allocator.hpp)) prefixes every allocation with a 32-byte header, aligns it to the SIMD register width via `_mm_malloc`, and escalates to `MEM_LARGE_PAGES` / `MAP_HUGETLB` huge pages for anything at or above 4 MB. For a 1-byte `std::atomic<char>`, that comes to roughly **64 bytes per character before the control block is counted.** It is a superb allocator for parse buffers. We are not using it for parse buffers.

## Benchmarks

Measured with **[benchmarksuite](https://github.com/nihilai-collective/benchmarksuite)** on a 1024-character random lowercase string, MSVC 19.44, Release, `/arch:AVX2`, Windows 11. Throughput in MB/s, higher is better (for some of us).

| Test | std::string | atomic_string (std alloc) | atomic_string (jsonifier alloc) | Slowdown |
|---|---:|---:|---:|---:|
| construct-1024 | 15,470 | 15.7 | 15.4 | **~1000x** |
| copy-construct | 15,625 | 16.3 | 15.7 | **~1000x** |
| append-64-byte-chunks | 13,767 | 24.5 | 24.4 | **~560x** |
| push-back-1024 | 1,978 | 23.5 | 21.6 | ~90x |
| index-sum | 2,331 | 44.7 | 45.7 | ~51x |
| compare-equal | 21,701 | 633 | 576 | ~36x |
| insert-middle | 30,340 | 1,063 | 1,003 | ~30x |
| find-needle-at-end | 9,361 | 1,281 | 1,031 | ~8x |

### What the numbers are telling us

- **Allocation dominates everything.** Construct and copy sit near 200 cycles per byte. That is the sound of 2048 `malloc` calls per kilobyte.
- **The lock is cheap next to `malloc`.** `find` locks once per call and loses by only 8x. `index-sum` goes through `operator[]`, locks once per character, and loses by 51x.
- **`insert-middle` gets off lightly** because `std::vector` only shifts 16-byte `shared_ptr` handles, and only the 16 inserted characters are new allocations.
- **The Jsonifier allocator made it slower.** Every tiny cell now spans more cache lines, so the pointer-chasing tests (`find`, `compare`) lose up to 20%, while the `malloc`-bound tests tie. We have achieved slow allocation at peak allocation speed.
- **Several rows did not converge**, mostly on the `std::string` side, where a run takes under 100 ns and noise dominates. Treat the ratios as single-machine, single-compiler, order-of-magnitude truths.

## Correctness

Before a single cycle is measured, `main.cpp` checks both string types against `std::string` on `find`, `rfind`, `find_first_of`, `find_last_not_of`, `substr`, `operator+`, `starts_with`, `ends_with`, `replace`, `c_str` and proxy writes through `operator[]`. If anything differs, it prints:

> atomic_string diverged from std::string, refusing to benchmark a liar.

and exits with status 1. We are slow, not dishonest.

## Usage

```cpp
#include "atomic_string.hpp"

int main() {
	atomic_strings::atomic_string greeting{ "hello" };
	greeting += ", world";
	greeting[0] = 'H';
	std::cout << greeting << '\n';
	return greeting.contains("world") ? 0 : 1;
}
```

Every line above takes a mutex at least once. The `greeting[0] = 'H'` line takes it once in the proxy and once more for the store, because why not.

## Building

Requires CMake 3.28+ and a C++23 compiler. benchmarksuite comes in through `FetchContent`, or from a sibling `../benchmarksuite` checkout when one exists.

```bash
cmake -S . -B out/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build out/build/release
```

```bash
./out/build/release/atomic_string/atomic_string
```

On Windows, run these from a Visual Studio x64 developer prompt.

## Layout

| File | Purpose |
|---|---|
| [atomic_string/atomic_string.hpp](atomic_string/atomic_string.hpp) | `basic_atomic_string`, its proxy reference, iterators, cell policies and `std::hash` |
| [atomic_string/aligned_allocator.hpp](atomic_string/aligned_allocator.hpp) | The Jsonifier `aligned_allocator` port and `aligned_delete` |
| [atomic_string/main.cpp](atomic_string/main.cpp) | Parity check and the three-way benchmarksuite stage |

## Known Limitations

- `data()` returns a pointer into a shadow buffer, so writes through it vanish and the pointer is invalidated by the next `data()` call from any thread.
- Iterators lock per dereference but not across operations, so another thread can reallocate underneath you. The mutex protects every function and none of the invariants.
- Iterators are proxy iterators, so algorithms that need a real `char&` will not accept them.
- Memory use is roughly 60 to 150 times that of `std::string` by our estimate, depending on policy and the platform allocator's overhead.

## FAQ

**Should I use this in production?**
Only if production is a museum.

**Is it thread-safe?**
Each individual call is. Your program is not.

**Why?**
Because someone had to measure exactly how much a character can cost, and now we know: about 64 bytes and 200 cycles.
