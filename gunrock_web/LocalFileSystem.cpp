#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <cmath>

#include "include/LocalFileSystem.h"
#include "include/ufs.h"

using namespace std;

LocalFileSystem::LocalFileSystem(Disk *disk) {
  this->disk = disk;
}

void LocalFileSystem::readSuperBlock(super_t *super) {
  char buffer[UFS_BLOCK_SIZE]; // Allocate buffer
  disk->readBlock(0, buffer); // Read the first block from disk into buffer
  memcpy(super, buffer, sizeof(super_t)); // Copy contents of buffer into super_t 
}

int LocalFileSystem::lookup(int parentInodeNumber, string name) {
  // Read parent inode
  inode_t parentInode;
  int statResult = stat(parentInodeNumber, &parentInode);
  if (statResult != 0) {
    return -EINVALIDINODE; // Invalid parent inode or not a directory
  }
  
  // Read data blocks of parent inode
  char block[UFS_BLOCK_SIZE];
  int numBlocks = (int)ceil((double)parentInode.size / UFS_BLOCK_SIZE);
  for (int i = 0; i < numBlocks; i++) {
    if (parentInode.direct[i] != 0) {
      disk->readBlock(parentInode.direct[i], block);
      dir_ent_t *entries = reinterpret_cast<dir_ent_t *>(block);
      for (int j = 0; j < (int)(UFS_BLOCK_SIZE / sizeof(dir_ent_t)); j++) {
        if (entries[j].inum != -1 && strcmp(entries[j].name, name.c_str()) == 0) {
          return entries[j].inum;
        }
      }
    }
  }
  return -ENOTFOUND; // Not found
}

int LocalFileSystem::stat(int inodeNumber, inode_t *inode) {
  if (inodeNumber < 0 || inode == nullptr) {
      return -1; // Invalid inode number or inode pointer
  }

  super_t super;
  readSuperBlock(&super);
  char block[UFS_BLOCK_SIZE];

  // Calculate the block that the inode is in
  // 1.) Start at the inode region address which is given as the block address (in blocks)
  // 2.) Then add the inode number (first inode? second inode? etc.) divided by
  //     # inodes per block, (bytes/block / bytes/inode => bytes/block * inode/bytes => inodes/block)
  //     which gives you how many blocks into the inode region the inode is
  // 3.) To get the block # on the disk
  int inodesPerBlock = UFS_BLOCK_SIZE / sizeof(inode_t);
  if (inodeNumber >= super.num_inodes) {
      return -1; // Inode number out of range
    }

    int blockNumber = super.inode_region_addr + (inodeNumber / inodesPerBlock);
    disk->readBlock(blockNumber, block);
    // Calculate the offset of the inode into the block
    // 1.) Mod the inode number by # of inodes per block. This gives you how many inodes into the block
    //     the inode is
    // 2.) Multiply the calculated value by the size of the inode to get how many bytes into the block
    //     the inode is
    int offset = (inodeNumber % inodesPerBlock) * sizeof(inode_t);

    memcpy(inode, block + offset, sizeof(inode_t));
    return 0;
}

int LocalFileSystem::read(int inodeNumber, void *buffer, int size) {
    inode_t inode;
    int statResult = stat(inodeNumber, &inode);
    if (statResult != 0) {
        return -EINVALIDINODE; // Invalid inode
    }

    if(size < 0 || size > MAX_FILE_SIZE) {
        return -EINVALIDSIZE;
    }
    
    if(size > inode.size) {
        size = inode.size;
    }

    int bytesRead = 0;
    char block[UFS_BLOCK_SIZE];
    int numBlocks = (int)ceil((double)size / UFS_BLOCK_SIZE);
    // Iterating through the direct pointers (each pointer refers to one disk block that belongs to the file) 
    // as well as keeping in mind that the bytesRead must be less than the number of bytes we want to read
    for (int i = 0; i < numBlocks; i++) {
        if (inode.direct[i] != 0) { // Skip super block
            disk->readBlock(inode.direct[i], block);
            // Min makes sure if we have less than a block of bytes left to read then we only read the amount
            // of bytes we have left to read
            int bytesToRead = min(size - bytesRead, UFS_BLOCK_SIZE);
            memcpy(static_cast<char *>(buffer) + bytesRead, block, bytesToRead);
            bytesRead += bytesToRead;
        }
    }
    return bytesRead;
}

