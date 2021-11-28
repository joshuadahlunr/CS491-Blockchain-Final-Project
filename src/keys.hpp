/**
 * @file keys.hpp
 * @author Joshua Dahl (jdahl@unr.edu)
 * @brief File which specifies key generation, verification, and signature
 * @version 0.1
 * @date 2021-11-28
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#ifndef KEYS_HPP
#define KEYS_HPP

#include "utility.hpp"
#include "cryptopp/eccrypto.h"
#include <breep/util/serialization.hpp>

namespace key {
	// Key definitions
	using byte = CryptoPP::byte;
	// We are using SHA3_256 backed ECC keys
	using KeyBase = CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA3_256>;
	using PublicKey = KeyBase::PublicKey;
	using PrivateKey = KeyBase::PrivateKey;

	/**
	 * @brief Exception thrown when we can't generate/verify a key
	 */
	struct InvalidKey : public std::runtime_error { using std::runtime_error::runtime_error; };

	/**
	 * @brief Pair of a public and private key
	 */
	struct KeyPair {
		const PrivateKey pri;
		const PublicKey pub;

		// Function which checks if the key pair properly sign and verify eachother
		bool validate();
	};

	// Function which generates a private and public key pair
	KeyPair generateKeyPair(const CryptoPP::OID& oid);

	// Functions which print out keys
	void print(const PrivateKey& key);
	void print(const PublicKey& key);
	void print(const KeyPair& pair);

	// Functions which convert keys to byte arrays
	std::vector<byte> save(const PrivateKey& key);
	std::vector<byte> save(const PublicKey& key);
	std::string saveBase64(const PublicKey& key);
	std::vector<byte> save(const PrivateKey& pri, const PublicKey& pub);
	inline std::vector<byte> save(const KeyPair& pair) { return save(pair.pri, pair.pub); }

	// Functions which convert a public key into a hash
	inline std::string hash(const PublicKey& key) { return util::hash( util::bytes2string(key::save(key)) ); }
	inline std::string hash(const KeyPair& pair) { return hash(pair.pub); }

	// Functions which convert byte arrays to keys
	PrivateKey loadPrivate(CryptoPP::VectorSource& source);
	inline PrivateKey loadPrivate(CryptoPP::VectorSource&& source) { return loadPrivate(source); }
	inline PrivateKey loadPrivate(const std::vector<byte>& source) { return loadPrivate({source, true}); }
	inline PrivateKey loadPrivate(const std::vector<byte>&& source) { return loadPrivate({source, true}); }
	PublicKey loadPublic(CryptoPP::VectorSource& source);
	PublicKey loadPublicBase64(const std::string& source);
	inline PublicKey loadPublic(CryptoPP::VectorSource&& source) { return loadPublic(source); }
	inline PublicKey loadPublic(const std::vector<byte>& source) { return loadPublic({source, true}); }
	inline PublicKey loadPublic(const std::vector<byte>&& source) { return loadPublic({source, true}); }
	inline KeyPair load(CryptoPP::VectorSource& source) { return { loadPrivate(source), loadPublic(source) }; }
	inline KeyPair load(CryptoPP::VectorSource&& source) { return { loadPrivate(source), loadPublic(source) }; }
	inline KeyPair load(const std::vector<byte>& source) { return load({source, true}); }
	inline KeyPair load(const std::vector<byte>&& source) { return load({source, true}); }

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
	std::vector<key::byte> data = key::save(pub);
	s << util::compress(util::bytes2string(data));

	return s;
}
inline breep::deserializer& operator>>(breep::deserializer& d, key::PublicKey& pub) {
	// Read the compressed binary
	std::string compressed;
	d >> compressed;

	// Decompress it and load the key from it
	pub = key::loadPublic(util::string2bytes<key::byte>(util::decompress(compressed)));

	return d;
}

#endif /* end of include guard: KEYS_HPP */
