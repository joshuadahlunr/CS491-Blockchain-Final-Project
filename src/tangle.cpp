#include "tangle.hpp"

// Create a transaction node, automatically mining and performing consensus on it
// NOTE: when this transaction is added to the tangle, verification of the transaction will automatically be preformed
TransactionNode::ptr TransactionNode::createAndMine(const Tangle& t, const std::vector<Transaction::Input>& inputs, const std::vector<Transaction::Output>& outputs){
	// Select two different (unless there is only 1) tips at random
	auto tip1 = t.biasedRandomWalk();
	auto tip2 = t.biasedRandomWalk();
	while(t.getTips().size() > 1 && tip1 == tip2)
		tip2 = t.biasedRandomWalk();

	// Create the transaction
	auto trx = TransactionNode::create({tip1, tip2}, inputs, outputs);
	if(t.getTips().size() == 1) trx = TransactionNode::create({tip1}, inputs, outputs);
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

	auto out = create(parents, trx.inputs, trx.outputs);
	util::makeMutable(out->timestamp) = trx.timestamp;
	util::makeMutable(out->nonce) = trx.nonce;
	util::makeMutable(out->miningDifficulty) = trx.miningDifficulty;
	util::makeMutable(out->miningTarget) = trx.miningTarget;
	util::makeMutable(out->hash) = out->hashTransaction(); // Rehash since the timestamp and nonce have been overridden

	return out;
}
