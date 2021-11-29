# CS491: Prunable Memory-Only Tangle

# Arguments and Operation

The command to run the program is:

```bash
./tangle [IP to connect to]
```

For the most basic example run:

```bash
./tangle
```

In one terminal to create a new network (you will need to press enter once the application starts to generate an account). Followed by:

```bash
./tangle 127.0.0.1  # You may replace 127.0.0.1 with a remote IP address if needed
```
In a second terminal to connect to the network (you will need to press enter once the application starts to generate an account). Type 'd' to print out a visual representation and note the topology of the Tangle (you will need to press enter to skip transaction display). Once both peers have booted up and connected, press 'b' to check the account key for each peer. Then on one peer type 't', paste in one of the account keys which were just noted, an amount, and a mining difficulty (difficulties higher than 3 take a long time). Once the transaction has been received by the other peer type 'g' to prune the tangle. Type 'd' again and note the differences in the tangle's topology.

## Arguments
* If an IP address is NOT provided, it will create a new network.
* If an IP address IS provided, it will attempt to connect to an existing network.


## Operation
It will take a moment to connect, once done you will be given the option to enter several commands:

* (B)alance - Query our current balance (also displays our address)
* ( C)lear - Clear the screen
* (D)ebug - Display a debug output of the tangle and (optionally) a transaction in the tangle
* (H)elp - Show a help message (similar to this one)
* (G)enerate - Generates the Latest Common Genesis and prunes the tangle
* (K)ey management - Options to manage your keys
* (P)inging toggle - Toggle whether received transactions should be immediately forwarded elsewhere (simulates a more vibrant network)
* (S)ave <file\> - Save the tangle to a file
* (L)oad <file\> - Loads a tangle from a file
* (T)ransaction - Create a new transaction
* (W)eights - Manually start propagating weights through the tangle
* (Q)uit - Quits the program


# Dependencies, Building, and Running

## Project Layout

1. Transaction.h/cpp contains an implementation of a transaction.
2. Tangle.h/cpp contains both a node in the tangle, and a manager for a tangle.
3. Networking.hpp contains code for an automatic connection handshake and a messaging extension.

These three files build on each other, adding additional functionality to the previous file’s classes.

* Main.cpp contains a driver for the tangle, it performs some initialization and starts the menu loop.
* Keys.hpp provides a cryptography wrapper, containing everything for ECC signatures.
* Utility.hpp contains some helper functions used by the rest of the program.
* Monitor.hpp provides a simple thread safe wrapper used by the tangle implementations to increase the thread safety of vectors.

## Dependency Instructions
The project depends on a local installation of Boost. The remaining dependencies are included as git submodules and can be acquired by running:

```bash
git submodule init
git submodule update
```
However, Breep our networking library requires an additional patch, see the section below for specific instructions.

## Boost
Boost must be locally installed as a system library, instructions on how to do this can be found at [Windows](https://www.boost.org/doc/libs/1_77_0/more/getting_started/windows.html) or [*UNIX](https://www.boost.org/doc/libs/1_77_0/more/getting_started/unix-variants.html)

## Breep
Breep will be downloaded as a submodule automatically. If git is not being used the source code can be downloaded from https://github.com/Organic-Code/Breep, in this case you will need to move the cryptopp file downloaded into the thirdparty directory.

We have applied an additional patch over breep, once you have a Breep folder in thirdparty, the following commands will apply the patch.

```bash
cd thirdparty # If not already there
cp Breep.patch Breep/Breep.patch
cd Breep
git am Breep.patch
```

## Crypto++
Crypto++ will be downloaded as a submodule automatically. If git is not being used the source code can be downloaded from https://github.com/weidai11/cryptopp.git, in this case you will need to move the cryptopp file downloaded into the thirdparty directory.


## Building Instructions
A compiler capable of compiling c++20 code is required to compile this code (any compiler shipped with a modern distribution of Linux should be sufficient). Then simply running make should compile the project:

```bash
make # Must be run in the root directory of the project
```
