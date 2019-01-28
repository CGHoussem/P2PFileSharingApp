#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <signal.h>
#include <gtk/gtk.h>
#include "../fonctions.h"

#define ERROR     		-1
#define BUFFER    		512      //this is max size of input and output buffer used to store send and recieve data
#define LISTENING_PORT 	2000
#define MAX_CLIENTS    	4

typedef struct {
	int sock; // sock is socket desriptor for connecting to remote server 
	int peer_sock; // socket descriptor for peer during fetch
	int listen_sock; // socket descriptor for listening to incoming connections
} server_params;

void *clientServerThread(void *args);
void *guiThread(void *args);
void on_menuitemHelp_activate();
void on_menuitemupload_activate();
void refreshTreeView();
void stopServer();
void destroy();

pthread_t threads[2];
server_params *server_p;
GtkWidget *window, *fcdialog;
GtkMenuItem *menuitemmenu, *menuitemfiles, *menuitemhelp;
GtkTreeView *treeview;
GtkStatusbar *statusbar;

int main(int argc, char **argv) 
{
	GtkBuilder *builder;

	// GTK init
	gtk_init(&argc, &argv);

	builder = gtk_builder_new();
	gtk_builder_add_from_file(builder, "client gui.glade", NULL);

	window = GTK_WIDGET(gtk_builder_get_object(builder, "client_window"));
	menuitemmenu= GTK_MENU_ITEM(gtk_builder_get_object(builder, "menuitemMenu"));	
	menuitemfiles= GTK_MENU_ITEM(gtk_builder_get_object(builder, "menuitemFiles"));
	menuitemhelp = GTK_MENU_ITEM(gtk_builder_get_object(builder, "menuitemHelp"));
	treeview = GTK_TREE_VIEW(gtk_builder_get_object(builder, "treeview"));
	statusbar = GTK_STATUSBAR(gtk_builder_get_object(builder, "statusbar"));
	fcdialog = gtk_file_chooser_dialog_new(
		"Choose File to Upload", window, GTK_FILE_CHOOSER_ACTION_OPEN, 
		"_Cancel", GTK_RESPONSE_CANCEL, 
		"_Choose", GTK_RESPONSE_ACCEPT, NULL);
	gtk_builder_connect_signals(builder, NULL);
	g_object_unref(builder);
	gtk_widget_show_all(window);

	// Threads
	pthread_create(&threads[0], NULL, guiThread, NULL);
	pthread_create(&threads[1], NULL, clientServerThread, NULL);
	
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);

	return 0;
}

void on_menuitemHelp_activate()
{
	printf("Open help dialog\n");
}
void on_menuitemupload_activate()
{
	GtkFileChooser *chooser = GTK_FILE_CHOOSER(fcdialog);

	gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
	gint result = gtk_dialog_run(GTK_DIALOG(fcdialog));

	if (result == GTK_RESPONSE_ACCEPT)
	{
		char *filename;
		filename = gtk_file_chooser_get_filename(chooser);
		printf("Filename: %s\n", filename);
		g_free(filename);
	}

	gtk_widget_destroy(fcdialog);

	/* */
	
}

void publishFile()
{
	char *temp;
	temp="pub";

	send(server_p->sock, temp, sizeof(temp) ,0); // send input to server

	/*printf("Enter the file name with extension, Filepath ");
	scanf(" %[^\t\n]s", server_p->input); //receive user input
	sprintf(server_p->input, "%s %d", server_p->input, LISTENING_PORT);
	send(server_p->sock, server_p->input, strlen(server_p->input) ,0); // send input to server
	server_p->len = recv(server_p->sock, server_p->output, BUFFER, 0); // recieve confirmation message from server
	server_p->output[server_p->len] = '\0';
	printf("%s\n" , server_p->output); // display confirmation message
	bzero(server_p->output, BUFFER); // pad buffer with zeros*/
}

void refreshTreeView(){}

void stopServer()
{
	close(server_p->sock);
	close(server_p->peer_sock);
	close(server_p->listen_sock);
}

void destroy()
{
	// KILL SERVER
	stopServer();
	pthread_kill(threads[1], SIGKILL);
	free(server_p);
	// KILL GUI
	gtk_main_quit();
	pthread_kill(threads[0], SIGKILL);
	exit(0);
}

