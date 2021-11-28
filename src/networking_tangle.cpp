/**
 * @file networking_tangle.cpp
 * @author Joshua Dahl (jdahl@unr.edu)
 * @brief The code backing the tangle half of networking.hpp
 * @version 0.1
 * @date 2021-11-28
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#include "networking.hpp"

/**
 * @brief Constructor that links the network, connects network listeners, and sets up the network queue 
 * @param network The network this tangle is connected to
 */
NetworkedTangle::NetworkedTangle(breep::tcp::network& network) : network(network) {
    // Make sure that the network queue has some memory backing it
    networkAdditionQueue.getContainer().resize(NETWORK_QUEUE_MIN_SIZE);

    // Listen to dis/connection events
    auto connect_disconnectListenerClosure = [this] (breep::tcp::network& network, const breep::tcp::peer& peer) -> void {
        this->connect_disconnectListener(network, peer);
    };
    network.add_connection_listener(connect_disconnectListenerClosure);
    network.add_disconnection_listener(connect_disconnectListenerClosure);

    // Listen for public keys
    network.add_data_listener<PublicKeySyncResponse>([this] (breep::tcp::netdata_wrapper<PublicKeySyncResponse>& dw) -> void {
        PublicKeySyncResponse::listener(dw, *this);
    });
    network.add_data_listener<PublicKeySyncRequest>([this] (breep::tcp::netdata_wrapper<PublicKeySyncRequest>& dw) -> void {
        PublicKeySyncRequest::listener(dw, *this);
    });

    // Listen for synchronization requests
    network.add_data_listener<GenesisVoteRequest>([this] (breep::tcp::netdata_wrapper<GenesisVoteRequest>& dw) -> void {
        GenesisVoteRequest::listener(dw, *this);
    });
    network.add_data_listener<GenesisVoteResponse>([this] (breep::tcp::netdata_wrapper<GenesisVoteResponse>& dw) -> void {
        GenesisVoteResponse::listener(dw, *this);
    });
    network.add_data_listener<TangleSynchronizeRequest>([this] (breep::tcp::netdata_wrapper<TangleSynchronizeRequest>& dw) -> void {
        TangleSynchronizeRequest::listener(dw, *this);
    });
    network.add_data_listener<UpdateWeightsRequest>([this] (breep::tcp::netdata_wrapper<UpdateWeightsRequest>& dw) -> void {
        UpdateWeightsRequest::listener(dw, *this);
    });
    network.add_data_listener<SyncGenesisRequest>([this] (breep::tcp::netdata_wrapper<SyncGenesisRequest>& dw) -> void {
        SyncGenesisRequest::listener(dw, *this);
    });
    network.add_data_listener<SynchronizationAddTransactionRequest>([this] (breep::tcp::netdata_wrapper<SynchronizationAddTransactionRequest>& dw) -> void {
        SynchronizationAddTransactionRequest::listener(dw, *this);
    });

    // Listen for new transactions
    network.add_data_listener<AddTransactionRequest>([this] (breep::tcp::netdata_wrapper<AddTransactionRequest>& dw) -> void {
        AddTransactionRequest::listener(dw, *this);
    });
}

/**
 * @brief Function which sets the keypair
 * @param pair - New keys
 * @param networkSync - (optional) Whether or not we should sync the keys over the network
 */
void NetworkedTangle::setKeyPair(const std::shared_ptr<key::KeyPair>& pair, bool networkSync /*= true*/){
    util::mutable_cast(personalKeys) = pair;
    peerKeys[network.self().id()] = personalKeys->pub;
    if(networkSync) network.send_object(NetworkedTangle::PublicKeySyncResponse(*pair));
}

/**
 * @brief Function which finds a key given its hash
 * 
 * @param keyHash - The key to search for
 * @return const key::PublicKey& - The discovered key
 * @exception exception-object exception description
 */
const key::PublicKey& NetworkedTangle::findAccount(Hash keyHash) const {
    for(auto& [uuid, key]: peerKeys)
        if(key::hash(key) == keyHash)
            return key;

    throw InvalidAccount(keyHash);
}

