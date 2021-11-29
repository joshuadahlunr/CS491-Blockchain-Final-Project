/**
 * @file main.cpp
 * @author Joshua Dahl (jdahl@unr.edu)
 * @brief Entrypoint/Driver for this tangle implementation
 * @version 0.1
 * @date 2021-11-29
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include <fstream>
#include <signal.h>

#include "cryptopp/oids.h"
#include "networking.hpp"

// Bool marking that the handshake thread should shutdown
bool handshakeThreadShouldRun = true;
// Pointer to the peer-to-peer network network
std::unique_ptr<breep::tcp::network> network;
// Reference to the thread responsible for handshaking
std::thread handshakeThread;

/**
 * @brief Function which loads a keypair from a file
 * @note Saved keys are compressed
 * 
 * @param fin Filestream to load from
 * @return key::KeyPair - The returned keys
 */
key::KeyPair loadKeyFile(std::ifstream& fin) {
	// Calculate the size of the file, and create a string large enough to hold it
	std::string buffer;
	fin.seekg(0l, std::ios::end);
	buffer.resize(fin.tellg());

	// Read the data into the string we created
	fin.seekg(0l, std::ios::beg);
	fin.clear();
	fin.read( reinterpret_cast<char*>(&buffer[0]), buffer.size() );

	// Decompress the keys and convert them to a key pair
	key::KeyPair keyPair = key::load( util::string2bytes<key::byte>(util::decompress(buffer)) );
	// Validate the keypair
	keyPair.validate();

	return keyPair;
}

/**
 * @brief Function which saves a key to a file
 * @note Saved keys are compressed
 * 
 * @param keyPair - The keys to save
 * @param fout - The file stream to save the keys to
 */
void saveKeyFile(key::KeyPair& keyPair, std::ofstream& fout){
	auto buffer = util::compress(util::bytes2string(key::save(keyPair)));
	fout.write( reinterpret_cast<char*>(buffer.data()), buffer.size() );
}

/**
 * @brief Function which handles cleaning up the program (used for normal termination and gracefully cleaning up when terminated)
 * 
 * @param signal - The interrupt signal which caused this function to be called 
 */
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

/**
 * @brief Main function
 */
