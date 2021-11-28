/**
 * @file networking.hpp
 * @author Joshua Dahl (jdahl@unr.edu)
 * @brief Defines networking structures, has functions for automatically determining ports as well as an extension to the tangle which provides network infastructure.
 * @version 0.1
 * @date 2021-11-28
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#ifndef NETWORKING_HPP
#define NETWORKING_HPP

#include "tangle.hpp"

#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include <breep/network/tcp.hpp>

// The default port to start searching for ports at
#define DEFAULT_PORT_NUMBER 12345;

// Network Queue limits
#define NETWORK_QUEUE_MIN_SIZE 8
#define NETWORK_QUEUE_MAX_SIZE 1024

/**
 * @brief Function which attempts to remotely read data from a sodket until the <timeout> amount of time has elapsed
 * 
 * @param io_service - ASIO IO Service
 * @param sock - The socket to read data from
 * @param buffers - The buffer to store data in
 * @param timeout - How long to read for before giving
 * @return bool - Returns true if the read was successful, false otherwise
 */
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


/**
 * @brief Class which provides a network synchronization for a tangle
 */
struct NetworkedTangle: public Tangle {
	/**
	 * @brief Exception thrown when the tangle encounters an invalid account
	 */
	struct InvalidAccount : public std::runtime_error { Hash account; InvalidAccount(Hash account): std::runtime_error("Account `" + account + "` not found!"), account(account) {} };

	// The network this tangle is connected to
	breep::tcp::network& network;

	// This account's public and private keypair
	const std::shared_ptr<key::KeyPair> personalKeys;
	// Public keys for connected peers
	std::unordered_map<boost::uuids::uuid, key::PublicKey, boost::hash<boost::uuids::uuid>> peerKeys;

	NetworkedTangle(breep::tcp::network& network);

	void setKeyPair(const std::shared_ptr<key::KeyPair>& pair, bool networkSync = true);
	const key::PublicKey& findAccount(Hash keyHash) const;

	Hash add(TransactionNode::ptr node);

	TransactionNode::ptr createLatestCommonGenesis();
	void prune();

	void saveTangle(std::ostream& out);
	void loadTangle(std::istream& in, size_t size);

private:
	// Pointer to a map used for counting votes for different tangles during startup
	std::unique_ptr<std::map<std::vector<std::string>, std::pair<boost::uuids::uuid, size_t>>> genesisVotes = nullptr;
	// Hash we expect a genesis sync to have (invalid hash means we aren't expecting a new genesis)
	std::string genesisSyncExpectedHash = INVALID_HASH;

	// Struct containing both features needed to verify a transaction's hash
	struct HashVerificationPair {
		boost::uuids::uuid peerID;
		std::string signature;
	};

	// Struct which combines a Transaction and HashVerificationPair for use in the networkAdditionQueue
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
	// Queue which holds incoming transactions that weren't immediately added to the tangle
	ModifiableQueue<TransactionAndHashVerificationPair, circular_buffer<std::vector<TransactionAndHashVerificationPair>>> networkAdditionQueue;

	/**
	 * @brief Function that expands the network queue if it is getting close to running out of size
	 */
	void growNetworkQueue(){
		auto& container = networkAdditionQueue.getContainer();
		if(size_t size = container.size(), capacity = container.capacity(); size == capacity && size < NETWORK_QUEUE_MAX_SIZE)
			networkAdditionQueue.getContainer().resize(std::clamp(size * 2, (size_t) NETWORK_QUEUE_MIN_SIZE, (size_t) NETWORK_QUEUE_MAX_SIZE)); // Clamp the queues size in the range [10, NETWORK_QUEUE_MAX_SIZE]
	}