/**
 * @brief Adds a new node to the tangle (network synced)
 * 
 * @param node - The node to add
 * @return Hash - The hash of the node if successfully added
 */
Hash NetworkedTangle::add(TransactionNode::ptr node){
    Hash out = Tangle::add(node);
    network.send_object(AddTransactionRequest(*node, *personalKeys)); // The add gets validated by the base tangle, if we get to this code (no exception) then the node is acceptable
    return out;
}

/**
 * @brief Function which creates the latest common genesis (node representing a set of what were once tips with 100% confidence)
 * @return TransactionNode::ptr - The generated genesis
 */
TransactionNode::ptr NetworkedTangle::createLatestCommonGenesis(){
    // If there are no genesis candidates our genesis is the latest common genesis
    if (genesisCandidates.empty()) return genesis;

    std::cout << "Genesis candidates found" << std::endl;

    // Look through the queue and find the latest candidate set of nodes with 100% confidence
    std::vector<TransactionNode::const_ptr>* _chosen = nullptr;
    for(auto& canidate: genesisCandidates.getContainer()){
        bool valid = true;
        for(auto& trx: canidate)
            if(trx->confirmationConfidence() < 1){
                valid = false;
                break;
            }

        if(valid)
            _chosen = &canidate;
    }

    // If we didn't find any canidates with 100% confidence, our genesis is the latest common genesis
    if(!_chosen) return genesis;
    std::cout << "Picked Genesis Nodes" << std::endl;

    std::vector<TransactionNode::const_ptr>& chosen = *_chosen;
    std::vector<TransactionNode::const_ptr> parents;
    std::vector<Transaction::Input> inputs;
    std::vector<Transaction::Output> outputs;

    // Lamba which calculates the balance of the given acount as seen by the chosen nodes
    auto reverseBalanceQuery = [&](const key::PublicKey& account){
        std::list<std::string> considered;
        double balance = 0;

        std::queue<TransactionNode::const_ptr> q;
        for(auto& c: chosen) q.push(c);

        while(!q.empty()){
            auto head = q.front();
            q.pop();
            if(!head) continue;

            // NOTE: the balances have already been validated going forward... assuming they are correct
            // Add up how this transaction takes away from the balance of interest
            for(const Transaction::Input& input: head->inputs)
                if(input.account() == account)
                    balance -= input.amount;
            // Add up how this transaction adds to the balance of interest
            for(const Transaction::Output& output: head->outputs)
                if(output.account() == account)
                    balance += output.amount;

            // Add all of the parents to the queue if they weren't already there
            for(auto& parent: head->parents)
                if(std::find(considered.begin(), considered.end(), parent->hash) == considered.end()){
                    q.push(parent);
                    considered.push_back(parent->hash);
                }
        }

        return balance;
    };

    // Lambda which generates a list of every account refernced in the tangle
    auto listAccounts = [&](){
        std::list<std::string> considered;
        std::list<key::PublicKey> out;

        std::queue<TransactionNode::const_ptr> q;
        q.push(genesis);

        while(!q.empty()){
            auto head = q.front();
            q.pop();
            if(!head) continue;

            // Find all of the accounts referenced in this transaction and add them to the output list (if they aren't already there)
            for(const Transaction::Input& input: head->inputs)
                if(auto account = input.account(); std::find(out.begin(), out.end(), account) == out.end())
                    out.push_back(account);
            // Add up how this transaction adds to the balance of interest
            for(const Transaction::Output& output: head->outputs)
                if(auto account = output.account(); std::find(out.begin(), out.end(), account) == out.end())
                    out.push_back(account);

            // Determine if this node is one of the chosen nodes
            bool isChosen = false;
            for(auto& c: chosen)
                if(head->hash == c->hash){
                    isChosen = true;
                    break;
                }

            // Add this node's children unless we have already considered them (making sure we don't go past the chosen nodes)
            if(!isChosen) {
                auto lock = head->children.read_lock();
                for(size_t i = 0, size = lock->size(); i < size; i++)
                    if(auto child = lock[i]; std::find(considered.begin(), considered.end(), child->hash) == considered.end()){
                        q.push(child);
                        considered.push_back(child->hash);
                    }
            }
        }

        return out;
    };

    // Calculate the balance of every peer referenced in an account before the chosen nodes, and add that balance as an output of the genesis
    for(auto accounts = listAccounts(); auto& account: accounts)
        outputs.emplace_back(account, reverseBalanceQuery(account));

    std::cout << "Tabulated account balances" << std::endl;

    // Create a new transaction and set its hash to the hash of the first chosen node
    auto trx = TransactionNode::create(parents, inputs, outputs);
    util::mutable_cast(trx->hash) = chosen[0]->hash;

    // Fill the transaction's parent hashes with the remaining hashes of the chosen nodes
    auto& parentHashes = util::mutable_cast(trx->parentHashes);
    delete [] parentHashes.data(); // Free the current parent hashes
    parentHashes = {new Hash[chosen.size() - 1], chosen.size() - 1}; // Create new memory to back the parent hashes
    for(int i = 1; i < chosen.size(); i++)
        util::mutable_cast(parentHashes[i]) = chosen[i]->hash;

    return trx;
}

