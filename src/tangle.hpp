/**
 * @file tangle.hpp
 * @author Joshua Dahl (jdahl@unr.edu)
 * @brief File containing the base code for a tangle and the nodes which make up its graph
 * @version 0.1
 * @date 2021-11-29
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#ifndef TANGLE_HPP
#define TANGLE_HPP

#include <iostream>

#include "monitor.hpp"
#include "circular_buffer.hpp"

#include "transaction.hpp"

// The number of tips there can be at most in a given instant of time to qualify to be converted into a genesis
#define GENESIS_CANDIDATE_THRESHOLD 3
// How many levels behind the current tips a transaction needs to be before it is considered left behind
#define LEFT_BEHIND_TIP_THRESHOLD 5

// Tangle forward declaration
struct Tangle;

// Transaction nodes act as a wrapper around transactions, providing graph connectivity information
struct TransactionNode : public Transaction, public std::enable_shared_from_this<TransactionNode> {
	// Smart pointer type of the node
	using ptr = std::shared_ptr<TransactionNode>;
	using const_ptr = std::shared_ptr<const TransactionNode>;

	// Variable tracking the cumulative weight of this node
	const float cumulativeWeight = 0;
	// Variable tracking weather or not this transaction is the genesis transaction
	const bool isGenesis = false; // TODO: should this go in the base transaction or here?
	// Immutable list of parents of the node
	const std::vector<TransactionNode::const_ptr> parents;
	// List of children of the node, thread safe access
	monitor<std::vector<TransactionNode::ptr>> children;

	TransactionNode(const std::vector<TransactionNode::const_ptr> parents, const std::vector<Input>& inputs, const std::vector<Output>& outputs, uint8_t difficulty = 3);

	/**
	 * @brief Function which creates a pointer to a transaction node
	 * 
	 * @param parents - List of pointers to parents
	 * @param inputs - List of Transaction::Inputs
	 * @param outputs - List of Transaction::Outputs
	 * @param difficulty - The difficulty of mining this transaction
	 * @return TransactionNode::ptr - Pointer to the newly converted transaction
	 */
	inline static TransactionNode::ptr create(const std::vector<TransactionNode::const_ptr>& parents, std::vector<Input> inputs, std::vector<Output> outputs, uint8_t difficulty = 3) {
		return std::make_shared<TransactionNode>(parents, inputs, outputs, difficulty);
	}

	static TransactionNode::ptr create(const Tangle& t, const Transaction& trx);
	static TransactionNode::ptr createAndMine(const Tangle& t, const std::vector<Input>& inputs, const std::vector<Output>& outputs, uint8_t difficulty = 3);
	
	// Function which dumps the metrics added over top a base transaction
	void debugDump();

	TransactionNode::const_ptr find(Hash& hash) const;
	TransactionNode::ptr find(Hash& hash);

	/**
	 * @brief Function which recursively determines if the <target> is a child
	 * 
	 * @param target - The node determined to be a child
	 * @return True of the node is a child, false otherwise
	 */
	inline bool isChild(TransactionNode::const_ptr& target) const { return util::mutable_cast(this)->find(target->hash) != nullptr; }

	void recursiveDebugDump(std::list<std::string>& considered, size_t depth = 0) const;
	void recursivelyListTransactions(std::list<TransactionNode*>& transactions);


	// -- Consensus Functions


	/**
	 * @brief Function which calculates the weight of this transcation, based on its mining difficulty (capped at 1 when difficulty is 5)
	 * 
	 * @return float - The weight of this transaction in isolation
	 */
	inline float ownWeight() const { return std::min(miningDifficulty / 5.f, 1.f); }

	size_t height() const;
	size_t depth() const;

	TransactionNode::const_ptr biasedRandomWalk(double alpha = 10) const;
	float confirmationConfidence() const;
};

/**
 * @brief Class managing the graph which represents our local Tangle
 */
