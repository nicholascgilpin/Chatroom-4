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
using hw2::MessengerServer;

//Helper function used to create a Message object given a username and message
Message MakeMessage(const std::string& username, const std::string& msg) {
  Message m;
  m.set_username(username);
  m.set_msg(msg);
  google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
  timestamp->set_seconds(time(NULL));
  timestamp->set_nanos(0);
  m.set_allocated_timestamp(timestamp);
  return m;
}

class MessengerClient {
 public:
  MessengerClient(std::shared_ptr<Channel> channel)
      : stub_(MessengerServer::NewStub(channel)) {}

  //Calls the List stub function and prints out room names
  void List(const std::string& username){
    //Data being sent to the server
    Request request;
    request.set_username(username);
  
    //Container for the data from the server
    ListReply list_reply;

    //Context for the client
    ClientContext context;
  
    Status status = stub_->List(&context, request, &list_reply);

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

    Status status = stub_->Join(&context, request, &reply);

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

    Status status = stub_->Leave(&context, request, &reply);

    if(status.ok()){
      std::cout << reply.msg() << std::endl;
    }
    else{
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      std::cout << "RPC failed\n";
    }
  }

  //Called when a client is run
  std::string Login(const std::string& username){
    Request request;
    
    request.set_username(username);

    Reply reply;

    ClientContext context;

    Status status = stub_->Login(&context, request, &reply);

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
  void Chat (const std::string& username) {
    ClientContext context;

    std::shared_ptr<ClientReaderWriter<Message, Message>> stream(
	stub_->Chat(&context));

    //Thread used to read chat messages and send them to the server
    std::thread writer([username, stream]() { 
      std::string input = "Set Stream";
      Message m = MakeMessage(username, input);
      stream->Write(m);
      std::cout << "Enter chat messages: \n";
      while(getline(std::cin, input)){
        m = MakeMessage(username, input);
        stream->Write(m);
      }
      stream->WritesDone();
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
  std::unique_ptr<MessengerServer::Stub> stub_;
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
  std::string port = "3010";
  int opt = 0;
  while ((opt = getopt(argc, argv, "h:u:p:")) != -1){
    switch(opt) {
      case 'h':
	  hostname = optarg;break;
      case 'u':
          username = optarg;break;
      case 'p':
          port = optarg;break;
      default: 
	  std::cerr << "Invalid Command Line Argument\n";
    }
  }

  std::string login_info = hostname + ":" + port;

  //Create the messenger client with the login info
  MessengerClient *messenger = new MessengerClient(grpc::CreateChannel(
        login_info, grpc::InsecureChannelCredentials())); 
  //Call the login stub function
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
    messenger->Chat(username);
  }
  return 0;
}
