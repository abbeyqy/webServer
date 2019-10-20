#include <sysexits.h>

#include "logger.hpp"
#include "HttpdServer.hpp"
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>

HttpdServer::HttpdServer(INIReader &t_config)
	: config(t_config)
{
	auto log = logger();

	string pstr = config.Get("httpd", "port", "");
	if (pstr == "")
	{
		log->error("port was not in the config file");
		exit(EX_CONFIG);
	}
	port = pstr;

	string dr = config.Get("httpd", "doc_root", "");
	if (dr == "")
	{
		log->error("doc_root was not in the config file");
		exit(EX_CONFIG);
	}
	doc_root = dr;
}

int HttpdServer::launch()
{
	auto log = logger();

	log->info("Launching web server");
	log->info("Port: {}", port);
	log->info("doc_root: {}", doc_root);

	// Put code here that actually launches your webserver...
	int PORT = stoi(port);

	// 1. socket()
	int sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock < 0)
	{
		cerr << "ERROR WHILE CREATING SOCKET" << endl;
		return 0;
	}

	// create a sockaddr_in struct
	struct sockaddr_in server_address;
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(PORT);
	server_address.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY indicates localhost

	// 2. bind()
	int b = bind(sock, (struct sockaddr *)&server_address,
				 sizeof(server_address));

	if (b < 0)
	{
		cerr << "ERROR WHILE BINDING SOCKET" << endl;
		close(sock);
		return 0;
	}

	log->info("SERVER IS RUNNING");

	// 3. listen
	listen(sock, 1);

	struct sockaddr_in client_address;
	socklen_t client_length = sizeof(client_address);

	char buffer[256];
	bzero(buffer, 256);

	// 4. accept and receive
	while (1)
	{
		int new_sock = accept(sock, (struct sockaddr *)&client_address,
							  &client_length);

		if (new_sock < 0)
		{
			cerr << "ERROR WHILE ACCEPTING CONNECTION" << endl;
			close(sock);
			continue;
		}
		int n = read(new_sock, buffer, 256);

		if (n < 0)
		{
			cerr << "ERROR WHILE GETTING MESSAGE" << endl;
		}

		// Handle file request
		handle_request(buffer, new_sock);
	}
}

void HttpdServer::handle_request(char *buf, int client_sock)
{
}
