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
#include <time.h>
#include <gtk/gtk.h>
#include <pthread.h>
#include "../fonctions.h"

#define ERROR			-1
#define MAX_CLIENTS		4
#define MAX_BUFFER		512
#define SERVER_PORT		1996

typedef struct {
	int sock;
	int new;
	struct sockaddr_in server;
	struct sockaddr_in client;
	int sockaddr_len;
	char buffer[MAX_BUFFER];
	char file_name[MAX_BUFFER];
	char *peer_ip;
	char user_key[MAX_BUFFER];
	int len;
	int pid;
} server_params;

time_t current_time;
pthread_t threads[2];
server_params* server_p;
guint signal_id;

int add_IP(char*);
int update_IPlist(char *);
void startServer();
void stopServer();
void writeLog(char* ch);
void *serverThread(void *args);
void *guiThread(void *args);
void destroy();
void on_serverStateBtn_toggled();
void on_refreshBtn_clicked();

GtkWidget *window;
GtkViewport *viewport;
GtkScrolledWindow *scrolledwindow;
GtkVBox *vbox;
GtkTextView *debugtv;
GtkTextBuffer *buffer;
GtkHButtonBox *hbuttonbox;
GtkToggleButton *serverStateBtn;
GtkButton *refreshBtn, *exitBtn;
gboolean is_server_running;

int main(int argc, char **argv)
{
	GtkBuilder *builder;
	is_server_running = TRUE;
	
	// GTK init
	gtk_init(&argc, &argv);

	builder = gtk_builder_new();
	gtk_builder_add_from_file(builder, "server gui.glade", NULL);

	window = GTK_WIDGET(gtk_builder_get_object(builder, "server_window"));
	scrolledwindow = GTK_SCROLLED_WINDOW(gtk_builder_get_object(builder, "scrolledwindow"));
	viewport = GTK_VIEWPORT(gtk_builder_get_object(builder, "viewport"));
	vbox = GTK_WIDGET(gtk_builder_get_object(builder, "vbox"));
	debugtv = GTK_TEXT_VIEW(gtk_builder_get_object(builder, "debugtv"));
	buffer = gtk_text_buffer_new(NULL);
	gtk_text_view_set_buffer(debugtv, buffer);
	
	hbuttonbox = GTK_WIDGET(gtk_builder_get_object(builder, "hbuttonbox"));
	serverStateBtn = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "serverStateBtn"));
	refreshBtn = GTK_BUTTON(gtk_builder_get_object(builder, "refreshBtn"));
	exitBtn = GTK_BUTTON(gtk_builder_get_object(builder, "exitBtn"));

	gtk_builder_connect_signals(builder, NULL);

	g_object_unref(builder);

	gtk_widget_show(window);
	
	// server params
	server_p = malloc(sizeof *server_p);
	server_p->sockaddr_len = sizeof (struct sockaddr_in);
	server_p->server.sin_family = AF_INET;
	server_p->server.sin_port = htons(SERVER_PORT);
	server_p->server.sin_addr.s_addr = INADDR_ANY;
	bzero(&(server_p->server.sin_zero), 8);

	// Threads
	pthread_create(&threads[0], NULL, guiThread, NULL);
	pthread_create(&threads[1], NULL, serverThread, server_p);
	
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	
	return 0;
}

