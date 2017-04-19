/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*Will Adams and Nicholas Jackson
  CSCE 438 Section 500*/

#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include "fb.grpc.pb.h"

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using hw2::Message;
using hw2::ListReply;
using hw2::Request;
using hw2::Reply;
using hw2::MessengerServer;
using hw2::ServerChat;
using hw2::Credentials;
using hw2::DataSync;
using grpc::Channel;
using grpc::ClientContext;
// Forwards ////////////////////////////////////////////////////////////////////
class ServerChatClient;

// Global Variables ////////////////////////////////////////////////////////////
bool isMaster = false;
bool isLeader = false;
std::string port = "2323"; // Port for clients to connect to
std::string workerPort = "8888"; // Port for workers to connect to
std::string workerToConnect = "8889"; // Port for this process to contact
std::string masterPort = "10002"; // Port that leading master monitors
std::vector<std::string> defaultWorkerPorts;
std::vector<std::string> defaultWorkerHostnames;
std::vector<ServerChatClient> localWorkersComs;
std::vector<std::string> messageIDs; //messageIDs for workers
std::vector<std::string> masterMessageIDs; //MessageIDs for master
static ServerChatClient* masterCom; // Connection to leading master
std::string host_x = "";
std::string host_y = "";
std::string masterHostname = "lenss-comp1"; // Port for this process to contact
//Client struct that holds a user's username, followers, and users they follow
struct Client {
  std::string username;
  bool connected = true;
  int following_file_size = 0;
  std::vector<Client*> client_followers;
  std::vector<Client*> client_following;
  ServerReaderWriter<Message, Message>* stream = 0;
  bool operator==(const Client& c1) const{
    return (username == c1.username);
  }
}; 

class vectorClock {
private:
	/* data */
	std::vector<google::protobuf::Timestamp> clock;
public:
	vectorClock (int unique_server_id, int vectorSize){
		// @TODO: Create vector
	}
	bool operator<(const google::protobuf::Timestamp &left){
		return false;
	}
	bool operator=(const google::protobuf::Timestamp &left){
		return false;
	}
	void updateClock(google::protobuf::Timestamp){
		
	}
	virtual ~vectorClock ();
};


//Vector that stores every client that has been created
std::vector<Client> client_db;

// Utility Functions /////////////////////////////////////////////////////////
//Helper function used to find a Client object given its username
int find_user(std::string username){
  int index = 0;
  for(Client c : client_db){
    if(c.username == username)
      return index;
    index++;
  }
  return -1;
}

bool doesFileExist(std::string fileName){
    std::ifstream infile(fileName);
    return infile.good();
}

std::vector<std::string> collectIDs(){
    std::string line;
    std::vector<std::string> fileIDs;
    std::string delimiter = "::";
    unsigned int curLine = 0;

    for(uint i = 0; i < client_db.size(); i++){
      std::string filename = client_db[i].username+".txt";
      if(doesFileExist(filename)){
        std::ifstream file(filename);
        while(std::getline(file, line)) {
          curLine++;
          std::string id = line.substr(0, line.find(delimiter));
          if (line.find(id, 0) != std::string::npos) {
            fileIDs.push_back(id);
          }
        }
      }
    } 
    return fileIDs;
}

// Get an exclusive lock on filename or return -1 (already locked)
int fileLock(std::string filename){
	// Create file if needed; open otherwise
	int fd = open(filename.c_str(), O_RDWR | O_CREAT, 0666);
	if (fd < 0){
		std::cout << "Couldn't lock to restart process" << std::endl;
		return -1;
	}

	// -1 = File already locked; 0 = file unlocked
	int rc = flock(fd, LOCK_EX | LOCK_NB);
	return rc;
}

// Opposite of fileLock
int fileUnlock(std::string filename){
	// Create file if needed; open otherwise
	int fd = open(filename.c_str(), O_RDWR, 0666);
	if (fd < 0){
		std::cout << "Couldn't unlock file" << std::endl;
	}

	// -1 = Error; 0 = file unlocked
	int rc = flock(fd, LOCK_UN | LOCK_NB);
	return rc;
}

