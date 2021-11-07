#ifndef TRANSACTION_HPP
#define TRANSACTION_HPP

#include <string>
#include <sstream>
#include <vector>
#include <span>
#include <iostream>

#include "utility.hpp"

// A hash is an immutable string
typedef const std::string Hash;

// Structure representing a transcation in the tangle
struct Transaction {
	const double amount;

	const std::span<Hash> parentHashes;
	Hash hash;

	Transaction(const std::span<Hash> parentHashes, const double amount) : amount(amount),
		// Copy the parent hashes so that they are locally owned
		parentHashes([](std::span<Hash> parentHashes) -> std::span<Hash> {
			Hash* backing = new Hash[parentHashes.size()];
			for(size_t i = 0; i < parentHashes.size(); i++)
				(*((std::string*) backing + i)) = parentHashes[i]; // Drop the const to allow a copy to occur

			return {backing, parentHashes.size()};
		}(parentHashes)),
		// Hash everything stored
		hash([&]() -> std::string {
			std::stringstream hash;
			hash << amount;

			for(Hash& h: parentHashes)
				hash << h;

			return util::hash(hash.str());
		}()) {}

	~Transaction(){
		// Clean up the parent hashes so that we don't have a memory leak
		delete [] parentHashes.data();
	}
};

#endif /* end of include guard: TRANSACTION_HPP */
