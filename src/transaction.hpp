#ifndef TRANSACTION_HPP
#define TRANSACTION_HPP

#include <string>
#include <sstream>
#include <vector>
#include <span>
#include <iostream>
#include <exception>
#include <iomanip>

#include <breep/util/serialization.hpp>
#include <cryptopp/osrng.h>

#include "timer.h"

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
	const size_t nonce = 0;

	// Variables tracking how much work was needed to mine this particular transaction
	const uint8_t miningDifficulty = 3; // How many characters at the start of the hash must be the target
	const char miningTarget = 'A'; // What character the first few characters of the hash must be

	// A transaction output is an account and an amount to assign to that account
	struct Output {
		friend breep::serializer& operator<<(breep::serializer& s, const Transaction& t);
		friend breep::deserializer& operator>>(breep::deserializer& d, Transaction& t);
	protected:
		std::string _accountBase64;
	public:
		key::PublicKey account() const { return key::loadPublicBase64(_accountBase64); }
		double amount;


		Hash hashContribution() const {
			std::stringstream contrib;
			contrib << _accountBase64;
			contrib << amount;
			return contrib.str();
		}

		Output() = default;
		Output(const key::KeyPair& pair, const double amount) : _accountBase64( key::saveBase64(pair.pub) ), amount(amount) {}
		Output(const key::PublicKey& account, double amount) : _accountBase64( key::saveBase64(account) ), amount(amount) {}
		Output(const key::PublicKey&& account, double amount) : Output(account, amount) {}
	};

	// A transaction input is an account, amount to take from that account, and a signed copy of the amount verifying that the sender aproves of the transaction
	struct Input : public Output {
		std::string signature;

		Hash hashContribution() const {
			std::stringstream contrib;
			contrib << _accountBase64;
			contrib << amount;
			contrib << signature;
			return contrib.str();
		}

		Input() = default;
		Input(const key::KeyPair& pair, const double amount) : Output(pair, amount), signature( key::signMessage(pair.pri, std::to_string(amount))) {}
		Input(const key::PublicKey& account, double amount, std::string signature) : Output(account, amount), signature(signature) {}
		Input(const key::PublicKey&& account, double amount, std::string signature) : Output(account, amount), signature(signature) {}
	};

	const std::vector<Input> inputs = {};
	const std::vector<Output> outputs = {};

	const std::span<Hash> parentHashes;
	Hash hash = INVALID_HASH;

	Transaction() = default;
	Transaction(const std::span<Hash> parentHashes, const std::vector<Input>& inputs, const std::vector<Output>& outputs, uint8_t difficulty = 3) : timestamp(util::utc_now()), miningDifficulty(difficulty), inputs(inputs), outputs(outputs),
		// Set a random initial value for the nonce
		nonce([]() -> size_t {
			// Seed random number generator
			static CryptoPP::AutoSeededRandomPool rng;
			return rng.GenerateWord32() + rng.GenerateWord32();
		}()),
		// Copy the parent hashes so that they are locally owned
		parentHashes([](std::span<Hash> parentHashes) -> std::span<Hash> {
			Hash* backing = new Hash[parentHashes.size()];
			for(size_t i = 0; i < parentHashes.size(); i++)
				(*((std::string*) backing + i)) = parentHashes[i]; // Drop the const to allow a copy to occur

			return {backing, parentHashes.size()};
		}(parentHashes)),
		// Hash everything stored
		hash(hashTransaction()) {}

	Transaction(const Transaction& other) : timestamp(other.timestamp), inputs(other.inputs), outputs(other.outputs), parentHashes(other.parentHashes), hash(other.hash) { *this = other; }

	~Transaction(){
		// Clean up the parent hashes so that we don't have a memory leak
		if(parentHashes.data()) delete [] parentHashes.data();
	}

	Transaction& operator=(const Transaction& _new){
		util::makeMutable(timestamp) = _new.timestamp;
		util::makeMutable(nonce) = _new.nonce;
		util::makeMutable(miningDifficulty) = _new.miningDifficulty;
		util::makeMutable(miningTarget) = _new.miningTarget;
		util::makeMutable(inputs) = _new.inputs;
		util::makeMutable(outputs) = _new.outputs;

		Hash* backing = new Hash[_new.parentHashes.size()];
		for(size_t i = 0; i < _new.parentHashes.size(); i++)
			*util::makeMutable(backing + i) = _new.parentHashes[i]; // Drop the const to allow a copy to occur

		util::makeMutable(parentHashes) = {backing, _new.parentHashes.size()};
		util::makeMutable(hash) = _new.hash;

		return *this;
	}

	Transaction& operator=(Transaction&& _new){
		util::makeMutable(timestamp) = _new.timestamp;
		util::makeMutable(nonce) = _new.nonce;
		util::makeMutable(miningDifficulty) = _new.miningDifficulty;
		util::makeMutable(miningTarget) = _new.miningTarget;
		util::makeMutable(inputs) = std::move(_new.inputs);
		util::makeMutable(outputs) = std::move(_new.outputs);
		util::makeMutable(parentHashes) = _new.parentHashes;
		util::makeMutable(_new.parentHashes) = {(Hash*) nullptr, 0}; // The memory is now managed by this object... not the other one
		util::makeMutable(hash) = _new.hash;

		return *this;
	}

	void debugDump(){
		std::cout << "Hash: " << hash << std::endl
			<< "Parent Hashes: [";
		for(auto& p: parentHashes)
			std::cout << p << ", ";
		std::cout << " ]" << std::endl;

		std::cout << "Timestamp: " << std::put_time(std::localtime((time_t*) &timestamp), "%c %Z") << std::endl
			<< "Nonce: " << nonce << std::endl
			<< "Difficulty: " << (int) miningDifficulty << std::endl;

		std::cout << "Inputs: [" << std::endl;
		for(auto& i: inputs)
			std::cout << "\t Account: " << key::hash(i.account()) << ", Amount: " << i.amount << std::endl;
		std::cout << "]" << std::endl
			<< "Outputs: [" << std::endl;
		for(auto& o: outputs)
			std::cout << "\t Account: " << key::hash(o.account()) << ", Amount: " << o.amount << std::endl;
		std::cout << "]" << std::endl;
	}

	// Function which checks if the transaction has been mined
	bool validateTransactionMined() {
		if(miningDifficulty > hash.size()) return false;

		std::string targetHash(miningDifficulty, miningTarget);
		targetHash += std::string(hash.size() - miningDifficulty, '/');

		return util::base64Compare(hash, targetHash) <= 0;
	}

	// Function which mines the transaction
	void mineTransaction(){
		std::cout << "Started mining transaction..." << std::endl;
		Timer t;
		while( !validateTransactionMined() ){
			util::makeMutable(nonce)++;
			util::makeMutable(hash) = hashTransaction();
		}
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

		// Make sure all of the inputs agreed to their contribution
		for(const Input& input: inputs)
			good &= key::verifyMessage(input.account(), std::to_string(input.amount), input.signature);

		return good;
	}

	// Function which hashes a transaction
	Hash hashTransaction() const {
		std::stringstream hash;
		hash << timestamp;
		hash << nonce;
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
	s << t.nonce;
	s << t.miningDifficulty;
	s << t.miningTarget;

	s << t.inputs.size();
	for(const Transaction::Input& input: t.inputs){
		s << input._accountBase64;
		s << input.amount;
		s << input.signature;
	}

	s << t.outputs.size();
	for(const Transaction::Output& output: t.outputs){
		s << output._accountBase64;
		s << output.amount;
	}

	return s;
}
inline breep::deserializer& operator>>(breep::deserializer& d, Transaction& t) {
	size_t parentHashesSize;
	std::vector<std::string> parentHashes;
	int64_t timestamp;
	size_t nonce;
	uint8_t miningDifficulty;
	char miningTarget;
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
	d >> nonce;
	d >> miningDifficulty;
	d >> miningTarget;

	d >> inputsSize;
	inputs.resize(inputsSize);
	for(int i = 0; i < inputsSize; i++){
		d >> inputs[i]._accountBase64;
		d >> inputs[i].amount;
		d >> inputs[i].signature;
	}

	d >> outputsSize;
	outputs.resize(outputsSize);
	for(int i = 0; i < outputsSize; i++){
		d >> outputs[i]._accountBase64;
		d >> outputs[i].amount;
	}

	t = Transaction(parentHashes, inputs, outputs, miningDifficulty);
	// Update several variables behind the scenes
	util::makeMutable(t.timestamp) = timestamp;
	util::makeMutable(t.nonce) = nonce;
	util::makeMutable(t.miningTarget) = miningTarget;
	util::makeMutable(t.hash) = t.hashTransaction(); // Rehash since the timestamp and nonce have been overridden
	return d;
}

#endif /* end of include guard: TRANSACTION_HPP */
