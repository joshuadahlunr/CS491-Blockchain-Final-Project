#ifndef UTILITY_HPP
#define UTILITY_HPP

#include <string>
#include <cryptopp/sha3.h>
#include <cryptopp/filters.h>
#include <cryptopp/base64.h>

namespace util {

	inline std::string replace(const std::string base, const std::string_view& toFind, const std::string_view& toReplace, size_t pos = 0, size_t maxReplacements = -1);
	inline std::string hash(std::string in){
		std::string digest;
	    CryptoPP::SHA3_256 hash;

	    CryptoPP::StringSource foo(in, true, new CryptoPP::HashFilter(hash, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(digest) ) ) );

	    return replace(digest, "\n", ""); // Make sure there aren't any newlines
		// TODO: does this replacement good cause problems?
	}

	// String extensions
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