void destroy()
{
	// KILL SERVZER
	stopServer();
	pthread_kill(threads[1], SIGKILL);
	free(server_p);
	// KILL GUI
	gtk_main_quit();
	pthread_kill(threads[0], SIGKILL);
	exit(0);
}
void on_refreshBtn_clicked()
{
	GtkTextIter start_iter;
	GtkTextIter end_iter;
	
	FILE *logfile = fopen("log.txt", "r");
	if (logfile != NULL){
		gtk_text_buffer_get_start_iter(buffer, &start_iter);
		gtk_text_buffer_get_end_iter(buffer, &end_iter);
		gtk_text_buffer_delete(buffer, &start_iter, &end_iter);

		char c, ch[256] = "";
		while (1)
		{
			c = fgetc(logfile);
			if (feof(logfile))
				break;
			append(ch, c);
		}
		fclose(logfile);

		gtk_text_buffer_insert(buffer, &end_iter, ch, strlen(ch));
	}
}
void on_serverStateBtn_toggled()
{
	is_server_running = !is_server_running;
	char msg[32];
	
	if (is_server_running){
		strcpy(msg, "The server has started running!");
		gtk_button_set_label(GTK_BUTTON(serverStateBtn), "STOP");
	} else {
		strcpy(msg, "The server has stopped running!");
		gtk_button_set_label(GTK_BUTTON(serverStateBtn), "START");
		stopServer();
	}

	writeLog(msg);
	on_refreshBtn_clicked();
}
int update_IPlist(char *peer_ip)
{
	char* msg = (char*) malloc(sizeof(char) * 64);
	sprintf(msg, "%s disconnected from the server", peer_ip);
	writeLog(msg);
}
int add_IP(char *peer_ip)
{
	char* msg = (char*) malloc(sizeof(char) * 64);
	sprintf(msg, "%s connected from the server", peer_ip);
	writeLog(msg);
}
void stopServer()
{
	close(server_p->new);
	close(server_p->sock);
}
void startServer()
{
	/*get socket descriptor */
	if ((server_p->sock= socket(AF_INET, SOCK_STREAM, 0)) == ERROR)
	{ 
		perror("server socket error: "); 
		exit(-1);
	}
	
	/*binding the socket */
	if((bind(server_p->sock, (struct sockaddr *)&(server_p->server), server_p->sockaddr_len)) == ERROR)
	{
		perror("bind");
		exit(-1);
	}

	/*listen the incoming connections */
	if((listen(server_p->sock, MAX_CLIENTS)) == ERROR)
	{
		perror("listen");
		exit(-1);
	}
}
void writeLog(char* ch)
{
	FILE *logfile = fopen("log.txt", "a+");
	time_t timer;
	char time_buffer[26];
	char *logmsg = (char*) malloc(sizeof(char) * 128);;
	struct tm* tm_info;

	if(logfile != NULL)
    {
		time(&timer);
		tm_info = localtime(&timer);

		strftime(time_buffer, 26, "%d/%m/%y %H:%M", tm_info);
		sprintf(logmsg, "[%s] %s\n", time_buffer, ch);
		fwrite(logmsg, 1, strlen(logmsg), logfile);
		fclose(logfile);
    } else {
		printf("Unable to open log file. Error");
	}

	free(logmsg);
}
void *serverThread(void *args)
{
	server_params *vargp = args;
	char* msg;
	startServer();

	// create log file
	FILE *logfile = fopen("log.txt", "w");
	fclose(logfile);

	writeLog("Server Started");
	on_refreshBtn_clicked();

	while(1)
	{   
		if ((vargp->new = accept(server_p->sock, (struct sockaddr *)&vargp->client, &server_p->sockaddr_len)) == ERROR)
		{
			perror("ACCEPT. Error accepting new connection");
			exit(-1);
		}

		vargp->pid=fork(); //creates separate process for each client at server
		if (!vargp->pid)
		{ 
            close(server_p->sock); // close the socket for other connections

			msg = (char*) malloc(sizeof(char) * 128);
			sprintf(msg, "New client connected from port no %d and IP %s", ntohs(vargp->client.sin_port), inet_ntoa(vargp->client.sin_addr));
			writeLog(msg);
			
		 	vargp->peer_ip = inet_ntoa(vargp->client.sin_addr);
	 		add_IP(vargp->peer_ip); // Adding Client IP into IP List
		
			while(1)
			{
				vargp->len=recv(vargp->new, vargp->buffer, MAX_BUFFER, 0);
				vargp->buffer[vargp->len] = '\0';
				printf("%s\n",vargp->buffer);
	
				// Connection error checking
				if(vargp->len<=0) // Connection closed by client or error
				{
					if(vargp->len==0) //connection closed
					{
						sprintf(msg, "Peer %s hung up\n",inet_ntoa(vargp->client.sin_addr));
						writeLog(msg);
						update_IPlist(vargp->peer_ip);
					}
					else //error
					{
						perror("ERROR IN RECIEVE");
					}
					close(vargp->new);
					exit (0);
				}
	
				// ALL FILES
				if(vargp->buffer[0]=='a' && vargp->buffer[1]=='l' && vargp->buffer[2]=='l')
				{
					printf("print all files (from server)\n");
					char* fileinfo = "filelist.txt";
					FILE *filedet = fopen(fileinfo, "r");
					if (filedet != NULL)
					{
						char fileslist[256];
						while (1)
						{
							char c = fgetc(filedet);
							if (c != feof(filedet))
								append(fileslist, c);
							else 
								break;
						}
						printf("Files list (from server): %s\n", fileslist);
						//send(vargp->new, fileslist, sizeof(fileslist), 0);
						printf("Files list has been sent to the client\n");
					}
				}
				//PUBLISH OPERATION
				if(vargp->buffer[0]=='p' && vargp->buffer[1]=='u' && vargp->buffer[2]=='b') // Check if user wants to publish a file
				{
					// Adding publised file details to publish list
					char* fileinfo = "filelist.txt";
					FILE *filedet = fopen(fileinfo, "a+");
			
					if(filedet==NULL)
        			{
            			printf("Unable to open File. Error\n");
            			return NULL;  
        			}   	
        			else
					{
						fwrite("\n", sizeof(char), 1, filedet);
							
						vargp->len=recv(vargp->new, vargp->file_name, MAX_BUFFER, 0);
						fwrite(&vargp->file_name, vargp->len,1, filedet);
						char Report[] = "File published"; 
						send(vargp->new,Report,sizeof(Report),0);
				
						/*fwrite("\t",sizeof(char),1, filedet);
						vargp->peer_ip = inet_ntoa(vargp->client.sin_addr);
						// Adding peer IP address to given file
						fwrite(vargp->peer_ip,1, strlen(vargp->peer_ip), filedet);
						fclose(filedet);*/

						sprintf(msg, "%s has published %s", vargp->peer_ip, vargp->file_name);
						writeLog(msg);

						printf("%s\n","FILE PUBLISHED");
					}	
				}
				//SEARCH OPERATION
				else if(vargp->buffer[0]=='s' && vargp->buffer[1]=='e' && vargp->buffer[2]=='a') //check keyword for search sent by client
				{
					bzero(vargp->buffer,MAX_BUFFER); // clearing the buffer by padding
			
					vargp->len=recv(vargp->new, vargp->user_key, MAX_BUFFER, 0); //receive keyword from client to search
					char Report3[] = "Keyword recieved"; 
					send(vargp->new,Report3,sizeof(Report3),0);
					vargp->user_key[vargp->len] = '\0';
					printf("%s\n",vargp->user_key);
			
					char command[128];
			
					/* Call the Script */
					system("chmod +x searchscript.sh");
					strcpy(command, "./searchscript.sh ");
					strcat(command,vargp->user_key );
   					system(command);
			
   		 			char* search_result = "searchresult.txt";
		 			char buffer[MAX_BUFFER]; 
		 			
		 			FILE *file_search = fopen(search_result, "r");
		 			if(file_search == NULL)
		    		{
		        		fprintf(stderr, "ERROR while opening file on server.");
						exit(1);
		    		}
		
		    		bzero(vargp->buffer, MAX_BUFFER); 
		    		int file_search_send; 
		    		
		    		//send search result to peer
		    		while((file_search_send = fread(vargp->buffer, sizeof(char), MAX_BUFFER, file_search))>0) 
		    		{
		        		vargp->len=send(vargp->new, vargp->buffer, file_search_send, 0);
		
		        		if(vargp->len < 0)
		        		{
		            		fprintf(stderr, "ERROR: File not found");
		            		exit(1);
		        		}
		        		bzero(vargp->buffer, MAX_BUFFER);
		    		}
		    		fclose(file_search);
		    		char Reportsearch[] = "Search complete. You are disconnected from server now. Connect again for further actions"; 
        			send(vargp->new, Reportsearch, sizeof(Reportsearch),0);
        			
		    		printf("Search complete!!!!\n");
					sprintf(msg, "Client disconnected from port no %d and IP %s\n\n", ntohs(vargp->client.sin_port), inet_ntoa(vargp->client.sin_addr));
		    		vargp->peer_ip = inet_ntoa(vargp->client.sin_addr);
					update_IPlist(vargp->peer_ip);
					close(vargp->new); // disconnect this client so that other users can connect server
					writeLog(msg);
					exit(0);
				}
				//TERMINATE OPERATION: When user want to disconnect from server
				else if(vargp->buffer[0]=='t' && vargp->buffer[1]=='e' && vargp->buffer[2]=='r')
				{
					sprintf(msg, "Client disconnected from port no %d and IP %s\n\n", ntohs(vargp->client.sin_port), inet_ntoa(vargp->client.sin_addr));
					vargp->peer_ip = inet_ntoa(vargp->client.sin_addr);
					update_IPlist(vargp->peer_ip);
					close(vargp->new);
					writeLog(msg);
					exit(0);
				}
			}
			free(msg);
		}
		close(vargp->new);
	}
	close(server_p->sock);
	return NULL;
}
void *guiThread(void *args)
{
	gtk_main();
}
