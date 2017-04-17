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

/* Will Adams and Nicholas Jackson
   CSCE 438 Section 500*/

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>

#include <grpc++/grpc++.h>

#include "fb.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using hw2::Message;
using hw2::ListReply;
using hw2::Request;
using hw2::Reply;
using hw2::Credentials;
using hw2::MessengerServer;

std::string generateId(Message message){ 
   int id = message.msg().length();
   id = id + (rand() % (id + id/4 + 1));
   id = id + rand();
   return std::to_string(id);
}
//Helper function used to create a Message object given a username and message
Message MakeMessage(const std::string& username, const std::string& msg) {
  Message m;
  m.set_username(username);
  m.set_msg(msg);
  m.set_id(generateId(m));
  google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
  timestamp->set_seconds(time(NULL));
  timestamp->set_nanos(0);
  m.set_allocated_timestamp(timestamp);
  return m;
}

class MessengerClient {
 public:
  MessengerClient(std::shared_ptr<Channel> channel, std::string name, std::string address){
      clientStub_ = MessengerServer::NewStub(channel);
      username = name;
      workerAddress = address;
  }


  //Calls the List stub function and prints out room names
  void List(const std::string& username){
    //Data being sent to the server
    Request request;
    request.set_username(username);
  
    //Container for the data from the server
    ListReply list_reply;

    //Context for the client
    ClientContext context;
  
    Status status = clientStub_->List(&context, request, &list_reply);

    //Loop through list_reply.all_rooms and list_reply.joined_rooms
    //Print out the name of each room 
    if(status.ok()){
        std::cout << "All Rooms: \n";
        for(std::string s : list_reply.all_rooms()){
	  std::cout << s << std::endl;
        }
        std::cout << "Following: \n";
        for(std::string s : list_reply.joined_rooms()){
          std::cout << s << std::endl;;
        }
    }
    else{
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
    } 
  }

  //Calls the Join stub function and makes user1 follow user2
  void Join(const std::string& username1, const std::string& username2){
    Request request;
    //username1 is the person joining the chatroom
    request.set_username(username1);
    //username2 is the name of the room we're joining
    request.add_arguments(username2);

    Reply reply;

    ClientContext context;

    Status status = clientStub_->Join(&context, request, &reply);

    if(status.ok()){
      std::cout << reply.msg() << std::endl;
    }
    else{
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      std::cout << "RPC failed\n";
    }
  }

  //Calls the Leave stub function and makes user1 no longer follow user2
  void Leave(const std::string& username1, const std::string& username2){
    Request request;

    request.set_username(username1);
    request.add_arguments(username2);

    Reply reply;

    ClientContext context;

    Status status = clientStub_->Leave(&context, request, &reply);

    if(status.ok()){
      std::cout << reply.msg() << std::endl;
    }
    else{
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      std::cout << "RPC failed\n";
    }
  }

  //Sends the client to the master server, then sends the client to the first available worker.
  //Does not yet actually send client to a new worker. 
  //Needs to be re-evaluated once workers are communicating with masters
  void SendCredentials(const std::string& host, const std::string& port){

    std::cout<<"In SendCredentials clientside" << std::endl;

    if(clientStub_ == NULL){
      std::cout<<"Service not initialized, check again asshat." << std::endl;
      return;
    }

    Credentials credentials;
    
    credentials.set_hostname(host);
    credentials.set_portnumber(port);

    Credentials reply;

    ClientContext context;
    std::cout<<"Client stub sending to server" << std::endl;
    Status status = clientStub_->SendCredentials(&context, credentials, &reply);

    if(status.ok()){

      std::cout<<"Made it into sendcredentials"<<std::endl;

      if(reply.confirmation() == "toMaster"){
        std::cout<<"Made it to be redirected from worker to master"<<std::endl;
      //Needs to reconnect twice
        std::string login_info = reply.hostname()+ ":" +reply.portnumber();
        std::shared_ptr<Channel> channel = grpc::CreateChannel(login_info, grpc::InsecureChannelCredentials());
          clientStub_ = MessengerServer::NewStub(channel);

        Credentials reply2;
        ClientContext context2;
        credentials.set_hostname(reply.hostname());
        credentials.set_portnumber(reply.portnumber());

        Status status = clientStub_->SendCredentials(&context2, credentials, &reply2);
        if(status.ok()){
          std::string login_info = reply2.hostname()+ ":" +reply2.portnumber();
          std::shared_ptr<Channel> channel = grpc::CreateChannel(login_info, grpc::InsecureChannelCredentials());
            clientStub_ = MessengerServer::NewStub(channel);
        }
      }
      else{
        std::cout<<"Made it to be redirected from Master to Worker"<<std::endl;
        std::string login_info = reply.hostname()+ ":" +reply.portnumber();
        std::shared_ptr<Channel> channel = grpc::CreateChannel(login_info, grpc::InsecureChannelCredentials());
          clientStub_ = MessengerServer::NewStub(channel);
      }
    }
    else{
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
    }
  }


