#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>
#include <vector>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

// Comparator for sorting directory entries using strcmp
bool compareEntries(const dir_ent_t &a, const dir_ent_t &b) {
    return strcmp(a.name, b.name) < 0;
}

void printDirectory(const string &path, int inodeNumber, LocalFileSystem &lfs) {
    inode_t inode;
    if (lfs.stat(inodeNumber, &inode) != 0) {
        cerr << "Failed to stat inode " << inodeNumber << endl;
        return;
    }

    if (inode.type != UFS_DIRECTORY) {
        cerr << "Inode " << inodeNumber << " is not a directory" << endl;
        return;
    }

    vector<dir_ent_t> entries;
    // char block[UFS_BLOCK_SIZE];

    // Calculate the total number of directory entries
    int totalEntries = inode.size / sizeof(dir_ent_t);
    int entriesRead = 0;

    char* buffer = new char[inode.size];  
    lfs.read(inodeNumber, buffer, inode.size);
    
    dir_ent_t *dirEntry = reinterpret_cast<dir_ent_t *>(buffer);
    // Number of entries to read from this block
    int entriesInBlock = min(totalEntries - entriesRead, static_cast<int>(UFS_BLOCK_SIZE / sizeof(dir_ent_t)));
    for (int j = 0; j < entriesInBlock; ++j) {
        if (dirEntry[j].inum != -1) {
            entries.push_back(dirEntry[j]);
        }
        entriesRead++;
    }

    sort(entries.begin() + 2, entries.end(), compareEntries);

    cout << "Directory " << path << endl;

    for (const auto &entry : entries) {
        cout << entry.inum << "\t" << entry.name << endl;
    }
    cout << endl;

    for (const auto &entry : entries) {
        if (strcmp(entry.name, ".") != 0 && strcmp(entry.name, "..") != 0) {
            string newPath = path + entry.name + "/";
            printDirectory(newPath, entry.inum, lfs);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cout << argv[0] << ": diskImageFile" << endl;
        return 1;
    }

    Disk disk = Disk(argv[1], UFS_BLOCK_SIZE);
    LocalFileSystem lfs(&disk);

    string rootPath = "/";
    int rootInodeNumber = UFS_ROOT_DIRECTORY_INODE_NUMBER;
    printDirectory(rootPath, rootInodeNumber, lfs);

    return 0;
}