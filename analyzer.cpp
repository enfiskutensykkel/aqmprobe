#include <cstdio>
#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <sstream>
#include <map>
#include <vector>

using namespace std;

typedef map<string, uint32_t> connmap;

string connstr(sockaddr_in& src, sockaddr_in& dst)
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



struct queue_info
{
	connmap count;
	connmap queue[qdisc_len];

	void add(string& s, int idx)
	{
		connmap::iterator it = count.find(s);

		if (it == count.end())
		{
			count[s] = 1;
			queue[idx][s] = 1;
			return;
		}

		it->second += 1;
		queue[idx][s] += 1;
	}
};


map<string, queue_info> data;

struct event
{
	uint16_t reserved;
	uint16_t queue_len;
};

struct packet
{
	struct sockaddr_in source, destination;
	uint16_t packet_len;
};


int main()
{
	FILE* fp = stdin;
	event evt;

	while (fread(&evt, sizeof(event), 1, fp))
	{
		packet first;

		if (!fread(&first, sizeof(packet), 1, fp))
		{
			return 2;
		}

		string connection = connstr(first.source, first.destination);

		for (uint16_t i = 1; i < evt.queue_len; ++i)
		{
			packet pkt;

			if (!fread(&pkt, sizeof(packet), 1, fp))
			{
				return 1;
			}

			string packetstr = connstr(pkt.source, pkt.destination);
			data[connection].add(packetstr, i - 1);
		}
	}

	for (map<string, queue_info>::iterator it = data.begin(); it != data.end(); ++it)
	{
		printf("%s\n", it->first.c_str());

		for (uint16_t i = 0; i < qdisc_len; ++i)
		{
			printf("\t%3u\n", i);

			for (connmap::iterator a = it->second.queue[i].begin(); a != it->second.queue[i].end(); ++a)
			{
				printf("\t\t%s : %3u\n", a->first.c_str(), a->second);
			}
		}
	}

	return 0;
}
