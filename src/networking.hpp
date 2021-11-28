#ifndef NETWORKING_HPP
#define NETWORKING_HPP

#include "tangle.hpp"
#include "keys.hpp"
#include <map>
#include <list>
#include <ostream>
#include <istream>
#include "circular_buffer.hpp"

#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include <boost/uuid/uuid_io.hpp>
#include <breep/network/tcp.hpp>
#include <breep/util/serialization.hpp>

// The default port to start searching for ports at
#define DEFAULT_PORT_NUMBER 12345;

// Function which attempts to remotely read data from a sodket until the <timeout> amount of time has elapsed
// Returns true if the read was successful, false otherwise
template <typename MutableBufferSequence, typename Duration>
bool readWithTimeout(boost::asio::io_service& io_service, boost::asio::ip::tcp::socket& sock, const MutableBufferSequence& buffers, Duration timeout){
	// Start a timer with <timeout> duration
	std::optional<boost::system::error_code> timer_result;
	boost::asio::steady_timer timer(io_service);
	timer.expires_from_now(timeout);
	// Have the timer store its result when done
	timer.async_wait([&timer_result](const boost::system::error_code& error){
		timer_result = error;
	});


	// Begin an async read, saving the error code which results
	std::optional<boost::system::error_code> read_result;
	sock.async_read_some(buffers, [&read_result](const boost::system::error_code& error, std::size_t bytes_transferred){
		read_result = error;
	});

	// Wait for the read or the timer to finish
	io_service.reset();
	while (io_service.run_one()) {
		if (read_result)
			timer.cancel();
		else if (timer_result)
			sock.cancel();
	}

	// If there was an error reading... propagate it
	if (*read_result)
		throw boost::system::system_error(*read_result);

	// If there was no problem reading... return success
	if(read_result && read_result == boost::system::errc::success)
		return true;
	// Otherwise... return failure
	return false;
}

// Function which finds a free port to listen on
unsigned short determineLocalPort();

namespace handshake {
	// Function which runs in a thread... looking for handshake pings
	void acceptHandshakeConnection(boost::asio::ip::tcp::acceptor& acceptor, boost::asio::io_service& io_service, unsigned short localNetworkPort);

	// Function which pings ports on a remote address for connectivity
	unsigned short determineRemotePort(boost::asio::io_service& io_service, boost::asio::ip::address& address);
}


// -- Networked Tangle --


// Class which provides a network synchronization for a tangle
struct NetworkedTangle: public Tangle {
	struct InvalidAccount : public std::runtime_error { Hash account; InvalidAccount(Hash account): std::runtime_error("Account `" + account + "` not found!"), account(account) {} };

	breep::tcp::network& network;

	// This account's public and private keypair
	const std::shared_ptr<key::KeyPair> personalKeys;
	// Public keys for connected peers
	std::unordered_map<boost::uuids::uuid, key::PublicKey, boost::hash<boost::uuids::uuid>> peerKeys;

