#include "keys.hpp"

#include <iostream>
#include "cryptopp/osrng.h"

namespace key {

	// Function which checks if the key pair properly sign and verify eachother
	bool KeyPair::validate(){
		std::string message = "VALIDATION";
		std::string signature = signMessage(*this, message);
		return verifyMessage(*this, message, signature);
	}

	// Function which generates a private key
	PrivateKey generatePrivateKey(const CryptoPP::OID& oid) {
		CryptoPP::AutoSeededRandomPool prng;
		PrivateKey key;

		key.Initialize(prng, oid);
		if(!key.Validate(prng, 3))
			throw InvalidKey("Private Key failed to pass validation.");

		return key;
	}

	// Function which generates a public key
	PublicKey generatePublicKey(const PrivateKey& privateKey) {
		CryptoPP::AutoSeededRandomPool prng;
		PublicKey publicKey;

		privateKey.MakePublicKey(publicKey);
		if(!publicKey.Validate(prng, 3))
			throw InvalidKey("Public Key failed to pass validation.");

		return publicKey;
	}

	// Function which generates a private and public key pair
	KeyPair generateKeyPair(const CryptoPP::OID& oid){
		PrivateKey pri = generatePrivateKey(oid);
		return { pri, generatePublicKey(pri) };
	}

	// Function which prints a private key
	void print(const PrivateKey& key) {
		std::cout << std::endl;
		std::cout << "Private Exponent:" << std::endl;
		std::cout << " " << key.GetPrivateExponent() << std::endl;
	}

	// Function which prints a public key
	void print(const PublicKey& key) {
		std::cout << std::endl;
		std::cout << "Public Element:" << std::endl;
		std::cout << " X: " << key.GetPublicElement().x << std::endl;
		std::cout << " Y: " << key.GetPublicElement().y << std::endl;
	}

	// Function which prints a keypair
	void print(const KeyPair& pair){
		print(pair.pri);
		print(pair.pub);
	}

	// Function which converts a private key to a byte array
	std::vector<Byte> save(const PrivateKey& key) {
		std::vector<Byte> out;
		key.Save(CryptoPP::VectorSink(out).Ref());
		return out;
	}

	// Function which converts a public key to a byte array
	std::vector<Byte> save(const PublicKey& key) {
		std::vector<Byte> out;
		key.Save(CryptoPP::VectorSink(out).Ref());
		return out;
	}

	// Function which converts a string to base 64 string
	std::string saveBase64(const PublicKey& key){
		auto decoded = save(key);
		std::string encoded;

		CryptoPP::VectorSource ss(decoded, true, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(encoded)) );
		return encoded;
	}

	// Function which converts a keypair to a byte array
	std::vector<Byte> save(const PrivateKey& pri, const PublicKey& pub) {
		std::vector<Byte> out = save(pri);
		std::vector<Byte> pubVec = save(pub);
		// Append the data for public key to the private key
		std::move(pubVec.begin(), pubVec.end(), std::back_inserter(out));
		return out;
	}

	// Function which converts a byte array to a private key
	PrivateKey loadPrivate(CryptoPP::VectorSource& source) {
		PrivateKey key;
		key.Load(source.Ref());
		return key;
	}

	// Function which converts a byte array to a public key
	PublicKey loadPublic(CryptoPP::VectorSource& source) {
		PublicKey key;
		key.Load(source.Ref());
		return key;
	}

	// Function which loads a public key from a base 64 string
	PublicKey loadPublicBase64(const std::string& encoded){
		std::vector<Byte> decoded;
		CryptoPP::StringSource ss(encoded, true, new CryptoPP::Base64Decoder(new CryptoPP::VectorSink(decoded)));

		return loadPublic(decoded);
	}

	// Function which signs the provided message
	std::string signMessage(const PrivateKey& key, const std::string& message) {
		CryptoPP::AutoSeededRandomPool prng;
		std::string signature;

		CryptoPP::StringSource(message, true, new CryptoPP::SignerFilter(prng, KeyBase::Signer(key), new CryptoPP::StringSink(signature)));
		return signature;
	}

	// Function which confirms that the signature was created from the message with the matching private key
	bool verifyMessage(const PublicKey& key, const std::string& message, const std::string& signature) {
		bool result = false;

		CryptoPP::StringSource(signature + message, true,
			new CryptoPP::SignatureVerificationFilter(KeyBase::Verifier(key), new CryptoPP::ArraySink((CryptoPP::byte*)&result, sizeof(result)))
		);

		return result;
	}

} // key
