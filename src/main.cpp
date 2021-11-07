#define BOOST_BIND_GLOBAL_PLACEHOLDERS

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                               //
// Copyright 2017 Lucas Lazare.                                                                  //
// This file is part of Breep project which is released under the                                //
// European Union Public License v1.1. If a copy of the EUPL was                                 //
// not distributed with this software, you can obtain one at :                                   //
// https://joinup.ec.europa.eu/community/eupl/og_page/european-union-public-licence-eupl-v11     //
//                                                                                               //
///////////////////////////////////////////////////////////////////////////////////////////////////


/**
 * @file chat/main.cpp
 * @author Lucas Lazare
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <array>
#include <boost/uuid/uuid_io.hpp>
#include <breep/network/tcp.hpp>
#include <breep/util/serialization.hpp>

#include "utility.hpp"
#include "transaction.hpp"
#include "tangle.hpp"
#include "networking.hpp"

int main(int argc, char* argv[]) {
	if (argc != 2 && argc != 4) {
		std::cout << "Usage: " << argv[0] << " <hosting port> [<target ip> <target port>]" << std::endl;
		return 1;
	}

	breep::tcp::network network(static_cast<unsigned short>(std::atoi(argv[1])));
	NetworkedTangle t(network);

	// Disabling all logs (set to 'warning' by default).
	network.set_log_level(breep::log_level::none);

	// If we receive a class for which we don't have any listener (such as an int, for example), this will be called.
	network.set_unlistened_type_listener([](breep::tcp::network&,const breep::tcp::peer&,breep::deserializer&,bool,uint64_t) -> void {
		std::cout << "Unidentified message received!" << std::endl;
	});

	if (argc == 2) {
		// runs the network in another thread.
		network.awake();

		// If we are the host add a few transactions to the tangle
		t.add(TransactionNode::create({t.getTips()[0]}, 100) );
		t.add(TransactionNode::create({t.getTips()[0]}, 200) );
	} else {
		// let's try to connect to a buddy at address argv[2] and port argv[3]
		boost::asio::ip::address address = boost::asio::ip::address::from_string(argv[2]);
		if(!network.connect(address, static_cast<unsigned short>(atoi(argv[3])))) {
			// oh noes, it failed!
			std::cout << "Connection failed." << std::endl;
			return 1;
		}

		// If we are a client... ask the host for the tangle
		network.send_object(NetworkedTangle::TangleSynchronizeRequest(t));
	}

	std::cout << "Connected to the network" << std::endl;

	char cmd;
	while((cmd = tolower(std::cin.get())) != 'q') {
		if(cmd == 't')
			t.add(TransactionNode::create(t.getTips(), 100 +
				std::chrono::duration_cast<std::chrono::hours>(std::chrono::high_resolution_clock::now().time_since_epoch()).count())
			);
	}

	network.disconnect();
	std::cout << "Disconnected from the network" << std::endl;

	std::cout << t.getTips()[0]->hash << std::endl;
	// Tangle g;
	// TransactionNode::ptr t = std::make_shared<TransactionNode>(std::vector<TransactionNode::ptr>{g.genesis}, 27.8);
	// g.add(t);
	//
	// std::cout << g.genesis->hash << " - " << g.genesis->amount << std::endl;
	// std::cout << t->hash << " - " << t->amount << " - " << g.genesis->children[0]->hash << std::endl;
	//
	// std::cout << g.getTips()[0]->hash << std::endl;
	// g.removeTip(t);
	// std::cout << g.getTips()[0]->hash << std::endl;
}
