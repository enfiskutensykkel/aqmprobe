#include <cstdio>
#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <sstream>
#include <map>

using namespace std;

struct report
{
	sockaddr_in source, dest;
	uint64_t time_stamp;
	uint32_t queue_length;
	uint16_t packet_length;
	uint8_t dropped;
	uint8_t reserved;
};


string connection(sockaddr_in& src, sockaddr_in& dst)
{
	ostringstream str;

	union {
		uint32_t addr;
		uint8_t str[4];
	} ipaddr;

	ipaddr.addr = src.sin_addr.s_addr;
	str << (int) ipaddr.str[0];
	for (int i = 1; i < 4; ++i)
	{
		str << "." << (int) ipaddr.str[i];
	}
	str << ":" << ntohs(src.sin_port);

	str << "-";

	ipaddr.addr = dst.sin_addr.s_addr;
	str << (int) ipaddr.str[0];
	for (int i = 1; i < 4; ++i)
	{
		str << "." << (int) ipaddr.str[i];
	}
	str << ":" << ntohs(dst.sin_port);

	return str.str();
}


static map<string, uint32_t> total_count;
static map<string, uint32_t> total_dropped;

int main(int argc, char** argv)
{

	FILE* fp = stdin;
	report tmp;

	uint64_t first = 0;

	while (fread(&tmp, sizeof(report), 1, fp))
	{
		string s(connection(tmp.source, tmp.dest));
		
		if (total_count.count(s) > 0)
		{
			total_count[s] += 1;
			total_dropped[s] += tmp.dropped;
		}
		else
		{
			total_count[s] = 1;
			total_dropped[s] = 1;
		}

		if (first == 0)
		{
			first = tmp.time_stamp;
		}

		fprintf(stdout, "%lu, %u, %s, %u, %u, %u\n", (tmp.time_stamp - first), tmp.queue_length, s.c_str(), tmp.packet_length, tmp.dropped, 
				ntohs(tmp.dest.sin_port)
				);
	}


	for (map<string,uint32_t>::iterator it = total_count.begin(); it != total_count.end(); it++)
	{
		uint32_t dropped = total_dropped[it->first];
		fprintf(stderr, "%20s %8u %8u %8.3f %%\n", it->first.c_str(), it->second, dropped, (dropped / (double) it->second) * 100.0);
	}

	return 0;
}
