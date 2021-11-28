/**
 * @file tangle.cpp
 * @author Joshua Dahl (jdahl@unr.edu)
 * @brief Code backing tangle.hpp
 * @version 0.1
 * @date 2021-11-29
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#include "tangle.hpp"

#include <thread>

#include <cryptopp/osrng.h>

/**
 * @brief Creates a transaction node from a pointers to parents, inputs, outputs, and mining difficulty
 *
 * @param parents - List of pointers to parents
 * @param inputs - List of Transaction::Inputs
 * @param outputs - List of Transaction::Outputs
 * @param difficulty - The difficulty of mining this transaction
 */
TransactionNode::TransactionNode(const std::vector<TransactionNode::const_ptr> parents, const std::vector<Input>& inputs, const std::vector<Output>& outputs, uint8_t difficulty /*= 3*/) :
	// Construct the base transaction with the hashes of the parent nodes
	Transaction([](const std::vector<TransactionNode::const_ptr>& parents) -> std::vector<std::string> {
		// Make sure the node has no duplicate parents listed (comparing hashes)
		util::removeDuplicates(util::mutable_cast(parents), [](const TransactionNode::const_ptr& a, const TransactionNode::const_ptr& b){
			return a->hash == b->hash;
		});

		// Create the list of parentHashes from the list of parents
		std::vector<std::string> out;
		for(const TransactionNode::const_ptr& p: parents)
			out.push_back(p->hash);
		return out;
	}(parents), inputs, outputs, difficulty), parents(parents) { }

/**
 * @brief Function which converts a transaction into a transaction node
 * @note Requires a tangle and searches for nodes in that tangle
 *
 * @param t - The tangle to search for parents in
 * @param trx - The transaction to convert
 * @return TransactionNode::ptr - Pointer to the newly converted transaction
 */
TransactionNode::ptr TransactionNode::create(const Tangle& t, const Transaction& trx) {
	std::vector<TransactionNode::const_ptr> parents;
	for(Hash& hash: trx.parentHashes)
		if(TransactionNode::const_ptr parent = t.find(hash); parent)
			parents.push_back(parent);
		else throw Tangle::NodeNotFoundException(hash);

	auto out = create(parents, trx.inputs, trx.outputs, trx.miningDifficulty);
	util::mutable_cast(out->timestamp) = trx.timestamp;
	util::mutable_cast(out->nonce) = trx.nonce;
	util::mutable_cast(out->miningTarget) = trx.miningTarget;
	util::mutable_cast(out->hash) = out->hashTransaction(); // Rehash since the timestamp and nonce have been overridden

	return out;
}

/**
 * @brief Create a transaction node, automatically mining and performing (G-IOTA) consensus on it
 * @note When this transaction is added to the tangle, verification of the transaction will automatically be preformed
 * @note G-IOTA DOI: 10.1109/INFCOMW.2019.8845163
 */
TransactionNode::ptr TransactionNode::createAndMine(const Tangle& t, const std::vector<Transaction::Input>& inputs, const std::vector<Transaction::Output>& outputs, uint8_t difficulty /*= 3*/){
	// Select two different (unless there is only 1) tips at random
	std::vector<TransactionNode::const_ptr> parents;
	parents.push_back(t.biasedRandomWalk()); // Tip1 = front
	parents.push_back(t.biasedRandomWalk()); // Tip2 = back
	// 256 tries to find a different tip before giving up
	for(auto [counter, tipCount] = std::make_pair(uint8_t(1), t.tips.read_lock()->size());
	  tipCount > 1 && parents.front() == parents.back() && counter != 0; counter++)
		parents.back() = t.biasedRandomWalk();

	if(!parents.front() || !parents.back()) throw std::runtime_error("Failed to find a tip!");

	// Calculate the (truncated) average height of our chosen parents
	size_t avgHeight = 0;
	for(auto& parent: parents)
		avgHeight += parent->height();
	avgHeight /= parents.size();

	// If we can find a tip whose height (longest path to genesis) qualifies it as left behind, also add it as a parent
	for(auto [i, tipLock] = std::make_pair(size_t(0), util::mutable_cast(t.tips).read_lock()); i < tipLock->size(); i++)
		if(tipLock[i]->height() <= avgHeight - LEFT_BEHIND_TIP_THRESHOLD){
			parents.push_back(tipLock[i]);
			break;
		}

	// Ensure that each node only appears once in the list of parents
	util::removeDuplicates(parents);

	// Create and mine the transaction
	TransactionNode::ptr trx = TransactionNode::create(parents, inputs, outputs, difficulty);
	trx->mineTransaction();
	return trx;
}