void *clientServerThread(void *args)
{
	struct sockaddr_in remote_server; // contains IP and port no of remote server
	char input[BUFFER];  //user input stored
	char temp_ch[BUFFER];  //user temp stored 
	char output[BUFFER]; //recd from remote server
	int len;//to measure length of recieved input stram on TCP
	char *temp; // variable to store temporary values
	int choice;//to take user input

	//variables declared for fetch operation
	char file_fet[BUFFER];//store file name keyword to be fetched
	char peer_ip[BUFFER];//store IP address of the peer for connection
	char peer_port[BUFFER];//store port no of the peer for fetch
	struct sockaddr_in peer_connect; // contains IP and port no of desired peer for fetch

	//variables for acting as server
	struct sockaddr_in server; //server structure when peer acting as server
	struct sockaddr_in client; //structure for peer acting as server to bind to particular incoming peer
	int sockaddr_len=sizeof (struct sockaddr_in);	
	int pid;//variable to store process id of process created after fork

	//variables for select system call
	fd_set master; // this is master file desriptor
	fd_set read_fd; // for select

	//for connecting with server for publishing and search files
	if ((server_p->sock= socket(AF_INET, SOCK_STREAM, 0)) == ERROR)
	{ 
		perror("socket");  // error checking the socket
		exit(-1);  
	}

	remote_server.sin_family = AF_INET; // family
	remote_server.sin_port = htons(atoi("1996")); // Port No and htons to convert from host to network byte order. atoi to convert asci to 		integer
	remote_server.sin_addr.s_addr = inet_addr("127.0.0.1");//IP addr in ACSI form to network byte order converted using inet
	bzero(&remote_server.sin_zero, 8); //padding zeros
	
	if((connect(server_p->sock, (struct sockaddr *)&remote_server,sizeof(struct sockaddr_in)))  == ERROR) //pointer casted to sockaddr*
	{
		perror("connect");
		exit(-1);
	}
	gtk_statusbar_push(statusbar, 0, "Connected to the server!");

	// Setting up own port for listening incoming connections for fetch
	// Initialising
	if ((server_p->listen_sock= socket(AF_INET, SOCK_STREAM, 0)) == ERROR) // creating socket
	{ 
		perror("socket");  // error while checking the socket
		exit(-1);  
	} 

	/* peer as server */ 
	server.sin_family = AF_INET; // protocol family
	server.sin_port = htons(LISTENING_PORT); // Port No and htons to convert from host to network byte order. 
	server.sin_addr.s_addr = INADDR_ANY;//INADDR_ANY means server will bind to all netwrok interfaces on machine for given port no
	bzero(&server.sin_zero, 8); //padding zeros

	/* Binding the listening socket */
	if((bind(server_p->listen_sock, (struct sockaddr *)&server, sockaddr_len)) == ERROR) //pointer casted to sockaddr*
	{
		perror("bind");
		exit(-1);
	}

	/* Listen the incoming connections */
	if((listen(server_p->listen_sock, MAX_CLIENTS)) == ERROR) // listen for max connections
	{
		perror("listen");
		exit(-1);
	}

	// Using select system call to handle multiple connections
	FD_ZERO(&master);// clear the set 
	FD_SET(server_p->listen_sock,&master) ; //adding our descriptor to the set
	int i;

	pid=fork();
	
	if (!pid) 
	{ 
		while(1)
		{
			read_fd = master; //waiting for incoming request
			if(select(FD_SETSIZE,&read_fd,NULL,NULL,NULL)==-1)
			{
				perror("select");
				return NULL;
			}

			//handle multiple connections
			for (i = 0; i < FD_SETSIZE; ++i)
			{
				if(FD_ISSET(i,&read_fd)) //returns true if i in read_fd
				{
					if(i==server_p->listen_sock)
					{
						int new_peer_sock; //new socket descriptor for peer
						if ((new_peer_sock= accept(server_p->listen_sock, (struct sockaddr *)&client, &sockaddr_len)) == ERROR) // accept takes pointer to variable containing len of struct
						{
							perror("ACCEPT. Error accepting new connection");
							exit(-1);
						}

						else
						{
							FD_SET (new_peer_sock, &master); // add to master set
							printf("New peer connected from port no %d and IP %s\n", ntohs(client.sin_port),inet_ntoa(client.sin_addr));
						}
					}
					else
					{//handle data from a client
						bzero(input, BUFFER); 
						if((len=recv(i,input,BUFFER,0))<=0)//connection closed by client or error
						{
							if(len==0)//connection closed
							{
								printf("Peer %d with IP address %s hung up\n",i,inet_ntoa(client.sin_addr));
							}
							else //error
							{
								perror("ERROR IN RECIEVE");
							}
							close(i);//closing this connection
							FD_CLR(i,&master);//remove from master set
						}
						else
						{
							printf("%s\n", input); //file name of file requested by other client

							//file read and transfer operation starts from here

							char* requested_file = input; // create file handler pointer for file Read operation
							//bzero(input, BUFFER); 

							FILE *file_request = fopen(requested_file, "r"); //opening the existing file
							
							if(file_request == NULL) //If requested file not found at given path on given peer
							{
								fprintf(stderr, "ERROR : Opening requested file.REQUESTED FILE NOT FOUND \n");
								close(i);//closing this connection
								FD_CLR(i,&master);//remove from master set
							}
							else
							{
							bzero(output, BUFFER); 
							int file_request_send; //variable to store bytes recieved
							//fseek(file_request, 0, SEEK_SET); //to set pointer to first element in file
							while((file_request_send = fread(output, sizeof(char), BUFFER, file_request))>0) // read file and send bytes
							{
								
								if((send(i, output, file_request_send, 0)) < 0) // error while transmiting file
								{
									fprintf(stderr, "ERROR: Not able to send file");
									//exit(1);
								}

								bzero(output, BUFFER);
							}
							//fclose(file_request);
							close(i);
							FD_CLR(i,&master);
							}
						}
					}
				}
		
			}

		}
		
		close(server_p->listen_sock);
		exit(0);
	}
	
	while(1)
	{
		//DISPLAY MENU FOR USER INPUTS
		printf("\nWELCOME. ENTER YOUR CHOICE\n");
		printf("1.PUBLISH THE FILE\n");
		printf("2.SEARCH THE FILE\n");
		printf("3.FETCH THE FILE\n");
		printf("4.TERMINATE THE CONNECTION TO SERVER\n");
		printf("5.EXIT\n");
		printf("Enter your choice: ");
		if(scanf("%d", &choice) <= 0) {
	    	printf("Enter only an integer from 1 to 5\n");
	    	kill(pid, SIGKILL); // on exit, the created lsitening process to be killed
	    	exit(0);
		} else {
	   		switch(choice)
			{
				case 1:  //PUBLISH OPERATION
					
					temp="pub"; // keyword to be send to server so that server knows it is a publish operation

					send(server_p->sock, temp, sizeof(temp) ,0); // send input to server
					
					printf("Enter the file name with extension, Filepath     ");
					scanf(" %[^\t\n]s",temp_ch); //recieve user input
					sprintf(input, "%s %d", temp_ch, LISTENING_PORT);
					send(server_p->sock, input, strlen(input) ,0); // send input to server
					len = recv(server_p->sock, output, BUFFER, 0); // recieve confirmation message from server
					output[len] = '\0';
					printf("%s\n" , output); // display confirmation message
					bzero(output,BUFFER); // pad buffer with zeros
                 	break;

      		 	case 2: //SEARCH OPERATION
					temp="sea"; // keyword to be send to server so that server knows it is a search operation

					send(server_p->sock, temp, sizeof(temp) ,0); // send input to server
					printf("Enter the file name(keyword) to search     ");
						
					scanf(" %[^\t\n]s", input);
					send(server_p->sock, input, strlen(input) ,0); // send input keyword to server
					len = recv(server_p->sock, output, BUFFER, 0);
					output[len] = '\0';
					printf("%s\n" , output);
					bzero(output,BUFFER);  
								
					printf("Server searching...... Waiting for response\n\n");
					printf("-----------------------------------------------------\n");
					printf("Filename\tFilepath\tPort No\t Peer IP\n"); //format of recieved output from server 
					while((len=recv(server_p->sock, output, BUFFER, 0))>0)
					{
						//len=recv(server_p->sock, output, BUFFER, 0);
						output[len] = '\0'; // checking null for end of data
						printf("%s\n", output);
						bzero(output,BUFFER);
					}
					close(server_p->sock); // Disconnect from server
					printf("-----------------------------------------------------\n");
					printf("SEARCH COMPLETE!!! \n");
					printf("DISCONNECTED FROM SERVER. GO TO OPTION 3 FOR FETCH");
	                break;

        		case 3: //FETCH OPERATION
        			printf("Enter file to be fetched (with path if not same directory.Use Format:./p2p-files/<filename.extn>)\t");
        			scanf(" %[^\t\n]s",file_fet);
        			printf("Enter peer IP address\t");
        			scanf(" %[^\t\n]s",peer_ip);
        			printf("Enter peer listening port number\t");
        			scanf(" %[^\t\n]s",peer_port);

        			//create socket to contact the desired peer

        			if ((server_p->peer_sock= socket(AF_INET, SOCK_STREAM, 0)) == ERROR)
					{ 
						perror("socket"); 
						 // error checking the socket
						kill(pid,SIGKILL); // on exit, the created lsitening process to be killed
						exit(-1);  
					} 
	  
					peer_connect.sin_family = AF_INET; // family
					peer_connect.sin_port =htons(atoi(peer_port)); // Port No and htons to convert from host to network byte order. atoi to convert asci to integer
					peer_connect.sin_addr.s_addr = inet_addr(peer_ip);//IP addr in ACSI form to network byte order converted using inet
					bzero(&peer_connect.sin_zero, 8); //padding zeros
					
					//try to connect desired peer
					if((connect(server_p->peer_sock, (struct sockaddr *)&peer_connect,sizeof(struct sockaddr_in)))  == ERROR) //pointer casted to sockaddr*
					{
						perror("connect");
						kill(pid,SIGKILL); // on exit, the created lsitening process to be killed
						exit(-1);
					}

					//send file keyword with path to peer
					send(server_p->peer_sock, file_fet, strlen(file_fet) ,0); //send file keyword to peer
					
					printf("Recieving file from peer. Please wait \n"); // if file found on client/peer

					//file recieve starts from here
					char* recd_name = file_fet;
					FILE *fetch_file = fopen(recd_name, "w");
					if(fetch_file == NULL) //error creating file 
					{
						printf("File %s cannot be created.\n", recd_name);
					}
					else
					{
						bzero(input,BUFFER);
		    			int file_fetch_size=0;
		    			int len_recd=0; 
		    			while((file_fetch_size = recv(server_p->peer_sock, input, BUFFER, 0))>0) // recieve file sent by peer 
		    			{
		    				
		    				len_recd = fwrite(input, sizeof(char),file_fetch_size,fetch_file);

		    				if(len_recd < file_fetch_size) //error while writing to file
							{
	            				perror("Error while writing file.Try again\n");
	            				kill(pid,SIGKILL); // on exit, the created lsitening process to be killed
	                 			exit(-1);
	       				 	}
	       				 	bzero(input,BUFFER);

							if(file_fetch_size == 0 || file_fetch_size != 512)  //error in recieve packet
		       		 		{
		            			break;
		        			}
		        		}

		        		if(file_fetch_size < 0) //error in recieve
		    			{
		      				perror("Error in recieve\n");	  
							exit(1);
	            		}
		        			
		    	 		fclose(fetch_file); //close opened file
						printf("FETCH COMPLETE");
						close(server_p->peer_sock); //close socket
					}	
                	break;

        		case 4: //when client want to terminate connection with server  
					temp="ter"; // keyword to be send to server so that server knows client wants to terminate connection to server
					send(server_p->sock, temp, sizeof(temp) ,0); // send input to server
					close(server_p->sock);
					printf("Connection terminated with server.\n");
					break;

       			case 5:    
        			kill(pid,SIGKILL); // on exit, the created lsitening process to be killed
        			close(server_p->sock);
      	 			return NULL;
				case 6:
					temp = "all";
					send(server_p->sock, temp, sizeof(temp), 0);
					break;
        		default:    
					printf("Invalid option\n");
					break;
			}
		}
	}

	close(server_p->listen_sock);
	return NULL;
}

void *guiThread(void *args)
{
	gtk_main();
}
