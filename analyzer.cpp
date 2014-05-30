#include <cstdio>
#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <sstream>
#include <map>
#include <vector>

using namespace std;


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



int main(int argc, char** argv)
{
	FILE* fp = stdin;

	event evt;

	while (fread(&evt, sizeof(event), 1, fp))
	{
		const size_t n = evt.queue_len;

		packet pkt;
		fread(&pkt, sizeof(packet), 1, fp);
		
		printf("%s dropped (qlen=%u)\n", (connection(pkt.source, pkt.destination)).c_str(), evt.queue_len);


		vector<packet> packets;
		for (size_t i = 0; i < n && fread(&pkt, sizeof(packet), 1, fp); ++i)
		{

			packets.push_back(pkt);
		}

		for (vector<packet>::iterator it = packets.begin(); it != packets.end(); ++it)
		{
			printf("\t%s ps=%u\n", (connection(it->source, it->destination)).c_str(), it->packet_len);
		}
	}

	return 0;
}
