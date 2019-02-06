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
#include <ifaddrs.h>
#include "../fonctions.h"

#define ERROR     		-1
#define BUFFER    		512
#define MAX_BUFFER		262144
#define LISTENING_PORT 	2000
#define MAX_CLIENTS    	4

typedef struct server_params {
	int sock; // sock is socket desriptor for connecting to remote server 
	int peer_sock; // socket descriptor for peer during fetch
	int listen_sock; // socket descriptor for listening to incoming connections
	int pid; //variable to store process id of process created after fork
} server_params;

/*------------------*/
/*      THREADS     */
/*------------------*/
void *clientServerThread(void *args);
void *guiThread(void *args);
/*------------------*/

/*------------------*/
/* SIGNALS HANDLERS */
/*------------------*/
void on_upload_btn_clicked(GtkButton* upload_btn, GtkFileChooserDialog* upload_dialog);
void on_refresh_btn_activate(GtkMenuItem *refresh_btn, GtkListBox *list_box);
void on_about_btn_activate(GtkMenuItem *about_btn, GtkAboutDialog *about_dialog);
void on_choose_btn_clicked(GtkButton *choose_btn, GtkFileChooserDialog *upload_dialog);
void on_list_box_row_activated(GtkListBox *list_box, gpointer user_data);
void destroy();
gboolean on_server_btn_state_set(GtkSwitch *server_btn, gboolean user_data);
/*------------------*/

/*------------------*/
/* SERVER FUNCTIONS */
/*------------------*/
void getAllFilesFromServer(char *output);
void publishFile(char* filename, char* filepath);
void downloadFile(char* file, char* peer_ip, char* peer_port);
void getLocalIP(char *output);
void stopServer();
/*------------------*/

pthread_t threads[2];
server_params *server_p;
gboolean server_running;

int main(int argc, char **argv) {
	GtkBuilder *builder;
	GtkWidget *window;
	
	// GTK init
	gtk_init(&argc, &argv);

	builder = gtk_builder_new();
	gtk_builder_add_from_file(builder, "client_app.glade", NULL);

	window = GTK_WIDGET(gtk_builder_get_object(builder, "client_app"));

	gtk_builder_connect_signals(builder, NULL);
	g_object_unref(builder);

	gtk_widget_show_all(window);

	server_p = (server_params*) malloc(sizeof(server_params));

	// Threads
	pthread_create(&threads[0], NULL, guiThread, NULL);
	pthread_create(&threads[1], NULL, clientServerThread, NULL);
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);

	free(server_p);
	printf("\nEND OF MAIN\n");

	return 0;
}