// gRPC classes ////////////////////////////////////////////////////////////////
// Recieving side of interserver chat Service
//MASTER SERVER PORTION
class ServerChatImpl final : public ServerChat::Service {
	// Asks for a reply and restarts the server if no replay is recieved
	Status pulseCheck(ServerContext* context, const Reply* in, Reply* out) override{
		std::string pid = std::to_string(getpid());
		out->set_msg(workerPort);
    if(isMaster){
      out->set_msg("You're on the master");
    }
		return Status::OK;
	}
//All workers send to master, master writes to its own database if the message isnt already there
  //Thats what datasend does
//When a worker requests a dataSync, worker sends IDs of all its message to the master
  //The master takes the set difference of all its own database message IDs, and IDs it received from the worker request
  //And then replies with the messages that it has, and receives the messages from the worker that it doesnt have
  //Could have a dataSync message type that has a repeatable string in it for sending lists of IDs and the message response list


/*
A clientWorker is going to call the serverChatClient version of DataSend every time they are about the write a message.
Datasend sends the information to the master for the master to write to its database.

A clientWorker calls dataSync before a client chats for the first time. Maybe called in a loop in CHAT.
NEEDS TO BE CALLED SOMEWHERE IN CHAT.
Only goes to the worker that requested it.
Put it at the start of the chatmessage. It would be good if it was also run
every X seconds.
*/
	// Record sync info to master database
	Status joinSync(ServerContext* context, const Request* in, Reply* out)override{
		
		return Status::OK;
	}
	// Record sync info to master database
	Status leaveSync(ServerContext* context, const Request* in, Reply* out)override{
		
		return Status::OK;
	}
	
  //Add user data to master's database
  Status DataSend(ServerContext* context, const Reply* in, Reply* out) override{
    unsigned int curLine = 0;
    bool flag = false;
    std::string line;
    std::string toWrite = in->msg();
    std::string delimiter = "::";
    
    std::string id = toWrite.substr(0, toWrite.find(delimiter));
    toWrite.erase(0, toWrite.find(delimiter) + delimiter.length());
    toWrite.erase(0, toWrite.find(delimiter) + delimiter.length());
    
    std::string nameandmessage = toWrite.substr(0, toWrite.find(delimiter));
    std::string username = nameandmessage.substr(0, nameandmessage.find(':'));
    std::string filename = username+".txt";

    if(find_user(username) < 0){
      Client c;
      c.username = username; 
      client_db.push_back(c);
    }

    std::ifstream file(filename);
    while(std::getline(file, line)) {
      curLine++;
      if (line.find(id, 0) != std::string::npos) {
          flag = true;
      }
    }
    if(flag == true){
      return Status::OK;
    }
    else{
      std::string filename = username+".txt";
      std::ofstream user_file(filename,std::ios::app|std::ios::out|std::ios::in);
      user_file << in->msg();
      return Status::OK;
    }
    return Status::OK;
  }

  //Add the following data to the master's database
  Status DataSendFollowers(ServerContext* context, const Request* in, Reply* out) override{
    unsigned int curLine = 0;
    bool flag = false;
    std::string line;
    std::string followerfile = in->username();
    std::string toWrite = in->arguments(0);
    std::string delimiter = "::";
    if(in->arguments(1).compare("true") == 0){
      followerfile = followerfile + "following.txt";
    }
    else
      followerfile = followerfile + ".txt";

    std::string id = toWrite.substr(0, toWrite.find(delimiter));

    std::ifstream file(followerfile);
    while(std::getline(file, line)) {
      curLine++;
      if (line.find(id, 0) != std::string::npos) {
          flag = true;
      }
    }
    if(flag == true){
      return Status::OK;
    }
    else{
      std::ofstream user_file(followerfile,std::ios::app|std::ios::out|std::ios::in);
      user_file << in->arguments(0);
      return Status::OK;
    }
    return Status::OK;
  }