	NetworkedTangle(breep::tcp::network& network) : network(network) {
		// Make sure that the network queue has some memory backing it
		networkQueue.getContainer().resize(NETWORK_QUEUE_MIN_SIZE);

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

	// Function which sets the keypair
	void setKeyPair(const std::shared_ptr<key::KeyPair>& pair, bool networkSync = true){
		util::makeMutable(personalKeys) = pair;
		peerKeys[network.self().id()] = personalKeys->pub;
		if(networkSync) network.send_object(NetworkedTangle::PublicKeySyncResponse(*pair));
	}

	// Function which finds a key given its hash
	const key::PublicKey& findAccount(Hash keyHash) const {
		for(auto& [uuid, key]: peerKeys)
			if(key::hash(key) == keyHash)
				return key;

		throw InvalidAccount(keyHash);
	}

	// Adds a new node to the tangle (network synced)
	Hash add(TransactionNode::ptr node){
		Hash out = Tangle::add(node);
		network.send_object(AddTransactionRequest(*node, *personalKeys)); // The add gets validated by the base tangle, if we get to this code (no exception) then the node is acceptable
		return out;
	}

	TransactionNode::ptr createLatestCommonGenesis(){
		// If there are no genesis canidates our genesis is the latest common genesis
		if(genesisCanidates.empty()) return genesis;

		std::cout << "Genesis canidates found" << std::endl;

		// Look through the queue and find the latest canidate set of nodes with 100% confidence
		std::vector<TransactionNode::const_ptr>* _chosen = nullptr;
		for(auto& canidate: genesisCanidates.getContainer()){
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
		util::makeMutable(trx->hash) = chosen[0]->hash;

		// Fill the transaction's parent hashes with the remaining hashes of the chosen nodes
		auto& parentHashes = util::makeMutable(trx->parentHashes);
		delete [] parentHashes.data(); // Free the current parent hashes
		parentHashes = {new Hash[chosen.size() - 1], chosen.size() - 1}; // Create new memory to back the parent hashes
		for(int i = 1; i < chosen.size(); i++)
			util::makeMutable(parentHashes[i]) = chosen[i]->hash;

		return trx;
	}

	void prune(){
		// Generate the new latest common genesis
		auto genesis = createLatestCommonGenesis();

		// Cache a copy of the current tips and then clear the tangle's copy
		auto originalTips = tips.unsafe();
		util::makeMutable(tips)->clear();

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
					util::makeMutable(parent->children)->clear(); // Clear out the children of the node's parent
					util::makeMutable(tips)->push_back(parent); // Mark the now childless parent as a tip
				}
			}
			for(auto& hash: genesis->parentHashes){
				auto node = find(hash);
				auto lock = node->children.write_lock();
				children = std::move(*lock);
				lock->clear(); // Clear the list in the tree so they don't get pruned when we swap out genesises

				// Clear the node's parent's children and mark it as a tip
				for(auto& parent: node->parents){
					util::makeMutable(parent->children)->clear(); // Clear out the children of the node's parent
					util::makeMutable(tips)->push_back(parent); // Mark the now childless parent as a tip
				}
			}

			// Ensure there are no duplicate children or tips
			util::removeDuplicates(children);
			util::removeDuplicates(util::makeMutable(tips.unsafe()));

			// Move the list of children into the new genesis
			*genesis->children.write_lock() = std::move(children);

			// Update the children to point to the new genesis
			auto lock = genesis->children.write_lock();
			for(size_t i = 0; i < lock->size(); i++)
				util::makeMutable(lock[i]->parents) = { genesis };
		}
		std::cout << "Situated children" << std::endl;

		// Update the tangle's genesis (removes all the nodes up to the temporary list of tips)
		setGenesis(genesis);

		// Restore the original list of tips
		*util::makeMutable(tips.write_lock()) = originalTips;
	}

	// Function which allows saving a tangle to a file
	void saveTangle(std::ostream& out) {
		// List all of the transactions in the tangle
		std::list<TransactionNode*> transactions = listTransactions();
		// Sort them according to time, ensuring that the genesis remains at the front of the list
		Transaction* genesis = transactions.front();
		transactions.sort([genesis](Transaction* a, Transaction* b){
			if(a->hash == genesis->hash) return true;
			if(b->hash == genesis->hash) return false;
			return a->timestamp < b->timestamp;
		});

		breep::serializer s;
		s << transactions.size();

		for(Transaction* _t: transactions){
			const Transaction& t = *_t;
			s << t;
		}

		auto raw = s.str();
		std::string compressed = util::compress(*(std::string*) &raw);

		out.write(reinterpret_cast<char*>(compressed.data()), compressed.size());
	}

	// Function which allows loading a tangle from a file
	void loadTangle(std::istream& in, size_t size) {
		std::string compressed;
		compressed.resize(size);
		in.read(&compressed[0], compressed.size());

		compressed = util::decompress(compressed);
		std::basic_string<unsigned char> raw = *(std::basic_string<unsigned char>*) &compressed;
		breep::deserializer d(raw);

		size_t transactionCount;
		d >> transactionCount;

		// The genesis is always the first transaction in the file
		Transaction trx;
		d >> trx;
		listeningForGenesisSync = true; // Flag us as prepared to receive a new genesis
		network.send_object_to_self(SyncGenesisRequest(trx, *personalKeys));

		// Read in each transaction from the deserializer and then add it to the tangle
		for(int i = 0; i < transactionCount - 1; i++) { // Minus 1 since we already synced the genesis
			d >> trx;
			network.send_object_to_self(SynchronizationAddTransactionRequest(trx, *personalKeys));
		}

		// Update our weights
		network.send_object_to_self(UpdateWeightsRequest());
	}

