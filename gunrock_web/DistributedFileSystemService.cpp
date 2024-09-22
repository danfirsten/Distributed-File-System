#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "DistributedFileSystemService.h"
#include "ClientError.h"
#include "ufs.h"
#include "WwwFormEncodedDict.h"

using namespace std;

DistributedFileSystemService::DistributedFileSystemService(string diskFile) : HttpService("/ds3/") {
  this->fileSystem = new LocalFileSystem(new Disk(diskFile, UFS_BLOCK_SIZE));
}  

void DistributedFileSystemService::get(HTTPRequest *request, HTTPResponse *response) {
    string fullPath = request->getPath();  // Full path including /ds3/
    string path = fullPath.substr(5);  // Remove /ds3/ part
    
    if (path.empty()) {
        throw ClientError::badRequest();
    }
    
    // Split path into components
    vector<string> components;
    stringstream ss(path);
    string item;
    while (getline(ss, item, '/')) {
        components.push_back(item);
    }

    int parentInodeNumber = 0;  // root directory inode number
    int inodeNumber = -1;
    
    // Traverse the path to find the inode number of the requested file/directory
    for (size_t i = 0; i < components.size(); ++i) {
        inodeNumber = this->fileSystem->lookup(parentInodeNumber, components[i]);
        if (inodeNumber < 0) {
            throw ClientError::notFound();
        }
        parentInodeNumber = inodeNumber;
    }

    inode_t inode;
    int statResult = this->fileSystem->stat(inodeNumber, &inode);
    if (statResult != 0) {
        throw ClientError::notFound();
    }

    if (inode.type == UFS_REGULAR_FILE) {
        // Handle file read
        char *buffer = new char[inode.size];
        int bytesRead = this->fileSystem->read(inodeNumber, buffer, inode.size);
        if (bytesRead < 0) {
            delete[] buffer;
            throw ClientError::notFound();
        }
        response->setStatus(200);  // HTTP 200 OK
        response->setBody(string(buffer, bytesRead));
        delete[] buffer;
    } else if (inode.type == UFS_DIRECTORY) {
        // Handle directory listing
        stringstream directoryContent;
        char *buffer = new char[inode.size];
        int bytesRead = this->fileSystem->read(inodeNumber, buffer, inode.size);
        if (bytesRead < 0) {
            delete[] buffer;
            throw ClientError::notFound();
        }

        dir_ent_t *entries = reinterpret_cast<dir_ent_t *>(buffer);
        for (int i = 0; i < bytesRead / (int)sizeof(dir_ent_t); ++i) {
            if (entries[i].inum != -1) {
                if (entries[i].name != string(".") && entries[i].name != string("..")) {
                    if (this->fileSystem->stat(entries[i].inum, &inode) == 0 && inode.type == UFS_DIRECTORY) {
                        directoryContent << entries[i].name << "/\n";
                    } else {
                        directoryContent << entries[i].name << "\n";
                    }
                }
            }
        }
        delete[] buffer;

        string directoryListing = directoryContent.str();
        response->setStatus(200);  // HTTP 200 OK
        response->setBody(directoryListing);
    } else {
        throw ClientError::badRequest();
    }
}


void DistributedFileSystemService::put(HTTPRequest *request, HTTPResponse *response) {
    string fullPath = request->getPath();  // Full path including /ds3/
    string path = fullPath.substr(5);  // Remove /ds3/ part
    
    if (path.empty()) {
        throw ClientError::badRequest();
    }
    
    // Split path into components
    vector<string> components;
    stringstream ss(path);
    string item;
    while (getline(ss, item, '/')) {
        components.push_back(item);
    }
    
    string fileName = components.back();
    components.pop_back();  // Remove fileName from components
    
    // Ensure the file name is not empty
    if (fileName.empty()) {
        throw ClientError::badRequest();
    }

    // Begin a transaction
    this->fileSystem->disk->beginTransaction();

    // Create directories as needed
    int parentInodeNumber = 0;  // root directory inode number
    for (const string &dir : components) {
        int result = this->fileSystem->lookup(parentInodeNumber, dir);
        if (result == -ENOTFOUND) {
            result = this->fileSystem->create(parentInodeNumber, UFS_DIRECTORY, dir);
            if (result < 0) {
                this->fileSystem->disk->rollback();
                throw ClientError::insufficientStorage();
            }
        } else if (result < 0) {
            this->fileSystem->disk->rollback();
            throw ClientError::notFound();
        }
        parentInodeNumber = result;
    }

    // Check for conflicts with existing files
    int fileInodeNumber = this->fileSystem->lookup(parentInodeNumber, fileName);
    if (fileInodeNumber >= 0) {
        inode_t inode;
        int result = this->fileSystem->stat(fileInodeNumber, &inode);
        if (result == 0 && inode.type == UFS_DIRECTORY) {
            this->fileSystem->disk->rollback();
            throw ClientError::conflict();
        }
    } else if (fileInodeNumber != -ENOTFOUND) {
        this->fileSystem->disk->rollback();
        throw ClientError::notFound();
    }

    // Create or overwrite the file
    if (fileInodeNumber == -ENOTFOUND) {
        fileInodeNumber = this->fileSystem->create(parentInodeNumber, UFS_REGULAR_FILE, fileName);
        if (fileInodeNumber < 0) {
            this->fileSystem->disk->rollback();
            throw ClientError::insufficientStorage();
        }
    }

    // Write the file content
    const string &fileContent = request->getBody();
    int writeResult = this->fileSystem->write(fileInodeNumber, fileContent.c_str(), fileContent.size());
    if (writeResult < 0) {
        this->fileSystem->disk->rollback();
        if (writeResult == -ENOTENOUGHSPACE) {
            throw ClientError::insufficientStorage();
        } else {
            throw ClientError::badRequest();
        }
    }

    // Commit the transaction
    this->fileSystem->disk->commit();
    response->setStatus(201);  // HTTP 201 Created
    response->setBody("File created/updated successfully");
}

void DistributedFileSystemService::del(HTTPRequest *request, HTTPResponse *response) {
    string fullPath = request->getPath();
    string path = fullPath.substr(5);
    if (path.empty()) {
        throw ClientError::badRequest();
    }
    

    vector<string> diffComponent;
    stringstream ss(path);
    string item;

    while (getline(ss, item, '/')) {
        diffComponent.push_back(item);
    }

    int parentInodeNumber = UFS_ROOT_DIRECTORY_INODE_NUMBER;
    int inodeNum = -1;
 
    for (size_t i = 0; i < diffComponent.size() - 1; ++i) {
        inodeNum = this->fileSystem->lookup(parentInodeNumber, diffComponent[i]);
        if (inodeNum < 0) {
            throw ClientError::notFound();
        }
        parentInodeNumber = inodeNum;
    }

    int checkIt = this->fileSystem->unlink(parentInodeNumber, diffComponent[diffComponent.size()-1]);
    if (checkIt < 0) {
      throw ClientError::badRequest();
    }
}
