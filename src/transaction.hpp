#ifndef TRANSACTION_HPP
#define TRANSACTION_HPP

#include <string>
#include <sstream>
#include <vector>
#include <span>
#include <iostream>
#include <exception>

#include <breep/util/serialization.hpp>

#include "utility.hpp"

// A hash is an immutable string
typedef const std::string Hash;

#define INVALID_HASH "Invalid"

// Structure representing a transcation in the tangle
struct Transaction {
	friend breep::deserializer& operator>>(breep::deserializer& d, Transaction& n);

	// Exception thrown when the transaction encounters an invalid hash
	struct InvalidHash : public std::runtime_error { InvalidHash(Hash actual, Hash claimed) : std::runtime_error("Data integrity violated, claimed hash `" + claimed + "` is not the same as the actual hash `" + actual + "`") {} };

	// Mark the deserializer as a friend so it can use the copy operator
	friend breep::deserializer& operator>>(breep::deserializer& d, Transaction& n);

	const int64_t timestamp = 0;
	const double amount = 0;

	const std::span<Hash> parentHashes;
	Hash hash = INVALID_HASH;

	Transaction() = default;
	Transaction(const std::span<Hash> parentHashes, const double amount) : timestamp(util::utc_now()), amount(amount),
		// Copy the parent hashes so that they are locally owned
		parentHashes([](std::span<Hash> parentHashes) -> std::span<Hash> {
			Hash* backing = new Hash[parentHashes.size()];
			for(size_t i = 0; i < parentHashes.size(); i++)
				(*((std::string*) backing + i)) = parentHashes[i]; // Drop the const to allow a copy to occur

			return {backing, parentHashes.size()};
		}(parentHashes)),
		// Hash everything stored
		hash(hashTransaction()) {}

	Transaction(Transaction& other) : timestamp(other.timestamp), amount(other.amount), parentHashes(other.parentHashes), hash(other.hash) { *this = other; }

	~Transaction(){
		// Clean up the parent hashes so that we don't have a memory leak
		if(parentHashes.data()) delete [] parentHashes.data();
	}

	bool validateTransaction() {
		Hash validate = hashTransaction();
		return validate == hash;
	}

	Hash hashTransaction(){
		std::stringstream hash;
		hash << timestamp;
		hash << amount;

		for(Hash& h: parentHashes)
			hash << h;

		return util::hash(hash.str());
	}

	Transaction& operator=(const Transaction& _new){
		(*(int64_t*) &timestamp) = _new.timestamp;
		(*(double*) &amount) = _new.amount;

		Hash* backing = new Hash[_new.parentHashes.size()];
		for(size_t i = 0; i < _new.parentHashes.size(); i++)
			(*((std::string*) backing + i)) = _new.parentHashes[i]; // Drop the const to allow a copy to occur

		(*(std::span<Hash>*) &parentHashes) = {backing, _new.parentHashes.size()};
		(*(std::string*) &hash) = _new.hash;

		Hash validate = hashTransaction();
		if(validate != hash)
			throw InvalidHash(validate, hash);

		return *this;
	}

	Transaction& operator=(const Transaction&& _new){
		(*(int64_t*) &timestamp) = _new.timestamp;
		(*(double*) &amount) = _new.amount;
		(*(std::span<Hash>*) &parentHashes) = _new.parentHashes;
		(*(std::span<Hash>*) &_new.parentHashes) = {(Hash*) nullptr, 0}; // The memory is now managed by this object... not the other one
		(*(std::string*) &hash) = _new.hash;

		Hash validate = hashTransaction();
		if(validate != hash)
			throw InvalidHash(validate, hash);

		return *this;
	}
};

// De/serialization
inline breep::serializer& operator<<(breep::serializer& s, const Transaction& t) {
	// s << t.hash; // TODO: Need to serialize hash?
	s << t.parentHashes.size();
	for(Hash &h: t.parentHashes)
		s << h;

	s << t.timestamp;
	s << t.amount;
	return s;
}
inline breep::deserializer& operator>>(breep::deserializer& d, Transaction& t) {
	// std::string hash;
	size_t parentHashesSize;
	std::vector<std::string> parentHashes;
	int64_t timestamp;
	double amount;

	// d >> hash;
	d >> parentHashesSize;
	parentHashes.resize(parentHashesSize);
	for(int i = 0; i < parentHashesSize; i++)
		d >> parentHashes[i];

	d >> timestamp;
	d >> amount;

	t = Transaction(parentHashes, amount);
	(*(int64_t*) &t.timestamp) = timestamp;
	(*(std::string*) &t.hash) = t.hashTransaction(); // Rehash since the timestamp has been overridden
	return d;
}

#endif /* end of include guard: TRANSACTION_HPP */
