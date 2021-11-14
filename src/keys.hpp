#ifndef KEYS_HPP
#define KEYS_HPP

#include "utility.hpp"
#include "cryptopp/eccrypto.h"
#include <breep/util/serialization.hpp>

namespace key {
	using Byte = CryptoPP::byte;
	// Key definitons
	using KeyBase = CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA3_256>;
	using PublicKey = KeyBase::PublicKey;
	using PrivateKey = KeyBase::PrivateKey;

	// Exception thrown when we can't generate a key
	struct InvalidKey : public std::runtime_error { using std::runtime_error::runtime_error; };

	// Pair of a public and private key
	struct KeyPair {
		const PrivateKey pri;
		const PublicKey pub;
	};

	// Function which generates a private and public key pair
	KeyPair generateKeyPair(const CryptoPP::OID& oid);

	// Functions which print out keys
	void print(const PrivateKey& key);
	void print(const PublicKey& key);
	void print(const KeyPair& pair);

	// Functions which convert keys to byte arrays
	std::vector<Byte> save(const PrivateKey& key);
	std::vector<Byte> save(const PublicKey& key);
	std::vector<Byte> save(const PrivateKey& pri, const PublicKey& pub);
	inline std::vector<Byte> save(const KeyPair& pair) { return save(pair.pri, pair.pub); }

	// Functions which convert byte arrays to keys
	PrivateKey loadPrivate(CryptoPP::VectorSource& source);
	inline PrivateKey loadPrivate(CryptoPP::VectorSource&& source) { return loadPrivate(source); }
	inline PrivateKey loadPrivate(std::vector<Byte>& source) { return loadPrivate({source, true}); }
	inline PrivateKey loadPrivate(std::vector<Byte>&& source) { return loadPrivate({source, true}); }
	PublicKey loadPublic(CryptoPP::VectorSource& source);
	inline PublicKey loadPublic(CryptoPP::VectorSource&& source) { return loadPublic(source); }
	inline PublicKey loadPublic(std::vector<Byte>& source) { return loadPublic({source, true}); }
	inline PublicKey loadPublic(std::vector<Byte>&& source) { return loadPublic({source, true}); }
	inline KeyPair load(CryptoPP::VectorSource& source) { return { loadPrivate(source), loadPublic(source) }; }
	inline KeyPair load(CryptoPP::VectorSource&& source) { return { loadPrivate(source), loadPublic(source) }; }
	inline KeyPair load(std::vector<Byte>& source) { return load({source, true}); }
	inline KeyPair load(std::vector<Byte>&& source) { return load({source, true}); }

	// Function which signs a message
	std::string signMessage(const PrivateKey& key, const std::string& message);
	inline std::string signMessage(const KeyPair& pair, const std::string& message) { return signMessage(pair.pri, message); }
	// Function which verifies that the given message is correctly signed
	bool verifyMessage(const PublicKey& key, const std::string& message, const std::string& signature);
	inline bool verifyMessage(const KeyPair& pair, const std::string& message, const std::string& signature) { return verifyMessage(pair.pub, message, signature); }

} // key

// De/serialization
inline breep::serializer& operator<<(breep::serializer& s, const key::PublicKey& pub) {
	// Convert the key into decompressed binary and send it
	std::vector<key::Byte> data = key::save(pub);
	s << util::compress(util::bytes2string(data));

	key::print(pub); // TODO: remove

	return s;
}
inline breep::deserializer& operator>>(breep::deserializer& d, key::PublicKey& pub) {
	// Read the compressed binary
	std::string compressed;
	d >> compressed;

	// Decompress it and load the key from it
	pub = key::loadPublic(util::string2bytes<key::Byte>(util::decompress(compressed)));

	key::print(pub); // TODO: remove

	return d;
}

#endif /* end of include guard: KEYS_HPP */