int LocalFileSystem::create(int parentInodeNumber, int type, string name) {
    if (parentInodeNumber < 0) {
        return -EINVALIDINODE; // Invalid parent inode number
    }

    if (name.length() > DIR_ENT_NAME_SIZE) {
        return -EINVALIDNAME; // Name too long
    }

    if (type != UFS_REGULAR_FILE && type != UFS_DIRECTORY) {
        return -EINVALIDTYPE; // Invalid type
    }

    // Check if name already exists in the parent directory
    int existingInodeNumber = lookup(parentInodeNumber, name);
    if (existingInodeNumber != -ENOTFOUND) {
        inode_t existingInode;
        int checkIt = stat(existingInodeNumber, &existingInode);
        if (checkIt == 0 && existingInode.type == type) {
            return existingInodeNumber; // File/directory already exists with the correct type
        } else {
            return -EINVALIDTYPE; // Name exists but with a different type
        }
    }

    // Read the superblock
    super_t super;
    readSuperBlock(&super);

    // Read inode and data bitmaps
    vector<unsigned char> inodeBitmap(super.inode_bitmap_len * UFS_BLOCK_SIZE);
    vector<unsigned char> dataBitmap(super.data_bitmap_len * UFS_BLOCK_SIZE);
    readInodeBitmap(&super, inodeBitmap.data());
    readDataBitmap(&super, dataBitmap.data());

    // Find a free inode
    int newInodeNumber = -1;
    for (int i = 0; i < super.num_inodes; ++i) {
        if ((inodeBitmap[i / 8] & (1 << (i % 8))) == 0) {
            newInodeNumber = i;
            inodeBitmap[i / 8] |= 1 << (i % 8); // Mark inode as used
            break;
        }
    }

    if (newInodeNumber == -1) {
        return -ENOTENOUGHSPACE; // No free inodes
    }

    // Initialize the new inode
    inode_t newInode;
    memset(&newInode, 0, sizeof(newInode)); // Initialize inode to zero
    newInode.type = type;

    if (type == UFS_REGULAR_FILE) {
        newInode.size = 0;
    } else if (type == UFS_DIRECTORY) {
        // Allocate a data block for the new directory
        int newDirBlock = -1;
        for (int i = 0; i < super.num_data; i++) {
            if ((dataBitmap[i / 8] & (1 << (i % 8))) == 0) {
                newDirBlock = super.data_region_addr + i;
                dataBitmap[i / 8] |= 1 << (i % 8); // Mark block as used
                break;
            }
        }

        if (newDirBlock == -1) {
            return -ENOTENOUGHSPACE; // No free data blocks
        }

        // Initialize the new directory block with . and ..
        char newDirBlockContent[UFS_BLOCK_SIZE];
        memset(newDirBlockContent, 0, UFS_BLOCK_SIZE); // Clear the block
        dir_ent_t *entries = reinterpret_cast<dir_ent_t *>(newDirBlockContent);

        // Entry for .
        strcpy(entries[0].name, ".");
        entries[0].inum = newInodeNumber;

        // Entry for ..
        strcpy(entries[1].name, "..");
        entries[1].inum = parentInodeNumber;

        disk->writeBlock(newDirBlock, entries);

        newInode.direct[0] = newDirBlock;
        newInode.size = 2 * sizeof(dir_ent_t);
    }

    // Write the new inode to disk
    int inodesPerBlock = UFS_BLOCK_SIZE / sizeof(inode_t);
    int inodeBlockNumber = super.inode_region_addr + (newInodeNumber / inodesPerBlock);
    int inodeOffset = (newInodeNumber % inodesPerBlock) * sizeof(inode_t);
    char block2[UFS_BLOCK_SIZE];

    disk->readBlock(inodeBlockNumber, block2);
    memcpy(block2 + inodeOffset, &newInode, sizeof(inode_t));
    disk->writeBlock(inodeBlockNumber, block2);

    // Update the parent directory
    inode_t parentInode;
    int checkIt2 = stat(parentInodeNumber, &parentInode);
    if (checkIt2 != 0 || parentInode.type != UFS_DIRECTORY) {
        return -EINVALIDINODE; // Invalid parent inode or not a directory
    }

    char block[UFS_BLOCK_SIZE];
    bool added = false;
    int numEntries = parentInode.size / (int)sizeof(dir_ent_t);
    dir_ent_t *parent_entries = new dir_ent_t[numEntries + 1];
    read(parentInodeNumber, parent_entries, parentInode.size);
    for(int i = 0; i < numEntries; i++) {
        if(parent_entries[i].inum == -1) {
            strcpy(parent_entries[i].name, name.c_str());
            parent_entries[i].inum = newInodeNumber;
            added = true;
            break;
        }
    }

    if(!added) {
        parent_entries[numEntries].inum = newInodeNumber;
        strcpy(parent_entries[numEntries].name, name.c_str());
        parentInode.size += sizeof(dir_ent_t);
    }

    int numBlocks = (parentInode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
    int dirCopy = 0;
    for(int j = 0; j < numBlocks; j++) {
        char buffer[UFS_BLOCK_SIZE];
        int dir2Copy = min(UFS_BLOCK_SIZE / (int)sizeof(dir_ent_t), (numEntries - dirCopy) * (int)(sizeof(dir_ent_t)));
        memcpy(buffer, parent_entries + dirCopy, sizeof(dir_ent_t) * dir2Copy);
        disk->writeBlock(parentInode.direct[j], buffer);
        dirCopy += dir2Copy;
    }

    // Write the updated parent inode to disk
    int parentInodeBlockNumber = super.inode_region_addr + (parentInodeNumber / inodesPerBlock);
    int parentInodeOffset = (parentInodeNumber % inodesPerBlock) * sizeof(inode_t);
    disk->readBlock(parentInodeBlockNumber, block);
    memcpy(block + parentInodeOffset, &parentInode, sizeof(inode_t));
    disk->writeBlock(parentInodeBlockNumber, block);

    // Write the updated inode bitmap to disk
    for (int i = 0; i < super.inode_bitmap_len; ++i) {
        disk->writeBlock(super.inode_bitmap_addr + i, &inodeBitmap[i * UFS_BLOCK_SIZE]);
    }

    // Write the updated data bitmap to disk
    for (int i = 0; i < super.data_bitmap_len; i++) {
        disk->writeBlock(super.data_bitmap_addr + i, &dataBitmap[i * UFS_BLOCK_SIZE]);
    }

    return newInodeNumber;
}




int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {

    inode_t inode;
    int statResult = stat(inodeNumber, &inode);
    if (statResult != 0) {
        return -EINVALIDINODE; // Invalid inode
    }

    if (inode.type != UFS_REGULAR_FILE) {
        return -EINVALIDTYPE; // Not a regular file
    }

    if (size > MAX_FILE_SIZE) {
        return -EINVALIDSIZE; // Exceeds max file size
    }

    int blocksNeeded = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE; // Round up to nearest block

    unsigned char dataBitmap[UFS_BLOCK_SIZE];
    super_t super;
    readSuperBlock(&super);
    readDataBitmap(&super, dataBitmap);

    // Find free blocks in the data region
    vector<int> freeBlocks;
    for (int i = 0; i < super.num_data; i++) {
        if ((dataBitmap[i / 8] & (1 << (i % 8))) == 0) {
            freeBlocks.push_back(super.data_region_addr + i);
        }
        if ((int)freeBlocks.size() == blocksNeeded) {
            break;
        }
    }

    // Check if there are enough free blocks available
    if ((int)freeBlocks.size() < blocksNeeded) {
        return -ENOTENOUGHSPACE; // Not enough space
    }

    // Write data to the free blocks
    char block[UFS_BLOCK_SIZE];
    int bytesWritten = 0;
    for (int i = 0; i < blocksNeeded; i++) {
        int bytesToWrite = min(size - bytesWritten, UFS_BLOCK_SIZE);
        memcpy(block, static_cast<const char *>(buffer) + bytesWritten, bytesToWrite);

        disk->writeBlock(freeBlocks[i], block);
        inode.direct[i] = freeBlocks[i];

        // Mark the block as used in the data bitma
        dataBitmap[(freeBlocks[i] - super.data_region_addr) / 8] |= 1 << ((freeBlocks[i] - super.data_region_addr) % 8);

        bytesWritten += bytesToWrite;
    }

    inode.size = size;
    int inodesPerBlock = UFS_BLOCK_SIZE / sizeof(inode_t);
    int inodeBlockNumber = super.inode_region_addr + (inodeNumber / inodesPerBlock);
    int inodeOffset = (inodeNumber % inodesPerBlock) * sizeof(inode_t);

    disk->readBlock(inodeBlockNumber, block);
    memcpy(block + inodeOffset, &inode, sizeof(inode_t));
    disk->writeBlock(inodeBlockNumber, block);

    // Write the updated data bitmap back to the disk
    for (int i = 0; i < super.data_bitmap_len; i++) {
        disk->writeBlock(super.data_bitmap_addr + i, &dataBitmap[i * UFS_BLOCK_SIZE]);
    }
    

    return bytesWritten;
    // return 0;
}

int LocalFileSystem::unlink(int parentInodeNumber, std::string name) {
    // Ensure name is not "." or ".."
    if (name == "." || name == "..") {
        return -EUNLINKNOTALLOWED;
    }

    // Check if name is too long
    if (name.length() >= DIR_ENT_NAME_SIZE) {
        return -EINVALIDNAME;
    }

    // Read the superblock
    super_t super;
    readSuperBlock(&super);

    // Check if the parent inode number is valid
    inode_t parentInode;
    if (stat(parentInodeNumber, &parentInode) < 0) {
        return -EINVALIDINODE;
    }

    // Check if the parent inode is a directory
    if (parentInode.type != UFS_DIRECTORY) {
        return -EINVALIDTYPE;
    }

    // Read the directory entries
    char buffer[UFS_BLOCK_SIZE];
    int bytesRead = read(parentInodeNumber, buffer, UFS_BLOCK_SIZE);
    if (bytesRead < 0) {
        return bytesRead;  // Propagate the error code
    }

    // Find the entry with the given name
    dir_ent_t* entry = nullptr;
    int entryIndex = -1;
    for (int i = 0; i < bytesRead; i += sizeof(dir_ent_t)) {
        dir_ent_t* currentEntry = reinterpret_cast<dir_ent_t*>(buffer + i);
        if (currentEntry->name == name) {
            entry = currentEntry;
            entryIndex = i;
            break;
        }
    }

    // Entry not found
    if (entry == nullptr) {
        return 0;  // Not a failure according to the problem statement
    }

    // Check if the entry inode number is valid
    if (entry->inum < 0 || entry->inum >= super.num_inodes) {
        return -EINVALIDINODE;
    }

    inode_t entryInode;
    if (stat(entry->inum, &entryInode) < 0) {
        return -EINVALIDINODE;
    }

    // Handle directory unlink
    if (entryInode.type == UFS_DIRECTORY) {
        // Check if the directory is empty
        bool isEmpty = true;
        char dirBuffer[UFS_BLOCK_SIZE];
        int dirBytesRead = read(entry->inum, dirBuffer, UFS_BLOCK_SIZE);
        if (dirBytesRead < 0) {
            return dirBytesRead;  // Propagate the error code
        }

        for (int i = 0; i < dirBytesRead; i += sizeof(dir_ent_t)) {
            dir_ent_t* dirEntry = reinterpret_cast<dir_ent_t*>(dirBuffer + i);
            if (strcmp(dirEntry->name, ".") != 0 && strcmp(dirEntry->name, "..") != 0 && dirEntry->inum != 0) {
                isEmpty = false;
                break;
            }
        }

        if (!isEmpty) {
            return -EDIRNOTEMPTY;
        }

        // Deallocate the blocks used by the directory
        vector<unsigned char> inodeBitmap(super.inode_bitmap_len * UFS_BLOCK_SIZE);
        vector<unsigned char> dataBitmap(super.data_bitmap_len * UFS_BLOCK_SIZE);
        readInodeBitmap(&super, inodeBitmap.data());
        readDataBitmap(&super, dataBitmap.data());

        inodeBitmap[entry->inum / 8] &= ~(1 << (entry->inum % 8));
        for (int i = 0; i < DIRECT_PTRS && entryInode.direct[i] != 0; i++) {
            int blockIndex = entryInode.direct[i] - super.data_region_addr;
            dataBitmap[blockIndex / 8] &= ~(1 << (blockIndex % 8));
        }

        writeInodeBitmap(&super, inodeBitmap.data());
        writeDataBitmap(&super, dataBitmap.data());
    }

    // Handle file unlink
    if (entryInode.type == UFS_REGULAR_FILE) {
        // Deallocate the blocks used by the file
        vector<unsigned char> dataBitmap(super.data_bitmap_len * UFS_BLOCK_SIZE);
        readDataBitmap(&super, dataBitmap.data());

        for (int i = 0; i < DIRECT_PTRS && entryInode.direct[i] != 0; i++) {
            int blockIndex = entryInode.direct[i] - super.data_region_addr;
            dataBitmap[blockIndex / 8] &= ~(1 << (blockIndex % 8));
        }

        writeDataBitmap(&super, dataBitmap.data());
    }

    // Deallocate the target inode
    vector<unsigned char> inodeBitmap(super.inode_bitmap_len * UFS_BLOCK_SIZE);
    readInodeBitmap(&super, inodeBitmap.data());

    inodeBitmap[entry->inum / 8] &= ~(1 << (entry->inum % 8));
    writeInodeBitmap(&super, inodeBitmap.data());

    // Remove the entry from the parent directory
    memmove(buffer + entryIndex, buffer + entryIndex + sizeof(dir_ent_t), bytesRead - entryIndex - sizeof(dir_ent_t));
    bytesRead -= sizeof(dir_ent_t);
    parentInode.size -= sizeof(dir_ent_t);

    // Update the parent directory block
    this->disk->writeBlock(parentInode.direct[0], buffer);

    // Update the parent inode
    parentInode.size = bytesRead;
    int parentInodeBlockNumber = super.inode_region_addr + (parentInodeNumber / (UFS_BLOCK_SIZE / sizeof(inode_t)));
    int parentInodeOffset = (parentInodeNumber % (UFS_BLOCK_SIZE / sizeof(inode_t))) * sizeof(inode_t);

    char parentBlock[UFS_BLOCK_SIZE];
    disk->readBlock(parentInodeBlockNumber, parentBlock);
    memcpy(parentBlock + parentInodeOffset, &parentInode, sizeof(inode_t));
    disk->writeBlock(parentInodeBlockNumber, parentBlock);

    return 0;
}

void LocalFileSystem::readInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
    assert(super != nullptr && inodeBitmap != nullptr); // Ensure pointers are valid
    for (int i = 0; i < super->inode_bitmap_len; i++) {
        disk->readBlock(super->inode_bitmap_addr + i, &inodeBitmap[i * UFS_BLOCK_SIZE]);
    }
}

