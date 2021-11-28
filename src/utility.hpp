/**
 * @file utility.hpp
 * @author Joshua Dahl (jdahl@unr.edu)
 * @brief File containing helper functions used to make certain operations in the rest of the program easier
 * @version 0.1
 * @date 2021-11-28
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#ifndef UTILITY_HPP
#define UTILITY_HPP

#include <chrono>
#include <istream>
#include <ostream>
#include <queue>
#include <string>
#include <unordered_set>

#include <cryptopp/sha3.h>
#include <cryptopp/base64.h>
#include <cryptopp/gzip.h>

/**
 * @brief Extension to std::queue which allows modification of its container
 * 
 * @tparam T - The type the queue manages
 * @tparam Container - The type the queue is backed by
 */
template<typename T, typename Container = std::deque<T>>
struct ModifiableQueue: public std::queue<T, Container> {
	using Base = std::queue<T, Container>;
	using Base::Base;

	/**
	 * @brief Get the Container backing the queue
	 * @return Container& - The Container backing the queue
	 */
	Container& getContainer() { return Base::c; }
};

namespace util {

	// Make a const variable mutable
	template<typename T> typename std::remove_cv<T>::type* mutable_cast(const T* in) { return (T*) in; }
	// Make a const variable mutable
	template<typename T> typename std::remove_cv<T>::type& mutable_cast(const T& in) { return *((T*) &in); }

	/**
	 * @brief Function which checks if the provided range includes the specified <needle>, checking for equality with function <f>
	 * 
	 * @tparam T - The type in question
	 * @tparam Iterator - Contauiner iterators
	 * @tparam Function - Comparison function
	 * @param begin - Iterator pointing to the beginning of the containers
	 * @param end - Iterator pointing to the end of the containers
	 * @param needle - The value to search for
	 * @param equals - Function used to check for equality of <T>s
	 * @return True if the value is in the container, false otherwise
	 */
	template<typename T, typename Iterator, typename Function>
	bool contains(Iterator begin, Iterator end, T needle, Function equals){
		for(auto i = begin; i != end; i++)
			if( equals(*i, needle) )
				return true;
		return false;
	}

	inline std::string replace(const std::string base, const std::string_view& toFind, const std::string_view& toReplace, size_t pos = 0, size_t maxReplacements = -1);
	/**
	 * @brief Function which hashes the specified string
	 * 
	 * @param in - String to hash
	 * @return std::string - The hashed string
	 */
	inline std::string hash(std::string in){
		std::string digest;
	    CryptoPP::SHA3_256 hash;

	    CryptoPP::StringSource foo(in, true, new CryptoPP::HashFilter(hash, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(digest))));

