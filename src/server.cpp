//
// Created by Spencer Keith on 11/18/22.
//

#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include "prompts.cpp"
#include "fileIO.cpp"
#include "packetIO.cpp"
#include <bits/stdc++.h>

#define FINAL_SEQUENCE_NUMBER -1


//*****//*****//*****//*****//*****//*****//*****//*****//*****//*****//
//Function declarations                          //*****//*****//*****//

void stopAndWaitProtocol(int clientSocket, int packetSize, const std::string& filePath);

void selectiveRepeatProtocol(int serverSocket, int clientSize);


int packet_size, slidingWindowSize, sequence_range;
std::string filePath;

//*****//*****//*****//*****//*****//*****//*****//*****//*****//*****//
//Function implementations (including main)      //*****//*****//*****//

int main() {

    //set up TCP socket
    struct sockaddr_in serverAddress = {0};
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        perror("Failed to create server socket!");
        exit(EXIT_FAILURE);
    }

    //bind server socket to port
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Failed to bind socket to port (setsockopt)!");
        exit(EXIT_FAILURE);
    }

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    bool quit; //true for yes, false for no
    do {

        //prompt user for each of the following fields
        //port number of the target server
        int portNum = 9091;//userIntPrompt("What is the port number of the target server:", 0, 9999);
        serverAddress.sin_port = htons(portNum);
        if (bind(serverSocket, (sockaddr*) &serverAddress, sizeof(serverAddress)) == -1) {
            perror("Failed to bind socket to port (bind)!");
            exit(EXIT_FAILURE);
        }
        //0 for S&W, 1 for SR
        int protocolType = userIntPrompt("Type of protocol, S&W (0) or SR (1):", 0, 1);
        //specified size of packets to be sent
        packet_size = 30;//userIntPrompt("Size of packets (must be same as sender):", 1, 30000);//INT_MAX, but 30000 avoids segfault?

		//Only selective repeat uses sequence_range and windows
        if(protocolType > 0) {
			//TODO - SEE client.cpp
			sequence_range = 80;//userIntPrompt("Sequence range: ", 2, 1000);
            slidingWindowSize = 20;//userIntPrompt("Size of sliding window:", 1, sequence_range/2);
        }
        //path of file to be sent
        filePath = "output.txt";//userStringPrompt("What is the filepath of the file you wish to write TO:");

        //listen for client connection
        std::cout << std::endl << "Listening for client connection..." << std::endl;
        if (listen(serverSocket, SOMAXCONN) < 0) {
            perror("Failed to listen for client connection!");
            exit(EXIT_FAILURE);
        }

        //wait for connection
        sockaddr_in client = {0};
        socklen_t clientSize = sizeof(client);
        int clientSocket = accept(serverSocket, (sockaddr*) &client, &clientSize);
        if(clientSocket == -1){
            perror("Client socket failed to connect!");
            exit(EXIT_FAILURE);
        }

        switch (protocolType) {
            case 0:
                std::cout << std::endl << "Executing Stop & Wait protocol..." << std::endl << std::endl;
                stopAndWaitProtocol(clientSocket, packet_size, filePath);
                break;
            case 1:
                std::cout << std::endl << "Executing Selective Repeat protocol..." << std::endl << std::endl;
                selectiveRepeatProtocol(clientSocket, clientSize);
                break;
            default:
                break;
        }

        quit = userBoolPrompt("Would you like to exit (1), or perform another file transfer (0):");
    } while(!quit);

	//TODO - do we have to close sockets? I don't know

}


//*****//*****//*****//*****//*****//*****//*****//*****//*****//*****//
//Network protocols (algorithms)

/*
 * Stop and Wait algorithm implementaiton
 */
void stopAndWaitProtocol(int clientSocket, int packetSize, const std::string& filePath) {

    char packet[ sizeof(int) + packetSize ];
    char ack[ sizeof(int) ];
    int iterator = 0;
    while(true) {

        if(read(clientSocket, packet, sizeof(packet)) != -1) {

            int packetSeqNum;
            std::memcpy(&packetSeqNum, &packet, sizeof(int));
            if(packetSeqNum == FINAL_SEQUENCE_NUMBER) {
                break;
            }

            std::cout << "Received packet #" << packetSeqNum << "! ";
            printPacket(packet, packetSize);

            if(packetSeqNum <= iterator) {
                std::copy(static_cast<const char*>(static_cast<const void*>(&packetSeqNum)),
                          static_cast<const char*>(static_cast<const void*>(&packetSeqNum)) + sizeof(packetSeqNum),
                          ack);
                sendAckSW(clientSocket, ack, packetSeqNum);
                appendPacketToFile(packet, packetSize, filePath);
                iterator++;
            } else {
                std::cout << "Received packet is corrupted!" << std::endl;
            }

        }

    }

}