int main(int argc, char* argv[]) {
	// If we are given an invalid number of arguments, explain to the user how to use the program
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


	// Find an open port for the handshake listener, and create a thread accepting handshakes
	auto handshakePort = determineLocalPort();
	boost::asio::ip::tcp::acceptor acceptor(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), handshakePort));
	// Find another open thread for the networkd
	unsigned short networkPort = determineLocalPort();
	handshakeThread = std::thread([&acceptor, &io_service, networkPort](){
		while(handshakeThreadShouldRun)
			handshake::acceptHandshakeConnection(acceptor, io_service, networkPort);
	});
	std::cout << "Started handshake listener on port " << handshakePort << std::endl;


	// reate a network listening on the network port we found
	network = std::make_unique<breep::tcp::network>(networkPort);
	// Create a network synched tangle
	NetworkedTangle t(*network);


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


	// Establish a network if not given an IP to connect to
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
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				try {
					// Only give the connecting peer money if they don't have any
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

		std::cout << "Established a network on port " << networkPort << std::endl;

	// Otherwise connect to the network...
	} else {
		std::cout << "Attempting to automatically connect to the network..." << std::endl;

		// Find network connection (if we can't quickly find one ask for a manual port number)
		boost::asio::ip::address address = boost::asio::ip::address::from_string(argv[1]);
		unsigned short remotePort = handshake::determineRemotePort(io_service, address);
		if(!network->connect(address, remotePort)){ // TODO: Hangs on invalid connection
			std::cout << "Failed to connect to the network" << std::endl;
			return 2;
		}

		std::thread([&t, networkPort](){
			// Wait half a second
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			// Send our public key to the rest of the network
			network->send_object(NetworkedTangle::PublicKeySyncRequest());

			// Wait half a second
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::cout << "Connected to the network (listening on port " << networkPort << ")" << std::endl;

			// If we are a client... ask the network to vote on our new genesis
			network->send_object(NetworkedTangle::GenesisVoteRequest(t));
		}).detach();
	}


	// Explain how to get to help message
	std::cout << "Press `h` for additional instruction" << std::endl;


	// Menu loop
	char cmd;
	while((cmd = tolower(std::cin.get())) != 'q') {
		switch(cmd){
		// Query our balance
		case 'b':
			{
				std::cout << "Our (Account = " << key::hash(*t.personalKeys) << ") balance is: " << t.queryBalance(t.personalKeys->pub) << "(0%) " << t.queryBalance(t.personalKeys->pub, .5) << "(50%) " <<  t.queryBalance(t.personalKeys->pub, .95) << "(95%)"<< std::endl;
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

		// Help
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
					<< "(s)ave <file> - Save the tangle to a file" << std::endl
					<< "(l)oad <file> - Loads a tangle from a file" << std::endl
					<< "(t)ransaction - Create a new transaction" << std::endl
					<< "(w)eights - Manually start propigating weights through the tangle" << std::endl
					<< "(q)uit - Quits the program" << std::endl
					<< std::endl
					<< "Select an operation:" << std::endl;
			}
			break;

		// Generates the latest common genesis and prunes the tree
		case 'g':
			{
				t.prune();
				t.genesis->debugDump();
			}
			break;

		// Key management
		case 'k':
			{
				// Determine which operation we should perform
				std::cout << "(l)oad keys, (s)ave keys, (g)enerate keys: ";
				std::string _cmd = "";
				std::getline(std::cin, _cmd);
				std::cout << _cmd << std::endl;
				char cmd = tolower(_cmd[0]);

				// If we are saving or loading... determine the path to do it to
				std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Ignore everything up until a new line
				std::string path = "";
				if(cmd == 's' || cmd == 'l'){
					std::cout << "Relative path: ";
					std::getline(std::cin, path);
				}

				// Generate new key pair
				if(cmd == 'g'){
					auto keyPair = std::make_shared<key::KeyPair>( key::generateKeyPair(CryptoPP::ASN1::secp160r1()) );
					keyPair->validate();

					// Update our key and send it to the rest of the network
					t.setKeyPair(keyPair);

				// Save current keypair to a file
				} else if(cmd == 's'){
					std::ofstream fout(path, std::ios::binary);
					if(!fout){
						std::cerr << "Invalid path: `" << path << "`!" << std::endl;
						continue;
					}

					saveKeyFile(*t.personalKeys, fout);
					fout.close();
				
				// Load new keypair from a file
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
				// ID of the pining listener (used to remove it later)
				static breep::listener_id pingingID = 0;
				// The number of threads actively pinging
				static std::atomic<size_t> pingingThreads = 0;
				
				// If we currently have pinging enabled... disable it
				if(pingingID){
					if(network->remove_data_listener<NetworkedTangle::AddTransactionRequest>(pingingID)){
						pingingID = 0;
						std::cout << "Stopped pinging transactions" << std::endl;
					}

				// Otherwise add a new listerner to ping transactions
				} else {
					pingingID = network->add_data_listener<NetworkedTangle::AddTransactionRequest>([&t] (breep::tcp::netdata_wrapper<NetworkedTangle::AddTransactionRequest>& dw) -> void {
						// Calculate how much we recieved from this transaction
						double recieved = 0;
						for(const Transaction::Output& output: dw.data.transaction.outputs)
							recieved += output.amount;

						// Only allow there to be 1 active pinging threads
						if(pingingThreads < 1)
							std::thread([&t, recieved, hash = dw.data.transaction.hash](){
								// Increment the thread count
								pingingThreads++;
								std::this_thread::sleep_for(std::chrono::milliseconds(100));

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

								// Decrement the thread count
								pingingThreads--;
							}).detach();
					}).id();

					// If we successfully added a new ping listener... tell the user
					if(pingingID)
						std::cout << "Started pinging transactions" << std::endl;
				}
			}
			break;

		// Save tangle
		case 's':
			{
				// Determine the path to save to
				// std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Ignore everything up until a new line
				std::cout << "Enter relative path to save tangle to: ";
				std::string path;
				std::getline(std::cin, path);

				// Open a connection to the file
				std::ofstream fout(path, std::ios::binary);
				if(!fout){
					std::cerr << "Invalid path: `" << path << "`!" << std::endl;
					continue;
				}

				// Save the tangle
				t.saveTangle(fout);
				fout.close();

				std::cout << "Tangle saved to " << path << std::endl;
			}
			break;

		// Load tangle
		case 'l':
			{
				// Determine the path to load from
				// std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Ignore everything up until a new line
				std::cout << "Enter relative path to load tangle from: ";
				std::string path;
				std::getline(std::cin, path);

				// Open a connection to the file
				std::ifstream fin(path, std::ios::binary);
				if(!fin){
					std::cerr << "Invalid path: `" << path << "`!" << std::endl;
					continue;
				}

				// Determine the size of the file
				fin.seekg(0l, std::ios::end);
				size_t size = fin.tellg();
				fin.seekg(0l, std::ios::beg);
				fin.clear();
				// Load the tangle
				t.loadTangle(fin, size);
				fin.close();

				std::cout << "Successfully loaded tangle from " << path << std::endl;
			}
			break;

		// Create transaction
		case 't':
			{
				// Ask who to send too, how much to send, and how much effort to put into mining
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

		// Clear any errors in cin
		std::cin.clear();
	}

	// Clean up
	shutdownProcedure(0);
}
