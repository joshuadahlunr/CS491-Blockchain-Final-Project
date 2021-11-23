#ifndef GRAPH_H
#define GRAPH_H

#include <memory>
#include <exception>
#include <mutex>
#include <algorithm>
#include <list>
#include <queue>

#include "transaction.hpp"

struct Tangle;

// Transaction nodes act as a wrapper around transactions, providing graph connectivity information
struct TransactionNode : public Transaction, public std::enable_shared_from_this<TransactionNode> {
	// Smart pointer type of the node
	using ptr = std::shared_ptr<TransactionNode>;

	// Variable tracking weather or not this transaction is the genesis transaction
	const bool isGenesis = false; // TODO: should this go in the base transaction or here?
	// Immutable list of parents of the node
	const std::vector<TransactionNode::ptr> parents;
	// List of children of the node
	std::vector<TransactionNode::ptr> children;

	TransactionNode(const std::vector<TransactionNode::ptr> parents, const std::vector<Input>& inputs, const std::vector<Output>& outputs) :
		// Upon construction, construct the base transaction with the hashes of the parent nodes
		Transaction([](const std::vector<TransactionNode::ptr>& parents) -> std::vector<std::string> {
			std::vector<std::string> out;
			for(const TransactionNode::ptr& p: parents)
				out.push_back(p->hash);
			return out;
		}(parents), inputs, outputs), parents(parents) {}

	// Function which creates a node ptr
	static TransactionNode::ptr create(const std::vector<TransactionNode::ptr> parents, std::vector<Input> inputs, std::vector<Output> outputs) {
		return std::make_shared<TransactionNode>(parents, inputs, outputs);
	}

	// Create a transaction node, automatically mining and performing consensus on it
	static TransactionNode::ptr createAndMine(const Tangle& t, const std::vector<Input>& inputs, const std::vector<Output>& outputs);

	// Function which converts a transaction into a transaction node
	static TransactionNode::ptr create(const Tangle& t, const Transaction& trx);

	// Function which finds a node given its hash
	TransactionNode::ptr recursiveFind(Hash& hash) {
		// If our hash matches... return a smart pointer to ourselves
		if(this->hash == hash){
			// If we are the genesis node then the best we can do is convert the this pointer to a smart pointer
			if(parents.empty())
				return shared_from_this();
			// Otherwise... find the pointer to ourselves in the first parent's list of child pointers
			else for(const TransactionNode::ptr& parentsChild: parents[0]->children)
				if(parentsChild->hash == hash)
					return parentsChild;
		}

		// If our hash doesn't match check each child to see if it or its children contains the searched for hash
		for(const TransactionNode::ptr& child: children)
			if(auto recursiveResult = child->recursiveFind(hash); recursiveResult != nullptr)
				return recursiveResult;

		// If the hash doesn't exist in the children return a nullptr
		return nullptr;
	}

	// Function which recursively determines if the target is a child
	bool isChild(TransactionNode::ptr& target) const {
		for(auto& child: children){
			// If this child is the target then it is a child of this node
			if(child->hash == target->hash)
				return true;
			// Otherwise... look in the child's children
			else return child->isChild(target);
		}
		// If none of the children or their children are the target it is not a child of this node
		return false;
	}

	// Function which recursively prints out all of nodes in the graph
	void recursiveDebugDump(std::list<std::string>& foundNodes, size_t depth = 0) const {
		// Only print out information about a node if it hasn't already been printed
		if(std::find(foundNodes.begin(), foundNodes.end(), hash) != foundNodes.end()) return;

		std::cout << std::left << std::setw(5) << depth << std::string(depth + 1, ' ') << hash << " children: [ ";
		for(const TransactionNode::ptr& child: children)
			std::cout << child->hash << ", ";
		std::cout << "]" << std::endl;

		for(const TransactionNode::ptr& child: children)
			child->recursiveDebugDump(foundNodes, depth + 1);

		foundNodes.push_back(hash);
	}

	// Function which converts the tangle into a list
	void recursivelyListTransactions(std::list<Transaction*>& transactions){
		Transaction* self = this;
		// We only care about a node if it isn't already in the list
		if(std::search(transactions.begin(), transactions.end(), &self, (&self) + 1, [](Transaction* a, Transaction* b){
			return a->hash == b->hash;
		}) != transactions.end()) return;

		// Add us to the list
		transactions.push_back(self);
		// Add our children to the list
		for(auto& child: children)
			child->recursivelyListTransactions(transactions);
	}

	// -- Consensus Functions


	// Function which recursively calculates the weight of a transaction
	size_t cumulativeWeight() const {
		// Tips have weight 1
		if(children.empty())
			return 1;

		size_t sum = 1;
		for(const TransactionNode::ptr& child: children)
			sum += child->cumulativeWeight();

		return sum;
	}