	/**
	 * @brief Function which shrinks the network queue if more than half of it is wasted space
	 */
	void shrinkNetworkQueue(){
		auto& container = networkAdditionQueue.getContainer();
		if(size_t size = container.size(), capacity = container.capacity(); size <= capacity / 2 && size > NETWORK_QUEUE_MIN_SIZE)
			container = std::vector<TransactionAndHashVerificationPair>(container.begin(), container.end()); // Make a new container containing only the used values of the buffer with its start reset
	}

protected:
	/**
	 * @brief Function which prints a message when a peer dis/connects
	 * 
	 * @param network 
	 * @param peer 
	 */
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
	/**
	 * @brief Message which requests the receiver to send us their public key
	 */
	struct PublicKeySyncRequest {
		// Track the person we last sent a key too
		static boost::uuids::uuid lastSent;

		static void listener(breep::tcp::netdata_wrapper<PublicKeySyncRequest>& networkData, NetworkedTangle& t);
	};

	/**
	 * @brief Message which sends our public key to the requester
	 */
	struct PublicKeySyncResponse {
		// String that is signed then verified to ensure that the public key is ligitimate
		#define VERIFICATION_STRING "VERIFY"

		// The key to send
		key::PublicKey _key;
		// Signature to verify the the key
		std::string signature;
		PublicKeySyncResponse() = default;
	
		/**
		 * @brief Constructor which stores the public key and a signature done with the associated private key
		 * @param pair - Keypair, the public key is sent and the private key is used to confirm the validity of the public key
		 */
		PublicKeySyncResponse(const key::KeyPair& pair) : _key(pair.pub), signature(key::signMessage(pair.pri, VERIFICATION_STRING)) {}

		/**
		 * @brief Listener for PublicKeySyncResponse events. If the received key is verifiable, mark it as the key associated with the sending peer.
		 * 
		 * @param networkData - The event received
		 * @param t - The tangle which received the event
		 */
		static void listener(breep::tcp::netdata_wrapper<PublicKeySyncResponse>& networkData, NetworkedTangle& t){
			// If the signature they provided is verified with the sent public key...
			if(key::verifyMessage(networkData.data._key, VERIFICATION_STRING, networkData.data.signature))
				// Mark the key as the sending peer's public key
				t.peerKeys[networkData.source.id()] = networkData.data._key;
			else std::cout << "Failed to verify key from `" << networkData.source.id() << "`" << std::endl;
		}

		#undef VERIFICATION_STRING
	};

	/**
	 * @brief Message which requests a vote for what genesis is being used
	 */
	struct GenesisVoteRequest {
		GenesisVoteRequest() = default;
		GenesisVoteRequest(NetworkedTangle& t) { // Use this constructor to mark the local tangle as accepting of requests
			t.genesisVotes = std::make_unique<std::map<std::vector<std::string>, std::pair<boost::uuids::uuid, size_t>>>();
		}

		/**
		 * @brief Listener for GenesisVoteRequest events. Sends the hashes our genesis represents to the requester
		 * 
		 * @param networkData - The event received
		 * @param t - The tangle which received the event
		 */
		static void listener(breep::tcp::netdata_wrapper<GenesisVoteRequest> &networkData, NetworkedTangle &t) {
			t.network.send_object_to(networkData.source, GenesisVoteResponse(t));
			std::cout << "Sent genesis vote to `" << networkData.source.id() << "`" << std::endl;
		}
	};

	/**
	 * @brief Message which sends the hashes our genesis block represent to the reequesting node 
	 * @note The hash of the genesis node itself should be last in the list
	 */
	struct GenesisVoteResponse {
		// List of hashes the genesis represents
		std::vector<std::string> genesisHashes;
		// Signature to ensure integrity of data
		std::string signature;

		GenesisVoteResponse() = default;
		GenesisVoteResponse(const NetworkedTangle& t);

		static void listener(breep::tcp::netdata_wrapper<GenesisVoteResponse> &networkData, NetworkedTangle &t);
	};


