#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>
#include <vector>

#include "include/LocalFileSystem.h"
#include "include/Disk.h"
#include "include/ufs.h"

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

    // cout << "Current Inode: " << inodeNumber << endl; // TEST

    if (inode.type != UFS_DIRECTORY) {
        // cout << "Inode type: " << inode.type << endl;
        // cerr << "Inode " << inodeNumber << " is not a directory" << endl;
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
    cout << "Total Entries: " << totalEntries << endl;
    //cout << "Total other: " << static_cast<int>(UFS_BLOCK_SIZE / sizeof(dir_ent_t)) << endl;
    //cout << "Entries Read: " << entriesRead << endl;
    int entriesInBlock = min(totalEntries - entriesRead, static_cast<int>(UFS_BLOCK_SIZE / sizeof(dir_ent_t)));
    for (int j = 0; j < entriesInBlock; ++j) {
        if (dirEntry[j].inum != -1) {
            //cout << "j = " << j << " dirEntry[j].inum = " << dirEntry[j].inum << endl; // TEST
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
    // Disk disk =Disk("disk_testing/a.img", UFS_BLOCK_SIZE);
    LocalFileSystem lfs(&disk);

    string rootPath = "/";
    int rootInodeNumber = UFS_ROOT_DIRECTORY_INODE_NUMBER;
    // printDirectory(rootPath, rootInodeNumber, lfs);
    //lfs.create(rootInodeNumber, UFS_REGULAR_FILE, "helpme.txt");
    // printDirectory(rootPath, rootInodeNumber, lfs);
    // for(int i = 1; i < 40; i++) {
    //     //string fileName = "1";
    //     string fileName = to_string(i);
    //     //fileName += ".txt";
    //     int setType = UFS_DIRECTORY;
    //     int result = lfs.create(rootInodeNumber, setType, fileName);
    //     if(result < 0) {
    //         cout << "Failed on Inode: " << i << endl;
    //     }
    // }

    string fileName = "Dir_yossi5";
    int setType = UFS_DIRECTORY;
    int result = lfs.create(rootInodeNumber, setType, fileName);

    // cout << endl;
    // cout << endl;
    if (result >= 0) {
        // cout << "result var is positive" << endl;
        if(setType == UFS_REGULAR_FILE) {
            cout << "File '" << fileName << "' created successfully with inode number: " << result << endl;
        } else if (setType == UFS_DIRECTORY) {
            cout << "Directory '" << fileName << "' created successfully with inode number: " << result << endl;
        }
       
    } else {
        //cout << "Root Inode Num: " << rootInodeNumber << endl;
        // cout << ""
        cerr << "Error creating file '" << fileName << "': " << result << endl;
    }
    cout << endl;
    cout << endl;

    printDirectory(rootPath, rootInodeNumber, lfs);

    cout << endl;
    cout << endl;

    cout << "Checking Unlink:" << endl;

    int result2 = lfs.unlink(rootInodeNumber, fileName);

    printDirectory(rootPath, rootInodeNumber, lfs);


    


    return 0;
}