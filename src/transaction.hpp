#ifndef TRANSACTION_HPP
#define TRANSACTION_HPP

#include <string>
#include <sstream>
#include <vector>
#include <span>
#include <iostream>
#include <exception>

#include <breep/util/serialization.hpp>

#include "keys.hpp"
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

	// A transaction output is an account and an amount to assign to that acount
	struct Output {
		key::PublicKey account;
		double amount;

		Hash hashContribution() const {
			std::stringstream contrib;
			contrib << util::bytes2string( key::save(account) );
			contrib << amount;
			return contrib.str();
		}
	};

	// A transaction input is an account, amount to take from that account, and a signed copy of the amount verifying that the sender aproves of the transaction
	struct Input : public Output {
		std::string signature;

		Hash hashContribution() const {
			std::stringstream contrib;
			contrib << util::bytes2string( key::save(account) );
			contrib << amount;
			contrib << signature;
			return contrib.str();
		}

		Input() = default;
		Input(const key::KeyPair& pair, const double amount) : Output{pair.pub, amount}, signature( key::signMessage(pair.pri, std::to_string(amount))) {}
		Input(key::PublicKey account, double amount, std::string signature) : Output{account, amount}, signature(signature) {}
	};

	const std::vector<Input> inputs = {};
	const std::vector<Output> outputs = {};

	const std::span<Hash> parentHashes;
	Hash hash = INVALID_HASH;

	Transaction() = default;
	Transaction(const std::span<Hash> parentHashes, const std::vector<Input>& inputs, const std::vector<Output>& outputs) : timestamp(util::utc_now()), inputs(inputs), outputs(outputs),
		// Copy the parent hashes so that they are locally owned
		parentHashes([](std::span<Hash> parentHashes) -> std::span<Hash> {
			Hash* backing = new Hash[parentHashes.size()];
			for(size_t i = 0; i < parentHashes.size(); i++)
				(*((std::string*) backing + i)) = parentHashes[i]; // Drop the const to allow a copy to occur

			return {backing, parentHashes.size()};
		}(parentHashes)),
		// Hash everything stored
		hash(hashTransaction()) {}

	Transaction(Transaction& other) : timestamp(other.timestamp), inputs(other.inputs), outputs(other.outputs), parentHashes(other.parentHashes), hash(other.hash) { *this = other; }

	~Transaction(){
		// Clean up the parent hashes so that we don't have a memory leak
		if(parentHashes.data()) delete [] parentHashes.data();
	}

	Transaction& operator=(const Transaction& _new){
		(*(int64_t*) &timestamp) = _new.timestamp;
		(*(std::vector<Input>*) &inputs) = _new.inputs;
		(*(std::vector<Output>*) &outputs) = _new.outputs;

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

	Transaction& operator=(Transaction&& _new){
		(*(int64_t*) &timestamp) = _new.timestamp;
		(*(std::vector<Input>*) &inputs) = std::move(_new.inputs);
		(*(std::vector<Output>*) &outputs) = std::move(_new.outputs);
		(*(std::span<Hash>*) &parentHashes) = _new.parentHashes;
		(*(std::span<Hash>*) &_new.parentHashes) = {(Hash*) nullptr, 0}; // The memory is now managed by this object... not the other one
		(*(std::string*) &hash) = _new.hash;

		Hash validate = hashTransaction();
		if(validate != hash)
			throw InvalidHash(validate, hash);

		return *this;
	}

	// Function which checks if the total value coming into a transaction is at least the value coming out of the transaction
	bool validateTransactionTotals() const {
		double inputSum = 0, outputSum = 0;
		for(const Transaction::Input& input: inputs)
			inputSum += input.amount;
		for(const Transaction::Output& output: outputs)
			outputSum += output.amount;

		return inputSum >= outputSum;
	}

	// Function which ensures the transaction's hash is valid and every input agreeded to the transaction
	bool validateTransaction() const {
		bool good = true;
		// Make sure the hash matches
		good &= hashTransaction() == hash;

		// Make sure all of the inputs aggreed to their contribution
		for(const Input& input: inputs)
			good &= key::verifyMessage(input.account, std::to_string(input.amount), input.signature);

		return good;
	}

	// Function which hashes a transaction
	Hash hashTransaction() const {
		std::stringstream hash;
		hash << timestamp;
		for(Input input: inputs)
			hash << input.hashContribution();
		for(Output output: outputs)
			hash << output.hashContribution();

		for(Hash& h: parentHashes)
			hash << h;

		return util::hash(hash.str());
	}
};

// De/serialization
inline breep::serializer& operator<<(breep::serializer& s, const Transaction& t) {
	s << t.parentHashes.size();
	for(Hash &h: t.parentHashes)
		s << h;

	s << t.timestamp;
	s << t.inputs.size();
	for(const Transaction::Input& input: t.inputs){
		s << input.account;
		s << input.amount;
		s << input.signature;
	}

	s << t.outputs.size();
	for(const Transaction::Output& output: t.outputs){
		s << output.account;
		s << output.amount;
	}

	return s;
}
inline breep::deserializer& operator>>(breep::deserializer& d, Transaction& t) {
	size_t parentHashesSize;
	std::vector<std::string> parentHashes;
	int64_t timestamp;
	size_t inputsSize;
	std::vector<Transaction::Input> inputs;
	size_t outputsSize;
	std::vector<Transaction::Output> outputs;
	std::string compressed;

	d >> parentHashesSize;
	parentHashes.resize(parentHashesSize);
	for(int i = 0; i < parentHashesSize; i++)
		d >> parentHashes[i];

	d >> timestamp;
	d >> inputsSize;
	inputs.resize(inputsSize);
	for(int i = 0; i < inputsSize; i++){
		d >> inputs[i].account;
		d >> inputs[i].amount;
		d >> inputs[i].signature;
	}

	d >> outputsSize;
	outputs.resize(outputsSize);
	for(int i = 0; i < outputsSize; i++){
		d >> outputs[i].account;
		d >> outputs[i].amount;
	}

	t = Transaction(parentHashes, inputs, outputs);
	(*(int64_t*) &t.timestamp) = timestamp;
	(*(std::string*) &t.hash) = t.hashTransaction(); // Rehash since the timestamp has been overridden
	return d;
}

#endif /* end of include guard: TRANSACTION_HPP */
