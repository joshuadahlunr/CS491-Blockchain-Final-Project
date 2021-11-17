#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include <iostream>
#include <vector>
#include <chrono>
#include <array>
#include <signal.h>
#include <boost/uuid/uuid_io.hpp>
#include <breep/network/tcp.hpp>
#include <breep/util/serialization.hpp>

#include "utility.hpp"
#include "transaction.hpp"
#include "tangle.hpp"
#include "networking.hpp"

#include "keys.hpp"
#include "cryptopp/oids.h"

bool handshakeThreadShouldRun = true;
std::unique_ptr<breep::tcp::network> network;
std::thread handshakeThread;

// Function which handles cleaning up the program (used for normal termination and gracefully cleaning up when terminated)
void shutdownProcedure(int signal){
	// Clean up the handshake thread (if started)
	if(handshakeThread.joinable()){
		handshakeThreadShouldRun = false;
		handshakeThread.detach();
		std::cout << "Stopped handshake listener" << std::endl;
	}

	// Disconnect from the network (if connected)
	if(network){
		network->disconnect();
		std::cout << "Disconnected from the network" << std::endl;
	}

	// Force quit the program (required when this is called from a signal)
	std::exit(signal);
}

int main(int argc, char* argv[]) {
	if (argc != 1 && argc != 2) {
		std::cout << "Usage: " << argv[0] << " [<target ip>]" << std::endl;
		return 1;
	}

	// Clean up the network connection and handshake thread if we are force shutdown
	signal(SIGINT, shutdownProcedure);

	// Create an IO service used by the handshake algorithm
	boost::asio::io_service io_service;

	// Find an open port for us to listen on and create a network listening on it
	unsigned short localPort = determineLocalPort();
	network = std::make_unique<breep::tcp::network>(localPort);
	// Create a network synched tangle
	NetworkedTangle t(*network);

	// TODO: Need a mechanism for saving generated keys and loading them
	t.setKeyPair( std::make_shared<key::KeyPair>(key::generateKeyPair(CryptoPP::ASN1::secp160r1())) );

	// Disabling all logs (set to 'warning' by default).
	network->set_log_level(breep::log_level::none);

	// If we receive a class for which we don't have any listener (such as an int, for example), this will be called.
	network->set_unlistened_type_listener([](breep::tcp::network&,const breep::tcp::peer&,breep::deserializer&,bool,uint64_t) -> void {
		std::cout << "Unidentified message received!" << std::endl;
	});


	// Connect to the network
	if (argc == 1) {
		// runs the network in another thread.
		network->awake();

		std::cout << "Established a network on port " << localPort << std::endl;

		std::vector<Transaction::Input> inputs;
		inputs.emplace_back(*t.personalKeys, 100.0);
		std::vector<Transaction::Output> outputs;
		outputs.push_back({t.personalKeys->pub, 100.0});

		// If we are the host add a few transactions to the tangle
		t.add(TransactionNode::create(t.getTips(), inputs, outputs) );
		t.add(TransactionNode::create(t.getTips(), inputs, outputs) );
	} else {
		std::cout << "Attempting to automatically connect to the network..." << std::endl;

		// Find network connection (if we can't quickly find one ask for a manual port number)
		boost::asio::ip::address address = boost::asio::ip::address::from_string(argv[1]);
		unsigned short remotePort = handshake::determineRemotePort(io_service, address);
		if(!network->connect(address, remotePort)){ // TODO: Hangs on invalid connection
			std::cout << "Failed to connect to the network" << std::endl;
			return 2;
		}

		// Send our public key to the rest of the network
		network->send_object(NetworkedTangle::PublicKeySyncRequest());
		// Wait half a second
		std::this_thread::sleep_for(std::chrono::milliseconds(500)); // TODO: Make this delay unessicary

		std::cout << "Connected to the network (listening on port " << localPort << ")" << std::endl;

		// If we are a client... ask the network for the tangle
		network->send_object(NetworkedTangle::TangleSynchronizeRequest(t)); // TODO: Only force us to sync keys with people we actually communicate with
	}


	// Find an open port for the handshake listener, and create a thread accepting handshakes
	auto lp = determineLocalPort();
	boost::asio::ip::tcp::acceptor acceptor(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), lp));
	handshakeThread = std::thread([&acceptor, &io_service, localPort](){
		while(handshakeThreadShouldRun)
			handshake::acceptHandshakeConnection(acceptor, io_service, localPort);
	});
	std::cout << "Started handshake listener on port " << lp << std::endl;





	char cmd;
	while((cmd = tolower(std::cin.get())) != 'q') {
		switch(cmd){
		// Create transaction
		case 't':
			{
				std::vector<Transaction::Input> inputs;
				inputs.emplace_back(*t.personalKeys, 100.0 + std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
				std::vector<Transaction::Output> outputs;
				outputs.push_back({t.peerKeys[network->peers().begin()->second.id()], 100.0});

				t.add(TransactionNode::create(t.getTips(), inputs, outputs));
			}
			break;

		// Debug output
		case 'd':
			{
				// Print out the whole tangle
				t.debugDump();
				std::cout << std::endl;

				// Read transaction hash
				std::string hash = "";
				std::getline(std::cin, hash);
				std::cout << "Enter transaction hash (blank for genesis): ";
				std::getline(std::cin, hash);

				// Print out the requested transaction, or the genesis tranaction if no transaction provided
				auto trx = t.find(hash);
				if(trx) trx->debugDump();
				else t.genesis->debugDump();
			}
			break;
		}
	}

	// Clean up
	shutdownProcedure(0);
}