/**
 * @brief // Function which prunes the tangle, it finds the latest common genesis and removes all nodes before it
 */
void NetworkedTangle::prune(){
    // Generate the new latest common genesis
    auto genesis = createLatestCommonGenesis();

    // Cache a copy of the current tips and then clear the tangle's copy
    auto originalTips = tips.unsafe();
    util::mutable_cast(tips)->clear();

    {
        // Find all of the children of the nodes which were merged together into the new genesis node
        std::vector<TransactionNode::ptr> children;
        {
            auto node = find(genesis->hash);
            auto lock = node->children.write_lock();
            children = std::move(*lock);
            lock->clear(); // Clear the list in the tree so they don't get pruned when we swap out genesises

            // Clear the node's parent's children and mark it as a tip
            for(auto& parent: node->parents){
                util::mutable_cast(parent->children)->clear(); // Clear out the children of the node's parent
                util::mutable_cast(tips)->push_back(parent); // Mark the now childless parent as a tip
            }
        }
        for(auto& hash: genesis->parentHashes){
            auto node = find(hash);
            auto lock = node->children.write_lock();
            children = std::move(*lock);
            lock->clear(); // Clear the list in the tree so they don't get pruned when we swap out genesises

            // Clear the node's parent's children and mark it as a tip
            for(auto& parent: node->parents){
                util::mutable_cast(parent->children)->clear(); // Clear out the children of the node's parent
                util::mutable_cast(tips)->push_back(parent); // Mark the now childless parent as a tip
            }
        }

        // Ensure there are no duplicate children or tips
        util::removeDuplicates(children);
        util::removeDuplicates(util::mutable_cast(tips.unsafe()));

        // Move the list of children into the new genesis
        *genesis->children.write_lock() = std::move(children);

        // Update the children to point to the new genesis
        auto lock = genesis->children.write_lock();
        for(size_t i = 0; i < lock->size(); i++)
            util::mutable_cast(lock[i]->parents) = { genesis };
    }
    std::cout << "Situated children" << std::endl;

    // Update the tangle's genesis (removes all the nodes up to the temporary list of tips)
    setGenesis(genesis);

    // Restore the original list of tips
    *util::mutable_cast(tips.write_lock()) = originalTips;
}

/**
 * @brief Function which saves a tangle to a file (or aarbitrary output stream)
 * @param out - Output stream to save the file to
 */
void NetworkedTangle::saveTangle(std::ostream& out) {
    // List all of the transactions in the tangle
    std::list<TransactionNode*> transactions = listTransactions();
    // Sort them according to time, ensuring that the genesis remains at the front of the list
    Transaction* genesis = transactions.front();
    transactions.sort([genesis](Transaction* a, Transaction* b){
        if(a->hash == genesis->hash) return true;
        if(b->hash == genesis->hash) return false;
        return a->timestamp < b->timestamp;
    });

    // Serialize the number of transactions
    breep::serializer s;
    s << transactions.size();

    // Serialize each of the transactions
    for(Transaction* _t: transactions){
        const Transaction& t = *_t;
        s << t;
    }

    // Compress the serialized data
    auto raw = s.str();
    std::string compressed = util::compress(*(std::string*) &raw);

    // Write the serialized data to the file
    out.write(reinterpret_cast<char*>(compressed.data()), compressed.size());
}