  Status dataSync(ServerContext *context, const DataSync* in, DataSync* out) override{
     if(isMaster){
        std::vector<std::string> masterIDs = collectIDs();
        std::vector<std::string> workerIDsToSend;
        std::string found;
        for (uint i=0; i < in->ids().size(); i++){
          messageIDs.push_back(in->ids(i));
        }
        for (uint i = 0; i < masterIDs.size(); i++) {
          for (uint k = 0; k < messageIDs.size(); k++) {
            if (masterIDs[i] == messageIDs[k]) {
              found = ""; // add this
              break;
            } 
            else if (masterIDs[i] != messageIDs[k]) {
              found = masterIDs[i];
            }
          }
          if (found != "") { // to trigger this and not save garbage
            workerIDsToSend.push_back(found);
          }
        }
     }
     else
      return Status::OK;
    return Status::OK;
  }
/*  Status dataSync(ServerContext *context, const Reply* in, Reply* out) override{
 
    if(isMaster){
      unsigned int curLine = 0;
      std::string line;
      std::string toWrite = in->msg();
      std::string delimiter = "::";

      std::string id = toWrite.substr(0, toWrite.find(delimiter));
      toWrite.erase(0, toWrite.find(delimiter) + delimiter.length());
      toWrite.erase(0, toWrite.find(delimiter) + delimiter.length());

      std::string nameandmessage = toWrite.substr(0, toWrite.find(delimiter));
      std::string username = nameandmessage.substr(0, nameandmessage.find(':'));
      std::string filename = username+".txt";
      std::ifstream file(filename);
      while(std::getline(file, line)) {
        curLine++;
        if (line.find(id, 0) != std::string::npos) {
            return Status::OK;
        }
      }
    }
  	return Status::OK;
	}*/

};

// Sending side of interserver chat Service
//CLIENTWORKER SERVER PORTION
class ServerChatClient {
private:
	std::unique_ptr<ServerChat::Stub> stub_;
public:
 ServerChatClient(std::shared_ptr<Channel> channel)
	 : stub_(ServerChat::NewStub(channel)) {}

	 // Checks if other endpoint is responsive
	 bool pulseCheck() {
		 // Data we are sending to the server.
		 Reply request;
		 request.set_msg("a");

		 // Container for the data we expect from the server.
		 Reply reply;

		 // Context for the client. It could be used to convey extra information to
		 // the server and/or tweak certain RPC behaviors.
		 ClientContext context;

		 // The actual RPC.
		 Status status = stub_->pulseCheck(&context, request, &reply);

		 // Act upon its status.
		 if (status.ok()) {
			  //std::cout << "Pulse " << workerPort << " --> " << reply.msg() << std::endl;
			 return true;
		 } else {
			  std::cout << status.error_code() << ": " << status.error_message()
			 					 << std::endl;
			return false;
		 }
	 }
	 
	 // Forwards request data to master
	 void joinSync(Request r){
		 // Data we are sending to the server.
		 Request request = r;

		 // Container for the data we expect from the server.
		 Reply reply;

		 // Context for the client. It could be used to convey extra information to
		 // the server and/or tweak certain RPC behaviors.
		 ClientContext context;

		 // The actual RPC.
		 Status status = stub_->joinSync(&context, request, &reply);

		 // Act upon its status.
		 if (status.ok()) {
				//std::cout << "Pulse " << workerPort << " --> " << reply.msg() << std::endl;
		 } else {
				std::cout << status.error_code() << ": " << status.error_message()
								 << std::endl;
		 }
	 }
	 
