#include "networking.hpp"

// Function which finds a free port to listen on
unsigned short determineLocalPort(){
	// Function which checks if a port is open
	auto portInUse = [](unsigned short port) -> bool {
	    boost::asio::io_service svc;
	    boost::asio::ip::tcp::acceptor a(svc);

		// Attempt to connect to the port
	    boost::system::error_code ec;
	    a.open(boost::asio::ip::tcp::v4(), ec) || a.bind({ boost::asio::ip::tcp::v4(), port }, ec);

		// Return if the port is currently being used
	    return ec == boost::asio::error::address_in_use;
	};

	// Start searching at the default port and increment until a port is found
	unsigned short localPortNumber = DEFAULT_PORT_NUMBER;
	while(portInUse(localPortNumber)) localPortNumber++;
	return localPortNumber;
}

// Structure which stores the result of a handshake, and a short header to validate the shake
struct Handshake {
	char H = 'H', A = 'A', N = 'N', D = 'D', S = 'S', K = 'K', E = 'E'; // Header (for validation)
	unsigned short port;
};

// Function which runs in a thread... looking for handshake pings
void handshake::acceptHandshakeConnection(boost::asio::ip::tcp::acceptor& acceptor, boost::asio::io_service& io_service, unsigned short localNetworkPort) {
	const int max_length = 1024;
	char data[max_length];
	Handshake hs;

	// Wait for a new connection
	boost::asio::ip::tcp::socket sock(io_service);
	acceptor.accept(sock);

	// Wait for a request on the connection
    boost::system::error_code error;
    sock.read_some(boost::asio::buffer(data), error);
    if ( !(error == boost::asio::error::eof || error == boost::system::errc::success) )
		throw boost::system::system_error(error); // Some other error.

	// If it is asking for our port... send them the port to connect to
	if(std::string(data) == "REMOTE PORT"){
		hs.port = localNetworkPort;
		sock.write_some(boost::asio::buffer(reinterpret_cast<char*>(&hs), sizeof(hs)));
	}
}

// Function which pings ports on a remote address for connectivity
unsigned short handshake::determineRemotePort(boost::asio::io_service& io_service, boost::asio::ip::address& address){
	unsigned short handshakePort = DEFAULT_PORT_NUMBER;
	unsigned short remotePort = -1;
	Handshake hs;

	// Try connecting to incremented ports for 5 seconds...
	auto start = std::chrono::steady_clock::now();
	while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 5000)
		try{
			// Ping for a handshake
			boost::asio::ip::tcp::socket sock(io_service);
			sock.connect(boost::asio::ip::tcp::endpoint(address, handshakePort++));
			std::string data = "REMOTE PORT";
			sock.write_some(boost::asio::buffer(data, data.size()));

			// Wait 500ms for a response
			if(readWithTimeout(io_service, sock, boost::asio::buffer(reinterpret_cast<char*>(&hs), sizeof(hs)), std::chrono::milliseconds(500))){ // TODO: is 500ms too short of a time frame?
				sock.close();

				// Quick validation of the returned data
				if(hs.H == 'H' && hs.A == 'A' && hs.N == 'N' && hs.D == 'D' && hs.S == 'S' && hs.K == 'K' && hs.E == 'E'){
					if(hs.port != std::numeric_limits<unsigned short>::max())
						// If the data looks good return the port
						return hs.port;
				}
			}

			// Make sure we close the socket before we open a new one
			sock.close();

		} catch(...) {}

	// If we couldn't connect in 5 seconds
	std::cout << "We were unable to automatically detect a network on `" << address.to_string() << "`" << std::endl << " please provide a port manually: ";
	std::cin >> remotePort;

	return remotePort;
}