/**
 * @brief Function which loads a tangle from a file (or arbitray input stream)
 * @param in - Input stream to load tangle from
 * @param size - The number of bytes of tangle to load
 */
void NetworkedTangle::loadTangle(std::istream& in, size_t size) {
    // Read the compressed data from the file
    std::string compressed;
    compressed.resize(size);
    in.read(&compressed[0], compressed.size());

    // Decompress and create a deserializer
    compressed = util::decompress(compressed);
    std::basic_string<unsigned char> raw = *(std::basic_string<unsigned char>*) &compressed;
    breep::deserializer d(raw);

    // Determine how many transactions there are to read
    size_t transactionCount;
    d >> transactionCount;

    // The genesis is always the first transaction in the file
    Transaction trx;
    d >> trx;
    genesisSyncExpectedHash = trx.hash; // Flag us as prepared to receive a new genesis
    network.send_object_to_self(SyncGenesisRequest(trx, *personalKeys));

    // Read in each transaction from the deserializer and then add it to the tangle
    for(int i = 0; i < transactionCount - 1; i++) { // Minus 1 since we already synced the genesis
        d >> trx;
        network.send_object_to_self(SynchronizationAddTransactionRequest(trx, *personalKeys));
    }

    // Update our weights
    network.send_object_to_self(UpdateWeightsRequest());
}


// -- Message Listeners --


// Storage for the ID of the last key receiver
boost::uuids::uuid NetworkedTangle::PublicKeySyncRequest::lastSent;

/**
 * @brief Listener for PublicKeySyncRequest events. Sends our public key to the requesting party
 * 
 * @param networkData - The event recieved
 * @param t - The tangle which recieved the event
 */
void NetworkedTangle::PublicKeySyncRequest::listener(breep::tcp::netdata_wrapper<PublicKeySyncRequest>& networkData, NetworkedTangle& t){
    // If we don't have a keypair, error
    if(!t.personalKeys)
        throw key::InvalidKey("Missing Personal Keypair!");
    // If our keys don't validate eachother, error
    if(!t.personalKeys->validate())
        throw key::InvalidKey("Personal Keypair's public and private key were not created from eachother!");

    // Don't service this request if we just sent this person our key
    if(lastSent != networkData.source.id()){
        t.network.send_object_to(networkData.source, PublicKeySyncResponse(*t.personalKeys));
        std::cout << "Sent public key to `" << networkData.source.id() << "`" << std::endl;
    }
    lastSent = networkData.source.id();

    // If we don't have keys for this peer, request them
    if(!t.peerKeys.contains(networkData.source.id()))
        t.network.send_object_to(networkData.source, PublicKeySyncRequest());
}

/**
 * @brief Construct a vote response message from the genesis stored in the provided tangle
 * @param t - The tangle to generate from
 */
NetworkedTangle::GenesisVoteResponse::GenesisVoteResponse(const NetworkedTangle& t): genesisHashes(t.genesis->parentHashes.begin(), t.genesis->parentHashes.end()) {
    genesisHashes.push_back(t.genesis->hash);

    // Combine the hashes and sign them to ensure validity
    std::string message;
    for(auto& hash: genesisHashes)
        message += hash;
    signature = key::signMessage(*t.personalKeys, message);
}

/**
 * @brief Listener for GenesisVoteResponse events. Counts the vote of the sender, if enouph votes have accrued for a genesis or everyone we are aware of has voted request it
 * 
 * @param networkData - The event recieved
 * @param t - The tangle which recieved the event
 */
