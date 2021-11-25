#include "tangle.hpp"

#define LEFT_BEHIND_TIP_DELTA 5

// Create a transaction node, automatically mining and performing (G-IOTA) consensus on it
// NOTE: when this transaction is added to the tangle, verification of the transaction will automatically be preformed
// NOTE: G-IOTA DOI: 10.1109/INFCOMW.2019.8845163
TransactionNode::ptr TransactionNode::createAndMine(const Tangle& t, const std::vector<Transaction::Input>& inputs, const std::vector<Transaction::Output>& outputs, uint8_t difficulty /*= 3*/){
	// Select two different (unless there is only 1) tips at random
	std::vector<TransactionNode::ptr> parents;
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

	{
		// If we can find a tip whoes height (longest path to genesis) qualifies it as left behind, also add it as a parent
		auto tipLock = t.tips.read_lock();
		for(int i = 0; i < tipLock->size(); i++)
			if(tipLock[i]->height() <= avgHeight - LEFT_BEHIND_TIP_DELTA){
				parents.push_back(tipLock[i]);
				break;
			}
	}

	// Ensure that each node only appears once in the list of parents
	util::removeDuplicates(parents);

	// Create and mine the transaction
	TransactionNode::ptr trx = TransactionNode::create(parents, inputs, outputs, difficulty);
	trx->mineTransaction();
	return trx;
}

// Function which converts a transaction into a transaction node
TransactionNode::ptr TransactionNode::create(const Tangle& t, const Transaction& trx) {
	std::vector<TransactionNode::ptr> parents;
	for(Hash& hash: trx.parentHashes)
		if(TransactionNode::ptr parent = t.find(hash); parent)
			parents.push_back(parent);
		else throw Tangle::NodeNotFoundException(hash);

	auto out = create(parents, trx.inputs, trx.outputs, trx.miningDifficulty);
	util::makeMutable(out->timestamp) = trx.timestamp;
	util::makeMutable(out->nonce) = trx.nonce;
	util::makeMutable(out->miningTarget) = trx.miningTarget;
	util::makeMutable(out->hash) = out->hashTransaction(); // Rehash since the timestamp and nonce have been overridden

	return out;
}