	 // Forwards request data to master
	 void leaveSync(Request r){
		 // Data we are sending to the server.
		 Request request = r;

		 // Container for the data we expect from the server.
		 Reply reply;

		 // Context for the client. It could be used to convey extra information to
		 // the server and/or tweak certain RPC behaviors.
		 ClientContext context;

		 // The actual RPC.
		 Status status = stub_->leaveSync(&context, request, &reply);

		 // Act upon its status.
		 if (status.ok()) {
				//std::cout << "Pulse " << workerPort << " --> " << reply.msg() << std::endl;
		 } else {
				std::cout << status.error_code() << ": " << status.error_message()
								 << std::endl;
		 }
	 }
	 
    //Forwards messages to the master when it receives a message
    void DataSend(std::string input){
      Reply request;
      Reply reply;
      request.set_msg(input);
      ClientContext context;

      Status status = stub_->DataSend(&context, request, &reply);
      if(status.ok()){
        std::cout<<"Database synchronized.";
      } else{
        std::cout << status.error_code() << ": " << status.error_message()
                 << std::endl;
      }
    }

    void DataSendFollowers(std::string followerfile, std::string input, bool isFollowerFile){
      Request request;
      Reply reply;
      request.set_username(followerfile);
      request.add_arguments(input);
      if(isFollowerFile){
        request.add_arguments("true");
      }
      else
        request.add_arguments("false");
      
      ClientContext context;

      Status status = stub_->DataSendFollowers(&context, request, &reply);
      if(status.ok()){
        std::cout<<"Database synchronized.";
      } else{
        std::cout << status.error_code() << ": " << status.error_message()
                 << std::endl;
      }
    }

    //TODO
    void dataSync(std::vector<std::string> allMessageIDs){
      DataSync clientIDs;
      DataSync reply;
      ClientContext context;

      for(uint i=0; i<allMessageIDs.size(); i++){
        clientIDs.add_ids(allMessageIDs[i]);
      }

      Status status = stub_->dataSync(&context, clientIDs, &reply);

      if(status.ok()){
        //GET INFO FROM REPLY
        std::string delimiter = "::";
        /*std::string nameandmessage = input.substr(2, input.find(delimiter));
        std::string username = nameandmessage.substr(0, nameandmessage.find(':'));
        std::string filename = username+".txt";
        std::ofstream user_file(filename,std::ios::app|std::ios::out|std::ios::in);
        user_file << input;*/
        std::cout<<"Database synchronized.";
      } else{
        std::cout << status.error_code() << ": " << status.error_message()
                 << std::endl;
      }
    }
};

// Logic and data behind the server-client behavior.
class MessengerServiceImpl final : public MessengerServer::Service {
  
  //Sends the list of total rooms and joined rooms to the client
  Status List(ServerContext* context, const Request* request, ListReply* list_reply) override {
    Client user = client_db[find_user(request->username())];
    // int index = 0; // Possible uneeded
    for(Client c : client_db){
      list_reply->add_all_rooms(c.username);
    }
    std::vector<Client*>::const_iterator it;
    for(it = user.client_following.begin(); it!=user.client_following.end(); it++){
      list_reply->add_joined_rooms((*it)->username);
    }
    return Status::OK;
  }

  //Sets user1 as following user2
  Status Join(ServerContext* context, const Request* request, Reply* reply) override {
    std::string username1 = request->username();
    std::string username2 = request->arguments(0);
    int join_index = find_user(username2);
    //If you try to join a non-existent client or yourself, send failure message
    if(join_index < 0 || username1 == username2)
      reply->set_msg("Join Failed -- Invalid Username");
    else{
      Client *user1 = &client_db[find_user(username1)];
      Client *user2 = &client_db[join_index];
      //If user1 is following user2, send failure message
      if(std::find(user1->client_following.begin(), user1->client_following.end(), user2) != user1->client_following.end()){
	reply->set_msg("Join Failed -- Already Following User");
        return Status::OK;
      }
      user1->client_following.push_back(user2);
      user2->client_followers.push_back(user1);
      reply->set_msg("Join Successful");
    }
    return Status::OK; 
  }