private:
	bool listeningForGenesisSync = false;

	// Struct containing both features needed to verify a transaction's hash
	struct HashVerificationPair {
		boost::uuids::uuid peerID;
		std::string signature;
	};

	struct TransactionAndHashVerificationPair {
		Transaction transaction;
		HashVerificationPair pair;

		TransactionAndHashVerificationPair() = default;
		TransactionAndHashVerificationPair(const Transaction& transaction, const HashVerificationPair& pair) : transaction(transaction), pair(pair) {}
		TransactionAndHashVerificationPair(const TransactionAndHashVerificationPair& o) : transaction(o.transaction), pair(o.pair) {}
		TransactionAndHashVerificationPair(const TransactionAndHashVerificationPair&& o) : transaction(std::move(o.transaction)), pair(o.pair) {}
		TransactionAndHashVerificationPair& operator=(const TransactionAndHashVerificationPair& other){ transaction = other.transaction; pair = other.pair; return *this; }
		TransactionAndHashVerificationPair& operator=(const TransactionAndHashVerificationPair&& other){ transaction = std::move(other.transaction); pair = other.pair; return *this; }
		bool operator==(const TransactionAndHashVerificationPair& o) const {
			return transaction.hash == o.transaction.hash && pair.peerID == o.pair.peerID && pair.signature == o.pair.signature;
		}
	};
	// Queue which holds incoming transactions
	ModifiableQueue<TransactionAndHashVerificationPair, circular_buffer<std::vector<TransactionAndHashVerificationPair>>> networkQueue;
	const size_t NETWORK_QUEUE_MIN_SIZE = 8, NETWORK_QUEUE_MAX_SIZE = 1024; // TODO: make into a #define

	// If we are running out of room in the network queue, expand it
	void growNetworkQueue(){
		auto& container = networkQueue.getContainer();
		if(size_t size = container.size(), capacity = container.capacity(); size == capacity && size < NETWORK_QUEUE_MAX_SIZE)
			networkQueue.getContainer().resize(std::clamp(size * 2, NETWORK_QUEUE_MIN_SIZE, NETWORK_QUEUE_MAX_SIZE)); // Clamp the queues size in the range [10, NETWORK_QUEUE_MAX_SIZE]
	}

	// If the network queue is nearing half of its capacity, shrink it
	void shrinkNetworkQueue(){
		auto& container = networkQueue.getContainer();
		if(size_t size = container.size(), capacity = container.capacity(); size <= capacity / 2 && size > NETWORK_QUEUE_MIN_SIZE)
			container = std::vector<TransactionAndHashVerificationPair>(container.begin(), container.end()); // Make a new container containing only the used values of the buffer with its start reset
	}

protected:
	void connect_disconnectListener(breep::tcp::network& network, const breep::tcp::peer& peer) {
		// Someone connected...
		if (peer.is_connected())
			std::cout << peer.id() << " connected!" << std::endl;

		// Someone disconnected...
		else
			std::cout << peer.id() << " disconnected" << std::endl;
	}


	// -- Messages --