	// Function which performs a biased random walk starting from the current node, and returns the tip it discovers
	TransactionNode::ptr biasedRandomWalk(float alpha = 1.0) {
		// TODO: Whitepaper has more info on alpha

		// Seed random number generator
		static CryptoPP::AutoSeededRandomPool rng;

		// If we are a tip get the shared pointer referencing us
		if(children.empty())
			return shared_from_this();

		// Create a weighted list of children
		std::list<TransactionNode*> weightedList;
		for(TransactionNode::ptr& child: children){
			auto weight = child->cumulativeWeight();
			// Add weight * alpha copies of the child to the list
			for(size_t i = 0; i < size_t(std::min(weight * alpha, 1.0f)); i++)
				weightedList.push_back(child.get());
		}

		// Randomly chose a child from the weighted list
		auto index = rng.GenerateWord32(0, weightedList.size() - 1); // Inclusive generation so we must decrease the size of the list by 1
		auto chosen = weightedList.begin();
		for(size_t i = 0; i < index; i++) chosen++;

		// Recursively walk down the chosen child
		return (*chosen)->biasedRandomWalk(alpha);
	}

	// Function which determines how confident the network is in a transaction
	float confirmationConfidence() {
		uint8_t confidence = 0;
		for(size_t i = 0; i < 100; i++){
			auto tip = biasedRandomWalk();
			if(isChild(tip))
				confidence++;
		}

		// Convert the confidence to a fraction in the range [0, 1]
		return confidence / 100.0;
	}
};

// Class holding the graph which represents our local Tangle
struct Tangle {
	// Exception thrown when a node can't be found in the graph
	struct NodeNotFoundException : public std::runtime_error { NodeNotFoundException(Hash hash) : std::runtime_error("Failed to find node with hash `" + hash + "`") {} };
	// Exception thrown when an invalid balance is detected
	struct InvalidBalance : public std::runtime_error {
		TransactionNode::ptr node;
		const key::PublicKey& account;
		InvalidBalance(TransactionNode::ptr node, const key::PublicKey& account, double balance) : std::runtime_error("Node with hash `" + node->hash + "` results in a balance of `" + std::to_string(balance) + "` for an account."), node(node), account(account) {}
	};

protected:
	// Pointer to the Genesis block
	const TransactionNode::ptr genesis;
	// Mutex used to synchronize modifications across threads
	std::mutex mutex;

public:

	// Upon creation generate a genesis block
	Tangle() : genesis([]() -> TransactionNode::ptr {
		std::vector<TransactionNode::ptr> parents;
		std::vector<Transaction::Input> inputs;
		std::vector<Transaction::Output> outputs;
		return std::make_shared<TransactionNode>(parents, inputs, outputs);
	}()) {}

	// Clean up the graph, in memory, on exit
	~Tangle() {
		// Repeatedly remove tips until the genesis node is the only node left in the graph
		while(!genesis->children.empty())
			for(auto tip: getTips())
				removeTip(tip);
	}

	// Function which sets the genesis node
	void setGenesis(TransactionNode::ptr genesis){
		// Mark this old node as the genesis
		util::makeMutable(genesis->isGenesis) = true;

		// Free the memory for every child of the old genesis (if it exists)
		if(this->genesis)
			while(!this->genesis->children.empty())
				for(auto& tip: getTips())
					removeTip(tip);

		// Update the genesis
		util::makeMutable(this->genesis) = genesis;
	}

	// Function which finds a node in the graph given its hash
	TransactionNode::ptr find(Hash hash) const {
		return genesis->recursiveFind(hash);
	}

	// Function which performs a biased random walk on the tangle
	TransactionNode::ptr biasedRandomWalk() const {
		return genesis->biasedRandomWalk();
	}