  //Sets user1 as no longer following user2
  Status Leave(ServerContext* context, const Request* request, Reply* reply) override {
    std::string username1 = request->username();
    std::string username2 = request->arguments(0);
    int leave_index = find_user(username2);
    //If you try to leave a non-existent client or yourself, send failure message
    if(leave_index < 0 || username1 == username2)
      reply->set_msg("Leave Failed -- Invalid Username");
    else{
      Client *user1 = &client_db[find_user(username1)];
      Client *user2 = &client_db[leave_index];
      //If user1 isn't following user2, send failure message
      if(std::find(user1->client_following.begin(), user1->client_following.end(), user2) == user1->client_following.end()){
	reply->set_msg("Leave Failed -- Not Following User");
        return Status::OK;
      }
      // find the user2 in user1 following and remove
      user1->client_following.erase(find(user1->client_following.begin(), user1->client_following.end(), user2)); 
      // find the user1 in user2 followers and remove
      user2->client_followers.erase(find(user2->client_followers.begin(), user2->client_followers.end(), user1));
      reply->set_msg("Leave Successful");
    }
    return Status::OK;
  }

  //Called when the client startd and checks whether their username is taken or not
  Status Login(ServerContext* context, const Request* request, Reply* reply) override {
    Client c;
    std::string username = request->username();
    int user_index = find_user(username);
    if(user_index < 0){
      c.username = username;
      client_db.push_back(c);
      reply->set_msg("Login Successful!");
    }
    else{ 
      Client *user = &client_db[user_index];
      if(user->connected)
        reply->set_msg("Invalid Username");
      else{
        std::string msg = "Welcome Back " + user->username;
	reply->set_msg(msg);
        user->connected = true;
      }
    }
    return Status::OK;
  }

  Status Chat(ServerContext* context, 
		ServerReaderWriter<Message, Message>* stream) override {
    Message message;
    Client *c;

    //Read messages until the client disconnects
    while(stream->Read(&message)) {
      std::string username = message.username();
      std::string unique_id = message.id();  
      int user_index = find_user(username);
      c = &client_db[user_index];
      //Write the current message to "username.txt"
      std::string filename = username+".txt";
      std::ofstream user_file(filename,std::ios::app|std::ios::out|std::ios::in);
      google::protobuf::Timestamp temptime = message.timestamp();
      std::string time = google::protobuf::util::TimeUtil::ToString(temptime);
      std::string fileinput = unique_id +" :: "+time+" :: "+message.username()+":"+message.msg()+"\n";
 //     messageIDs.push_back(unique_id);
      //"Set Stream" is the default message from the client to initialize the stream
      if(message.msg() != "Set Stream"){
        user_file << fileinput;
        masterCom->DataSend(fileinput);
      }
      //If message = "Set Stream", print the first 20 chats from the people you follow
      else{
        if(c->stream == 0)
      	  c->stream = stream;

        //CALL FOR DATASYNC(messageIDs)
        masterCom->dataSync(collectIDs());

        std::string line;
        std::vector<std::string> newest_twenty;
        std::ifstream in(username+"following.txt");
        int count = 0;
        //Read the last up-to-20 lines (newest 20 messages) from userfollowing.txt
        while(getline(in, line)){
          if(c->following_file_size > 20){
	    if(count < c->following_file_size-20){
              count++;
	      continue;
            }
          }
          newest_twenty.push_back(line);
        }
        Message new_msg; 
 	//Send the newest messages to the client to be displayed
	for(uint i = 0; i<newest_twenty.size(); i++){
	  new_msg.set_msg(newest_twenty[i]);
          stream->Write(new_msg);
        }    
        continue;
      }
      //Send the message to each follower's stream
      std::vector<Client*>::const_iterator it;
      for(it = c->client_followers.begin(); it!=c->client_followers.end(); it++){
        Client *temp_client = *it;
      	if(temp_client->stream!=0 && temp_client->connected)
	  temp_client->stream->Write(message);
        //For each of the current user's followers, put the message in their following.txt file
        std::string temp_username = temp_client->username;
        std::string temp_file = temp_username + "following.txt";
	std::ofstream following_file(temp_file,std::ios::app|std::ios::out|std::ios::in);
	following_file << fileinput;
  masterCom->DataSendFollowers(temp_username, fileinput, true);
        temp_client->following_file_size++;
	std::ofstream user_file(temp_username + ".txt",std::ios::app|std::ios::out|std::ios::in);
        user_file << fileinput;
        masterCom->DataSendFollowers(temp_username, fileinput, false);
      }
    }
    //If the client disconnected from Chat Mode, set connected to false
    c->connected = false;
    return Status::OK;
  }

