#include <cstdio>
#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <sstream>
#include <map>
#include <vector>

using namespace std;



string info(sockaddr_in& src, sockaddr_in& dst)
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



struct packet
{
	struct sockaddr_in source, destination;
	uint16_t packet_len;
};

struct event
{
	uint16_t reserved;
	uint16_t queue_len;
};


string info(packet& pkt)
{
	ostringstream s;
	s << "conn=" << info(pkt.source, pkt.destination) << ", ps=" << pkt.packet_len;
	return s.str();
}



int main()
{
	FILE* fp = stdin;
	event evt;

	while (fread(&evt, sizeof(event), 1, fp))
	{
		printf("queue length = %u\n", evt.queue_len);
		for (uint16_t i = 0; i < evt.queue_len; ++i)
		{
			packet pkt;

			if (!fread(&pkt, sizeof(packet), 1, fp))
			{
				return 1;
			}

			printf("\t%s\n", info(pkt).c_str());
		}
	}

	return 0;
}
