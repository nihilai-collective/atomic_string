#pragma once

#include "aligned_allocator.hpp"

#include <algorithm>
#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <istream>
#include <iterator>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace atomic_strings {

	struct std_cell_policy {
		template<typename value_type> using vector_allocator = std::allocator<value_type>;
		using cell_type										 = std::shared_ptr<std::unique_ptr<std::atomic<char>>>;

		static cell_type make_cell(char value) {
			return std::make_shared<std::unique_ptr<std::atomic<char>>>(std::make_unique<std::atomic<char>>(value));
		}
	};

	struct jsonifier_cell_policy {
		template<typename value_type> using vector_allocator = aligned_allocator<value_type>;
		using atomic_cell									 = std::unique_ptr<std::atomic<char>, aligned_delete>;
		using cell_type										 = std::shared_ptr<atomic_cell>;

		static cell_type make_cell(char value) {
			atomic_cell owned{ new (aligned_allocator<std::atomic<char>>::allocate(1)) std::atomic<char>{ value } };
			return std::allocate_shared<atomic_cell>(aligned_allocator<atomic_cell>{}, std::move(owned));
		}
	};

	template<typename cell_type> char load_cell(const cell_type& cell) {
		return (*cell)->load(std::memory_order_seq_cst);
	}

	template<typename cell_type> void store_cell(const cell_type& cell, char value) {
		(*cell)->store(value, std::memory_order_seq_cst);
	}

	inline int compare_chars(char lhs, char rhs) {
		return static_cast<int>(static_cast<unsigned char>(lhs)) - static_cast<int>(static_cast<unsigned char>(rhs));
	}

	template<typename owner_type> class atomic_char_reference {
	  public:
		atomic_char_reference(owner_type& owner_new, std::size_t index_new) noexcept : owner{ &owner_new }, index{ index_new } {
		}

		atomic_char_reference(const atomic_char_reference&) = default;

		operator char() const {
			return static_cast<const owner_type&>(*owner)[index];
		}

		atomic_char_reference& operator=(char value) {
			owner->store_at(index, value);
			return *this;
		}

		atomic_char_reference& operator=(const atomic_char_reference& other) {
			return *this = static_cast<char>(other);
		}

	  private:
		owner_type* owner;
		std::size_t index;
	};

	template<typename owner_type, typename reference_type> class basic_atomic_iterator {
	  public:
		using iterator_category = std::random_access_iterator_tag;
		using difference_type	= std::ptrdiff_t;
		using reference			= reference_type;
		using value_type		= char;
		using pointer			= void;

		owner_type* owner{};
		std::size_t index{};

		basic_atomic_iterator() noexcept = default;

		basic_atomic_iterator(owner_type* owner_new, std::size_t index_new) noexcept : owner{ owner_new }, index{ index_new } {
		}

		operator basic_atomic_iterator<const owner_type, char>() const noexcept
			requires(!std::is_const_v<owner_type>)
		{
			return { owner, index };
		}

		reference operator*() const {
			return (*owner)[index];
		}

		reference operator[](difference_type offset) const {
			return (*owner)[static_cast<std::size_t>(static_cast<difference_type>(index) + offset)];
		}

		basic_atomic_iterator& operator++() noexcept {
			++index;
			return *this;
		}

		basic_atomic_iterator operator++(int) noexcept {
			basic_atomic_iterator previous{ *this };
			++index;
			return previous;
		}

		basic_atomic_iterator& operator--() noexcept {
			--index;
			return *this;
		}

		basic_atomic_iterator operator--(int) noexcept {
			basic_atomic_iterator previous{ *this };
			--index;
			return previous;
		}

		basic_atomic_iterator& operator+=(difference_type offset) noexcept {
			index = static_cast<std::size_t>(static_cast<difference_type>(index) + offset);
			return *this;
		}

		basic_atomic_iterator& operator-=(difference_type offset) noexcept {
			return *this += -offset;
		}

		friend basic_atomic_iterator operator+(basic_atomic_iterator iterator, difference_type offset) noexcept {
			return iterator += offset;
		}

		friend basic_atomic_iterator operator+(difference_type offset, basic_atomic_iterator iterator) noexcept {
			return iterator += offset;
		}

		friend basic_atomic_iterator operator-(basic_atomic_iterator iterator, difference_type offset) noexcept {
			return iterator -= offset;
		}

		friend difference_type operator-(const basic_atomic_iterator& lhs, const basic_atomic_iterator& rhs) noexcept {
			return static_cast<difference_type>(lhs.index) - static_cast<difference_type>(rhs.index);
		}

		friend bool operator==(const basic_atomic_iterator& lhs, const basic_atomic_iterator& rhs) noexcept {
			return lhs.index == rhs.index;
		}

		friend auto operator<=>(const basic_atomic_iterator& lhs, const basic_atomic_iterator& rhs) noexcept {
			return lhs.index <=> rhs.index;
		}
	};

	template<typename cell_policy> class basic_atomic_string {
	  public:
		using const_reverse_iterator = std::reverse_iterator<basic_atomic_iterator<const basic_atomic_string, char>>;
		using reverse_iterator		 = std::reverse_iterator<basic_atomic_iterator<basic_atomic_string, atomic_char_reference<basic_atomic_string>>>;
		using const_iterator		 = basic_atomic_iterator<const basic_atomic_string, char>;
		using iterator				 = basic_atomic_iterator<basic_atomic_string, atomic_char_reference<basic_atomic_string>>;
		using traits_type			 = std::char_traits<char>;
		using difference_type		 = std::ptrdiff_t;
		using const_reference		 = char;
		using reference				 = atomic_char_reference<basic_atomic_string>;
		using size_type				 = std::size_t;
		using value_type			 = char;

		static constexpr size_type npos{ static_cast<size_type>(-1) };

		basic_atomic_string() = default;

		basic_atomic_string(size_type count, char value) {
			assign(count, value);
		}

		basic_atomic_string(const char* text) {
			assign(std::string_view{ text });
		}

		basic_atomic_string(const char* text, size_type count) {
			assign(std::string_view{ text, count });
		}

		explicit basic_atomic_string(std::string_view text) {
			assign(text);
		}

		explicit basic_atomic_string(const std::string& text) {
			assign(std::string_view{ text });
		}

		template<std::input_iterator iterator_type> basic_atomic_string(iterator_type first, iterator_type last) {
			for (; first != last; ++first) {
				cells.emplace_back(cell_policy::make_cell(static_cast<char>(*first)));
			}
		}

		basic_atomic_string(std::initializer_list<char> values) {
			assign(std::string_view{ values.begin(), values.size() });
		}

		basic_atomic_string(const basic_atomic_string& other) {
			assign(other.snapshot());
		}

		basic_atomic_string(const basic_atomic_string& other, size_type position, size_type count = npos) {
			assign(other.substr(position, count).snapshot());
		}

		basic_atomic_string(basic_atomic_string&& other) noexcept {
			std::scoped_lock lock{ other.mutex };
			cells = std::move(other.cells);
		}

		basic_atomic_string& operator=(const basic_atomic_string& other) {
			if (this != &other) {
				assign(other.snapshot());
			}
			return *this;
		}

		basic_atomic_string& operator=(basic_atomic_string&& other) noexcept {
			if (this != &other) {
				std::scoped_lock lock{ mutex, other.mutex };
				cells = std::move(other.cells);
			}
			return *this;
		}

		basic_atomic_string& operator=(const char* text) {
			return assign(std::string_view{ text });
		}

		basic_atomic_string& operator=(std::string_view text) {
			return assign(text);
		}

		basic_atomic_string& operator=(char value) {
			return assign(1, value);
		}

		basic_atomic_string& operator=(std::initializer_list<char> values) {
			return assign(std::string_view{ values.begin(), values.size() });
		}

		basic_atomic_string& assign(size_type count, char value) {
			std::scoped_lock lock{ mutex };
			cells.clear();
			for (size_type x = 0; x < count; ++x) {
				cells.emplace_back(cell_policy::make_cell(value));
			}
			return *this;
		}

		basic_atomic_string& assign(std::string_view text) {
			std::scoped_lock lock{ mutex };
			cells.clear();
			for (char value: text) {
				cells.emplace_back(cell_policy::make_cell(value));
			}
			return *this;
		}

		basic_atomic_string& assign(const char* text) {
			return assign(std::string_view{ text });
		}

		basic_atomic_string& assign(const char* text, size_type count) {
			return assign(std::string_view{ text, count });
		}

		basic_atomic_string& assign(const basic_atomic_string& other) {
			return *this = other;
		}

		basic_atomic_string& assign(basic_atomic_string&& other) noexcept {
			return *this = std::move(other);
		}

		basic_atomic_string& assign(std::initializer_list<char> values) {
			return assign(std::string_view{ values.begin(), values.size() });
		}

		template<std::input_iterator iterator_type> basic_atomic_string& assign(iterator_type first, iterator_type last) {
			return *this = basic_atomic_string{ first, last };
		}

		char at(size_type index) const {
			std::scoped_lock lock{ mutex };
			if (index >= cells.size()) {
				throw std::out_of_range{ "basic_atomic_string::at" };
			}
			return load_cell(cells[index]);
		}

		reference at(size_type index) {
			std::scoped_lock lock{ mutex };
			if (index >= cells.size()) {
				throw std::out_of_range{ "basic_atomic_string::at" };
			}
			return reference{ *this, index };
		}

		char operator[](size_type index) const {
			std::scoped_lock lock{ mutex };
			return load_cell(cells[index]);
		}

		reference operator[](size_type index) {
			return reference{ *this, index };
		}

		char front() const {
			return (*this)[0];
		}

		reference front() {
			return (*this)[0];
		}

		char back() const {
			std::scoped_lock lock{ mutex };
			return load_cell(cells.back());
		}

		reference back() {
			return (*this)[size() - 1];
		}

		const char* data() const {
			std::scoped_lock lock{ mutex };
			shadow = snapshot_unlocked();
			return shadow.data();
		}

		char* data() {
			std::scoped_lock lock{ mutex };
			shadow = snapshot_unlocked();
			return shadow.data();
		}

		const char* c_str() const {
			return data();
		}

		operator std::string_view() const {
			return std::string_view{ data(), size() };
		}

		iterator begin() noexcept {
			return { this, 0 };
		}

		const_iterator begin() const noexcept {
			return { this, 0 };
		}

		const_iterator cbegin() const noexcept {
			return begin();
		}

		iterator end() noexcept {
			return { this, size() };
		}

		const_iterator end() const noexcept {
			return { this, size() };
		}

		const_iterator cend() const noexcept {
			return end();
		}

		reverse_iterator rbegin() noexcept {
			return reverse_iterator{ end() };
		}

		const_reverse_iterator rbegin() const noexcept {
			return const_reverse_iterator{ end() };
		}

		const_reverse_iterator crbegin() const noexcept {
			return rbegin();
		}

		reverse_iterator rend() noexcept {
			return reverse_iterator{ begin() };
		}

		const_reverse_iterator rend() const noexcept {
			return const_reverse_iterator{ begin() };
		}

		const_reverse_iterator crend() const noexcept {
			return rend();
		}

		bool empty() const noexcept {
			return size() == 0;
		}

		size_type size() const noexcept {
			std::scoped_lock lock{ mutex };
			return cells.size();
		}

		size_type length() const noexcept {
			return size();
		}

		size_type max_size() const noexcept {
			std::scoped_lock lock{ mutex };
			return cells.max_size();
		}

		void reserve(size_type new_capacity) {
			std::scoped_lock lock{ mutex };
			cells.reserve(new_capacity);
		}

		size_type capacity() const noexcept {
			std::scoped_lock lock{ mutex };
			return cells.capacity();
		}

		void shrink_to_fit() {
			std::scoped_lock lock{ mutex };
			cells.shrink_to_fit();
			shadow.clear();
			shadow.shrink_to_fit();
		}

		void clear() noexcept {
			std::scoped_lock lock{ mutex };
			cells.clear();
		}

		basic_atomic_string& insert(size_type position, size_type count, char value) {
			return insert(position, std::string(count, value));
		}

		basic_atomic_string& insert(size_type position, const char* text) {
			return insert(position, std::string_view{ text });
		}

		basic_atomic_string& insert(size_type position, const char* text, size_type count) {
			return insert(position, std::string_view{ text, count });
		}

		basic_atomic_string& insert(size_type position, const basic_atomic_string& other) {
			return insert(position, std::string_view{ other.snapshot() });
		}

		basic_atomic_string& insert(size_type position, std::string_view text) {
			std::scoped_lock lock{ mutex };
			insert_unlocked(position, text);
			return *this;
		}

		iterator insert(const_iterator position, char value) {
			insert(position.index, std::string_view{ &value, 1 });
			return { this, position.index };
		}

		iterator insert(const_iterator position, size_type count, char value) {
			insert(position.index, count, value);
			return { this, position.index };
		}

		iterator insert(const_iterator position, std::initializer_list<char> values) {
			insert(position.index, std::string_view{ values.begin(), values.size() });
			return { this, position.index };
		}

		template<std::input_iterator iterator_type> iterator insert(const_iterator position, iterator_type first, iterator_type last) {
			insert(position.index, basic_atomic_string{ first, last });
			return { this, position.index };
		}

		basic_atomic_string& erase(size_type position = 0, size_type count = npos) {
			std::scoped_lock lock{ mutex };
			erase_unlocked(position, count);
			return *this;
		}

		iterator erase(const_iterator position) {
			erase(position.index, 1);
			return { this, position.index };
		}

		iterator erase(const_iterator first, const_iterator last) {
			erase(first.index, last.index - first.index);
			return { this, first.index };
		}

		void push_back(char value) {
			std::scoped_lock lock{ mutex };
			cells.emplace_back(cell_policy::make_cell(value));
		}

		void pop_back() {
			std::scoped_lock lock{ mutex };
			cells.pop_back();
		}

		basic_atomic_string& append(size_type count, char value) {
			return append(std::string(count, value));
		}

		basic_atomic_string& append(std::string_view text) {
			std::scoped_lock lock{ mutex };
			insert_unlocked(cells.size(), text);
			return *this;
		}

		basic_atomic_string& append(const char* text) {
			return append(std::string_view{ text });
		}

		basic_atomic_string& append(const char* text, size_type count) {
			return append(std::string_view{ text, count });
		}

		basic_atomic_string& append(const basic_atomic_string& other) {
			return append(std::string_view{ other.snapshot() });
		}

		basic_atomic_string& append(std::initializer_list<char> values) {
			return append(std::string_view{ values.begin(), values.size() });
		}

		template<std::input_iterator iterator_type> basic_atomic_string& append(iterator_type first, iterator_type last) {
			return append(basic_atomic_string{ first, last });
		}

		basic_atomic_string& operator+=(const basic_atomic_string& other) {
			return append(other);
		}

		basic_atomic_string& operator+=(std::string_view text) {
			return append(text);
		}

		basic_atomic_string& operator+=(const char* text) {
			return append(text);
		}

		basic_atomic_string& operator+=(char value) {
			push_back(value);
			return *this;
		}

		basic_atomic_string& operator+=(std::initializer_list<char> values) {
			return append(values);
		}

		basic_atomic_string& replace(size_type position, size_type count, std::string_view text) {
			std::scoped_lock lock{ mutex };
			erase_unlocked(position, count);
			insert_unlocked(position, text);
			return *this;
		}

		basic_atomic_string& replace(size_type position, size_type count, const char* text) {
			return replace(position, count, std::string_view{ text });
		}

		basic_atomic_string& replace(size_type position, size_type count, const basic_atomic_string& other) {
			return replace(position, count, std::string_view{ other.snapshot() });
		}

		basic_atomic_string& replace(size_type position, size_type count, size_type fill_count, char value) {
			return replace(position, count, std::string(fill_count, value));
		}

		basic_atomic_string& replace(const_iterator first, const_iterator last, std::string_view text) {
			return replace(first.index, last.index - first.index, text);
		}

		basic_atomic_string& replace(const_iterator first, const_iterator last, const char* text) {
			return replace(first.index, last.index - first.index, std::string_view{ text });
		}

		basic_atomic_string& replace(const_iterator first, const_iterator last, const basic_atomic_string& other) {
			return replace(first.index, last.index - first.index, other);
		}

		size_type copy(char* destination, size_type count, size_type position = 0) const {
			std::scoped_lock lock{ mutex };
			if (position > cells.size()) {
				throw std::out_of_range{ "basic_atomic_string::copy" };
			}
			const size_type copied{ std::min(count, cells.size() - position) };
			for (size_type x = 0; x < copied; ++x) {
				destination[x] = load_cell(cells[position + x]);
			}
			return copied;
		}

		void resize(size_type count) {
			resize(count, char{});
		}

		void resize(size_type count, char value) {
			std::scoped_lock lock{ mutex };
			if (count < cells.size()) {
				cells.resize(count);
				return;
			}
			while (cells.size() < count) {
				cells.emplace_back(cell_policy::make_cell(value));
			}
		}

		void swap(basic_atomic_string& other) noexcept {
			if (this != &other) {
				std::scoped_lock lock{ mutex, other.mutex };
				cells.swap(other.cells);
			}
		}

		size_type find(std::string_view needle, size_type position = 0) const {
			std::scoped_lock lock{ mutex };
			const size_type count{ cells.size() };
			if (needle.size() > count || position > count - needle.size()) {
				return npos;
			}
			for (size_type x = position; x + needle.size() <= count; ++x) {
				if (matches_unlocked(x, needle)) {
					return x;
				}
			}
			return npos;
		}

		size_type find(const basic_atomic_string& needle, size_type position = 0) const {
			return find(std::string_view{ needle.snapshot() }, position);
		}

		size_type find(const char* needle, size_type position = 0) const {
			return find(std::string_view{ needle }, position);
		}

		size_type find(const char* needle, size_type position, size_type count) const {
			return find(std::string_view{ needle, count }, position);
		}

		size_type find(char value, size_type position = 0) const {
			return find(std::string_view{ &value, 1 }, position);
		}

		size_type rfind(std::string_view needle, size_type position = npos) const {
			std::scoped_lock lock{ mutex };
			const size_type count{ cells.size() };
			if (needle.size() > count) {
				return npos;
			}
			for (size_type x = std::min(position, count - needle.size());; --x) {
				if (matches_unlocked(x, needle)) {
					return x;
				}
				if (x == 0) {
					return npos;
				}
			}
		}

		size_type rfind(const basic_atomic_string& needle, size_type position = npos) const {
			return rfind(std::string_view{ needle.snapshot() }, position);
		}

		size_type rfind(const char* needle, size_type position = npos) const {
			return rfind(std::string_view{ needle }, position);
		}

		size_type rfind(const char* needle, size_type position, size_type count) const {
			return rfind(std::string_view{ needle, count }, position);
		}

		size_type rfind(char value, size_type position = npos) const {
			return rfind(std::string_view{ &value, 1 }, position);
		}

		size_type find_first_of(std::string_view set, size_type position = 0) const {
			std::scoped_lock lock{ mutex };
			for (size_type x = position; x < cells.size(); ++x) {
				if (set.find(load_cell(cells[x])) != std::string_view::npos) {
					return x;
				}
			}
			return npos;
		}

		size_type find_first_of(const basic_atomic_string& set, size_type position = 0) const {
			return find_first_of(std::string_view{ set.snapshot() }, position);
		}

		size_type find_first_of(const char* set, size_type position = 0) const {
			return find_first_of(std::string_view{ set }, position);
		}

		size_type find_first_of(char value, size_type position = 0) const {
			return find_first_of(std::string_view{ &value, 1 }, position);
		}

		size_type find_first_not_of(std::string_view set, size_type position = 0) const {
			std::scoped_lock lock{ mutex };
			for (size_type x = position; x < cells.size(); ++x) {
				if (set.find(load_cell(cells[x])) == std::string_view::npos) {
					return x;
				}
			}
			return npos;
		}

		size_type find_first_not_of(const basic_atomic_string& set, size_type position = 0) const {
			return find_first_not_of(std::string_view{ set.snapshot() }, position);
		}

		size_type find_first_not_of(const char* set, size_type position = 0) const {
			return find_first_not_of(std::string_view{ set }, position);
		}

		size_type find_first_not_of(char value, size_type position = 0) const {
			return find_first_not_of(std::string_view{ &value, 1 }, position);
		}

		size_type find_last_of(std::string_view set, size_type position = npos) const {
			std::scoped_lock lock{ mutex };
			if (cells.empty()) {
				return npos;
			}
			for (size_type x = std::min(position, cells.size() - 1);; --x) {
				if (set.find(load_cell(cells[x])) != std::string_view::npos) {
					return x;
				}
				if (x == 0) {
					return npos;
				}
			}
		}

		size_type find_last_of(const basic_atomic_string& set, size_type position = npos) const {
			return find_last_of(std::string_view{ set.snapshot() }, position);
		}

		size_type find_last_of(const char* set, size_type position = npos) const {
			return find_last_of(std::string_view{ set }, position);
		}

		size_type find_last_of(char value, size_type position = npos) const {
			return find_last_of(std::string_view{ &value, 1 }, position);
		}

		size_type find_last_not_of(std::string_view set, size_type position = npos) const {
			std::scoped_lock lock{ mutex };
			if (cells.empty()) {
				return npos;
			}
			for (size_type x = std::min(position, cells.size() - 1);; --x) {
				if (set.find(load_cell(cells[x])) == std::string_view::npos) {
					return x;
				}
				if (x == 0) {
					return npos;
				}
			}
		}

		size_type find_last_not_of(const basic_atomic_string& set, size_type position = npos) const {
			return find_last_not_of(std::string_view{ set.snapshot() }, position);
		}

		size_type find_last_not_of(const char* set, size_type position = npos) const {
			return find_last_not_of(std::string_view{ set }, position);
		}

		size_type find_last_not_of(char value, size_type position = npos) const {
			return find_last_not_of(std::string_view{ &value, 1 }, position);
		}

		int compare(std::string_view text) const {
			std::scoped_lock lock{ mutex };
			const size_type shared_count{ std::min(cells.size(), text.size()) };
			for (size_type x = 0; x < shared_count; ++x) {
				if (const int difference{ compare_chars(load_cell(cells[x]), text[x]) }; difference != 0) {
					return difference;
				}
			}
			return cells.size() < text.size() ? -1 : (cells.size() > text.size() ? 1 : 0);
		}

		int compare(const basic_atomic_string& other) const {
			return this == &other ? 0 : compare(std::string_view{ other.snapshot() });
		}

		int compare(const char* text) const {
			return compare(std::string_view{ text });
		}

		int compare(size_type position, size_type count, std::string_view text) const {
			return substr(position, count).compare(text);
		}

		int compare(size_type position, size_type count, const basic_atomic_string& other) const {
			return substr(position, count).compare(other);
		}

		bool starts_with(std::string_view prefix) const {
			std::scoped_lock lock{ mutex };
			return prefix.size() <= cells.size() && matches_unlocked(0, prefix);
		}

		bool starts_with(char value) const {
			return starts_with(std::string_view{ &value, 1 });
		}

		bool starts_with(const char* prefix) const {
			return starts_with(std::string_view{ prefix });
		}

		bool ends_with(std::string_view suffix) const {
			std::scoped_lock lock{ mutex };
			return suffix.size() <= cells.size() && matches_unlocked(cells.size() - suffix.size(), suffix);
		}

		bool ends_with(char value) const {
			return ends_with(std::string_view{ &value, 1 });
		}

		bool ends_with(const char* suffix) const {
			return ends_with(std::string_view{ suffix });
		}

		bool contains(std::string_view needle) const {
			return find(needle) != npos;
		}

		bool contains(char value) const {
			return find(value) != npos;
		}

		bool contains(const char* needle) const {
			return find(needle) != npos;
		}

		basic_atomic_string substr(size_type position = 0, size_type count = npos) const {
			std::scoped_lock lock{ mutex };
			if (position > cells.size()) {
				throw std::out_of_range{ "basic_atomic_string::substr" };
			}
			const size_type copied{ std::min(count, cells.size() - position) };
			basic_atomic_string result{};
			for (size_type x = 0; x < copied; ++x) {
				result.cells.emplace_back(cell_policy::make_cell(load_cell(cells[position + x])));
			}
			return result;
		}

		std::string snapshot() const {
			std::scoped_lock lock{ mutex };
			return snapshot_unlocked();
		}

		friend void swap(basic_atomic_string& lhs, basic_atomic_string& rhs) noexcept {
			lhs.swap(rhs);
		}

		friend bool operator==(const basic_atomic_string& lhs, const basic_atomic_string& rhs) {
			return lhs.compare(rhs) == 0;
		}

		friend bool operator==(const basic_atomic_string& lhs, std::string_view rhs) {
			return lhs.compare(rhs) == 0;
		}

		friend bool operator==(const basic_atomic_string& lhs, const char* rhs) {
			return lhs.compare(rhs) == 0;
		}

		friend std::strong_ordering operator<=>(const basic_atomic_string& lhs, const basic_atomic_string& rhs) {
			return lhs.compare(rhs) <=> 0;
		}

		friend std::strong_ordering operator<=>(const basic_atomic_string& lhs, std::string_view rhs) {
			return lhs.compare(rhs) <=> 0;
		}

		friend std::strong_ordering operator<=>(const basic_atomic_string& lhs, const char* rhs) {
			return lhs.compare(rhs) <=> 0;
		}

		friend std::ostream& operator<<(std::ostream& stream, const basic_atomic_string& value) {
			return stream << value.snapshot();
		}

		friend std::istream& operator>>(std::istream& stream, basic_atomic_string& value) {
			std::string buffer{};
			stream >> buffer;
			value.assign(std::string_view{ buffer });
			return stream;
		}

	  private:
		friend class atomic_char_reference<basic_atomic_string>;

		using char_cell	  = typename cell_policy::cell_type;
		using cell_vector = std::vector<char_cell, typename cell_policy::template vector_allocator<char_cell>>;

		cell_vector cells{};
		mutable std::string shadow{};
		mutable std::mutex mutex{};

		std::string snapshot_unlocked() const {
			std::string result(cells.size(), char{});
			for (size_type x = 0; x < cells.size(); ++x) {
				result[x] = load_cell(cells[x]);
			}
			return result;
		}

		bool matches_unlocked(size_type position, std::string_view needle) const {
			for (size_type x = 0; x < needle.size(); ++x) {
				if (load_cell(cells[position + x]) != needle[x]) {
					return false;
				}
			}
			return true;
		}

		void insert_unlocked(size_type position, std::string_view text) {
			if (position > cells.size()) {
				throw std::out_of_range{ "basic_atomic_string::insert" };
			}
			cell_vector new_cells{};
			new_cells.reserve(text.size());
			for (char value: text) {
				new_cells.emplace_back(cell_policy::make_cell(value));
			}
			cells.insert(cells.begin() + static_cast<difference_type>(position), new_cells.begin(), new_cells.end());
		}

		void erase_unlocked(size_type position, size_type count) {
			if (position > cells.size()) {
				throw std::out_of_range{ "basic_atomic_string::erase" };
			}
			const size_type erased{ std::min(count, cells.size() - position) };
			const auto first{ cells.begin() + static_cast<difference_type>(position) };
			cells.erase(first, first + static_cast<difference_type>(erased));
		}

		void store_at(size_type index, char value) {
			std::scoped_lock lock{ mutex };
			store_cell(cells[index], value);
		}
	};

	template<typename cell_policy> basic_atomic_string<cell_policy> operator+(const basic_atomic_string<cell_policy>& lhs, const basic_atomic_string<cell_policy>& rhs) {
		basic_atomic_string<cell_policy> result{ lhs };
		result.append(rhs);
		return result;
	}

	template<typename cell_policy> basic_atomic_string<cell_policy> operator+(basic_atomic_string<cell_policy>&& lhs, const basic_atomic_string<cell_policy>& rhs) {
		lhs.append(rhs);
		return std::move(lhs);
	}

	template<typename cell_policy> basic_atomic_string<cell_policy> operator+(const basic_atomic_string<cell_policy>& lhs, std::string_view rhs) {
		basic_atomic_string<cell_policy> result{ lhs };
		result.append(rhs);
		return result;
	}

	template<typename cell_policy> basic_atomic_string<cell_policy> operator+(const basic_atomic_string<cell_policy>& lhs, const char* rhs) {
		return lhs + std::string_view{ rhs };
	}

	template<typename cell_policy> basic_atomic_string<cell_policy> operator+(const char* lhs, const basic_atomic_string<cell_policy>& rhs) {
		basic_atomic_string<cell_policy> result{ lhs };
		result.append(rhs);
		return result;
	}

	template<typename cell_policy> basic_atomic_string<cell_policy> operator+(const basic_atomic_string<cell_policy>& lhs, char rhs) {
		basic_atomic_string<cell_policy> result{ lhs };
		result.push_back(rhs);
		return result;
	}

	template<typename cell_policy> basic_atomic_string<cell_policy> operator+(char lhs, const basic_atomic_string<cell_policy>& rhs) {
		basic_atomic_string<cell_policy> result(1, lhs);
		result.append(rhs);
		return result;
	}

	template<typename cell_policy> std::istream& getline(std::istream& stream, basic_atomic_string<cell_policy>& value, char delimiter = '\n') {
		std::string buffer{};
		std::getline(stream, buffer, delimiter);
		value.assign(std::string_view{ buffer });
		return stream;
	}

	using std_alloc_atomic_string = basic_atomic_string<std_cell_policy>;
	using atomic_string			  = basic_atomic_string<jsonifier_cell_policy>;

}

template<typename cell_policy> struct std::hash<atomic_strings::basic_atomic_string<cell_policy>> {
	std::size_t operator()(const atomic_strings::basic_atomic_string<cell_policy>& value) const {
		return std::hash<std::string>{}(value.snapshot());
	}
};
