#include <sysexits.h>

#include "logger.hpp"
#include "HttpdServer.hpp"
#include <string>
#include <regex>
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

#include <thread>
#include <time.h>
#include <fstream>
#include <sstream>
#include <pthread.h> // for multithreading

map<string, string> HttpdServer::mime;
struct timeval timeout;

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

		thread t1(&HttpdServer::client_handler, this, client_sock);
		t1.detach();
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

string get_error_header(int error_code)
{
	string header;
	if (error_code == 400)
	{
		header += "HTTP/1.1 400 CLIENT ERROR\r\n";
	}
	else if (error_code == 404)
	{
		header += "HTTP/1.1 404 NOT FOUND\r\n";
	}
	header += "Server: Myserver 1.0\r\n";
	header += "\r\n";
	return header;
}

bool escape_doc_root(string path, string doc_root)
{
	auto log = logger();
	char *c_real_path = realpath(path.c_str(), NULL);
	char *c_real_docpath = realpath(doc_root.c_str(), NULL);
	if (c_real_path == NULL || c_real_docpath == NULL)
	{
		return true;
	}
	string real_path = c_real_path;
	string real_docpath = c_real_docpath;
	log->info("abspath: " + real_docpath);
	if (real_path.find(real_docpath) == 0)
	{
		return false;
	}
	return true;
}

void HttpdServer::client_handler(int client_sock)
{
	auto log = logger();
	char complete_request[1000];
	bzero(complete_request, 1000);

	char buffer[256];
	bzero(buffer, 256);

	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	while (1)
	{
		bzero(buffer, 256);
		int n = read(client_sock, buffer, 256);

		if (n < 0)
		{
			cerr << "ERROR WHILE GETTING MESSAGE" << endl;
			break;
		}

		string sbuffer(buffer);
		if (sbuffer.length() == 0)
		{
			continue;
		}
		size_t pos = sbuffer.find("\r\n\r\n");
		if (pos == string::npos)
		// content in buffer has not reach the end of the request
		{
			log->info("Has not found end symbol in the buffer.");
			strcat(complete_request, sbuffer.c_str());
			continue;
		}

		string sbefore_end = sbuffer.substr(0, pos);
		strcat(complete_request, sbefore_end.c_str());

		// handle timeout
		if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		{
			break;
		}

		// Handle a complete request
		string request = complete_request;
		log->info("Handling a new request: {}", request);
		log->info("======Start processing the request.");
		int to_close = handle_request(complete_request, client_sock);
		log->info("======Finish processing the request.");
		if (to_close)
		{
			break;
		}

		// deal with next request
		bzero(complete_request, 1000);
		if (sbuffer.length() != pos + 4)
		{
			log->info("Has more request to proceed...");
			string pre_buffer = sbuffer.substr(pos + 4, sbuffer.length());
			log->info("pre_buffer has something: {}", pre_buffer);
			strcpy(complete_request, pre_buffer.c_str());
			log->info("strcpy succeeds!");
		}
	}
	// 5. close
	log->info("Close client sock!");
	close(client_sock);
}

int HttpdServer::handle_request(char *buf, int client_sock)
{
	auto log = logger();
	// int host = 0;
	int close = 0;
	int bad_request = 0;

	// Copy the buffer to parse
	char *buf_copy1 = (char *)malloc(strlen(buf) + 1);
	strcpy(buf_copy1, buf);

	// Get the url
	// GET / HTTP/1.1
	char *first_line = strsep(&buf_copy1, "\r"); // CR = \r, LF = \n
	strsep(&buf_copy1, "\n");

	// / HTTP/1,1
	strsep(&first_line, " ");

	// /
	char *url = strsep(&first_line, " ");

	// parse the next part of the request (key: value)
	while (buf_copy1 != NULL)
	{
		char *line = strsep(&buf_copy1, "\r");
		strsep(&buf_copy1, "\n");
		// check if request header is valid
		string sline = line;
		regex r("(\\S)+: (.)*");
		if (!regex_match(sline, r))
		{
			log->info("mal-formed request: {}", sline);
			bad_request = 1;
		}

		char *key = strsep(&line, ":");
		// check if there is Connection: close
		if (strcmp(key, "Connection") == 0)
		{
			if (strcmp(line, " close") == 0)
			{
				close = 1;
			}
		}
		// check if "Host" exists
		// if (strcmp(key, "Host") == 0)
		// {
		// 	host = 1;
		// }
	}

	// if (host == 0 || bad_request == 1 || strchr(url, '/') != url) // if Host not present or format invalid
	if (bad_request == 1 || strchr(url, '/') != url)
	{
		// build header
		string header = get_error_header(400);
		close = 1;
		// send header
		send(client_sock, (void *)header.c_str(), (ssize_t)header.size(), 0);
		return close;
	}

	// Prepend doc root to get the absolute path
	string full_path = doc_root + url;

	// check if url escapes doc_root, return 404
	if (escape_doc_root(full_path, doc_root))
	{
		log->info("Url escapes doc root/ file not found!");
		string header = get_error_header(404);
		// send header
		send(client_sock, (void *)header.c_str(), (ssize_t)header.size(), 0);
		return close;
	}

	// “http://server:port/" map to “http://server:port/index.html"
	log->info("url: {}", url);
	if (strcmp(url, "/") == 0)
	{
		log->info("transofrm");
		full_path += "index.html";
	}

	log->info("Get file: {}", full_path);

	string header;

	// Validate the file path, useless now... but keep it forsake it might be useful...
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
		header += "Content-Type: " + mime_type + "\r\n";
		header += "\r\n";
	}

	// requested file not there
	else
	{
		log->info("File not Found!");
		string header = get_error_header(404);
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
		//int h = sendfile(fd, client_sock, 0, &off, NULL, 0); // os version
		ssize_t h = sendfile(client_sock, fd, &off, finfo.st_size);
		log->info("sendfile status: {}", h);
	}

	log->info("Request processed. Close connection? {}", close);
	return close;
}