void NetworkedTangle::GenesisVoteResponse::listener(breep::tcp::netdata_wrapper<GenesisVoteResponse> &networkData, NetworkedTangle &t) {
    // If we aren't accepting votes... ignore the message
    if(!t.genesisVotes) return;
    // If we don't have the sender's public key, ask for it and then ask for their vote again
    if(!t.peerKeys.contains(networkData.source.id())){
        t.network.send_object_to(networkData.source, PublicKeySyncRequest());
        t.network.send_object_to(networkData.source, GenesisVoteRequest());
        return;
    }
    // Verify that the vote is from who it says it is
    std::string message;
    for(auto& hash: networkData.data.genesisHashes)
        message += hash;
    if(!key::verifyMessage(t.peerKeys[networkData.source.id()], message, networkData.data.signature))
        throw std::runtime_error("Genesis vote failed, sender's identity failed to be verified, discarding.");

    // Increment the hash's count in the recieved map
    auto& hashes = networkData.data.genesisHashes;
    auto& genesisVotes = *t.genesisVotes;
    if(!genesisVotes.contains(hashes))
        genesisVotes[hashes] = {networkData.source.id(), 1};
    else genesisVotes[hashes].second++;
    std::cout << "Recieved genesis vote from `" << networkData.source.id() << "`" << std::endl;

    
    // Lambda which accepts a vote from a peer
    auto acceptVote = [&t](const breep::tcp::peer& source, std::string_view expectedHash){
        // Clear the votes
        t.genesisVotes.reset(nullptr);

        // Mark that we are expecting the hash at the back of the list (the last hash is the actual hash, as opposed to the parent hashes)
        t.genesisSyncExpectedHash = expectedHash;
        // Request a tangle sync from the recieved voter
        t.network.send_object_to(source, TangleSynchronizeRequest());
    };

    // If this genesis pair has a majority of the vote
    if(genesisVotes[hashes].second > t.peerKeys.size() / 2)
        acceptVote(networkData.source, hashes.back());

    else {
        // Total how many votes there currently are
        size_t totalVotes = 0;
        for(auto& [hashes, idVotes]: genesisVotes)
            totalVotes += idVotes.second;
        // If everyone has voted
        if(totalVotes >= t.peerKeys.size() - 1){ // Minus 1 since we are also listed in peer keys
            // Find the genesis with the most votes
            auto best = *std::max_element(genesisVotes.begin(), genesisVotes.end(), [](const auto& a, const auto& b) {
                return a.second.second < b.second.second;
            });

            // Request a tangle sync from the first voter for the best genesis
            if(t.network.peers().contains(best.second.first))
                acceptVote(t.network.peers().at(best.second.first), best.first.back());
        }
    }
}

/**
 * @brief Listener for SyncGenesisRequest events. Validates the genesis and sets it as the tangle's genesis
 * 
 * @param networkData - The event recieved
 * @param t - The tangle which recieved the event
 */
void NetworkedTangle::SyncGenesisRequest::listener(breep::tcp::netdata_wrapper<SyncGenesisRequest>& networkData, NetworkedTangle& t){
    // If we didn't request a new genesis... do nothing
    if(t.genesisSyncExpectedHash == INVALID_HASH)
        return;
    // Don't start with a new genesis if its hash matches the current genesis
    if(t.genesis->hash == networkData.data.genesis.hash)
        return;
    // If the genesis isn't the one we are looking for, it is invalid
    if(t.genesisSyncExpectedHash != networkData.data.genesis.hash)
        throw std::runtime_error("Recieved genesis sync with invalid hash, discarding");
    // If the remote transaction's hash doesn't match what is actual... it has an invalid hash
    if(networkData.data.genesis.hashTransaction() != networkData.data.actualHash)
        throw Transaction::InvalidHash(networkData.data.actualHash, networkData.data.genesis.hash); // TODO: Exception caught by Breep, need alternative error handling?
    // If we don't have the sender's public key, ask for it and then ask them to resend the tangle
    if(!t.peerKeys.contains(networkData.source.id())){
        t.network.send_object_to(networkData.source, PublicKeySyncRequest());
        t.network.send_object_to(networkData.source, TangleSynchronizeRequest());
        return;
    }
    // If we can't verify the transaction discard it
    if(!key::verifyMessage(t.peerKeys[networkData.source.id()], networkData.data.genesis.hash + networkData.data.genesis.hashTransaction(), networkData.data.validitySignature))
        throw std::runtime_error("Syncing of genesis with hash `" + networkData.data.genesis.hash + "` failed, sender's identity failed to be verified, discarding.");

    // Ensure the genesis transaction doesn't have any inputs
    if(!networkData.data.genesis.inputs.empty())
        throw std::runtime_error("Remote genesis with hash `" + networkData.data.genesis.hash + "` failed, genesis transactions can't have inputs!");


    t.setGenesis(TransactionNode::create(t, networkData.data.genesis));
    util::mutable_cast(t.genesis->hash) = networkData.data.claimedHash;

    std::cout << "Synchronized new genesis with hash `" + t.genesis->hash + "` from `" << networkData.source.id() << "`" << std::endl;
    t.genesisSyncExpectedHash = INVALID_HASH;
}