struct Tangle {
	/**
	 * @brief Exception thrown when a node can't be found in the graph
	 */
	struct NodeNotFoundException : public std::runtime_error { NodeNotFoundException(Hash hash) : std::runtime_error("Failed to find node with hash `" + hash + "`") {} };
	/**
	 * @brief Exception thrown when an invalid balance is detected
	 */
	struct InvalidBalance : public std::runtime_error {
		// Node which caused the invalid balance
		TransactionNode::const_ptr node;
		// Account with the invalid balance
		const key::PublicKey& account;
		InvalidBalance(TransactionNode::const_ptr node, const key::PublicKey& account, double balance) : std::runtime_error("Node with hash `" + node->hash + "` results in a balance of `" + std::to_string(balance) + "` for an account."), node(node), account(account) {}
	};

	// Pointer to the Genesis block
	const TransactionNode::ptr genesis;
	// List of tips, with thread safe access
	const monitor<std::vector<TransactionNode::const_ptr>> tips;

protected:
	// Mutex used to synchronize modifications across threads
	std::recursive_mutex mutex;

	// Flag which determines if a transaction add should recalculate weights or not
	bool updateWeights = true;

	// Circular buffer queue of size 10 of candidates to be converted into the genesis
	ModifiableQueue<std::vector<TransactionNode::const_ptr>, secure_circular_buffer_array<std::vector<TransactionNode::const_ptr>, 10>> genesisCandidates;

public:

	// Upon creation generate a genesis block
	Tangle() : genesis([]() -> TransactionNode::ptr {
		std::vector<TransactionNode::const_ptr> parents;
		std::vector<Transaction::Input> inputs;
		std::vector<Transaction::Output> outputs;
		return std::make_shared<TransactionNode>(parents, inputs, outputs);
	}()) {}

	// Clean up the graph, in memory, on exit
	~Tangle() { setGenesis(nullptr); }

	void setGenesis(TransactionNode::ptr genesis);

	/**
	 * @brief Function which finds a node in the graph given its hash
	 * 
	 * @param hash - The hash to search for
	 * @return TransactionNode::const_ptr - The discovered node or nullptr if not found
	 */
	inline TransactionNode::const_ptr find(Hash hash) const { return genesis->find(hash); }
	/**
	 * @brief Function which finds a node in the graph given its hash
	 * @note Non-const version
	 * 
	 * @param hash - The hash to search for
	 * @return TransactionNode::const_ptr - The discovered node or nullptr if not found
	 */
	inline TransactionNode::ptr find(Hash hash) { return genesis->find(hash); }

	// 
	/**
	 * @brief Function which performs a biased random walk on the tangle
	 * 
	 * @param alpha Tradeoff between randomness and weight, low values are completely random, high values are completely based on weight differences
 	 * @return TransactionNode::const_ptr - The tip this walk results in
	 */
	inline TransactionNode::const_ptr biasedRandomWalk(double alpha = 10) const { return genesis->biasedRandomWalk(alpha); }

	Hash add(const TransactionNode::ptr node);
	void removeTip(TransactionNode::const_ptr node);

	double queryBalance(const key::PublicKey& account, float confidenceThreshold = 0) const;
	inline double queryBalance(const key::KeyPair& pair, float confidenceThreshold = 0) const { return queryBalance(pair.pub, confidenceThreshold); }

	/**
	 * @brief Function which prints out the tangle
	 */
	void debugDump() const {
		std::cout << "Genesis: " << std::endl;
		std::list<std::string> considered;
		genesis->recursiveDebugDump(considered);
	}

	/**
	 * @brief Function which lists all of the transactions in the tangle
	 * 
	 * @return std::list<TransactionNode*> - The listed transactions in the tangle
	 */
	std::list<TransactionNode*> listTransactions(){
		std::list<TransactionNode*> out;
		genesis->recursivelyListTransactions(out);
		return out;
	}

protected:
	void updateCumulativeWeights(TransactionNode::const_ptr source);

	/**
	 * @brief Update the cumulative weight of each of the current tips
	 */
	void updateCumulativeWeights(){
		for(auto [i, tipLock] = std::make_pair(size_t(0), util::mutable_cast(tips).read_lock()); i < tipLock->size(); i++)
			updateCumulativeWeights(tipLock[i]);
	}

};

#endif /* end of include guard: TANGLE_HPP */
