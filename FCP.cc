#include "RoutingProtocolImpl.h"

void RoutingProtocolImpl::forward_FCP(unsigned short src, unsigned short dst) {
  char *packet;
  void *v_pointer;
  multimap<unsigned short, unsigned short> fail_links;
  short map_size = 0;
  short pp_size = 96;
  packet = (char*)malloc(pp_size);
  
  fail_links[1] = 2;
  fail_links[2] = 3;
  map_size = sizeof(multimap<unsigned short, unsigned short>)*fail_links.size();
  
  *(ePacketType *) (packet) = (ePacketType) DATA;
  *(short *) (packet+16) = (short) htons(pp_size+map_size);
  *(short *) (packet+32) = (short) htons(src);
  *(short *) (packet+48) = (short) htons(dst);
  *(multimap<unsigned short,unsigned short> *) (packet+64) = fail_links;
  
  v_pointer = packet;
  
  sys->send(port_id, v_pointer, pp_size);
}

//TODO
//Check Whether the neighbors are alive, the failure links information will be stored 
multimap<unsigned short, unsigned short> RoutingProtocolImpl::store_failure_neighbors() {
	int i;
	multimap<unsigned short, unsigned short> fail_neighbors;
	for(i=0;i<num_ports;i++)
	{
		if( port_table.at(i).is_alive == false)
		{
			fail_neighbors.insert(pair<unsigned short, unsigned short>(this->router_id, i));
			port_table.at(i).cost = INFINITY_COST;
		}
	}
	return fail_neighbors;
}

//TODO
//Update the router's map based on failure linkes 
/*void RoutingProtocol::updateMap(multimap<unsigned short, unsigned short> fail_links) {
}*/

//TODO
//handle the situation when receive a packet with the type FCP
void RoutingProtocolImpl::recvFCP(unsigned short port, void *packet, unsigned short size) {
	char *pck = (char *)packet;
	unsigned short srcID = ntohs(*(unsigned short*)(pck + 4));
	unsigned int seq = ntohl(*(unsigned int*)(pck + 8));
	int i;

	printf("  receive FCP : from %d, sequence number: %d\n", srcID, seq);

	if (nodeVec.find(srcID) == nodeVec.end()) {
		printf("  This is a new FCP:\n");
		// add this new FCP
		FCP newFCP;
		newFCP.Node_ID = srcID;
		newFCP.sequence = seq;
		newFCP.update = sys->time();

		// insert the cost in the packet to FCP
		for (i = 0; i < size - 12; i += 4) {
			unsigned short nID = ntohs(*(unsigned short*)(pck + 12 + i));
			unsigned short ncost = ntohs(*(unsigned short*)(pck + 14 + i));
			printf("\tnode ID: %d, cost: %d\n", nID, ncost);
			newFCP.neighbour.insert(pair<unsigned short, unsigned short>(nID, ncost));
		}
		nodeVec.insert(pair<unsigned short, FCP>(srcID, newFCP));

		// TODO
		// insert the failure links in the packet to FCP

		dijkstra();
		sendFCPRecv(port, pck, size);
	}
	else if (nodeVec[srcID].sequence < seq) {
		printf("  Update current FCP: (old sequence number: %d; new sequence number: %d)\n", nodeVec[srcID].sequence, seq);
		// update this linkState
		FCP *f = &nodeVec[srcID];
		f->sequence = seq;
		f->neighbour.clear();
		f->update = sys->time();

		// insert the cost in the packet to linkState
		for (i = 0; i < size - 12; i += 4) {
			unsigned short nID = ntohs(*(unsigned short*)(pck + 12 + i));
			unsigned short ncost = ntohs(*(unsigned short*)(pck + 14 + i));
			printf("\tnode ID: %d, cost: %d\n", nID, ncost);
			f->neighbour.insert(pair<unsigned short, unsigned short>(nID, ncost));
		}

		// TODO
		// insert the failure links in the packet to FCP

		dijkstra();
		sendFCPRecv(port, pck, size);
	}
	else
		printf("  This is an old FCP. Just ignore it.\n");

	packet = NULL;
	delete pck;
}

//TODO
//send the received FCP packet
void RoutingProtocolImpl::sendFCPRecv(unsigned short port, char *packet, unsigned short size) {
	for (int i = 0; i < num_ports; i++)
	if (i != port) {
		char *toSend = (char*)malloc(sizeof(char)* size);
		*toSend = F_C_P;
		*(unsigned short*)(toSend + 2) = htons(size);
		*(unsigned short*)(toSend + 4) = *(unsigned short*)(packet + 4);
		*(unsigned int*)(toSend + 8) = *(unsigned int*)(packet + 8);
		for (int j = 0; j < size - 12; j += 4) {
			*(unsigned short*)(toSend + 12 + j) = *(unsigned short*)(packet + 12 + j);
			*(unsigned short*)(toSend + 14 + j) = *(unsigned short*)(packet + 14 + j);
		}
		sys->send(i, toSend, size);
	}
}

//TODO
//send FCP portInfo to all neighbours
void RoutingProtocolImpl::sendFCPPort(){
	char type = FCP;
	unsigned short size;
	unsigned short sourceId = router_id;

	map<unsigned short, unsigned short> portInfo;
	multimap<unsigned short, unsigned short> failPort;

	//get neighbours and fail ports
	for (int i = 0; i < num_ports; i++) {
		if (port_table.at(i).is_alive) {
			portInfo.insert(pair<unsigned short, unsigned short>(port_table.at(i).neighbor_id, port_table.at(i).cost));
		}
		else {
			failPort.insert(pair<unsigned short, unsigned short>(sourceId, i));
			port_table.at(i).cost = INFINITY_COST;
		}
	}

	int sequenceNumber = mySequence;
	mySequence++;
	size = 12 + (portInfo.size() * 4);

	//TODO
	//Insert fail ports

	bool printed = false;
	for (int i = 0; i < num_ports; i++) {
		if (port_table.at(i).is_alive) {
			printf("\tSend FCP to port %d.\n", i);
			char * packet = (char *)malloc(sizeof(char)* size);
			*packet = type;
			*(short *)(packet + 2) = htons(size);
			*(short *)(packet + 4) = htons(sourceId);
			*(int *)(packet + 8) = htonl(sequenceNumber);

			int index = 12;
			for (map<unsigned short, unsigned short>::iterator it = portInfo.begin(); it != portInfo.end(); it++) {
				unsigned short neighbourID = it->first;
				unsigned short newCost = it->second;
				if (!printed)
					printf("neighbour ID: %d, cost: %d\n", neighbourID, newCost);
				*(short *)(packet + index) = htons(neighbourID);
				*(short *)(packet + index + 2) = htons(newCost);
				index += 4;
			}
			printed = true;
			sys->send(i, packet, size);
		}
	}
}

//check FCP time out
/* void RoutingProtocolImpl::checkFCPTimeOut() {
for (map<unsigned short, linkState>::iterator it = nodeVec.begin(); it != nodeVec.end(); it++) {
linkState ls = it->second;
if (sys->time() - ls.update >= 45000)
nodeVec.erase(ls.Node_ID);
}
}*/