	/**
	 * @brief Message which causes the recipient to send us their tangle
	 */
	struct TangleSynchronizeRequest {
		/**
		 * @brief Listener for TangleSynchronizeRequest events. Sends the sender every node in our tangle
		 * 
		 * @param networkData - The event received
		 * @param t - The tangle which received the event
		 */
		static void listener(breep::tcp::netdata_wrapper<TangleSynchronizeRequest>& networkData, NetworkedTangle& t){
			std::scoped_lock lock(t.mutex); // Can't add or remove nodes while we are sending the tangle to someone
			// Send the tangle to the sender
			recursiveSendTangle(networkData.source, t, t.genesis);

			// Suggest that the recipient update their weights
			t.network.send_object_to(networkData.source, UpdateWeightsRequest());
			std::cout << "Sent tangle to `" << networkData.source.id() << "`" << std::endl;
		}

	protected:
		/**
		 * @brief Function which recursively sends all of the nodes in our tangle
		 * 
		 * @param requester - The peer to send nodes to
		 * @param t - Reference to the tangle (stores reference to the network and keys)
		 * @param node - The current node in the recursive call
		 */
		static void recursiveSendTangle(const breep::tcp::peer& requester, NetworkedTangle& t, const TransactionNode::ptr& node){
			// Send this node (make it a genesis sync if it is the genesis)
			if(node->isGenesis) t.network.send_object_to(requester, SyncGenesisRequest(*node, *t.personalKeys));
			else t.network.send_object_to(requester, SynchronizationAddTransactionRequest(*node, *t.personalKeys));

			// Recursively call for all of our children
			for(int i = 0; i < node->children.read_lock()->size(); i++)
				recursiveSendTangle(requester, t, node->children.read_lock()[i]);
		}
	};

	/**
	 * @brief Message which causes the tangle to update its weight
	 */
	struct UpdateWeightsRequest {
		/**
		 * @brief Listener for TangleSynchronizeRequest events. Creates a thread which trickles weights down the tangle
		 * 
		 * @param networkData - The event received
		 * @param t - The tangle which received the event
		 */
		static void listener(breep::tcp::netdata_wrapper<UpdateWeightsRequest>& networkData, NetworkedTangle& t){
			// Update all the weights (in a thread)
			std::thread([&t](){ t.updateCumulativeWeights(); }).detach();
			std::cout << "Started updating tangle weights" << std::endl;
		}
	};

	/**
	 * @brief Message which causes the recipent to update their genesis block (only valid if they are accepting of the change)
	 */
	struct SyncGenesisRequest {
		// Hash stored in the node
		Hash claimedHash = INVALID_HASH,
		// Calculated hash of the node
			actualHash = INVALID_HASH;
		// Signature which checks the validity of both hashes
		std::string validitySignature;
		// The node being sent
		Transaction genesis;

		SyncGenesisRequest() = default;
		/**
		 * @brief Constructs a sync request with automatic signing
		 * 
		 * @param _genesis - The transaction which should become the new genesis
		 * @param keys - Keypair used for signing
		 */
		SyncGenesisRequest(Transaction& _genesis, const key::KeyPair& keys) : claimedHash(_genesis.hash), actualHash(_genesis.hashTransaction()), validitySignature(key::signMessage(keys, claimedHash + actualHash)), genesis(_genesis) {}

		static void listener(breep::tcp::netdata_wrapper<SyncGenesisRequest>& networkData, NetworkedTangle& t);
	};

	/**
	 * @brief Base message which causes the recipient to add a new (non-genesis) transaction to their tangle
	 */
	struct AddTransactionRequestBase {
		// The hash of the transaction
		Hash validityHash = INVALID_HASH;
		// Signature verifying the integrity of the transaction
		std::string validitySignature;
		// The transaction to add to the tangle
		Transaction transaction;

		AddTransactionRequestBase() = default;
		/**
		 * @brief Constructs a sync request with automatic signing
		 * 
		 * @param _transaction - The transaction which should become the new genesis
		 * @param keys - Keypair used for signing
		 */
		AddTransactionRequestBase(Transaction& _transaction, const key::KeyPair& keys) : validityHash(_transaction.hash), validitySignature(key::signMessage(keys, validityHash)), transaction(_transaction) {}

		static void listener(breep::tcp::netdata_wrapper<AddTransactionRequestBase>& networkData, NetworkedTangle& t);

