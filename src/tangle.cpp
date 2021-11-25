#include "tangle.hpp"

// Create a transaction node, automatically mining and performing consensus on it
// NOTE: when this transaction is added to the tangle, verification of the transaction will automatically be preformed
TransactionNode::ptr TransactionNode::createAndMine(const Tangle& t, const std::vector<Transaction::Input>& inputs, const std::vector<Transaction::Output>& outputs, uint8_t difficulty /*= 3*/){
	size_t tipCount = t.getTips().size();

	// Select two different (unless there is only 1) tips at random
	auto tip1 = t.biasedRandomWalk();
	while(!tip1) tip1 = t.biasedRandomWalk(); // Make sure tip1 exists

	auto tip2 = t.biasedRandomWalk();
	while(tipCount > 2 && tip1 == tip2)
		tip2 = t.biasedRandomWalk();

	if(!tip1 || !tip2) throw std::runtime_error("Failed to find a tip!");

	// Create the transaction
	auto trx = TransactionNode::create({tip1, tip2}, inputs, outputs, difficulty);
	if(tipCount <= 2) trx = TransactionNode::create({tip1}, inputs, outputs, difficulty);
	// Mine the transaction
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
