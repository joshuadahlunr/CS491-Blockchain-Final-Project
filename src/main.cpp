#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include <iostream>
#include <vector>
#include <chrono>
#include <array>
#include <fstream>
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

key::KeyPair loadKeyFile(std::ifstream& fin) {
	std::string buffer;
	fin.seekg(0l, std::ios::end);
	buffer.resize(fin.tellg());

	fin.seekg(0l, std::ios::beg);
	fin.clear();
	fin.read( reinterpret_cast<char*>(&buffer[0]), buffer.size() );
	fin.close();

	key::KeyPair keyPair = key::load(util::string2bytes<key::Byte>(util::decompress(buffer)));
	keyPair.validate();

	return keyPair;
}

void saveKeyFile(key::KeyPair& keyPair, std::ofstream& fout){
	auto buffer = util::compress(util::bytes2string(key::save(keyPair)));
	fout.write( reinterpret_cast<char*>(buffer.data()), buffer.size() );
}

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

	// Seed random number generation
	srand(time(0));
	// Make cout print numbers up to the million
	std::cout << std::setprecision(7);

	// Clean up the network connection and handshake thread if we are force shutdown
	signal(SIGINT, shutdownProcedure);

	// Create an IO service used by the handshake algorithm
	boost::asio::io_service io_service;

	// Find an open port for us to listen on and create a network listening on it
	unsigned short localPort = determineLocalPort();
	network = std::make_unique<breep::tcp::network>(localPort);
	// Create a network synched tangle
	NetworkedTangle t(*network);

	// Disabling all logs (set to 'warning' by default).
	network->set_log_level(breep::log_level::warning);


	// Generate or load a keypair
	{
		std::cout << "Enter relative path to your key file (blank to generate new account): ";
		std::string path;
		std::getline(std::cin, path);

		std::ifstream fin(path);
		if(!fin){
			t.setKeyPair(std::make_shared<key::KeyPair>( key::generateKeyPair(CryptoPP::ASN1::secp160r1()) ), /*networkSync*/ false);
			std::cout << "Generated new account" << std::endl;
		} else {
			t.setKeyPair(std::make_shared<key::KeyPair>( loadKeyFile(fin) ), /*networkSync*/ false);
			std::cout << "Loaded account stored in: " << path << std::endl;
		}
		fin.close();
	}


	// Establish a network...
	if (argc == 1) {
		// Runs the network in another thread.
		network->awake();
		// Create a keypair for the network
		std::shared_ptr<key::KeyPair> networkKeys = std::make_shared<key::KeyPair>(key::generateKeyPair(CryptoPP::ASN1::secp160r1()));

		// Create a genesis which gives the network key "infinate" money
		std::vector<TransactionNode::const_ptr> parents;
		std::vector<Transaction::Input> inputs;
		std::vector<Transaction::Output> outputs;
		outputs.push_back({networkKeys->pub, std::numeric_limits<double>::max()});
		t.setGenesis(TransactionNode::create(parents, inputs, outputs));

		// Add a key response listener that give each key that connects to the network a million money
		network->add_data_listener<NetworkedTangle::PublicKeySyncResponse>([networkKeys, &t](breep::tcp::netdata_wrapper<NetworkedTangle::PublicKeySyncResponse>& dw){
			std::thread([networkKeys, &t, source = dw.source](){
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				try {
					if(t.queryBalance(t.peerKeys[source.id()]) == 0){
						std::cout << "Sending `" << key::hash(t.peerKeys[source.id()]) << "` a million money!" << std::endl;

						std::vector<Transaction::Input> inputs;
						inputs.emplace_back(*networkKeys, 1000000);
						std::vector<Transaction::Output> outputs;
						outputs.emplace_back(t.peerKeys[source.id()], 1000000);
						t.add(TransactionNode::createAndMine(t, inputs, outputs, 1));
					}
				} catch (...){}
			}).detach();
		});

		// Send us a million money
		std::thread([networkKeys, &t](){
			std::cout << "Sending us a million money!" << std::endl;

			try {
				std::vector<Transaction::Input> inputs;
				inputs.emplace_back(*networkKeys, 1000000);
				std::vector<Transaction::Output> outputs;
				outputs.emplace_back(*t.personalKeys, 1000000);
				t.add(TransactionNode::createAndMine(t, inputs, outputs, 1));
			} catch (...){}
		}).detach();

		std::cout << "Established a network on port " << localPort << std::endl;

	// Otherwise connect to the network
	} else {
		std::cout << "Attempting to automatically connect to the network..." << std::endl;

		// Find network connection (if we can't quickly find one ask for a manual port number)
		boost::asio::ip::address address = boost::asio::ip::address::from_string(argv[1]);
		unsigned short remotePort = handshake::determineRemotePort(io_service, address);
		if(!network->connect(address, remotePort)){ // TODO: Hangs on invalid connection
			std::cout << "Failed to connect to the network" << std::endl;
			return 2;
		}

		std::thread([&t, localPort](){
			// Wait half a second
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			// Send our public key to the rest of the network
			network->send_object(NetworkedTangle::PublicKeySyncRequest());

			// Wait half a second
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			std::cout << "Connected to the network (listening on port " << localPort << ")" << std::endl;

			// If we are a client... ask the network to vote on our new genesis
			network->send_object(NetworkedTangle::GenesisVoteRequest(t));
		}).detach();
	}


	// Find an open port for the handshake listener, and create a thread accepting handshakes
	auto lp = determineLocalPort();
	boost::asio::ip::tcp::acceptor acceptor(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), lp));
	handshakeThread = std::thread([&acceptor, &io_service, localPort](){
		while(handshakeThreadShouldRun)
			handshake::acceptHandshakeConnection(acceptor, io_service, localPort);
	});
	std::cout << "Started handshake listener on port " << lp << std::endl;


	// Explain how to get to help message
	std::cout << "Press `h` for additional instruction" << std::endl;


	breep::listener_id pingingID = 0;
	std::atomic<size_t> pingingThreads = 0;
	char cmd;
	while((cmd = tolower(std::cin.get())) != 'q') {
		switch(cmd){
		// Query our balance
		case 'b':
			{
				std::cout << "Our (" << key::hash(*t.personalKeys) << ") balance is: " << t.queryBalance(t.personalKeys->pub) << "(0%) " << t.queryBalance(t.personalKeys->pub, .5) << "(50%) " <<  t.queryBalance(t.personalKeys->pub, .95) << "(95%)"<< std::endl;
			}
			break;

		// Clear the screen
		case 'c':
			system("clear");
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
				std::cout << "Enter transaction hash (blank = skip): ";
				std::getline(std::cin, hash);

				// Print out the requested transaction
				auto trx = t.find(hash);
				if(trx)
					trx->debugDump();
			}
			break;

		case 'h':
			{
				std::cout << "Tangle operations:" << std::endl
					<< "(b)alance - Query our current balance (also displays our address)" << std::endl
					<< "(c)lear - Clear the screen" << std::endl
					<< "(d)ebug - Display a debug output of the tangle and (optionally) a transaction in the tangle" << std::endl
					<< "(h)elp - Show this help message" << std::endl
					<< "(g)enerate - Generates the Latest Common Genesis and prunes the tangle" << std::endl
					<< "(k)ey management - Options to manage your keys" << std::endl
					<< "(p)inging toggle - Toggle weather recieved transactions should be immediately forwarded elsewhere" << std::endl << "\t(simulates a more vibrant network)" << std::endl
					<< "(s)ave - Save the tangle to a file" << std::endl
					<< "(l)oad - Loads a tangle from a file" << std::endl
					<< "(t)ransaction - Create a new transaction" << std::endl
					<< "(w)eights - Manually start propigating weights through the tangle" << std::endl
					<< std::endl
					<< "Select an operation:" << std::endl;
			}
			break;

		// Generate the latest common genesis and prunes the tree
		case 'g':
			{
				t.prune();
				t.genesis->debugDump();
			}
			break;

		// Key management
		case 'k':
			{
				std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Ignore everything up until a new line
				std::cout << "(l)oad, (s)ave, (g)enerate: ";
				std::string _cmd = "";
				std::getline(std::cin, _cmd);
				std::cout << _cmd << std::endl;
				char cmd = tolower(_cmd[0]);

				std::string path = "";
				if(cmd == 's' || cmd == 'l'){
					std::cout << "Relative path: ";
					std::getline(std::cin, path);
				}

				if(cmd == 'g'){
					auto keyPair = std::make_shared<key::KeyPair>( key::generateKeyPair(CryptoPP::ASN1::secp160r1()) );
					keyPair->validate();

					// Update our key and send it to the rest of the network
					t.setKeyPair(keyPair);
				} else if(cmd == 's'){
					std::ofstream fout(path, std::ios::binary);
					if(!fout){
						std::cerr << "Invalid path: `" << path << "`!" << std::endl;
						continue;
					}

					saveKeyFile(*t.personalKeys, fout);
					fout.close();
				} else {
					std::ifstream fin(path, std::ios::binary);
					if(!fin){
						std::cerr << "Invalid path: `" << path << "`!" << std::endl;
						continue;
					}

					// Update our key and send it to the rest of the network
					t.setKeyPair( std::make_shared<key::KeyPair>(loadKeyFile(fin)) );
					fin.close();
				}
			}
			break;

		// Toggle pinging transactions
		case 'p':
			{
				if(pingingID){
					if(network->remove_data_listener<NetworkedTangle::AddTransactionRequest>(pingingID)){
						pingingID = 0;
						std::cout << "Stopped pinging transactions" << std::endl;
					}
				} else {
					pingingID = network->add_data_listener<NetworkedTangle::AddTransactionRequest>([&t, &pingingThreads] (breep::tcp::netdata_wrapper<NetworkedTangle::AddTransactionRequest>& dw) -> void {
						double recieved = 0;
						for(const Transaction::Output& output: dw.data.transaction.outputs)
							recieved += output.amount;

						// Only allow there to be 1 active pinging threads
						if(pingingThreads < 1)
							std::thread([&t, &pingingThreads, recieved, hash = dw.data.transaction.hash](){
								pingingThreads++;
								std::this_thread::sleep_for(std::chrono::milliseconds(500));

								// Check that the transaction was approved
								if(t.find(hash) && network->peers().size()){
									size_t id = rand() % network->peers().size();
									auto chosen = network->peers().begin();
									for(int i = 0; i < id; i++) chosen++;

									auto account = t.peerKeys[chosen->second.id()];

									try{
										// Create transaction inputs and outputs
										std::vector<Transaction::Input> inputs;
										inputs.emplace_back(*t.personalKeys, recieved);
										std::vector<Transaction::Output> outputs;
										outputs.emplace_back(account, recieved);

										// Create, mine, and add the transaction
										std::cout << "Pinging " << recieved << " money"/*to " << key::hash(account)*/ << std::endl;
										t.add(TransactionNode::createAndMine(t, inputs, outputs, /*difficulty*/ 3));
									} catch (Tangle::InvalidBalance ib) {
										std::cerr << ib.what() << " Discarding transaction!" << std::endl;
									} catch (NetworkedTangle::InvalidAccount ia) {
										std::cerr << ia.what() << " Discarding transaction!" << std::endl;
									}
								}
								pingingThreads--;
							}).detach();
					}).id();

					if(pingingID)
						std::cout << "Started pinging transactions" << std::endl;
				}
			}
			break;

		// Save tangle
		case 's':
			{
				std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Ignore everything up until a new line
				std::cout << "Enter relative path to save tangle to: ";
				std::string path;
				std::getline(std::cin, path);

				std::ofstream fout(path, std::ios::binary);
				if(!fout){
					std::cerr << "Invalid path: `" << path << "`!" << std::endl;
					continue;
				}

				t.saveTangle(fout);
				fout.close();

				std::cout << "Tangle saved to " << path << std::endl;
			}
			break;

		// Load tangle
		case 'l':
			{
				std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Ignore everything up until a new line
				std::cout << "Enter relative path to load tangle from: ";
				std::string path;
				std::getline(std::cin, path);

				std::ifstream fin(path, std::ios::binary);
				if(!fin){
					std::cerr << "Invalid path: `" << path << "`!" << std::endl;
					continue;
				}

				fin.seekg(0l, std::ios::end);
				size_t size = fin.tellg();
				fin.seekg(0l, std::ios::beg);
				fin.clear();
				t.loadTangle(fin, size);
				fin.close();

				std::cout << "Successfully loaded tangle from " << path << std::endl;
			}
			break;

		// Create transaction
		case 't':
			{
				// Ask who to send too and how much to send
				std::string accountHash;
				uint difficulty;
				double amount;
				std::cout << "Enter account to transfer to ('r' for random): ";
				std::cin >> accountHash;
				std::cout << "Enter amount to transfer: ";
				std::cin >> amount;
				std::cout << "Select mining difficulty (1-5): ";
				std::cin >> difficulty;

				// If they asked for random choose a random account
				if(accountHash == "r" && !network->peers().empty()){
					size_t id = rand() % network->peers().size();
					auto chosen = network->peers().begin();
					for(int i = 1; i < id; i++) chosen++;

					if(t.peerKeys.contains(chosen->second.id()))
						accountHash = key::hash(t.peerKeys[chosen->second.id()]);
				}
				// If we failed to find a random account to send to... send to ourselves
				if(accountHash == "r")
					accountHash = key::hash(t.personalKeys->pub);

				try{
					// Create transaction inputs and outputs
					std::vector<Transaction::Input> inputs;
					inputs.emplace_back(*t.personalKeys, amount);
					std::vector<Transaction::Output> outputs;
					outputs.emplace_back(t.findAccount(accountHash), amount);

					// Create, mine, and add the transaction
					std::cout << "Sending " << amount << " money to " << accountHash << std::endl;
					t.add(TransactionNode::createAndMine(t, inputs, outputs, difficulty));
				} catch (Tangle::InvalidBalance ib) {
					std::cerr << ib.what() << " Discarding transaction!" << std::endl;
				} catch (NetworkedTangle::InvalidAccount ia) {
					std::cerr << ia.what() << " Discarding transaction!" << std::endl;
				}
			}
			break;

		// Update the weights in the tangle
		case 'w':
			{
				t.network.send_object_to_self(NetworkedTangle::UpdateWeightsRequest());
			}
			break;
		}

		std::cin.clear();
	}

	// Clean up
	shutdownProcedure(0);
}
