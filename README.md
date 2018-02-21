# Texas A&M Distrubuted Computing Class
Section 500
Original Chatroom Code by: Will Adams and Nicholas Jackson
Fault Tolerance by: Nicholas Gilpin and Alexandra Stacy


A distributed fault tolerant multi-threaded chat room system using gRPC and Google Protocol Buffers.

## Core Features

Chat Client
Chat Server 
Real time chat rooms
Vector clock message ordering
Distributed chat logs that are synced between multiple computers
Automatically restarting chat servers
Bully election for electing a new master chat server
protobuf messaging protocol
gRPC network remote procedure calls

## Install and Build Instructions

Install [gRPC](https://grpc.io/) using default settings

Compile the code using the provided makefile:

    make

To clear the directory (and remove .txt files):
   
    make clean

To run the server on port 3010 (the default port used for testing):

    ./fbsd -p 3010

To run the client on the localhost, on port 3010, and with username "user1": 

    ./fbc -h localhost -p 3010 -u user1

