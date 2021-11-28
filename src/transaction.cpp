/**
 * @file transaction.cpp
 * @author Joshua Dahl (jdahl@unr.edu)
 * @brief Code backing transaction.hpp
 * @version 0.1
 * @date 2021-11-29
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#include "transaction.hpp"

#include <cryptopp/osrng.h>

#include "keys.hpp"
#include "timer.h"

/**
 * @brief Construct a new Transaction from its parents, outputs, and optional difficulty
 *
 * @param parentHashes Parents of this transaction
 * @param inputs Inputs to the transaction
 * @param outputs Outputs of the transaction
 * @param difficulty The difficulty of mining this transaction (increased difficulty results in increased weight)
 */
Transaction::Transaction(const std::span<Hash> parentHashes, const std::vector<Input>& inputs, const std::vector<Output>& outputs, uint8_t difficulty /*= 3*/) : timestamp(util::utc_now()), miningDifficulty(difficulty), inputs(inputs), outputs(outputs),
	// Set a random initial value for the nonce
	nonce([]() -> size_t {
		// Seed random number generator
		static CryptoPP::AutoSeededRandomPool rng;
		return rng.GenerateWord32() + rng.GenerateWord32();
	}()),
	// Copy the parent hashes so that they are locally owned
	parentHashes([](std::span<Hash> parentHashes) -> std::span<Hash> {
		// Ensure that there are no duplicate parent hashes
		std::unordered_set<std::string> uniqueHashesSet;
		for(auto& hash: parentHashes)
			uniqueHashesSet.insert(hash);
		// Sort the hashes
		std::vector<std::string> uniqueHashes(uniqueHashesSet.begin(), uniqueHashesSet.end());
		std::sort(uniqueHashes.begin(), uniqueHashes.end());

		// Create local storage for the hashes and copy the unique sorted hashes into it
		Hash* backing = new Hash[uniqueHashes.size()];
		for(size_t i = 0; i < uniqueHashes.size(); i++)
			*util::mutable_cast(backing + i) = uniqueHashes[i]; // Drop the const to allow a copy to occur

		return {backing, uniqueHashes.size()};
	}(parentHashes)),
	// Hash everything stored
	hash(hashTransaction()) {}

/**
 * @brief Assignment operator
 *
 * @param _new The transaction to copy into us
 * @return Transaction& - Reference to ourselves for chained assignment
 */
Transaction& Transaction::operator=(const Transaction& _new){
	util::mutable_cast(timestamp) = _new.timestamp;
	util::mutable_cast(nonce) = _new.nonce;
	util::mutable_cast(miningDifficulty) = _new.miningDifficulty;
	util::mutable_cast(miningTarget) = _new.miningTarget;
	util::mutable_cast(inputs) = _new.inputs;
	util::mutable_cast(outputs) = _new.outputs;

	// Create new parent hash memory and copy the data across
	Hash* backing = new Hash[_new.parentHashes.size()];
	for(size_t i = 0; i < _new.parentHashes.size(); i++)
		*util::mutable_cast(backing + i) = _new.parentHashes[i]; // Drop the const to allow a copy to occur

	util::mutable_cast(parentHashes) = {backing, _new.parentHashes.size()};
	util::mutable_cast(hash) = _new.hash;

	return *this;
}

/**
 * @brief Move operator
 *
 * @param _new The transaction to move into us
 * @return Transaction& - Reference to ourselves for chained assignment
 */
Transaction& Transaction::operator=(Transaction&& _new){
	util::mutable_cast(timestamp) = _new.timestamp;
	util::mutable_cast(nonce) = _new.nonce;
	util::mutable_cast(miningDifficulty) = _new.miningDifficulty;
	util::mutable_cast(miningTarget) = _new.miningTarget;
	util::mutable_cast(inputs) = std::move(_new.inputs);
	util::mutable_cast(outputs) = std::move(_new.outputs);
	util::mutable_cast(parentHashes) = _new.parentHashes;
	util::mutable_cast(_new.parentHashes) = {(Hash*) nullptr, 0}; // The memory is now managed by this object... not the other one
	util::mutable_cast(hash) = _new.hash;

	return *this;
}

/**
 * @brief Output the transaction for debug output
 */
