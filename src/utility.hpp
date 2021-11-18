#ifndef UTILITY_HPP
#define UTILITY_HPP

#include <chrono>
#include <string>
#include <ostream>
#include <istream>
#include <cryptopp/sha3.h>
#include <cryptopp/filters.h>
#include <cryptopp/base64.h>
#include <cryptopp/gzip.h>

namespace util {

	inline std::string replace(const std::string base, const std::string_view& toFind, const std::string_view& toReplace, size_t pos = 0, size_t maxReplacements = -1);
	// Function which hashes the provided string
	inline std::string hash(std::string in){
		std::string digest;
	    CryptoPP::SHA3_256 hash;

	    CryptoPP::StringSource foo(in, true, new CryptoPP::HashFilter(hash, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(digest) ) ) );

	    return replace(digest, "\n", ""); // Make sure there aren't any newlines
		// TODO: does this replacement good cause problems?
	}

	// Function which compresses the provided string
	inline std::string compress(std::string in){
		std::string compressed;
		CryptoPP::StringSource ss(in, true, new CryptoPP::Gzip( new CryptoPP::StringSink(compressed)));
		return compressed;
	}
	// Function which decompresses the provided string
	inline std::string decompress(std::string in){
		std::string decompressed;
		CryptoPP::StringSource ss(in, true, new CryptoPP::Gunzip( new CryptoPP::StringSink(decompressed)));
		return decompressed;
	}

	// Converts the current time to UTC and returns the number of seconds since the epoch.
	inline std::time_t utc_now() {
		std::time_t time_now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    	return std::mktime(std::gmtime(&time_now_t));
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
	std::ostream& bytes2stream(std::ostream& s, const std::vector<Byte>& bytes){
		size_t size = bytes.size();
		s.write((char*) &size, sizeof(size));
		s.write((char*) &bytes[0], bytes.size() * sizeof(Byte));
		return s;
	}
	// Function which reads a byte array from a standard output stream
	template<typename Byte>
	std::istream& stream2bytes(std::istream& s, std::vector<Byte>& bytes){
		size_t size;
		s.read((char*) &size, sizeof(size));
		bytes.resize(size);
		s.read((char*) &bytes[0], bytes.size() * sizeof(Byte));
		return s;
	}


	// -- String Extensions --


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

	// Return the number of times <needle> occurs in <base>
	inline size_t count(const std::string& base, const std::string_view& needle, size_t pos = 0) {
		pos = base.find(needle, pos);
		size_t count = 0;
		while(pos != std::string::npos){
			count ++;
			pos = base.find(needle, pos + needle.size());
		}

		return count;
	}

} // util


#endif /* end of include guard: UTILITY_HPP */