/**
 * @brief Function which dumps the metrics added over top a base transaction
 */
void TransactionNode::debugDump() {
	Transaction::debugDump();

	std::cout << "Is Genesis? " << (isGenesis ? "True" : "False")  << std::endl;
	std::cout << "Weight: " << ownWeight() << std::endl;
	std::cout << "Cumulative weight: " << cumulativeWeight << std::endl;
	std::cout << "Height: " << height() << std::endl;
	std::cout << "Depth: " << depth() << std::endl;
	std::cout << "Confidence: " << (confirmationConfidence() * 100) << "%" << std::endl;
}

/**
 * @brief Function which finds a given node in the tangle given its hash
 *
 * @param hash - The hash to search for
 * @return TransactionNode::const_ptr - The discovered node or nullptr if not found
 */
TransactionNode::const_ptr TransactionNode::find(Hash& hash) const {
	// Create a queue starting from this
	std::queue<TransactionNode::const_ptr> q; q.push(shared_from_this());
	std::list<std::string> considered;

	// While the queue isn't empty, pop each element off and...
	while(!q.empty()){
		auto head = q.front();
		q.pop();
		if(!head) continue;

		// If the hash matches return the current head
		if(head->hash == hash) return head;

		// If the node is the genesis node, its parent hashes include a list of hashes it is aliasing
		if(head->isGenesis)
			for(auto& h: head->parentHashes)
				if(h == hash)
					return head;

		// Add this node's children unless we have already considered them
		auto lock = head->children.read_lock();
		for(size_t i = 0, size = lock->size(); i < size; i++)
			if(auto child = lock[i]; std::find(considered.begin(), considered.end(), child->hash) == considered.end()){
				q.push(child);
				considered.push_back(child->hash);
			}
	}

	return nullptr;
}

/**
 * @brief Function which finds a given node in the tangle given its hash
 * @note Non-const version
 *
 * @param hash - The hash to search for
 * @return TransactionNode::const_ptr - The discovered node or nullptr if not found
 */
TransactionNode::ptr TransactionNode::find(Hash& hash) {
	// Create a queue starting from this
	std::queue<TransactionNode::ptr> q; q.push(shared_from_this());
	std::list<std::string> considered;

	// While the queue isn't empty, pop each element off and...
	while(!q.empty()){
		auto head = q.front();
		q.pop();
		if(!head) continue;

		// If the hash matches return the current head
		if(head->hash == hash) return head;

		// If the node is the genesis node, its parent hashes include a list of hashes it is aliasing
		if(head->isGenesis)
			for(auto& h: head->parentHashes)
				if(h == hash)
					return head;

		// Add this node's children unless we have already considered them
		auto lock = head->children.read_lock();
		for(size_t i = 0, size = lock->size(); i < size; i++)
			if(auto child = lock[i]; std::find(considered.begin(), considered.end(), child->hash) == considered.end()){
				q.push(child);
				considered.push_back(child->hash);
			}
	}

	return nullptr;
}

/**
 * @brief Function which recursively prints out all of nodes in the graph
 *
 * @param considered - List of nodes that have already been considered
 * @param height - The height of the current node
 */
void TransactionNode::recursiveDebugDump(std::list<std::string>& considered, size_t height /*= 0*/) const {
	// Only print out information about a node if it hasn't already been printed
	if(std::find(considered.begin(), considered.end(), hash) != considered.end()) return;

	std::cout << std::left << std::setw(5) << height << std::string(height + 1, ' ') << hash << " children: [ ";
	{
		auto lock = children.read_lock();
		for(size_t i = 0; i < children.unsafe().size(); i++)
			std::cout << children.unsafe()[i]->hash << ", ";
	}
	std::cout << "]" << std::endl;

	for(size_t i = 0; i < children.unsafe().size(); i++)
		children.unsafe()[i]->recursiveDebugDump(considered, height + 1);

	considered.push_back(hash);
}

/**
 * @brief Function which converts the tangle into a list
 *
 * @param transactions - The list the tangle is converted into
 */
