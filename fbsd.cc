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
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

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
using grpc::Channel;
using grpc::ClientContext;
// Forwards ////////////////////////////////////////////////////////////////////
class ServerChatClient;

// Global Variables ////////////////////////////////////////////////////////////
bool isMaster = false;
bool isLeader = false;
std::string workerPort = "8888"; // Port for workers to connect to
std::string workerToConnect = "8889"; // Port for this process to contact
std::string masterPort = "10001"; // Port for workers to connect to
std::string masterHostname = "lenss-comp4"; // Port for this process to contact
std::vector<std::string> defaultWorkerPorts;
std::vector<std::string> defaultWorkerHostnames;

;
std::vector<ServerChatClient> localWorkersComs;

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

//Vector that stores every client that has been created
std::vector<Client> client_db;

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

// Recieving side of interserver chat Service
class ServerChatImpl final : public ServerChat::Service {
	// Asks for a reply and restarts the server if no replay is recieved
	Status pulseCheck(ServerContext* context, const Reply* in, Reply* out) override{
		std::string pid = std::to_string(getpid());
		out->set_msg("Worker port:" + workerPort + " PID:" + pid);
		return Status::OK;
	}
};

// Sending side of interserver chat Service
class ServerChatClient {
private:
	std::unique_ptr<ServerChat::Stub> stub_;
public:
 ServerChatClient(std::shared_ptr<Channel> channel)
	 : stub_(ServerChat::NewStub(channel)) {}

	 void pulseCheck() {
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
			 std::cout << "Server Still Alive: " << reply.msg() << std::endl;
		 } else {
			 std::cout << "Server with pid: " << reply.msg() << " has died!" << std::endl;
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
      int user_index = find_user(username);
      c = &client_db[user_index];
      //Write the current message to "username.txt"
      std::string filename = username+".txt";
      std::ofstream user_file(filename,std::ios::app|std::ios::out|std::ios::in);
      google::protobuf::Timestamp temptime = message.timestamp();
      std::string time = google::protobuf::util::TimeUtil::ToString(temptime);
      std::string fileinput = time+" :: "+message.username()+":"+message.msg()+"\n";
      //"Set Stream" is the default message from the client to initialize the stream
      if(message.msg() != "Set Stream")
        user_file << fileinput;
      //If message = "Set Stream", print the first 20 chats from the people you follow
      else{
        if(c->stream == 0)
      	  c->stream = stream;
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
        temp_client->following_file_size++;
	std::ofstream user_file(temp_username + ".txt",std::ios::app|std::ios::out|std::ios::in);
        user_file << fileinput;
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
      reply->set_portnumber("3055");
      reply->set_confirmation("toMaster");
    }
    else if (isMaster || isLeader){
      std::cout << "Redirecting client to Worker: " << defaultWorkerHostnames[0] << std::endl;
      reply->set_hostname("localhost");
      reply->set_portnumber("3055");
      reply->set_confirmation("toWorker");
    }
    else{
      std::cout<<"There's been an error in the redirect: SendCredentials.'\n'";
      return Status::CANCELLED;
    }
    return Status::OK;
  }

};



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

void* setComLinks(void* invalidMemory){
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
	return 0;
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
  std::string port = "3055"; // Port for clients to connect to
	defaultWorkerPorts.push_back("10001");
	defaultWorkerPorts.push_back("10002");
	defaultWorkerPorts.push_back("10003");
  defaultWorkerHostnames.push_back("lenss-comp4");
  defaultWorkerHostnames.push_back("lenss-comp1");
  defaultWorkerHostnames.push_back("lenss-comp3");

	std::string host_x = "";
	std::string host_y = "";
	std::string reliableServer = "";
	
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
					reliableServer = optarg;
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
	
	//@TODO: This won't run until moved to another thread
	//@TODO: Add startup ports to scripts
	pthread_t thread_id, comLinks_tid = -1;
	// int wport = std::stoi(workerPort);
	pthread_create(&thread_id, NULL, &RunServerCom, (void*) NULL);
	// Set up communication links between worker servers
	pthread_create(&comLinks_tid, NULL, &setComLinks, (void*) NULL);
  RunServer(port);

  return 0;
}
