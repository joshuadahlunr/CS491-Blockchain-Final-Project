#include "tangle.hpp"

// Function which converts a transaction into a transaction node
TransactionNode::ptr TransactionNode::create(Tangle& t, const Transaction& trx) {
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