void TransactionNode::recursivelyListTransactions(std::list<TransactionNode*>& transactions){
	// We only care about a node if it isn't already in the list
	if(util::contains(transactions.begin(), transactions.end(), this, [](TransactionNode* a, TransactionNode* b){
		if(!a || !b) return false;
		return a->hash == b->hash;
	})) return;

	// Add us to the list
	transactions.push_back(this);
	// Add our children to the list
	for(size_t i = 0; i < children.read_lock()->size(); i++)
		children.read_lock()[i]->recursivelyListTransactions(transactions);
}


// -- TransactionNode Consensus Functions --


/**
 * @brief Function which calculates the height (longest path to genesis) of the transaction
 *
 * @return size_t - The height
 */
size_t TransactionNode::height() const {
	if(isGenesis) return 0;

	size_t max = 0;
	for(size_t i = 0; i < parents.size(); i++)
		max = std::max(parents[i]->height(), max);

	return max + 1;
}

/**
 * @brief Function which calculates the depth (longest path to tip) of the transaction
 *
 * @return size_t - The depth
 */
size_t TransactionNode::depth() const {
	auto childLock = children.read_lock();
	if(childLock->empty()) return 0;

	size_t max = 0;
	for(size_t i = 0; i < childLock->size(); i++)
		max = std::max(childLock[i]->depth(), max);

	return max + 1;
}

/**
 * @brief Function which performs a biased random walk starting from the current node, and returns the tip it discovers
 *
 * @param alpha - Tradeoff between randomness and weight, low values are completely random, high values are completely based on weight differences
 * @return TransactionNode::const_ptr - The tip this walk results in
 */
TransactionNode::const_ptr TransactionNode::biasedRandomWalk(double alpha /*= 10*/) const {
	// Seed random number generator
	CryptoPP::AutoSeededRandomPool rng;
	// (Read) Lock our children
	auto lock = children.read_lock();

	// If we are a tip, get the shared pointer referencing us
	if(lock->empty())
		return shared_from_this();

	// Variable that stores the generated weighted list
	std::vector<std::pair<TransactionNode::const_ptr, double>> weightedList(lock->size());
	// Variable that stores the total weight of the list
	double totalWeight = 0;

	// Create a weighted list of children
	for(size_t i = 0; i < weightedList.size(); i++){
		TransactionNode::const_ptr child = lock[i];
		double weight = std::max( std::exp(-alpha * (cumulativeWeight - child->cumulativeWeight)), std::numeric_limits<double>::min() );
		weightedList[i] = {child, weight};
		totalWeight += weight;
	}

	// Randomly choose a child from the weighted list
	double random = util::rand2double(rng.GenerateWord32(), rng.GenerateWord32()) * totalWeight;
	auto chosen = weightedList.begin();
	for(double w = 0; w <= random && chosen != weightedList.end(); w += chosen->second)
		if(w > 0) chosen++;

	// Recursively walk down the chosen child
	return chosen->first->biasedRandomWalk(alpha);
}

/**
 * @brief Function which determines how confident the network is in a transaction
 *
 * @return float - Confidence between [0, 1]
 */
float TransactionNode::confirmationConfidence() const {
	// Generates a list of all parents going <levels> deep (if able)
	auto generateWalkSet = [this](size_t levels = 5) -> std::list<TransactionNode::const_ptr>{
		auto self = shared_from_this();

		std::unordered_set<TransactionNode::const_ptr> set;
		{
			auto childLock = children.read_lock();
			for(size_t i = 0; i < childLock->size(); i++)
				set.insert(childLock[i]);
			if(set.empty()) set.insert(self); // Add us to the set if we have no children
			else levels++; // Otherwise add one to levels to compensate for starting at level -1

			// For each level deep we are going...
			for(int i = 0; i < levels; i++)
				// For each node in the set...
				for(auto& node: set)
					// Add its parents to the set
					if(!node->isGenesis)
						for(auto& parent: node->parents)
							set.insert(parent);

			// Remove our children from the set
			for(size_t i = 0; i < childLock->size(); i++)
				set.erase(childLock[i]);
		}
		// Remove ourself from the set
		set.erase(self);

		return { set.begin(), set.end() };
	};

	// Merges two lists together
	auto merge = [](std::list<TransactionNode::const_ptr>& a, std::list<TransactionNode::const_ptr> b){ // The second is a copy so that we duplicate the state of the list
		a.insert(a.begin(), b.begin(), b.end());
	};

	// Generate an at least 100 element long list of nodes to walk from
	std::list<TransactionNode::const_ptr> walkList = generateWalkSet();
	if(walkList.empty()) return 0; // If the walk list is empty, we have no confidence in the node
	while(walkList.size() < 100)
		merge(walkList, walkList);


	// Count the number of random walks from the set that result in a tip that aproves this node
	uint8_t confidence = 0;
	for(TransactionNode::const_ptr base: walkList){
		if(auto tip = base->biasedRandomWalk(); tip && isChild(tip))
			confidence++;
	}

	// Convert the confidence to a fraction in the range [0, 1]
	return confidence / float(walkList.size());
}


