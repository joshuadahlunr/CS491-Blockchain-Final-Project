#include "tangle.hpp"

// Function which converts a transaction into a transaction node
TransactionNode::ptr TransactionNode::create(Tangle& t, const Transaction& trx) {
	std::vector<TransactionNode::ptr> parents;
	for(Hash& hash: trx.parentHashes)
		if(TransactionNode::ptr parent = t.find(hash); parent)
			parents.push_back(parent);
		else throw Tangle::NodeNotFoundException(hash);

	auto out = create(parents, trx.inputs, trx.outputs);
	(*(int64_t*) &out->timestamp) = trx.timestamp;
	(*(std::string*) &out->hash) = out->hashTransaction(); // Rehash since the timestamp has been overridden

	return out;
}