  //Sends the client to the master server, then sends the client to the first available worker. 
  //Needs to be re-evaluated to actually connect client to new server
  //But works for now, not finished
  Status SendCredentials(ServerContext* context, const Credentials* credentials, Credentials* reply) override {
    std::cout<<"In SendCredentials Serverside"<< std::endl;
    std::string hostname = credentials->hostname();
    std::string portnumber = credentials->portnumber();

    if(!isMaster){
      std::cout << "Redirecting client to Master: " << masterPort << std::endl;
      reply->set_hostname(masterHostname);
      reply->set_portnumber("2323");
      reply->set_confirmation("toMaster");
    }
    else if (isMaster || isLeader){
      std::cout << "Redirecting client to Worker: " << defaultWorkerHostnames[1] << std::endl;
      reply->set_hostname("localhost");
      reply->set_portnumber("2323");
      reply->set_confirmation("toWorker");
    }
    else{
      std::cout<<"There's been an error in the redirect: SendCredentials.'\n'";
      return Status::CANCELLED;
    }
    return Status::OK;
  }

};

// Threads /////////////////////////////////////////////////////////////////////
// Starts a new server process on the same worker port as the crashed process
void* startNewServer(void* missingPort){
	int* mwp = (int*) missingPort;
	std::string missingWorkerPort = std::to_string(*mwp);
	std::cout << "Fixing crash on port: " << missingWorkerPort << '\n';
	std::string cmd = "./fbsd";
	if (isMaster){
		cmd = cmd + " -p " + port;
		cmd = cmd + " -x " + host_x;
		cmd = cmd + " -y " + host_y;
		cmd = cmd + " -m";
		cmd = cmd + " -w " + missingWorkerPort;
	}
	else{
		cmd = cmd + " -p " + port;
		cmd = cmd + " -x " + host_x;
		cmd = cmd + " -r " + masterHostname;
		cmd = cmd + " -w " + missingWorkerPort;
	}
	// THIS IS A BLOCKING FUNCTION!!!!!
	if(system(cmd.c_str()) == -1){
		std::cerr << "Error: Could not start new process" << '\n';
	}
	return 0;
}