public:
	// Message which sends our public key to a requester
	struct PublicKeySyncResponse {
		// String that is signed then verified to ensure that the public key is ligit
		#define VERIFICATION_STRING "VERIFY"

		key::PublicKey _key;
		std::string signature;
		PublicKeySyncResponse() = default;
		PublicKeySyncResponse(const key::KeyPair& pair) : _key(pair.pub), signature(key::signMessage(pair.pri, VERIFICATION_STRING)) {}

		static void listener(breep::tcp::netdata_wrapper<PublicKeySyncResponse>& networkData, NetworkedTangle& t){
			// If the signature they provided is verified with the sent public key...
			if(key::verifyMessage(networkData.data._key, VERIFICATION_STRING, networkData.data.signature))
				// Mark the key as the sending peer's public key
				t.peerKeys[networkData.source.id()] = networkData.data._key;
			else std::cout << "Failed to verify key from `" << networkData.source.id() << "`" << std::endl;
		}

		#undef VERIFICATION_STRING
	};

	// Message which requests the public key to be sent
	struct PublicKeySyncRequest {
		// Track the person we last sent a key too
		static boost::uuids::uuid lastSent;

		static void listener(breep::tcp::netdata_wrapper<PublicKeySyncRequest>& networkData, NetworkedTangle& t){
			if(!t.personalKeys)
				throw key::InvalidKey("Missing Personal Keypair!");
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
	};

	// Message which causes every node to send their tangle to the sender
	struct TangleSynchronizeRequest {
		// When we create a sync request mark that we are now listening for genesis syncs
		TangleSynchronizeRequest() = default;
		TangleSynchronizeRequest(NetworkedTangle& t) { t.listeningForGenesisSync = true; } // Use this constructor to mark the local tangle as accepting of requests

		static void listener(breep::tcp::netdata_wrapper<TangleSynchronizeRequest>& networkData, NetworkedTangle& t){
			// Can't add or remove nodes while we are sending the tangle to someone
			std::scoped_lock lock(t.mutex);
			recursiveSendTangle(networkData.source, t, t.genesis);

			// Suggest that the recipient update their weights
			t.network.send_object_to(networkData.source, UpdateWeightsRequest());

			std::cout << "Sent tangle to `" << networkData.source.id() << "`" << std::endl;
		}

	protected:
		static void recursiveSendTangle(const breep::tcp::peer& requester, NetworkedTangle& t, const TransactionNode::ptr& node){
			if(node->isGenesis) t.network.send_object_to(requester, SyncGenesisRequest(*node, *t.personalKeys));
			else t.network.send_object_to(requester, SynchronizationAddTransactionRequest(*node, *t.personalKeys));

			for(int i = 0; i < node->children.read_lock()->size(); i++)
				recursiveSendTangle(requester, t, node->children.read_lock()[i]);
		}
	};

	// Message which causes the tangle to update its weights
	struct UpdateWeightsRequest {
		static void listener(breep::tcp::netdata_wrapper<UpdateWeightsRequest>& networkData, NetworkedTangle& t){
			// Update all the weights
			std::thread([&t](){
				for(auto [i, tipLock] = std::make_pair(size_t(0), util::makeMutable(t.tips).read_lock()); i < tipLock->size(); i++)
					t.updateCumulativeWeights(tipLock[i]);
			}).detach();

			std::cout << "Started updating tangle weights" << std::endl;
		}
	};

	// Message which causes the recipent to update their genesis block (only valid if they are accepting of the change)
	struct SyncGenesisRequest {
		Hash claimedHash = INVALID_HASH, actualHash = INVALID_HASH;
		std::string validitySignature;
		Transaction genesis;
		SyncGenesisRequest() = default;
		SyncGenesisRequest(Transaction& _genesis, const key::KeyPair& keys) : claimedHash(_genesis.hash), actualHash(_genesis.hashTransaction()), validitySignature(key::signMessage(keys, claimedHash + actualHash)), genesis(_genesis) {}

		static void listener(breep::tcp::netdata_wrapper<SyncGenesisRequest>& networkData, NetworkedTangle& t){
			// If we didn't request a new genesis... do nothing
			if(!t.listeningForGenesisSync)
				return;
			// Don't start with a new genesis if its hash matches the current genesis
			if(t.genesis->hash == networkData.data.genesis.hash)
				return;
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
			util::makeMutable(t.genesis->hash) = networkData.data.claimedHash;

			std::cout << "Synchronized new genesis with hash `" + t.genesis->hash + "` from `" << networkData.source.id() << "`" << std::endl;
			t.listeningForGenesisSync = false;
		}
	};

	// Base message which causes the recipient to add a new transaction to their graph
	struct AddTransactionRequestBase {
		Hash validityHash = INVALID_HASH;
		std::string validitySignature;
		Transaction transaction;
		AddTransactionRequestBase() = default;
		AddTransactionRequestBase(Transaction& _transaction, const key::KeyPair& keys) : validityHash(_transaction.hash), validitySignature(key::signMessage(keys, validityHash)), transaction(_transaction) {}

		static void listener(breep::tcp::netdata_wrapper<AddTransactionRequestBase>& networkData, NetworkedTangle& t){
			const Transaction& transaction = networkData.data.transaction;

			// If the remote transaction's hash doesn't match what is claimed... it has an invalid hash
			if(transaction.hash != networkData.data.validityHash)
				throw Transaction::InvalidHash(networkData.data.validityHash, transaction.hash); // TODO: Exception caught by Breep, need alternative error handling?

			attemptToAddTransaction(transaction, {networkData.source.id(), networkData.data.validitySignature}, t);

			size_t listSize = t.networkQueue.size();
			for(size_t i = 0; i < listSize; i++){
				Transaction frontTrx = t.networkQueue.front().transaction;
				HashVerificationPair frontSig = t.networkQueue.front().pair;
				t.networkQueue.pop();
				attemptToAddTransaction(frontTrx, frontSig, t);
			}
			// Reduce the size of the network queue if we are wasting space
			t.shrinkNetworkQueue();

			std::cout << "Processed remote transaction add with hash `" + transaction.hash + "` from " << networkData.source.id() << std::endl;
		}

	protected:
		static void attemptToAddTransaction(const Transaction& transaction, HashVerificationPair validityPair, NetworkedTangle& t){
			try {
				// If we don't have the peer's public key, request it and enqueue the transaction for later
				if(!t.peerKeys.contains(validityPair.peerID)){
					auto& peers = t.network.peers();
					auto& sender = peers.at(validityPair.peerID);
					t.network.send_object_to(sender, PublicKeySyncRequest());

					// If we are running out of room in the network queue, expand it
					t.growNetworkQueue();
					t.networkQueue.emplace(transaction, validityPair);

					std::cout << "Received transaction add from unverified peer `" << validityPair.peerID << "`, enquing transaction with hash `" << transaction.hash << "` and requesting peer's key." << std::endl;
					return;
				}

				// If we can't verify the transaction discard it
				if(!key::verifyMessage(t.peerKeys[validityPair.peerID], transaction.hash, validityPair.signature))
					throw std::runtime_error("Transaction with hash `" + transaction.hash + "` sender's identity failed to be verified, discarding.");

				bool parentsFound = true;
				std::vector<TransactionNode::ptr> parents;
				for(Hash& hash: transaction.parentHashes){
					auto parent = t.find(hash);
					if(parent)
						parents.push_back(parent);
					else {
						// If we are running out of room in the network queue, expand it
						t.growNetworkQueue();
						t.networkQueue.emplace(transaction, validityPair);
						parentsFound = false;
						std::cout << "Remote transaction with hash `" + transaction.hash + "` is temporarily orphaned... enqueuing for later" << std::endl;
						break;
					}
				}

				if(parentsFound) {
					(*(Tangle*) &t).add(TransactionNode::create(t, transaction)); // Call the tangle version so that we don't spam the network with extra messages
					std::cout << "Added remote transaction with hash `" + transaction.hash + "` to the tangle" << std::endl;
				}
			} catch (std::exception& e) { std::cerr << "Invalid transaction in network queue, discarding" << std::endl << "\t" << e.what() << std::endl; }
		}
	};

	// Message which causes the recipient to add a transaction to their graph
	struct AddTransactionRequest: public AddTransactionRequestBase {
		using AddTransactionRequestBase::AddTransactionRequestBase;

		static void listener(breep::tcp::netdata_wrapper<AddTransactionRequest>& networkData, NetworkedTangle& t){
			// TODO: should we check if a transaction is too old?

			AddTransactionRequestBase::listener((*(breep::tcp::netdata_wrapper<AddTransactionRequestBase>*) &networkData), t); // TODO: is this a valid cast?
		}
	};


	// Message which causes the recipient to add a transaction to their graph (has specialized rule relaxations due to initial synchronization)
	struct SynchronizationAddTransactionRequest: public AddTransactionRequestBase {
		using AddTransactionRequestBase::AddTransactionRequestBase;

		static void listener(breep::tcp::netdata_wrapper<SynchronizationAddTransactionRequest>& networkData, NetworkedTangle& t){
			t.updateWeights = false; // Flag us as NOT reclaculating weights
			AddTransactionRequestBase::listener((*(breep::tcp::netdata_wrapper<AddTransactionRequestBase>*) &networkData), t); // TODO: is this a valid cast?
			t.updateWeights = true; // Flag us as reclaculating weights
		}
	};
};