void Transaction::debugDump(){
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

/**
 * @brief Function which checks if the transaction has been mined
 *
 * @return True if it appears to have been mined, false otherwise
 */
bool Transaction::validateTransactionMined() {
	if(miningDifficulty > hash.size()) return false;

	// Create the a target string based on the mining difficulty
	std::string targetHash(miningDifficulty, miningTarget);
	targetHash += std::string(hash.size() - miningDifficulty, '/');

	// Check that the hash represents a number less than the target
	return util::base64Compare(hash, targetHash) <= 0;
}

/**
 * @brief Function which mines the transaction
 */
void Transaction::mineTransaction(){
	// Provide feedback about when we start
	std::cout << "Started mining transaction..." << std::endl;
	// Time how long it took to mine (and display the result to the user)
	Timer t;

	// While the transaction is not successfully mined
	while( !validateTransactionMined() ){
		// Increment the nonce...
		util::mutable_cast(nonce)++;
		// And rehash
		util::mutable_cast(hash) = hashTransaction();
	}
}

/**
 * @brief Function which hashes a transaction
 *
 * @return Hash - The hashed transaction
 */
Hash Transaction::hashTransaction() const {
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

/**
 * @brief Function which checks if the total value coming into a transaction is at least the value coming out of the transaction
 *
 * @return True if validation succeeds, false otherwise
 */
bool Transaction::validateTransactionTotals() const {
	// Add up the value of the inputs and value of the outputs
	double inputSum = 0, outputSum = 0;
	for(const Transaction::Input& input: inputs)
		inputSum += input.amount;
	for(const Transaction::Output& output: outputs)
		outputSum += output.amount;

	// Ensure the inputs are at least as large as the outputs
	return inputSum >= outputSum;
}

/**
 * @brief Function which ensures the transaction's hash is valid and every input agreeded to the transaction
 *
 * @return True if validation succeeds, false otherwise
 */
bool Transaction::validateTransaction() const {
	bool good = true;
	// Make sure the hash matches
	good &= hashTransaction() == hash;

	// Make sure all of the inputs agreed to their contribution
	for(const Input& input: inputs)
		good &= key::verifyMessage(input.account(), std::to_string(input.amount), input.signature);

	return good;
}


// -- De/serialization --


breep::serializer& operator<<(breep::serializer& s, const Transaction& t) {
	// Mark how many hashes we have then output them all
	s << t.parentHashes.size();
	for(Hash &h: t.parentHashes)
		s << h;

	s << t.timestamp;
	s << t.nonce;
	s << t.miningDifficulty;
	s << t.miningTarget;

	// Mark how many inputs we have then output their values
	s << t.inputs.size();
	for(const Transaction::Input& input: t.inputs){
		s << input._accountBase64;
		s << input.amount;
		s << input.signature;
	}

	// Mark how many outputs we have then output their values
	s << t.outputs.size();
	for(const Transaction::Output& output: t.outputs){
		s << output._accountBase64;
		s << output.amount;
	}

	return s;
}
breep::deserializer& operator>>(breep::deserializer& d, Transaction& t) {
	// Read parent hashes
	size_t parentHashesSize;
	std::vector<std::string> parentHashes;
	d >> parentHashesSize;
	parentHashes.resize(parentHashesSize);
	for(int i = 0; i < parentHashesSize; i++)
		d >> parentHashes[i];

	// Read freestanding values
	int64_t timestamp;
	size_t nonce;
	uint8_t miningDifficulty;
	char miningTarget;
	d >> timestamp;
	d >> nonce;
	d >> miningDifficulty;
	d >> miningTarget;

	// Read inputs
	size_t inputsSize;
	std::vector<Transaction::Input> inputs;
	d >> inputsSize;
	inputs.resize(inputsSize);
	for(int i = 0; i < inputsSize; i++){
		d >> inputs[i]._accountBase64;
		d >> inputs[i].amount;
		d >> inputs[i].signature;
	}

	// Read outputs
	size_t outputsSize;
	std::vector<Transaction::Output> outputs;
	d >> outputsSize;
	outputs.resize(outputsSize);
	for(int i = 0; i < outputsSize; i++){
		d >> outputs[i]._accountBase64;
		d >> outputs[i].amount;
	}

	// Create the transaction
	t = Transaction(parentHashes, inputs, outputs, miningDifficulty);
	// Update several variables behind the scenes
	util::mutable_cast(t.timestamp) = timestamp;
	util::mutable_cast(t.nonce) = nonce;
	util::mutable_cast(t.miningTarget) = miningTarget;
	util::mutable_cast(t.hash) = t.hashTransaction(); // Rehash since the timestamp and nonce have been overridden
	return d;
}