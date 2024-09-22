#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

#include "include/LocalFileSystem.h"
#include "include/Disk.h"
#include "include/ufs.h"

using namespace std;


int main(int argc, char *argv[]) {
  if (argc != 2) {
    cout << argv[0] << ": diskImageFile" << endl;
    return 1;
  }
  
  Disk disk = Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem lfs(&disk);

  // Read the superblock
  super_t super;
  lfs.readSuperBlock(&super);

  // Print the superblock metadata
  cout << "Super\n";
  cout << "inode_region_addr " << super.inode_region_addr << endl;
  cout << "data_region_addr " << super.data_region_addr << endl;
  cout << "\n";

  // Allocate memory for the inode bitmap
  unsigned char *inodeBitmap = new unsigned char[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  lfs.readInodeBitmap(&super, inodeBitmap);

  cout << "Inode bitmap" << endl;
  for (int i = 0; i < super.inode_bitmap_len * UFS_BLOCK_SIZE; i++) {
    cout << (unsigned int)inodeBitmap[i] << " ";
  }
  cout << endl;
  cout << endl;


  // Allocate memory for the data bitmap
  unsigned char *dataBitmap = new unsigned char[super.data_bitmap_len * UFS_BLOCK_SIZE];
  lfs.readDataBitmap(&super, dataBitmap);

  cout << "Data bitmap" << endl;
  for (int i = 0; i < super.data_bitmap_len * UFS_BLOCK_SIZE; i++) {
    cout << (unsigned int)dataBitmap[i] << " ";
  }
  cout << endl;

  delete[] inodeBitmap;
  delete[] dataBitmap;

  return 0;
}