// -- Tangle --


/**
 * @brief Function which sets the genesis node, and cleans up the memory of the old genesis
 *
 * @param genesis - The new genesis
 */
void Tangle::setGenesis(TransactionNode::ptr genesis){
	// Mark the new node as the genesis
	if(genesis) util::mutable_cast(genesis->isGenesis) = true;

	// Free the memory for every child of the old genesis (if it exists)
	if(this->genesis)
		for(auto lock = this->genesis->children.read_lock(); !lock->empty(); )
			for(auto [i, tipsLock] = std::make_pair(size_t(0), util::mutable_cast(tips).read_lock()); i < tipsLock->size(); i++)
				removeTip(tipsLock[0]);

	// Update the genesis
	util::mutable_cast(this->genesis) = genesis;

	// If we are updating weights... start updating weights
	if(updateWeights) std::thread([this](){
		updateCumulativeWeights(this->genesis);
	}).detach();
}

//
/**
 * @brief Function which adds a node to the tangle, validates that the node is acceptable before adding it
 *
 * @param node - The node to add
 * @return Hash - Hash of the node once added
 */
Hash Tangle::add(const TransactionNode::ptr node){
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
		auto inputAccount = input.account();
		// The account's balance is invalid
		double balance = -1;

		// If the account's balance is cached... use the cached balance
		int i = 0;
		for(auto& [account, bal]: balanceMap){
			i++;
			if(account == inputAccount){
				balance = bal;
				break;
			}
		}
		// Otherwise... query its balance
		if(balance < 0) balance = queryBalance(inputAccount);

		// Subtace the input from the balance and ensure it doesn't cause the transaction to go into the negatives
		balance -= input.amount;
		if(balance < 0)
			throw InvalidBalance(node, inputAccount, balance);

		// Cache the balance (adding to the list if not already present)
		if(i == balanceMap.size())
			balanceMap.emplace_back(inputAccount, balance);
		else balanceMap[i].second = balance;
	}


	// For each parent of the new node... preform error validation
	for(const TransactionNode::const_ptr& parent: node->parents) {
		// Make sure the parent is in the graph
		if(!find(parent->hash))
			throw NodeNotFoundException(parent->hash);

		// Make sure the node isn't already a child of the parent
		for(int i = 0; i < parent->children.read_lock()->size(); i++)
			if(parent->children.read_lock()[i]->hash == node->hash)
				throw std::runtime_error("Transaction with hash `" + parent->hash + "` already has a child with hash `" + node->hash + "`");
	}


	{ // Begin Critical Region
		std::scoped_lock lock(mutex);

		// For each parent of the new node...
		// NOTE: this happens in a second loop since we need to ensure all of the parents are valid before we add the node as a child of any of them
		for(const TransactionNode::const_ptr& parent: node->parents){
			// Remove the parent from the list of tips
			auto tipsLock = tips.write_lock();
			std::erase(*tipsLock, parent);

			// Add the node as a child of that parent
			find(parent->hash)->children->push_back(node);
			// Add the node to the list of tips
			tipsLock->push_back(node);
		}

		// Update the weights of all the nodes aproved by this node
		if(updateWeights) std::thread([this, node](){
			updateCumulativeWeights(node);
		}).detach();

		// Add the current tips as canidate to become a new genesis
		if (auto tipsLock = tips.read_lock(); tipsLock->size() <= GENESIS_CANDIDATE_THRESHOLD)
			genesisCandidates.push(*tipsLock);
	} // End Critical Region

	// Return the hash of the node
	return node->hash;
}