/**
 * @brief Listener for AddTransactionRequestBase events. Validates the transaction and either adds it to the tangle or enqueues it to be added later
 * 
 * @param networkData - The event recieved
 * @param t - The tangle which recieved the event
 */
void NetworkedTangle::AddTransactionRequestBase::listener(breep::tcp::netdata_wrapper<AddTransactionRequestBase>& networkData, NetworkedTangle& t){
    const Transaction& transaction = networkData.data.transaction;

    // If the remote transaction's hash doesn't match what is claimed... it has an invalid hash
    if(transaction.hash != networkData.data.validityHash)
        throw Transaction::InvalidHash(networkData.data.validityHash, transaction.hash); // TODO: Exception caught by Breep, need alternative error handling?

    // Try to add the transaction to the tangle
    attemptToAddTransaction(transaction, {networkData.source.id(), networkData.data.validitySignature}, t);

    // For every transaction in the tangle's network addition queue... attempt to add that transaction
    size_t listSize = t.networkAdditionQueue.size();
    for(size_t i = 0; i < listSize; i++){
        Transaction frontTrx = t.networkAdditionQueue.front().transaction;
        HashVerificationPair frontSig = t.networkAdditionQueue.front().pair;
        t.networkAdditionQueue.pop();
        attemptToAddTransaction(frontTrx, frontSig, t);
    }
    // Reduce the size of the network queue if we are wasting space
    t.shrinkNetworkQueue();

    std::cout << "Processed remote transaction add with hash `" + transaction.hash + "` from " << networkData.source.id() << std::endl;
}

/**
 * @brief Function which attempts to add a transaction to the tangle, or enques it for later
 * 
 * @param transaction - The transaction to add
 * @param validityPair - Hash and key used for verification
 * @param t - The tangle to add the transaction to
 */
void NetworkedTangle::AddTransactionRequestBase::attemptToAddTransaction(const Transaction& transaction, HashVerificationPair validityPair, NetworkedTangle& t){
    try {
        // If we don't have the peer's public key, request it and enqueue the transaction for later
        if(!t.peerKeys.contains(validityPair.peerID)){
            auto& peers = t.network.peers();
            auto& sender = peers.at(validityPair.peerID);
            t.network.send_object_to(sender, PublicKeySyncRequest());

            // If we are running out of room in the network queue, expand it
            t.growNetworkQueue();
            t.networkAdditionQueue.emplace(transaction, validityPair);

            std::cout << "Received transaction add from unverified peer `" << validityPair.peerID << "`, enquing transaction with hash `" << transaction.hash << "` and requesting peer's key." << std::endl;
            return;
        }

        // If we can't verify the transaction discard it
        if(!key::verifyMessage(t.peerKeys[validityPair.peerID], transaction.hash, validityPair.signature))
            throw std::runtime_error("Transaction with hash `" + transaction.hash + "` sender's identity failed to be verified, discarding.");


        // Validate the transaction's parents
        bool parentsFound = true;
        std::vector<TransactionNode::ptr> parents;
        for(Hash& hash: transaction.parentHashes){
            auto parent = t.find(hash);
            // If the parnent is good contiune
            if(parent)
                parents.push_back(parent);
            // If the parent is not found enqueue the transaction for later
            else {
                // If we are running out of room in the network queue, expand it
                t.growNetworkQueue();
                t.networkAdditionQueue.emplace(transaction, validityPair);
                parentsFound = false;
                std::cout << "Remote transaction with hash `" + transaction.hash + "` is temporarily orphaned... enqueuing for later" << std::endl;
                break;
            }
        }

        // If the transaction's parents could be validated... add the transaction to the tangle
        if(parentsFound) {
            (*(Tangle*) &t).add(TransactionNode::create(t, transaction)); // Call the tangle version so that we don't spam the network with extra messages
            std::cout << "Added remote transaction with hash `" + transaction.hash + "` to the tangle" << std::endl;
        }
    // If an exception is thrown by the add process, discard the transaction and display an error message
    } catch (std::exception& e) { std::cerr << "Invalid transaction in network queue, discarding" << std::endl << "\t" << e.what() << std::endl; }
}


