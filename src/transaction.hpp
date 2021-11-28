/**
 * @file transaction.hpp
 * @author Joshua Dahl (jdahl@unr.edu)
 * @brief File which provides a basic transaction
 * @version 0.1
 * @date 2021-11-29
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#ifndef TRANSACTION_HPP
#define TRANSACTION_HPP

#include <iomanip>
#include <span>

#include "keys.hpp"

// A hash is an immutable string
typedef const std::string Hash;

// Invalid hash string
#define INVALID_HASH "Invalid"

// Structure representing a transcation in the tangle
struct Transaction {
	// Mark the deserializer as a friend so it can use the copy operator
	friend breep::deserializer& operator>>(breep::deserializer& d, Transaction& n);

	/**
	 * @brief Exception thrown when the transaction encounters an invalid hash
	 */
	struct InvalidHash : public std::runtime_error { InvalidHash(Hash actual, Hash claimed) : std::runtime_error("Data integrity violated, claimed hash `" + claimed + "` is not the same as the actual hash `" + actual + "`") {} };

	// The timestamp of this transaction's creation
	const int64_t timestamp = 0;
	// The nonce this transaction uses to ensure its hash is valid
	const size_t nonce = 0;

	// Variables tracking how much work was needed to mine this particular transaction
	const uint8_t miningDifficulty = 3;	// How many characters at the start of the hash must be the target
	const char miningTarget = 'A';		// What character the first few characters of the hash must be

	/**
	 * @brief A transaction output is an account and an amount to assign to that account
	 * @note Used as a base for transaction inputs
	 */
	struct Output {
		// Mark de/serializastion as friends so they can access the raw account
		friend breep::serializer& operator<<(breep::serializer& s, const Transaction& t);
		friend breep::deserializer& operator>>(breep::deserializer& d, Transaction& t);
	protected:
		// The base 64 representation of the key
		std::string _accountBase64;
	public:
		// The public key of the account
		key::PublicKey account() const { return key::loadPublicBase64(_accountBase64); }
		// The amount of money transferred
		double amount;

		/**
		 * @brief Calculates what this output contributes to the hash
		 *
		 * @return Hash - The hash contribution
		 */
		inline Hash hashContribution() const {
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

	/**
	 * @brief A transaction input is an account, amount to take from that account, and a signed copy of the amount verifying that the sender aproves of the transaction
	 */
	struct Input : public Output {
		// Signature proving that the sender approves this transaction
		std::string signature;

		/**
		 * @brief Calculates what this output contributes to the hash
		 *
		 * @return Hash - The hash contribution
		 */
		inline Hash hashContribution() const {
			std::stringstream contrib;
			contrib << _accountBase64;
			contrib << amount;
			contrib << signature;
			return contrib.str();
		}

		Input() = default;
		// Constructor automatically signs the string version of the amount
		Input(const key::KeyPair& pair, const double amount) : Output(pair, amount), signature( key::signMessage(pair.pri, std::to_string(amount))) {}
		Input(const key::PublicKey& account, double amount, std::string signature) : Output(account, amount), signature(signature) {}
		Input(const key::PublicKey&& account, double amount, std::string signature) : Output(account, amount), signature(signature) {}
	};

	// Inputs to this transaction
	const std::vector<Input> inputs = {};
	// outputs from this transaction
	const std::vector<Output> outputs = {};

	// List of hashes of parent transactions
	const std::span<Hash> parentHashes;
	// The hash of this transaction
	Hash hash = INVALID_HASH;

	Transaction() = default;
	Transaction(const std::span<Hash> parentHashes, const std::vector<Input>& inputs, const std::vector<Output>& outputs, uint8_t difficulty = 3);

	Transaction(const Transaction& other) : timestamp(other.timestamp), inputs(other.inputs), outputs(other.outputs), parentHashes(other.parentHashes), hash(other.hash) { *this = other; }
	Transaction(const Transaction&& other) : timestamp(other.timestamp), inputs(other.inputs), outputs(other.outputs), parentHashes(other.parentHashes), hash(other.hash) { *this = std::move(other); }

	// Clean up the parent hashes so that we don't have a memory leak
	~Transaction(){ if(parentHashes.data()) delete [] parentHashes.data(); }

	Transaction& operator=(const Transaction& _new);
	Transaction& operator=(Transaction&& _new);

	void debugDump();

	bool validateTransactionMined();
	void mineTransaction();
	Hash hashTransaction() const;

	bool validateTransactionTotals() const;
	bool validateTransaction() const;
};

// De/serialization
breep::serializer& operator<<(breep::serializer& s, const Transaction& t);
breep::deserializer& operator>>(breep::deserializer& d, Transaction& t);

#endif /* end of include guard: TRANSACTION_HPP */
