#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 

#include <sys/socket.h>
#include <sys/types.h>

#include <netinet/in.h>

#define PORT 9002

int main(){

	//create a socket
	//use an int to hold the fd for the socket
	int network_socket;
	//1st argument: domain/family of socket. For Internet family of IPv4 addresses, we use AF_INET 
	//2nd: type of socket TCP
	//3rd: protocol for connection, left 0 when using tcp
	network_socket = socket(AF_INET , SOCK_STREAM, 0);

	//check for fail error
	if (network_socket == -1) {
        printf("socket creation failed..\n");
        exit(EXIT_FAILURE);
    }
    else
    	printf("Client: socket CREATION success..\n");
    

	//connect to another socket on the other side
	
	//specify an address for the socket we want to connect to
	struct sockaddr_in server_address;
	
	//specify address family
	server_address.sin_family = AF_INET;
	
	//specify the port we want to connect to remotely
	//htons converts integer port to right format for the structure
	//9002 just some port we know the OS is not using
	server_address.sin_port = htons(PORT);

	//specify the actual IP address
	//connect to our local machine
	//INADDR_ANY gets any IP address used on our local machine
	//INADDR_ANY is an IP address that is used when we don't want to bind a socket to any specific IP. Basically, while implementing communication, we need to bind our socket to an IP address. When we don't know the IP address of our machine, we can use the special IP address INADDR_ANY. It allows our server to receive packets that have been targeted by any of the interfaces.
	server_address.sin_addr.s_addr = INADDR_ANY;


	//connect
	//1st : socket 
	//2nd: server address structure , cast to a pointer to a sockaddr struct so pass the address
	//3rd: size of address
	int connection_status = 
	connect(network_socket, 
			(struct sockaddr *) &server_address,
			sizeof(server_address));

	//check for errors with the connection
	if(connection_status == -1){
		printf("There was an error making a connection to the remote socket \n\n");
		exit(EXIT_FAILURE);
	}
	else
    	printf("Client: socket CONNECT success..\n");
    


	//now that we have the connect, we either send or receive data
	//since since is the client, we receive data

	//1st: socket
	//2nd: location for some string to hold data we get back from the server
	//3rd: size of data to be received
	//4th: optional flags , 0 default
	char server_response[256]; //empty string
	
    printf("Client: RECEIVING data..\n");
	recv(network_socket , &server_response , sizeof(server_response),0);

	//print out the data we get back
	printf("The server sent the data: %s\n" , server_response);


	//close socket after we are done
	close(network_socket);



	return 0;
}