void LocalFileSystem::readDataBitmap(super_t *super, unsigned char *dataBitmap) {
    assert(super != nullptr && dataBitmap != nullptr); // Ensure pointers are valid
    for (int i = 0; i < super->data_bitmap_len; i++) {
        disk->readBlock(super->data_bitmap_addr + i, &dataBitmap[i * UFS_BLOCK_SIZE]);
    }
}

void LocalFileSystem::writeInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
    assert(super != nullptr && inodeBitmap != nullptr); // Ensure pointers are valid
    for (int i = 0; i < super->inode_bitmap_len; i++) {
        disk->writeBlock(super->inode_bitmap_addr + i, &inodeBitmap[i * UFS_BLOCK_SIZE]);
    }
}

void LocalFileSystem::writeDataBitmap(super_t *super, unsigned char *dataBitmap) {
    assert(super != nullptr && dataBitmap != nullptr); // Ensure pointers are valid
    for (int i = 0; i < super->data_bitmap_len; i++) {
        disk->writeBlock(super->data_bitmap_addr + i, &dataBitmap[i * UFS_BLOCK_SIZE]);
    }
}

void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes) {
    assert(super != nullptr && inodes != nullptr); // Ensure pointers are valid
    int inodesPerBlock = UFS_BLOCK_SIZE / sizeof(inode_t);
    int numInodeBlocks = (super->num_inodes + inodesPerBlock - 1) / inodesPerBlock;

    for (int i = 0; i < numInodeBlocks; i++) {
        char buffer[UFS_BLOCK_SIZE];
        int inodesInThisBlock = min(inodesPerBlock, super->num_inodes - i * inodesPerBlock);
        memcpy(buffer, inodes + i * inodesPerBlock, inodesInThisBlock * sizeof(inode_t));
        disk->writeBlock(super->inode_region_addr + i, buffer);
    }
}

void LocalFileSystem::readInodeRegion(super_t *super, inode_t *inodes) {
    assert(super != nullptr && inodes != nullptr); // Ensure pointers are valid
    int inodesPerBlock = UFS_BLOCK_SIZE / sizeof(inode_t);
    int numInodeBlocks = (super->num_inodes + inodesPerBlock - 1) / inodesPerBlock;

    for (int i = 0; i < numInodeBlocks; i++) {
        char buffer[UFS_BLOCK_SIZE];
        disk->readBlock(super->inode_region_addr + i, buffer);
        int inodesInThisBlock = min(inodesPerBlock, super->num_inodes - i * inodesPerBlock);
        memcpy(inodes + i * inodesPerBlock, buffer, inodesInThisBlock * sizeof(inode_t));
    }
}