/*********************/
/* SERVER FUNCTIONS  */
/*********************/
void getAllFilesFromServer(char *output){
	int len;

	send(server_p->sock, "all", 4, 0);
	len = recv(server_p->sock, output, MAX_BUFFER, 0);
	output[len] = '\0';
}
void publishFile(char* filename, char* filepath){
	char output[BUFFER];
	char *message = (char*) malloc(sizeof(char) * 256);
	int len;

	send(server_p->sock, "pub", 4, 0);
	
	sprintf(message, "%s \"%s\" %d", filename, filepath, LISTENING_PORT);
	send(server_p->sock, message, strlen(message), 0);
	len = recv(server_p->sock, output, BUFFER, 0);
	output[len] = '\0';
	bzero(output, BUFFER);

	free(message);
}
void downloadFile(char* file, char* peer_ip, char* peer_port){
	char input[BUFFER];
	struct sockaddr_in peer_connect;

	//create socket to contact the desired peer
    if ((server_p->peer_sock= socket(AF_INET, SOCK_STREAM, 0)) == ERROR){ 
		perror("peer socket"); 
		// error checking the socket
		kill(server_p->pid,SIGKILL); // on exit, the created listening process to be killed
		exit(-1);  
	} 
	  
	peer_connect.sin_family = AF_INET; // family
	peer_connect.sin_port =htons(atoi(peer_port)); // Port No and htons to convert from host to network byte order. atoi to convert asci to integer
	peer_connect.sin_addr.s_addr = inet_addr(peer_ip);//IP addr in ACSI form to network byte order converted using inet
	bzero(&peer_connect.sin_zero, 8); //padding zeros
					
	//try to connect desired peer
	if((connect(server_p->peer_sock, (struct sockaddr *)&peer_connect,sizeof(struct sockaddr_in)))  == ERROR) //pointer casted to sockaddr*
	{
		perror("connection to peer");
		kill(server_p->pid,SIGKILL); // on exit, the created lsitening process to be killed
		exit(-1);
	}

	//send file keyword with path to peer
	send(server_p->peer_sock, file, strlen(file) ,0); //send file keyword to peer	
	printf("Receiving file from peer. Please wait \n"); // if file found on client/peer

	//file recieve starts from here
	char* recd_name = file;
	FILE *fetch_file = fopen(recd_name, "w");
	if(fetch_file == NULL) //error creating file 
	{
		printf("File %s cannot be created.\n", recd_name);
	} else {
		bzero(input,BUFFER);
		int file_fetch_size=0;
		int len_recd=0; 
		while((file_fetch_size = recv(server_p->peer_sock, input, BUFFER, 0))>0){ // recieve file sent by peer 
			len_recd = fwrite(input, sizeof(char),file_fetch_size,fetch_file);

		    if(len_recd < file_fetch_size) //error while writing to file
			{
	        	perror("Error while writing file.Try again\n");
	        	kill(server_p->pid,SIGKILL); // on exit, the created lsitening process to be killed
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
		printf("DOWNLOAD COMPLETE");
		close(server_p->peer_sock); //close socket
	}
}
void getLocalIP(char *output){
	// TODO: requirement (apt install net-tools)
	
	struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) 
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
    {
        if (ifa->ifa_addr == NULL)
            continue;  

        s=getnameinfo(ifa->ifa_addr,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
		// TODO: to optimize
        if((strcmp(ifa->ifa_name,"enp2s0")==0)&&(ifa->ifa_addr->sa_family==AF_INET))
        {
            if (s != 0)
            {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }
            printf("\tInterface : <%s>\n",ifa->ifa_name );
            printf("\t  Address : <%s>\n", host);
			strcpy(output, host);
        }
    }

    freeifaddrs(ifaddr);
}
void stopServer(){
	close(server_p->peer_sock);
	close(server_p->listen_sock);
	close(server_p->sock);
	printf("Disconnected from the server!");
}
/*********************/

/*********************/
/* SIGNALS HANDLERS  */
/*********************/
void on_upload_btn_clicked(GtkButton* upload_btn, GtkFileChooserDialog* upload_dialog){
	gtk_dialog_run(GTK_DIALOG(upload_dialog));
}
void on_refresh_btn_activate(GtkMenuItem *refresh_btn, GtkListBox *list_box){
	GtkWidget *label1, *label2, *hbox, *row;
	char* files_string = (char*) malloc(sizeof(char) * MAX_BUFFER);
	char* line = (char*) malloc(sizeof(char) * BUFFER);
	char* file_name = (char*) malloc(sizeof(char) * 32);
	char* file_path = (char*) malloc(sizeof(char) * BUFFER);
	char* peer_address = (char*) malloc(sizeof(char) * 22);
	char* peer_port = (char*) malloc(sizeof(char) * 5);
	char c;
	
	// Extract data from server
	getAllFilesFromServer(files_string);

	// Format data to row
	if (strlen(files_string) > 0) {
		strcpy(line, "");
		for (int i=0; i < strlen(files_string); i++) {
			c = files_string[i];
			// treat each line seperately
			if (c == '\n') {
				// Setup
				{
					row = gtk_list_box_row_new();
					hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
				}
				// Extractions
				{
					// Extract file name
					file_name = substr(line, 0, strpos(line, " "));

					// Extract file path
					file_path = substr(line, strpos(line, "/"), strlen(line)-strpos(line,"/"));
					char* lastocc = strrchr(line, '/');
					file_path = substr(file_path, 0, strlen(file_path)-strlen(lastocc)+1);
					strcut(line, strpos(line, "/"), strlen(file_path));

					// Extract peer address
					peer_address = substr(line, strpos(line, "\t")+1, strlen(line)-strpos(line, "\t"));
					strcut(line, strpos(line, "\t"), strlen(peer_address)+1);

					// Extract peer port
					peer_port = substr(line, strpos(line, " ")+1, strlen(line)-strpos(line, " "));
					peer_port = substr(peer_port, strpos(peer_port, " ")+1, strlen(peer_port)-strpos(peer_port, " "));
				}
				// Installation
				{
					file_path = strcat(file_path, file_name);
					peer_address = strcat(peer_address, ":");
					peer_address = strcat(peer_address, peer_port);
					peer_address = substr(peer_address, 0, strlen(peer_address));
					label1 = gtk_label_new(file_path);
					label2 = gtk_label_new(peer_address);

					gtk_container_add(GTK_CONTAINER(row), hbox);
					gtk_box_pack_start(GTK_BOX(hbox), label1, TRUE, TRUE, 0);
					gtk_box_pack_start(GTK_BOX(hbox), label2, TRUE, TRUE, 0);

					gtk_list_box_insert(list_box, row, 0);
					strcpy(line, "");
				}
				continue;
			}
			// append function
			append(line, c);
		}

		gtk_widget_show_all(GTK_WIDGET(list_box));
	}

	free(line);
	free(files_string);
	free(file_name);
	free(file_path);
	free(peer_address);
	free(peer_port);
}
void on_list_box_row_activated(GtkListBox *list_box, gpointer user_data){
	GtkListBoxRow* row = GTK_LIST_BOX_ROW(gtk_list_box_get_selected_row(list_box));
	GtkBox *hbox;
	GList *labelList;
	char counter = 0;
	char *file_name = (char*) malloc(sizeof(char) * 32);
	char *peer = (char*) malloc(sizeof(char) * 22);
	char *ip = (char*) malloc(sizeof(char) * 16);
	char *port = (char*) malloc(sizeof(char) * 5);

	// Extraction of the file name and the peer details
	hbox = GTK_BOX(gtk_bin_get_child(GTK_BIN(row)));
	labelList = gtk_container_get_children(GTK_CONTAINER(hbox));

	for (GList *l = labelList; l != NULL; l = l->next)
	{
		gpointer element_data = l->data;
		GtkLabel *label = (GtkLabel*) element_data;
		if (counter == 0)
		{
			strcpy(file_name, gtk_label_get_text(label));
		} else {
			strcpy(peer, gtk_label_get_text(label));
		}
		counter++;
	}
	ip = substr(peer, 0, strpos(peer, ":"));
	port = substr(peer, strpos(peer, ":")+1, strlen(peer) - strpos(peer, ":"));

	// Begin Download Request
	downloadFile(file_name, ip, port);

	// Freeing resources
	free(ip);
	free(port);
	free(file_name);
	free(peer);
}
void on_about_btn_activate(GtkMenuItem *about_btn, GtkAboutDialog *about_dialog){
	gtk_dialog_run(GTK_DIALOG(about_dialog));
}
void on_choose_btn_clicked(GtkButton *choose_btn, GtkFileChooserDialog *upload_dialog){
	char *file, *filepath, *filename;

	file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(upload_dialog));
	// extraction of file path and file name
	filename = strrchr(file, '/');
	filename = substr(filename, 1, strlen(filename)-1);
	filepath = substr(file, 0, strlen(file)-strlen(filename));

	// publish file
	publishFile(filename, filepath);

	gtk_widget_hide(GTK_WIDGET(upload_dialog));

	free(file);
	free(filepath);
	free(filename);
}
void destroy(){
	// KILL SERVER
	stopServer();
	pthread_cancel(threads[1]);

	// KILL GUI
	gtk_main_quit();
	pthread_cancel(threads[0]);
	
}
gboolean on_server_btn_state_set(GtkSwitch *server_btn, gboolean user_data){
	if (user_data == FALSE){
		printf("TODO: Server Closed\n");
	} else {
		printf("TODO: Server Opened\n");
	}
	return 0;
}
/*********************/

void *clientServerThread(void *args){
	server_running = TRUE;
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

	//variables for select system call
	fd_set master; // this is master file desriptor
	fd_set read_fd; // for select
	//((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr
	//for connecting with server for publishing and search files
	if ((server_p->sock= socket(AF_INET, SOCK_STREAM, 0)) == ERROR)
	{ 
		perror("socket");  // error checking the socket
		exit(-1);  
	}

	char* my_ip = (char*) malloc(sizeof(char) * 16);
	getLocalIP(my_ip);

	remote_server.sin_family = AF_INET; // family
	remote_server.sin_port = htons(atoi("1996")); // Port No and htons to convert from host to network byte order. atoi to convert asci to integer
	remote_server.sin_addr.s_addr = inet_addr(my_ip);//IP addr in ACSI form to network byte order converted using inet
	bzero(&remote_server.sin_zero, 8); //padding zeros
	
	if((connect(server_p->sock, (struct sockaddr *)&remote_server,sizeof(struct sockaddr_in)))  == ERROR) //pointer casted to sockaddr*
	{
		perror("connect");
		//exit(-1);
	}
	printf("Connected to the server!\n");

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
		//exit(-1);
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

	fork();
	server_p->pid = getpid();
	
	if (!server_p->pid) 
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
				if(FD_ISSET(i,&read_fd)) //returns TRUE if i in read_fd
				{
					if(i==server_p->listen_sock)
					{
						int new_peer_sock; //new socket descriptor for peer
						if ((new_peer_sock= accept(server_p->listen_sock, (struct sockaddr *)&client, &sockaddr_len)) == ERROR) // accept takes pointer to variable containing len of struct
						{
							perror("ACCEPT. Error accepting new connection");
							exit(-1);
						} else {
							FD_SET (new_peer_sock, &master); // add to master set
							printf("New peer connected from port no %d and IP %s\n", ntohs(client.sin_port),inet_ntoa(client.sin_addr));
						}
					} else {//handle data from a client
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
						} else {
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
							} else {
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
	
	/*while(1)
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
	    	kill(server_p->pid, SIGKILL); // on exit, the created lsitening process to be killed
	    	exit(0);
		} else {
	   		switch(choice)
			{
				case 1:  //PUBLISH OPERATION
					
					temp="pub"; // keyword to be send to server so that server knows it is a publish operation

					send(server_p->sock, temp, sizeof(temp) ,0); // send input to server
					
					printf("Enter the file name with extension, Filepath   ");
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
						kill(server_p->pid,SIGKILL); // on exit, the created lsitening process to be killed
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
						kill(server_p->pid,SIGKILL); // on exit, the created lsitening process to be killed
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
	            				kill(server_p->pid,SIGKILL); // on exit, the created lsitening process to be killed
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
        			kill(server_p->pid,SIGKILL); // on exit, the created lsitening process to be killed
        			close(server_p->sock);
      	 			return NULL;
				case 6:
					temp = "all";
					send(server_p->sock, temp, sizeof(temp), 0);
					len = recv(server_p->sock, output, BUFFER, 0);
					output[len] = '\0';
					printf("%s\n", output);
					break;
        		default:    
					printf("Invalid option\n");
					break;
			}
		}
	}*/

	close(server_p->listen_sock);
}
void *guiThread(void *args){
	gtk_main();
}