	    return replace(digest, "\n", ""); // Make sure there aren't any newlines
	}

	/**
	 * @brief Function which compresses the provided string
	 * 
	 * @param in - String to compress
	 * @return std::string - Compressed string
	 */
	inline std::string compress(std::string in){
		std::string compressed;
		CryptoPP::StringSource ss(in, true, new CryptoPP::Gzip( new CryptoPP::StringSink(compressed)));
		return compressed;
	}
	/**
	 * @brief Function which decompresses the provided string
	 * 
	 * @param in - String to decompress
	 * @return std::string - Decompressed string
	 */
	inline std::string decompress(std::string in){
		std::string decompressed;
		CryptoPP::StringSource ss(in, true, new CryptoPP::Gunzip( new CryptoPP::StringSink(decompressed)));
		return decompressed;
	}

	/**
	 * @brief Gets the current timestamp in UTC time
	 * 
	 * @return std::time_t - Current timestamp in UTC time
	 */
	inline std::time_t utc_now() {
		time_t now = std::time(nullptr);
		return std::mktime(std::gmtime(&now));
	}

	/**
	 * @brief Function which removes all duplicate elements from a vector
	 * 
	 * @param v - The vector reference to remove duplicates from
	 */
	template<typename T>
	void removeDuplicates(std::vector<T>& v){
		std::unordered_set<T> s;
		auto end = std::remove_if(v.begin(), v.end(), [&s](T const& i) {
			return !s.insert(i).second;
		});

		v.erase(end, v.end());
	}

	/**
	 * @brief Function which removes all duplicate elements from a vector (using an arbitrary predicate to check for equality)
	 * 
	 * @param v - The vector reference to remove duplicates from
	 * @param pred - Function used to check for equality
	 */
	template<typename T, typename Function >
	void removeDuplicates(std::vector<T>& v, Function equal){
		sort(v.begin(), v.end());
		v.erase(unique(v.begin(), v.end(), equal), v.end());
	}


	// -- Conversion Functions --


	// Function which converts a random unsigned 32bit integer into a float
	// NOTE: from https://www.doornik.com/research/randomdouble.pdf
	#define M_RAN_INVM32 2.32830643653869628906e-010
	inline float rand2float(uint32_t iRan1){
		return ((int)(iRan1) * M_RAN_INVM32 + (0.5 + M_RAN_INVM32 / 2));
	}

	// Function which converts two random unsigned 32bit integers into a double
	// NOTE: from https://www.doornik.com/research/randomdouble.pdf
	#define M_RAN_INVM52 2.22044604925031308085e-016
	inline double rand2double(uint32_t iRan1, uint32_t iRan2){
		return ((int)(iRan1) * M_RAN_INVM32 + (0.5 + M_RAN_INVM52 / 2) + (int)((iRan2) & 0x000FFFFF) * M_RAN_INVM52);
	}

	// Function which converts a byte array into a string
	template<typename Byte>
	inline std::string bytes2string(std::vector<Byte> bytes){
		std::string out;
		for(auto b: bytes) out += b;
		return out;
	}
	// Function which converts a string into a byte array
	template<typename Byte>
	inline std::vector<Byte> string2bytes(std::string string){
		std::vector<Byte> out;
		for(auto c: string) out.push_back(c);
		return out;
	}
	// Function which writes a byte array to a standard output stream
	template<typename Byte>
	inline std::ostream& bytes2stream(std::ostream& s, const std::vector<Byte>& bytes){
		size_t size = bytes.size();
		s.write((char*) &size, sizeof(size));
		s.write((char*) &bytes[0], bytes.size() * sizeof(Byte));
		return s;
	}
	// Function which reads a byte array from a standard output stream
	template<typename Byte>
	inline std::istream& stream2bytes(std::istream& s, std::vector<Byte>& bytes){
		size_t size;
		s.read((char*) &size, sizeof(size));
		bytes.resize(size);
		s.read((char*) &bytes[0], bytes.size() * sizeof(Byte));
		return s;
	}


	// -- String Extensions --


	/**
	 * @brief Functions which determines which of two base64 strings represent a bigger number
	 * 
	 * @param A - The first string
	 * @param B - The second atring 
	 * @return int - 1 = A bigger, 0 = equal, -1 = B bigger
	 */
	inline int base64Compare(std::string a, std::string b){
		// Lambda which performs the comparison on a per string scale
		auto base64CharCompare = [](char a, char b){
			// Ensure both characters are valid base 64 characters
			if( !(isalnum(a) || a == '+' || a == '/') )
				throw std::runtime_error(std::string("Character `") + a + "` is not a valid base 64 character");
			if( !(isalnum(b) || b == '+' || b == '/') )
				throw std::runtime_error(std::string("Character `") + b + "` is not a valid base 64 character");

			// If they are equal return 0
			if(a == b) return 0;

			// Check characters that correspond to bigger numbers first, if one of the characters falls in that range while the other doesn't it is bigger
			if(a == '/') return 1;
			if(b == '/') return -1;

			if(a == '+') return 1;
			if(b == '+') return -1;

			// If both characters are numbers, just compare them (number size corresponds with order in ascii table)
			if(isdigit(a) && isdigit(b)){
				if(a > b) return 1;
				if(b > a) return -1;
			}
			if(isdigit(a)) return 1;
			if(isdigit(b)) return -1;

			// If both characters are lowercase, just compare them (number size corresponds with order in ascii table)
			if(islower(a) && islower(b)){
				if(a > b) return 1;
				if(b > a) return -1;
			}
			if(islower(a)) return 1;
			if(islower(b)) return -1;

			// If both characters are upper case directly compare them
			// (we have already confirmed that both characters are valid, so they must be uppercase if they weren't detected earlier)
			if(a > b) return 1;
			return -1;
		};

		// Longer strings correspond to bigger numbers
		if(a.size() > b.size()) return 1;
		if(b.size() > a.size()) return -1;

		// Compare the two equally sized strings character by character
		for(int i = 0; i < a.size(); i++)
			if(int compare = base64CharCompare(a[i], b[i]); compare != 0)
				return compare;

		return 0;
	}

	// Replace the first instance of <toFind> in <base> with <toReplace>
	inline std::string& replace_first_original(std::string& base, const std::string_view& toFind, const std::string_view& toReplace, size_t pos = 0){
		pos = base.find(toFind, pos);
		if (pos == std::string::npos) return base;

		base.replace(pos, toFind.size(), toReplace);
		return base;
	}
	inline std::string replace_first(const std::string base, const std::string_view& toFind, const std::string_view& toReplace, size_t pos = 0) {
		std::string out = base;
		return replace_first_original(out, toFind, toReplace, pos);
	}

	// Replace the every instance of <toFind> in <base> with <toReplace>
	inline std::string& replace_original(std::string& base, const std::string_view& toFind, const std::string_view& toReplace, size_t pos = 0, size_t maxReplacements = -1){
		pos = base.find(toFind, pos);
		for(size_t count = 0; pos != std::string::npos && count < maxReplacements; count++){
			base.replace(pos, toFind.size(), toReplace);
			pos = base.find(toFind, pos + toReplace.size());
		}
		return base;
	}
	inline std::string replace(const std::string base, const std::string_view& toFind, const std::string_view& toReplace, size_t pos/* = 0*/, size_t maxReplacements/* = -1*/) {
		std::string out = base;
		return replace_original(out, toFind, toReplace, pos, maxReplacements);
	}

} // util


#endif /* end of include guard: UTILITY_HPP */