	protected:
		static void attemptToAddTransaction(const Transaction& transaction, HashVerificationPair validityPair, NetworkedTangle& t);
	};

	/**
	 * @brief Message which cause sthe recipient to add a new (non-genesis) transaction to their tangle
	 */
	struct AddTransactionRequest: public AddTransactionRequestBase {
		using AddTransactionRequestBase::AddTransactionRequestBase;

		static void listener(breep::tcp::netdata_wrapper<AddTransactionRequest>& networkData, NetworkedTangle& t){
			AddTransactionRequestBase::listener((*(breep::tcp::netdata_wrapper<AddTransactionRequestBase>*) &networkData), t);
		}
	};


	/**
	 * @brief Message which cause sthe recipient to add a new (non-genesis) transaction to their tangle (Has some specialized rules to make initial synchronization much faster)
	 */
	struct SynchronizationAddTransactionRequest: public AddTransactionRequestBase {
		using AddTransactionRequestBase::AddTransactionRequestBase;

		static void listener(breep::tcp::netdata_wrapper<SynchronizationAddTransactionRequest>& networkData, NetworkedTangle& t){
			t.updateWeights = false; // Flag us as NOT reclaculating weights
			AddTransactionRequestBase::listener((*(breep::tcp::netdata_wrapper<AddTransactionRequestBase>*) &networkData), t);
			t.updateWeights = true; // Flag us as reclaculating weights
		}
	};
};


// -- Message De/Serialization --


/*
 * All of these functions simply convert a particular type of message to/from a string
 */

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

// Empty serialization (no data to send)
inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::PublicKeySyncRequest& r) { return s; }
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::PublicKeySyncRequest& r) { return d; }
BREEP_DECLARE_TYPE(NetworkedTangle::PublicKeySyncRequest)

// Empty serialization (no data to send)
inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::GenesisVoteRequest& r) { return s; }
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::GenesisVoteRequest& r) { return d; }
BREEP_DECLARE_TYPE(NetworkedTangle::GenesisVoteRequest)

inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::GenesisVoteResponse& r) {
	s << r.genesisHashes;
	s << r.signature;
	return s;
}
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::GenesisVoteResponse& r) {
	d >> r.genesisHashes;
	d >> r.signature;
	return d;
}
BREEP_DECLARE_TYPE(NetworkedTangle::GenesisVoteResponse)

// Empty serialization (no data to send)
inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::TangleSynchronizeRequest& r) { return s; }
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::TangleSynchronizeRequest& r) { return d; }
BREEP_DECLARE_TYPE(NetworkedTangle::TangleSynchronizeRequest)

// Empty serialization (no data to send)
inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::UpdateWeightsRequest& r) { return s; }
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::UpdateWeightsRequest& r) { return d; }
BREEP_DECLARE_TYPE(NetworkedTangle::UpdateWeightsRequest)

// In .cpp
breep::serializer& operator<<(breep::serializer& _s, const NetworkedTangle::SyncGenesisRequest& r);
breep::deserializer& operator>>(breep::deserializer& _d, NetworkedTangle::SyncGenesisRequest& r);
BREEP_DECLARE_TYPE(NetworkedTangle::SyncGenesisRequest)

// In .cpp
breep::serializer& operator<<(breep::serializer& _s, const NetworkedTangle::AddTransactionRequest& r);
breep::deserializer& operator>>(breep::deserializer& _d, NetworkedTangle::AddTransactionRequest& r);
BREEP_DECLARE_TYPE(NetworkedTangle::AddTransactionRequest)

// In .cpp
breep::serializer& operator<<(breep::serializer& _s, const NetworkedTangle::SynchronizationAddTransactionRequest& r);
breep::deserializer& operator>>(breep::deserializer& _d, NetworkedTangle::SynchronizationAddTransactionRequest& r);
BREEP_DECLARE_TYPE(NetworkedTangle::SynchronizationAddTransactionRequest)

#endif /* end of include guard: NETWORKING_HPP */
