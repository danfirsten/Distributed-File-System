
#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

void printFileBlocksAndData(int inodeNumber, LocalFileSystem &lfs) {
    inode_t inode;
    if (lfs.stat(inodeNumber, &inode) != 0) {
        cerr << "Failed to stat inode " << inodeNumber << endl;
        return;
    }

    // Print file blocks
    cout << "File blocks" << endl;
    int numBlocks = (inode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE; // Number of blocks allocated
    for (int i = 0; i < numBlocks; i++) {
        if (inode.direct[i] > 0) { // Ensure the block number is positive
            cout << inode.direct[i] << endl;
        } else if (inode.direct[i] != 0) {
            cerr << "Invalid block number " << inode.direct[i] << endl;
        }
    }
    cout << endl;

    // Print file data
    cout << "File data" << endl;
    char* buffer = new char[inode.size];  
    lfs.read(inodeNumber, buffer, inode.size);
    cout << buffer;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        cout << argv[0] << ": diskImageFile inodeNumber" << endl;
        return 1;
    }

    try {
        Disk disk(argv[1], UFS_BLOCK_SIZE);
        LocalFileSystem lfs(&disk);
        int inodeNumber = stoi(argv[2]);
        //lfs.create(stoi(argv[2]), UFS_REGULAR_FILE, "check.txt");
        printFileBlocksAndData(inodeNumber, lfs);
    } catch (const std::exception &e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}