/*
 * Selective Repeat algorithm implementation
 */
void selectiveRepeatProtocol(int serverSocket, int clientSize) {

    int iterator = 0;
	
	int FAILSAFE = 0;
	
	int SR_window[slidingWindowSize];
	
	std::string received_packet_contents[packet_size];
	
	//populate SR_window with sequence numbers
	for (int i = 0; i < slidingWindowSize; i++)
		SR_window[i] = i;

	int last_packet_num = SR_window[slidingWindowSize-1];
	
    while(FAILSAFE < 1000000000) {
		FAILSAFE++;

		int packetArrSize = (int) (packet_size + sizeof(int) + sizeof(bool)); //TODO -> + sizeof(CHECKSUM)
		char packet[packetArrSize];

		if(recv(serverSocket, &packet, sizeof(packet), MSG_DONTWAIT) > 0) {//TODO - sizeof(packet) could just be packetArrSize?

			int packet_sequence_number = 0;
			bool packet_valid = int(packet[sizeof(int)]);
			std::string packet_contents;

			//this could be a function (probably unecessary, but it comes up in different files)
			//get the sequence number of the packet
			if (packet[0]==0 && packet[1]==1 && packet[2]==1 && packet[3]==0) {//final sequence num reached
				packet_sequence_number = -1;
			} else {//TODO - this is a rudimentary solution for the end of sequence
				for (int i=0; i < sizeof(int); i++) {
					packet_sequence_number += packet[i];
				}				
			}
			int length = (int) (sizeof(int) + sizeof(bool) + packet_size);
			for(int i = sizeof(int)+1; i < length; i++) {
				packet_contents = packet_contents + packet[i];
			}

			//TODO - can this be removed? I don't know
			if(packet_sequence_number == FINAL_SEQUENCE_NUMBER) {
				break;
			}
			if(packet_sequence_number < SR_window[0] && packet_sequence_number > SR_window[slidingWindowSize-1]) {
				std::cout << "Packet " << packet_sequence_number << " lost the ack, resend" << std::endl;				
				sendAck(serverSocket, packet_sequence_number);

				continue;
			}

            std::cout << "Packet " << packet_sequence_number << " recieved" << std::endl;

			//TODO-CHECKSUM - if checksum fails, continue;

			//find the index of the received packet, and send the ack
			for (int i = 0; i < slidingWindowSize; i++) {
				if (SR_window[i] == packet_sequence_number) {
					sendAck(serverSocket, packet_sequence_number);

					received_packet_contents[i] = packet_contents;
					iterator++;
					last_packet_num = SR_window[i];
					SR_window[i] = -2;
					break;
				}
			}

			//check if the window needs to move
			if (SR_window[0] == -2) {
				while (SR_window[0] == -2) {
				//write the information to the file
				
				writePacketToFile(true, received_packet_contents[0], filePath);
				//OPTIMIZE - while and for loop could be separated
					for(int i=0; i<slidingWindowSize-1; i++){
						SR_window[i] = SR_window[i+1];
						received_packet_contents[i] = received_packet_contents[i+1];
					}
					//If -2, it was received
					if (SR_window[slidingWindowSize-1] == -2) {
						SR_window[slidingWindowSize-1] = SR_window[slidingWindowSize-1]+1;
					} else {
						SR_window[slidingWindowSize-1] = SR_window[slidingWindowSize-2]+1;
					}
					
					if (SR_window[slidingWindowSize-1] >= sequence_range) {
						SR_window[slidingWindowSize-1] = 0;
					}
					
					received_packet_contents[slidingWindowSize-1] = '0';
				}
				//print the window
				std::cout << "receiver window: [";
				for (int i = 0; i < slidingWindowSize; i++)
					std::cout << SR_window[i] << ", ";
				std::cout << "]" << std::endl;
			} else if (SR_window[0] == FINAL_SEQUENCE_NUMBER) {//check if done
				break;
			}
		}

    }
	
	std::cout << "Last packet seq# received: " << last_packet_num << std::endl;
	//std::cout << ":" << std::endl << "Successfully received file." << std::endl;
	close(serverSocket);

}