// -- Message De/serialization --


/*
 * All of these functions simply convert a particular type of message to/from a string
 */

breep::serializer& operator<<(breep::serializer& _s, const NetworkedTangle::SyncGenesisRequest& r) {
	breep::serializer s;
	s << r.claimedHash;
	s << r.actualHash;
	s << r.validitySignature;
	s << r.genesis;

    // Compress the request
	auto uncompressed = s.str();
	_s << util::compress(*(std::string*) &uncompressed);
	return _s;
}
breep::deserializer& operator>>(breep::deserializer& _d, NetworkedTangle::SyncGenesisRequest& r) {
    // decompress the request
	std::string compressed;
	_d >> compressed;
	auto uncompressed = util::decompress(compressed);
	breep::deserializer d(*(std::basic_string<unsigned char>*) &uncompressed);

	util::mutable_cast(r.claimedHash).clear();
	d >> util::mutable_cast(r.claimedHash);
	util::mutable_cast(r.actualHash).clear();
	d >> util::mutable_cast(r.actualHash);
	d >> r.validitySignature;
	d >> r.genesis;
	util::mutable_cast(r.genesis.hash) = r.claimedHash;
	return _d;
}

breep::serializer& operator<<(breep::serializer& _s, const NetworkedTangle::AddTransactionRequest& r) {
	breep::serializer s;
	s << r.validityHash;
	s << r.validitySignature;
	s << r.transaction;

    // Compress the request
	auto uncompressed = s.str();
	_s << util::compress(*(std::string*) &uncompressed);
	return _s;
}
breep::deserializer& operator>>(breep::deserializer& _d, NetworkedTangle::AddTransactionRequest& r) {
    // Decompress the request
	std::basic_string<unsigned char> compressed;
	_d >> compressed;
	auto uncompressed = util::decompress(*(std::string*) &compressed);
	breep::deserializer d(*(std::basic_string<unsigned char>*) &uncompressed);

	std::string validityHash;
	d >> validityHash;
	(*(std::string*) &r.validityHash) = validityHash;
	d >> r.validitySignature;
	d >> r.transaction;
	return _d;
}

breep::serializer& operator<<(breep::serializer& _s, const NetworkedTangle::SynchronizationAddTransactionRequest& r) {
	breep::serializer s;
	s << r.validityHash;
	s << r.validitySignature;
	s << r.transaction;

    // Compress the request
	auto uncompressed = s.str();
	_s << util::compress(*(std::string*) &uncompressed);
	return _s;
}
breep::deserializer& operator>>(breep::deserializer& _d, NetworkedTangle::SynchronizationAddTransactionRequest& r) {
    // Decompress the request
	std::basic_string<unsigned char> compressed;
	_d >> compressed;
	auto uncompressed = util::decompress(*(std::string*) &compressed);
	breep::deserializer d(*(std::basic_string<unsigned char>*) &uncompressed);

	std::string validityHash;
	d >> validityHash;
	(*(std::string*) &r.validityHash) = validityHash;
	d >> r.validitySignature;
	d >> r.transaction;
	return _d;
}