	// Function which adds a node to the tangle
	Hash add(const TransactionNode::ptr node){
		// Ensure that the transaction passes verification
		if(!node->validateTransaction())
			throw std::runtime_error("Transaction with hash `" + node->hash + "` failed to pass validation, discarding.");
		// Ensure that the inputs are greater than or equal to the outputs
		if(!node->validateTransactionTotals())
			throw std::runtime_error("Transaction with hash `" + node->hash + "` tried to generate something from nothing, discarding.");
		// Ensure that the inputs are greater than or equal to the outputs
		if(!node->validateTransactionMined())
			throw std::runtime_error("Transaction with hash `" + node->hash + "` wasn't mined, discarding.");

		// Validate that the inputs to this transaction do not cause their owner's balance to go into the negatives
		std::vector<std::pair<key::PublicKey, double>> balanceMap; // List acting as a bootleg map of keys to balances
		for(const Transaction::Input& input: node->inputs){
			// The account's balance is invalid
			double balance = -1;

			// If the account's balance is cached... use the cached balance
			int i = 0;
			for(auto& [account, bal]: balanceMap){
				i++;
				if(account == input.account){
					balance = bal;
					break;
				}
			}
			// Otherwise... query its balance
			if(balance < 0) balance = queryBalance(input.account);

			// Subtace the input from the balance and ensure it doesn't cause the transaction to go into the negatives
			balance -= input.amount;
			if(balance < 0)
				throw InvalidBalance(node, input.account, balance);

			// Cache the balance (adding to the list if not already present)
			if(i == balanceMap.size())
				balanceMap.emplace_back(input.account, balance);
			else balanceMap[i].second = balance;
		}


		// For each parent of the new node... preform error validation
		for(const TransactionNode::ptr& parent: node->parents) {
			// Make sure the parent is in the graph
			if(!find(parent->hash))
				throw NodeNotFoundException(parent->hash);

			// Make sure the node isn't already a child of the parent
			for(const TransactionNode::ptr& child: parent->children)
				if(child->hash == node->hash)
					throw std::runtime_error("Transaction with hash `" + parent->hash + "` already has a child with hash `" + node->hash + "`");
		}

		{ // Begin Critical Region
			std::scoped_lock lock(mutex);

			// For each parent of the new node... add the node as a child of that parent
			// NOTE: this happens in a second loop since we need to ensure all of the parents are valid before we add the node as a child of any of them
			for(const TransactionNode::ptr& parent: node->parents)
				parent->children.push_back(node);
		} // End Critical Region

		// Return the hash of the node
		return node->hash;
	}

	// Function which removes a node from the graph (can only remove tips, nodes with no children)
	void removeTip(TransactionNode::ptr& node){
		// Ensure the node is in the graph
		if(!find(node->hash))
			throw NodeNotFoundException(node->hash);

		// Ensure the node doesn't have any children (is a tip)
		if(!node->children.empty())
			throw std::runtime_error("Only tip nodes can be removed from the graph. Tried to remove non-tip with hash `" + node->hash + "`");

		{ // Begin Critical Region
			std::scoped_lock lock(mutex);

			// Remove the node as a child from each of its parents
			for(const TransactionNode::ptr& parent: node->parents)
				std::erase(parent->children, node);
		} // End Critical Region

		// Nulify the passed in reference to the node
		node.reset((TransactionNode*) nullptr);
	}

	// Function which finds all of the tip nodes in the graph
	std::vector<TransactionNode::ptr> getTips() const {
		std::vector<TransactionNode::ptr> out;
		recursiveGetTips(genesis, out);
		return out;
	}

	// Function which queries the balance currently associated with a given key
	double queryBalance(const key::PublicKey& account) const {
		std::list<std::string> foundNodes;
		std::queue<TransactionNode::ptr> q;
		double balance = 0;

		q.push(genesis);
		TransactionNode::ptr head;
		while(!q.empty()){
			head = q.front();
			q.pop();

			// Don't count the same transaction more than once in the balance
			if(std::find(foundNodes.begin(), foundNodes.end(), head->hash) != foundNodes.end()) continue;

			// Add up how this transaction takes away from the balance of interest
			for(const Transaction::Input& input: head->inputs)
				if(input.account == account)
					balance -= input.amount;

			// If the balance becomes negative except
			if(balance < 0)
				throw InvalidBalance(head, account, balance);

			// Add up how this transaction adds to the balance of interest
			for(const Transaction::Output& output: head->outputs)
				if(output.account == account)
					balance += output.amount;

			// Add the children to the queue
			for(auto& child: head->children)
				q.push(child);
			// Mark ourselves as visited
			foundNodes.push_back(head->hash);
		}

		return balance;
	}
	double queryBalance(const key::KeyPair& pair) const { return queryBalance(pair.pub); }

	// Function which prints out the tangle
	void debugDump() const {
		std::cout << "Genesis: " << std::endl;
		std::list<std::string> foundNodes;
		genesis->recursiveDebugDump(foundNodes);
	}

	// Function which lists all of the transactions in the tangle
	std::list<Transaction*> listTransactions(){
		std::list<Transaction*> out;
		genesis->recursivelyListTransactions(out);

		return out;
	}


protected:
	// Helper function which recursively finds all of the tips in the graph
	void recursiveGetTips(const TransactionNode::ptr& head, std::vector<TransactionNode::ptr>& tips) const {
		// If the node has no children, it is a tip and should be added to the list of tips (if not already in the list of tips)
		if(head->children.empty() && std::search(tips.begin(), tips.end(), &head, (&head) + 1, [](const TransactionNode::ptr& a, const TransactionNode::ptr& b) {
			return a->hash == b->hash;
		}) == tips.end())
			tips.push_back(head);
		// Otherwise, recursively consider the node's children
		else for(const TransactionNode::ptr& child: head->children)
			recursiveGetTips(child, tips);
	}
};

#endif /* end of include guard: GRAPH_H */