  std::string Login(const std::string& username){
    Request request;
    
    request.set_username(username);

    Reply reply;

    ClientContext context;

    Status status = clientStub_->Login(&context, request, &reply);

    if(status.ok()){
      return reply.msg();
    }
    else{
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return "RPC failed";
    }
  }

  //Calls the Chat stub function which uses a bidirectional RPC to communicate
  void Chat (const std::string& username, const std::string& messages, const std::string& usec) {
    ClientContext context;

    std::shared_ptr<ClientReaderWriter<Message, Message>> stream(
	clientStub_->Chat(&context));

    //Thread used to read chat messages and send them to the server
    std::thread writer([username, messages, usec, stream]() {  
	  if(usec == "n") { 
        std::string input = "Set Stream";
        Message m = MakeMessage(username, input);
        stream->Write(m);
        std::cout << "Enter chat messages: \n";
        while(getline(std::cin, input)){
          m = MakeMessage(username, input);
          stream->Write(m);
        }
        stream->WritesDone();
      }
	  else {
		std::string input = "Set Stream";
        Message m = MakeMessage(username, input);
        stream->Write(m);
		int msgs = stoi(messages);
		int u = stoi(usec);
		time_t start, end;
        std::cout << "Enter chat messages: \n";
		time(&start);
        for(int i=0; i<msgs; i++) {
          input = "hello" + std::to_string(i);
		  m = MakeMessage(username, input);
          stream->Write(m);
		  std::cout << input << '\n';
		  usleep(u);
        }
		time(&end);
		std::cout << "Elapsed time: " << (double)difftime(end,start) << std::endl;
        stream->WritesDone();
	  }
	});

    //Thread used to display chat messages from users that this client follows 
    std::thread reader([username, stream]() {
       Message m;
       while(stream->Read(&m)){
	     std::cout << m.username() << " -- " << m.msg() << std::endl;
       }
    });

    //Wait for the threads to finish
    writer.join();
    reader.join();

  }

 private:
  std::string username;
  std::string workerAddress;
  std::unique_ptr<MessengerServer::Stub> stub_;
  std::unique_ptr<MessengerServer::Stub> clientStub_;
};

//Parses user input while the client is in Command Mode
//Returns 0 if an invalid command was entered
//Returns 1 when the user has switched to Chat Mode
int parse_input(MessengerClient* messenger, std::string username, std::string input){
  //Splits the input on spaces, since it is of the form: COMMAND <TARGET_USER>
  std::size_t index = input.find_first_of(" ");
  if(index!=std::string::npos){
    std::string cmd = input.substr(0, index);
    if(input.length()==index+1){
      std::cout << "Invalid Input -- No Arguments Given\n";
      return 0;
    }
    std::string argument = input.substr(index+1, (input.length()-index));
    if(cmd == "JOIN"){
      messenger->Join(username, argument);
    }
    else if(cmd == "LEAVE")
      messenger->Leave(username, argument);
    else{
      std::cout << "Invalid Command\n";
      return 0;   
    }
  }
  else{
    if(input == "LIST"){
      messenger->List(username); 
    }
    else if(input == "CHAT"){
      //Switch to chat mode
      return 1;
    }
    else{
      std::cout << "Invalid Command\n";
      return 0;   
    }
  }
  return 0;   
}

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint (in this case,
  // localhost at port 50051). We indicate that the channel isn't authenticated
  // (use of InsecureChannelCredentials()).

  std::string hostname = "localhost";
  std::string username = "default";
  std::string port = "2323";
  std::string messages = "10000";
  std::string usec = "n";
  int opt = 0;
  while ((opt = getopt(argc, argv, "h:u:p:m:t:")) != -1){
    switch(opt) {
      case 'h':
	  hostname = optarg;break;
      case 'u':
          username = optarg;break;
      case 'p':
          port = optarg;break;
	  case 'm':
		  messages = optarg;break;
	  case 't':
		  usec = optarg;break;
      default: 
	  std::cerr << "Invalid Command Line Argument\n";
    }
  }

  std::string login_info = hostname + ":" + port;

  //Create the messenger client with the login info
  std::shared_ptr<Channel> channel = grpc::CreateChannel(login_info, grpc::InsecureChannelCredentials());

  MessengerClient *messenger = new MessengerClient(channel, username, login_info); 


  //Call the Redirect function
  std::cout<<"Beginning Redirect"<<std::endl;
  //messenger = messenger->SendCredentials(hostname,port);
  messenger->SendCredentials(hostname,port);
  std::cout<<"End Redirect"<<std::endl;


  std::string response = messenger->Login(username);
  //If the username already exists, exit the client
  if(response == "Invalid Username"){
    std::cout << "Invalid Username -- please log in with a different username \n";
    return 0;
  }
  else{
    std::cout << response << std::endl;
   
    std::cout << "Enter commands: \n";
    std::string input; 
    //While loop that parses all of the command input
    while(getline(std::cin, input)){
      //If we have switched to chat mode, parse_input returns 1
      if(parse_input(messenger, username, input) == 1)
	break;
    }
    //Once chat mode is enabled, call Chat stub function and read input
    messenger->Chat(username, messages, usec);
  }
  return 0;
}
