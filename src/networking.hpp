#ifndef NETWORKING_HPP
#define NETWORKING_HPP

#include "tangle.hpp"
#include "keys.hpp"
#include <map>
#include <list>

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
	breep::tcp::network& network;

	// This account's public and private keypair
	const std::shared_ptr<key::KeyPair> personalKeys;
	// Public keys for connected peers
	std::unordered_map<boost::uuids::uuid, key::PublicKey, boost::hash<boost::uuids::uuid>> peerKeys;

	NetworkedTangle(breep::tcp::network& network) : network(network) {
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
	void setKeyPair(const std::shared_ptr<key::KeyPair>& pair){
		util::makeMutable(personalKeys) = pair;
		peerKeys[network.self().id()] = personalKeys->pub;
	}

	// Adds a new node to the tangle (network synced)
	Hash add(TransactionNode::ptr node){
		Hash out = Tangle::add(node);
		network.send_object(AddTransactionRequest(*node, *personalKeys)); // The add gets validated by the base tangle, if we get to this code (no exception) then the node is acceptable
		return out;
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
	};
	std::list<TransactionAndHashVerificationPair> networkQueue;

protected:
	void connect_disconnectListener(breep::tcp::network& network, const breep::tcp::peer& peer) {
		// Someone connected...
		if (peer.is_connected()) {
			std::cout << peer.id() << " connected!" << std::endl;

		// Someone disconnected...
		} else {
			std::cout << peer.id() << " disconnected" << std::endl;

			// Erase all keys associated with the disconnected peer's id
			std::erase_if(peerKeys, [id = peer.id()](const auto& item) {
		        auto const& [key, value] = item;
		        return key == id;
		    });
		}
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
		static void listener(breep::tcp::netdata_wrapper<PublicKeySyncRequest>& networkData, NetworkedTangle& t){
			if(!t.personalKeys)
				throw key::InvalidKey("Missing Personal Keypair!");
			if(!t.personalKeys->validate())
				throw key::InvalidKey("Personal Keypair's public and private key were not created from eachother!");

			t.network.send_object_to(networkData.source, PublicKeySyncResponse(*t.personalKeys));
			std::cout << "Sent public key to `" << networkData.source.id() << "`" << std::endl;

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
			recursiveSendTangle(networkData.source, t, t.genesis);

			std::cout << "Sent tangle to `" << networkData.source.id() << "`" << std::endl;
		}

	protected:
		static void recursiveSendTangle(const breep::tcp::peer& requester, NetworkedTangle& t, const TransactionNode::ptr& node){
			if(node->hash == t.genesis->hash) t.network.send_object_to(requester, SyncGenesisRequest(*node, *t.personalKeys));
			else t.network.send_object_to(requester, SynchronizationAddTransactionRequest(*node, *t.personalKeys));

			for(auto& child: node->children)
				recursiveSendTangle(requester, t, child);
		}
	};

	// Message which causes the recipent to update their genesis block (only valid if they are accepting of the change)
	// TODO: Genesis needs to be key validated
	struct SyncGenesisRequest {
		Hash validityHash = INVALID_HASH;
		std::string validitySignature;
		Transaction genesis;
		SyncGenesisRequest() = default;
		SyncGenesisRequest(Transaction& _genesis, const key::KeyPair& keys) : validityHash(_genesis.hash), validitySignature(key::signMessage(keys, validityHash)), genesis(_genesis) {}

		static void listener(breep::tcp::netdata_wrapper<SyncGenesisRequest>& networkData, NetworkedTangle& t){
			// If we didn't request a new genesis... do nothing
			if(!t.listeningForGenesisSync)
				return;
			// Don't start with a new genesis if its hash matches the current genesis
			if(t.genesis->hash == networkData.data.genesis.hash)
				return;
			// If the remote transaction's hash doesn't match what is claimed... it has an invalid hash
			if(networkData.data.genesis.hash != networkData.data.validityHash)
				throw Transaction::InvalidHash(networkData.data.validityHash, networkData.data.genesis.hash); // TODO: Exception caught by Breep, need alternative error handling?

			// If we don't have the sender's public key, ask for it and then ask them to resend the tangle
			if(!t.peerKeys.contains(networkData.source.id())){
				t.network.send_object_to(networkData.source, PublicKeySyncRequest());
				t.network.send_object_to(networkData.source, TangleSynchronizeRequest());
				return;
			}
			// If we can't verify the transaction discard it
			if(!key::verifyMessage(t.peerKeys[networkData.source.id()], networkData.data.genesis.hash, networkData.data.validitySignature)){
				std::cerr << "Syncing of genesis block with hash `" + networkData.data.genesis.hash + "` failed, sender's identity failed to be verified, discarding." << std::endl;
				return;
			}

			std::vector<TransactionNode::ptr> parents; // Genesis transactions have no parents
			(*(TransactionNode::ptr*) &t.genesis) = TransactionNode::create(t, networkData.data.genesis);

			std::cout << "Synchronized new genesis with hash `" + t.genesis->hash + "`" << std::endl;
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
			// If the remote transaction's hash doesn't match what is claimed... it has an invalid hash
			if(networkData.data.transaction.hash != networkData.data.validityHash)
				throw Transaction::InvalidHash(networkData.data.validityHash, networkData.data.transaction.hash); // TODO: Exception caught by Breep, need alternative error handling?

			attemptToAddTransaction(networkData.data.transaction, {networkData.source.id(), networkData.data.validitySignature}, t);

			size_t listSize = t.networkQueue.size();
			for(size_t i = 0; i < listSize; i++){
				Transaction frontTrx = t.networkQueue.front().transaction;
				HashVerificationPair frontSig = t.networkQueue.front().pair;
				t.networkQueue.pop_front();
				attemptToAddTransaction(frontTrx, frontSig, t);
			}

			std::cout << "Processed remote transaction add with hash `" + networkData.data.transaction.hash + "` from " << networkData.source.id() << std::endl;
		}

	protected:
		static void attemptToAddTransaction(const Transaction& transaction, HashVerificationPair validityPair, NetworkedTangle& t){
			// If we don't have the peer's public key, request it and enqueue the transaction for later
			if(!t.peerKeys.contains(validityPair.peerID)){
				auto& peers = t.network.peers();
				auto& sender = peers.at(validityPair.peerID);
				t.network.send_object_to(sender, PublicKeySyncRequest());
				t.networkQueue.emplace_back();
				t.networkQueue.back().transaction = transaction;
				t.networkQueue.back().pair = validityPair;

				std::cout << "Received transaction add from unverified peer `" << validityPair.peerID << "`, enquing transaction with hash `" << transaction.hash << "` and requesting peer's key." << std::endl;
				return;
			}

			// If we can't verify the transaction discard it
			if(!key::verifyMessage(t.peerKeys[validityPair.peerID], transaction.hash, validityPair.signature)){
				std::cerr << "Transaction with hash `" + transaction.hash + "` sender's identity failed to be verified, discarding." << std::endl;
				return;
			}

			bool parentsFound = true;
			std::vector<TransactionNode::ptr> parents;
			for(Hash& hash: transaction.parentHashes){
				auto parent = t.find(hash);
				if(parent)
					parents.push_back(parent);
				else {
					t.networkQueue.emplace_back();
					t.networkQueue.back().transaction = transaction;
					t.networkQueue.back().pair = validityPair;
					parentsFound = false;
					std::cout << "Remote transaction with hash `" + transaction.hash + "` is temporarily orphaned... enqueuing for later" << std::endl;
					break;
				}
			}

			if(parentsFound) {
				(*(Tangle*) &t).add(TransactionNode::create(t, transaction)); // Call the tangle version so that we don't spam the network with extra messages
				std::cout << "Added remote transaction with hash `" + transaction.hash + "` to the tangle" << std::endl;
			}
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
			AddTransactionRequestBase::listener((*(breep::tcp::netdata_wrapper<AddTransactionRequestBase>*) &networkData), t); // TODO: is this a valid cast?
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

inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::SyncGenesisRequest& r) {
	s << r.validityHash;
	s << r.validitySignature;
	s << r.genesis;
	return s;
}
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::SyncGenesisRequest& r) {
	std::string validityHash;
	d >> validityHash;
	(*(std::string*) &r.validityHash) = validityHash;
	d >> r.validitySignature;
	d >> r.genesis;
	return d;
}
BREEP_DECLARE_TYPE(NetworkedTangle::SyncGenesisRequest)

inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::AddTransactionRequest& r) {
	s << r.validityHash;
	s << r.validitySignature;
	s << r.transaction;
	return s;
}
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::AddTransactionRequest& r) {
	std::string validityHash;
	d >> validityHash;
	(*(std::string*) &r.validityHash) = validityHash;
	d >> r.validitySignature;
	d >> r.transaction;
	return d;
}
BREEP_DECLARE_TYPE(NetworkedTangle::AddTransactionRequest)

inline breep::serializer& operator<<(breep::serializer& s, const NetworkedTangle::SynchronizationAddTransactionRequest& r) {
	s << r.validityHash;
	s << r.validitySignature;
	s << r.transaction;
	return s;
}
inline breep::deserializer& operator>>(breep::deserializer& d, NetworkedTangle::SynchronizationAddTransactionRequest& r) {
	std::string validityHash;
	d >> validityHash;
	(*(std::string*) &r.validityHash) = validityHash;
	d >> r.validitySignature;
	d >> r.transaction;
	return d;
}
BREEP_DECLARE_TYPE(NetworkedTangle::SynchronizationAddTransactionRequest)


#endif /* end of include guard: NETWORKING_HPP */
