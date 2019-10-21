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
#include <sys/sendfile.h>

#include <time.h>
#include <fstream>
#include <sstream>

map<string, string> HttpdServer::mime;

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

	// buffer mime.types
	string line;

	ifstream infile("mime.types");

	while (getline(infile, line))
	{
		istringstream iss(line);
		//log->info(line);
		int space = line.find(' ');
		mime[line.substr(0, space)] = line.substr(space + 1);
	}
}

void HttpdServer::launch()
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
		return;
	}

	// create a sockaddr_in struct
	struct sockaddr_in server_address;
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(PORT);
	server_address.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY indicates localhost

	// 2. bind()
	int b = ::bind(sock, (struct sockaddr *)&server_address,
				   sizeof(server_address));

	if (b < 0)
	{
		cerr << "ERROR WHILE BINDING SOCKET" << endl;
		close(sock);
		return;
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
		int client_sock = accept(sock, (struct sockaddr *)&client_address,
								 &client_length);

		if (client_sock < 0)
		{
			cerr << "ERROR WHILE ACCEPTING CONNECTION" << endl;
			close(sock);
			continue;
		}
		int n = read(client_sock, buffer, 256);

		if (n < 0)
		{
			cerr << "ERROR WHILE GETTING MESSAGE" << endl;
		}

		// Handle file request
		int to_close = handle_request(buffer, client_sock);

		// 5. close
		close(client_sock);
		if (to_close)
		{
			break;
		}
	}
	close(sock);
}

string get_last_modified(const char *full_path)
{

	//auto log = logger();

	// struct stat info;
	//    stat(full_path, &info);

	//    printf(ctime(&info.st_mtimespec));
	//    return 0;

	FILE *fp;
	int fd;
	struct stat tbuf;
	fp = fopen(full_path, "r");
	fd = fileno(fp);

	fstat(fd, &tbuf);

	time_t modified_time = tbuf.st_mtime;
	struct tm lt;

	// multithreading
	localtime_r(&modified_time, &lt);
	char timebuf[80];
	strftime(timebuf, sizeof(timebuf), "%a, %d %b %y %T %z", &lt);

	fclose(fp);

	return timebuf;
}

int HttpdServer::handle_request(char *buf, int client_sock)
{
	auto log = logger();
	int host = 0;
	int close = 0;
	int bad_request = 0;

	// Copy the buffer to parse
	char *buf_copy = (char *)malloc(strlen(buf) + 1);
	strcpy(buf_copy, buf);

	// Get the url
	char *first_line = strsep(&buf_copy, "\r\n"); // CR = \r, LF = \n
	strsep(&first_line, " ");
	char *url = strsep(&first_line, " ");

	// parse the following part of the request
	while (buf_copy != NULL)
	{
		char *line = strsep(&buf_copy, "\r\n");
		if (strchr(line, ':') == NULL)
		// check if ':' exists
		{
			bad_request = 1;
		}
		char *key = strsep(&line, ":");
		if (strcmp(key, "Connection") == 0)
		{
			if (strcmp(line, "close") == 0)
			{
				close = 1;
			}
		}
		else if (strcmp(key, "Host") == 0)
		{
			host = 1;
		}
	}

	if (host == 0 || bad_request == 1) // if Host not present
	{
		// build header
		string header;
		header += "HTTP/1.1 400 CLIENT ERROR\r\n";
		header += "Server: Myserver 1.0\r\n";
		header += "\r\n";

		// send header
		send(client_sock, (void *)header.c_str(), (ssize_t)header.size(), 0);
		return close;
	}

	// Prepend doc root to get the absolute path
	string full_path = doc_root + url;
	log->info("Get file: {}", full_path);
	string header;

	// Validate the file path
	int is_valid = access(full_path.c_str(), F_OK);

	if (is_valid == 0)
	// if access succeeds
	{
		// get file size
		int f_size;
		struct stat finfo;
		stat(full_path.c_str(), &finfo);
		f_size = finfo.st_size;
		// get mime type
		string type = full_path.substr(full_path.find_last_of('.'));
		//log->info("type: " + HttpdServer::mime[type]);
		string mime_type;
		if (HttpdServer::mime.find(type) == HttpdServer::mime.end())
		{
			mime_type = "application/octet-stream";
		}
		else
			mime_type = HttpdServer::mime[type];

		// build header
		header += "HTTP/1.1 200 OK\r\n";
		header += "Server: Myserver 1.0\r\n";
		header += "Last-Modified: " + get_last_modified(full_path.c_str()) + "\r\n";
		header += "Content-Length: " + to_string(f_size) + "\r\n";
		header += "Content-type: " + mime_type;
		header += "\r\n";
	}
	else
	// requested file not there
	{
		header += "HTTP/1.1 404 NOT FOUND\r\n";
		header += "Server: Myserver 1.0\r\n";
		header += "\r\n";
	}

	// Send headers
	send(client_sock, (void *)header.c_str(), (ssize_t)header.size(), 0);

	if (is_valid == 0)
	// send body
	{
		struct stat finfo;
		int fd = open(full_path.c_str(), O_RDONLY);
		fstat(fd, &finfo);
		off_t off = 0;
		// int h = sendfile(fd, client_sock, 0, &off, NULL, 0);   // os version
		int h = sendfile(client_sock, fd, &off, finfo.st_size);
		log->info("sendfile status: {}", h);
	}
	return close;
}