// -- Message De/Serialization --

inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::PublicKeySyncResponse& r) {
	s << r.signature;
	s << r._key;
	return s;
}
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::PublicKeySyncResponse& r) {
	d >> r.signature;
	d >> r._key;

	return d;
}
BREEP_DECLARE_TYPE(NetworkedTangle::PublicKeySyncResponse)

inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::PublicKeySyncRequest& r) { return s; }
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::PublicKeySyncRequest& r) { return d; }
BREEP_DECLARE_TYPE(NetworkedTangle::PublicKeySyncRequest)

// Empty serialization (no data to send)
inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::TangleSynchronizeRequest& r) { return s; }
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::TangleSynchronizeRequest& r) { return d; }
BREEP_DECLARE_TYPE(NetworkedTangle::TangleSynchronizeRequest)

inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::UpdateWeightsRequest& r) { return s; }
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::UpdateWeightsRequest& r) { return d; }
BREEP_DECLARE_TYPE(NetworkedTangle::UpdateWeightsRequest)

inline breep::serializer& operator<<(breep::serializer& _s, const NetworkedTangle::SyncGenesisRequest& r) {
	breep::serializer s;
	s << r.claimedHash;
	s << r.actualHash;
	s << r.validitySignature;
	s << r.genesis;

	auto uncompressed = s.str();
	_s << util::compress(*(std::string*) &uncompressed);
	return _s;
}
inline breep::deserializer& operator>>(breep::deserializer& _d, NetworkedTangle::SyncGenesisRequest& r) {
	std::string compressed;
	_d >> compressed;
	auto uncompressed = util::decompress(compressed);
	breep::deserializer d(*(std::basic_string<unsigned char>*) &uncompressed);

	util::makeMutable(r.claimedHash).clear();
	d >> util::makeMutable(r.claimedHash);
	util::makeMutable(r.actualHash).clear();
	d >> util::makeMutable(r.actualHash);
	d >> r.validitySignature;
	d >> r.genesis;
	util::makeMutable(r.genesis.hash) = r.claimedHash;
	return _d;
}
BREEP_DECLARE_TYPE(NetworkedTangle::SyncGenesisRequest)

inline breep::serializer& operator<<(breep::serializer& _s, const NetworkedTangle::AddTransactionRequest& r) {
	breep::serializer s;
	s << r.validityHash;
	s << r.validitySignature;
	s << r.transaction;

	auto uncompressed = s.str();
	_s << util::compress(*(std::string*) &uncompressed);
	return _s;
}
inline breep::deserializer& operator>>(breep::deserializer& _d, NetworkedTangle::AddTransactionRequest& r) {
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
BREEP_DECLARE_TYPE(NetworkedTangle::AddTransactionRequest)

inline breep::serializer& operator<<(breep::serializer& _s, const NetworkedTangle::SynchronizationAddTransactionRequest& r) {
	breep::serializer s;
	s << r.validityHash;
	s << r.validitySignature;
	s << r.transaction;

	auto uncompressed = s.str();
	_s << util::compress(*(std::string*) &uncompressed);
	return _s;
}
inline breep::deserializer& operator>>(breep::deserializer& _d, NetworkedTangle::SynchronizationAddTransactionRequest& r) {
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
BREEP_DECLARE_TYPE(NetworkedTangle::SynchronizationAddTransactionRequest)


#endif /* end of include guard: NETWORKING_HPP */
