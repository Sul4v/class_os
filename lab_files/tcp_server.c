#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 

#include <sys/socket.h>
#include <sys/types.h>

#include <netinet/in.h>

#define PORT 9002


int main()
{
	//create a string to hold data we will send to the client
	char server_message[256] = "You have reached the server!";

	//create socket
	int server_socket;
	server_socket = socket(AF_INET , SOCK_STREAM,0);

	//check for fail error
	if (server_socket == -1) {
        printf("socket creation failed..\n");
        exit(EXIT_FAILURE);
    }
    else
    	printf("Server: socket CREATION success..\n");
    

	//define server address structure
	struct sockaddr_in server_address;
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(PORT);
	server_address.sin_addr.s_addr = INADDR_ANY;


	//bind the socket to our specified IP and port
	if (bind(server_socket , 
		(struct sockaddr *) &server_address,
		sizeof(server_address)) < 0)
	{
		printf("socket bind failed..\n");
        exit(EXIT_FAILURE);
	}
	 else
    	printf("Server: socket BIND success..\n");
    




	//after it is bound, we can listen for connections
	
	//2nd: how many connections can be waiting for this socket at one point in time so at least 1
	//So for example 5 is the maximum number of client connections that server will queue for this listening socket
	if(listen(server_socket,5)<0){
		printf("Listen failed..\n");
        exit(EXIT_FAILURE);
	}
	 else
    	printf("Server: socket LISTEN success..\n");
    

	//now after listen, socket is a fully functional listening socket

	//once we are able to listen to connections, we can accept connections
	//when we accept a connection, what we get back is the client socket that we will write to	

	int addrlen = sizeof(server_address);	

	int client_socket;
	
	//In the call to accept(), the server is put to sleep and when there is an incoming client request, then the function accept() wakes up and returns the socket descriptor representing the client socket
	//1st: The socket that has been created with socket(), bound to a local address with bind(), and is listening for connections after a listen().
	//2nd: A pointer to the sockaddr structure
	//3rd: addrlen - the size (in bytes) of the sockaddr structure
	client_socket = accept(server_socket,
		(struct sockaddr *) &server_address,
		(socklen_t *) &addrlen);

	if(client_socket<0){
		printf("accept failed..\n");
        exit(EXIT_FAILURE);
	}
	 else
    	printf("Server: socket ACCEPT success..\n");
    

	//we now have a client socket that we can send data to
	//send the message
    printf("Server: SENDING data..\n");


	//1st: socket we are sending data on
	//2nd: message we are sending
	//3rd: size of message
	//4th: optional flags 0
	send(client_socket , server_message , sizeof(server_message),0);


	//close the socket
	close(server_socket); 



	return 0;
}