//
/**
 * @brief Function which removes a node from the graph (can only remove tips [nodes with no children])
 *
 * @param tip - The tip to remove
 */
void Tangle::removeTip(TransactionNode::const_ptr tip){
	// Make sure the pointer is valid
	if(!tip) return;

	// Ensure the node is in the graph
	if(!find(tip->hash))
		throw NodeNotFoundException(tip->hash);

	// Ensure the node doesn't have any children (is a tip)
	if(!tip->children.read_lock()->empty())
		throw std::runtime_error("Only tip nodes can be removed from the graph. Tried to remove non-tip with hash `" + tip->hash + "`");

	{ // Begin Critical Region
		std::scoped_lock lock(mutex);

		// Remove the node as a child from each of its parents
		for(size_t i = 0; i < tip->parents.size(); i++){
			auto& children = util::mutable_cast(tip->parents[i]->children.unsafe());
			std::erase(children, tip);

			// If the parent no longer has children, mark it as a tip
			if(children.empty())
				util::mutable_cast(tips.unsafe()).push_back(tip->parents[i]);
		}

		// Remove the node from the list of tips
		std::erase(util::mutable_cast(tips.unsafe()), tip);

		// Clear the list of parents
		util::mutable_cast(tip->parents).clear();
	} // End Critical Region

	// Nulify the passed in reference to the node
	tip.reset((TransactionNode*) nullptr);
}
 
/**
 * @brief Function which queries the balance of a given key only using transactions with a certain level of confidence
 * 
 * @param account - The account to calculate the balance
 * @param confidenceThreshold - (Optional) Confidence threshold the node must be above to be considered in the calculation
 * @return double - The account's balance
 */
double Tangle::queryBalance(const key::PublicKey& account, float confidenceThreshold /*= 0*/) const {
	std::list<std::string> considered;
	// The queue starts with the genesis
	std::queue<TransactionNode::ptr> q; q.push(genesis);
	double balance = 0;

	// While there are nodes left in the queue, pop the front...
	while(!q.empty()){
		TransactionNode::ptr head = q.front();
		q.pop();
		if(!head) continue;

		// Add up how this transaction takes away from the balance of interest
		for(const Transaction::Input& input: head->inputs)
			if(input.account() == account)
				balance -= input.amount;
		// If the balance becomes negative except
		if(balance < 0)
			throw InvalidBalance(head, account, balance);

		// Add up how this transaction adds to the balance of interest
		for(const Transaction::Output& output: head->outputs)
			if(output.account() == account)
				balance += output.amount;
		// If the balance becomes negative except
		if(balance < 0)
			throw InvalidBalance(head, account, balance);

		// Add the children to the queue (if they have sufficient confidence and/or haven't already been considered)
		{
			auto childLock = head->children.read_lock();
			for(int i = 0; i < childLock->size(); i++)
				if(std::find(considered.begin(), considered.end(), childLock[i]->hash) == considered.end()){
					if(confidenceThreshold < std::numeric_limits<float>::epsilon() // Only check the transaction's confidence if the threshold is greater than 0
					  || childLock[i]->confirmationConfidence() >= confidenceThreshold)
						q.push(childLock[i]);
					considered.push_back(childLock[i]->hash);
				}
		}
	}

	return balance;
}

/**
 * @brief Function which updates the weights of nodes working backwards from a <source> node
 * 
 * @param source The node to work backwards from
 */
void Tangle::updateCumulativeWeights(TransactionNode::const_ptr source){
	// Add the source node to the queue
	std::queue<TransactionNode::const_ptr> q; q.push(source);

	// While the queue is not empty, pop the head off...
	while(!q.empty()){
		auto head = q.front();
		q.pop();
		if(!head) continue;

		// Update the weight of this node based on the weights of the children
		float cumulativeWeight = head->ownWeight();
		for(size_t i = 0, size = head->children.read_lock()->size(); i < size; i++)
			cumulativeWeight += head->children.read_lock()[i]->cumulativeWeight;
		util::mutable_cast(head->cumulativeWeight) = cumulativeWeight;

		// Add this node's parents to the back of the queue
		for(auto& parent: head->parents)
			q.push(parent);
	}
}