// Monitors and restarts other local prcesses if they crash
void* heartBeatMonitor(void* invalidMemory){
	pthread_t startNewServer_tid = -1;
	bool wasDisconnected = false;
	while(true){
		for (size_t i = 0; i < localWorkersComs.size(); i++) {
			std::string possiblyDeadPort = defaultWorkerPorts[i];
			int pdp = atoi(possiblyDeadPort.c_str());
			std::string contactInfo = "localhost:"+ possiblyDeadPort;

			
			if(localWorkersComs[i].pulseCheck()){
				// Connection alive
				if(wasDisconnected){
					std::cout << "Reconnected: " << workerPort << " --> " << possiblyDeadPort << '\n';
					// @TODO: 
					std::cout << "Start new election here" << '\n';
				}
				wasDisconnected = false;
			}
			else{
	 			std::cout << "No Pulse " << workerPort << " --> " << possiblyDeadPort << std::endl;
				wasDisconnected = true;
				// Connection dead
				if(	fileLock("heartBeatMonitorLock") == 0){
					// Start new process if file was unlocked
					pthread_create(&startNewServer_tid, NULL, &startNewServer, (void*) &pdp);
					
					if(fileUnlock("heartBeatMonitorLock") == -1){
						std::cerr << "Error unlocking heartBeatMonitorLock file" << '\n';
					}
					std::cout << "Peer: " << workerPort << " reconnecting..." << '\n';
					sleep(1); 
					//  update connection info reguardless of who restarted  it
					localWorkersComs[i] = ServerChatClient(grpc::CreateChannel(
					contactInfo, grpc::InsecureChannelCredentials()));
				}
				else{
					std::cout << "Peer: " << workerPort << " reconnecting..." << '\n';
					sleep(1);
					//  update connection info reguardless of who restarted  it
					localWorkersComs[i] = ServerChatClient(grpc::CreateChannel(
					contactInfo, grpc::InsecureChannelCredentials()));
				}
			}
		}
	}
	return 0;
}

// Secondary service (for fault tolerence) to listen for connecting workers
void* RunServerCom(void* invalidMemory) {
	std::string server_address = "0.0.0.0:"+workerPort;
  ServerChatImpl service;

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening for works on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
	return 0;
}

void setComLinks(){
	std::string contactInfo = "";
	if (defaultWorkerPorts.size() == 0){
		std::cout << "Error: Default ports uninitialized" << '\n';
	}
	for (size_t i = 0; i < defaultWorkerPorts.size(); i++) {
		if (defaultWorkerPorts[i] == workerPort){
			// Don't connect to self
		}
		else{
			contactInfo = "localhost:"+defaultWorkerPorts[i];
			std::cout << "Connecting: " << contactInfo << '\n';
			localWorkersComs.push_back(
				ServerChatClient(grpc::CreateChannel(
		  	contactInfo, grpc::InsecureChannelCredentials())));
		}
	}
	// Create connection to master host on leading master port 
	masterCom = new ServerChatClient(grpc::CreateChannel(
	masterHostname+":"+masterPort, grpc::InsecureChannelCredentials()));
}

void RunServer(std::string port_no) {
  std::string server_address = "0.0.0.0:"+port_no;
  MessengerServiceImpl service;

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening for clients on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  // Initialize default values
	defaultWorkerPorts.push_back("10001");
	defaultWorkerPorts.push_back("10002");
	defaultWorkerPorts.push_back("10003");
  defaultWorkerHostnames.push_back("lenss-comp4");
  defaultWorkerHostnames.push_back("lenss-comp1");
  defaultWorkerHostnames.push_back("lenss-comp3");

	// Parses options that start with '-' and adding ':' makes it mandontory
  int opt = 0;
  while ((opt = getopt(argc, argv, "c:w:p:x:y:r:ml")) != -1){
    switch(opt) {
      case 'p':
          port = optarg;
					break;
			case 'x':
					host_x = optarg;
					break;
			case 'y':
					host_y = optarg;
					break;
			case 'r':
					masterHostname = optarg;
					break;
			case 'l':
					isLeader = true;
					break;
			case 'm':
					isMaster = true;
					break;
			case 'w':
					workerPort = optarg;
					break;
			case 'c':
					workerToConnect = optarg;
					break;
	      default:
	  std::cerr << "Invalid Command Line Argument\n";
    }
  }
	
	pthread_t thread_id, heartbeat_tid = -1;
	sleep(1);
	pthread_create(&thread_id, NULL, &RunServerCom, (void*) NULL);
	sleep(1);
	// Set up communication links between worker servers
	setComLinks();
	sleep(1);
	// Monitor other local server heartbeats
	pthread_create(&heartbeat_tid, NULL, &heartBeatMonitor, (void*) NULL);	
	sleep(1);
  masterCom->pulseCheck();
	// Start servering clients
  RunServer(port);

  return 0;
}
