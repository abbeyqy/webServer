#ifndef HTTPDSERVER_HPP
#define HTTPDSERVER_HPP

#include "inih/INIReader.h"
#include "logger.hpp"

using namespace std;

class HttpdServer
{
public:
	HttpdServer(INIReader &t_config);

	int launch();
	void handle_request(char *buf, int client_sock);
	static map<string,string> mime;

protected:
	INIReader &config;
	string port;
	string doc_root;
};

#endif // HTTPDSERVER_HPP
