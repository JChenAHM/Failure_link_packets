#include "RoutingProtocolImpl.h"

//send the FCP table to all neighbours. This is the same as LS
void RoutingProtocolImpl::sendFCPtable(){
    char type = FCP;
    unsigned short size;
    unsigned short sourceId = router_id;
    
    map<unsigned short, unsigned short> portInfo;
	
    //get neighbours
    for (int i = 0; i < num_ports; i++) {
        if (port_table.at(i).is_alive) {
            portInfo.insert(pair<unsigned short, unsigned short>(port_table.at(i).neighbor_id, port_table.at(i).cost));
        }
    }

    int sequenceNumber = mySequence;
    mySequence++;
    size = 12 + (portInfo.size() * 4);
    
	bool printed = false;
    for (int i = 0; i < num_ports; i++) {
        if (port_table.at(i).is_alive) {
		printf("\tSend FCP table to port %d.\n", i);
            char * packet = (char *) malloc(sizeof(char) * size);
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

//handle the situation when receive a packet with the type FCP. This is the same as LS
void RoutingProtocolImpl::recvFCPtable(unsigned short port, void *packet, unsigned short size) {
	char *pck = (char *)packet;
	unsigned short srcID = ntohs(*(unsigned short*)(pck + 4));
	unsigned int seq = ntohl(*(unsigned int*)(pck + 8));
	int i;

	printf(" Receive FCP table: from %d, sequence number: %d\n", srcID, seq);

	// node not found in the FCPvec
	if (FCPVec.find(srcID) == FCPVec.end()) {
		printf("  This is a new FCP table:\n");
		// add this new linkState
		FCPtable newFcp;
		newFcp.Node_ID = srcID;
		newFcp.sequence = seq;
		newFcp.update = sys->time();
		
		// insert the cost in the packet to FCP table
		for (i = 0; i < size - 12; i += 4) {
			unsigned short nID = ntohs(*(unsigned short*)(pck + 12 + i));
			unsigned short ncost = ntohs(*(unsigned short*)(pck + 14 + i));
			printf("\tnode ID: %d, cost: %d\n", nID, ncost);
			newFcp.neighbour.insert(pair<unsigned short, unsigned short>(nID, ncost));
		}
		
		FCPVec.insert(pair<unsigned short, FCPtable>(srcID, newFcp));
		dijkstra();
		sendFCPRecv(port, pck, size);
	}
	else if (FCPVec[srcID].sequence < seq) {
		printf("  Update current FCP table: (old sequence number: %d; new sequence number: %d)\n", FCPVec[srcID].sequence, seq);
		// update this linkState
		FCPtable *fcp = &FCPVec[srcID];
		fcp->sequence = seq;
		fcp->neighbour.clear();
		fcp->update = sys->time();
		
		// insert the cost in the packet to FCP table
		for (i = 0; i < size - 12; i += 4) {
			unsigned short nID = ntohs(*(unsigned short*)(pck + 12 + i));
			unsigned short ncost = ntohs(*(unsigned short*)(pck + 14 + i));
			printf("\tnode ID: %d, cost: %d\n", nID, ncost);
			fcp->neighbour.insert(pair<unsigned short, unsigned short>(nID, ncost));
		}
		
		dijkstra();
		sendFCPRecv(port, pck, size);
	}
	else
		printf("This is an old FCP table. Just ignore it.\n");
	packet = NULL;
	delete pck;
}

//send the received FCP packet
void RoutingProtocolImpl::sendFCPRecv(unsigned short port, char *packet, unsigned short size) {
	for (int i = 0; i < num_ports; i++)
		if (i != port) {
			char *toSend = (char*)malloc(sizeof(char) * size);
			*toSend = FCP;
			*(unsigned short*)(toSend+2) = htons(size);
			*(unsigned short*)(toSend+4) = *(unsigned short*)(packet+4);
			*(unsigned int*)(toSend+8) = *(unsigned int*)(packet+8);
			for (int j = 0; j < size - 12; j += 4) {
				*(unsigned short*)(toSend+12+j) = *(unsigned short*)(packet + 12 + j);
				*(unsigned short*)(toSend+14+j) = *(unsigned short*)(packet + 14 + j);
			}
			sys->send(i, toSend, size);
		}
}

// handler funciton when receive an FCP
void RoutingProtocolImpl::recvFCP(char* packet) {
    //ePacketType packet_type = (ePacketType)(*(ePacketType*)packet);
    short size = (short)ntohs(*(short*)(packet+2));
    short src = (short)ntohs(*(short*)(packet+4));
    short dst = (short)ntohs(*(short*)(packet+6));
    
    cout<<"Size is "<<size<<endl;
    
    if(dst == this->router_id)
    {
        cout<<"Packet is delivered to "<<router_id<<endl;
        return;
    }
    
    // use to store the failure links
    multimap<unsigned short, unsigned short> failure_links;
    
    // copy the global map to the curretnVec
    map<unsigned short, FCPtable> currentVec;
    currentVec.insert(FCPVec.begin(), FCPVec.end());
    
    // change links' aliveness based on the failure information
    for(int index = 8; index < size; index+=4)
    {
        // get the two router_ids which repersent a failure link
        unsigned short router_id1 = (unsigned short)ntohs(*(unsigned short*)(packet+index));
        unsigned short router_id2 = (unsigned short)ntohs(*(unsigned short*)(packet+index+2));
        
        // we need to check the router id in the failure carrying packet is not the same as the received router
        if(this->router_id != router_id1)
        {
            FCPtable fcp1 = currentVec.at(router_id1);
            map<unsigned short, unsigned short> port_info1 = fcp1.neighbour;
            
            for(map<unsigned short, unsigned short>::iterator iter = port_info1.begin(); iter!= port_info1.end();iter++)
            {
                // delete the failure port based on the router id
                if(iter->first == router_id2)
                    currentVec.at(router_id1).neighbour.erase(iter->first);
            }
        }
        
        // the same for router_id2
        if(this->router_id != router_id2)
        {
            FCPtable fcp2 = currentVec.at(router_id2);
            map<unsigned short, unsigned short> port_info2 = fcp2.neighbour;
            
            for(map<unsigned short, unsigned short>::iterator iter = port_info2.begin(); iter!=port_info2.end();iter++)
            {
                // delete the feature port based on the router id
                if(iter->first == router_id1)
                    currentVec.at(router_id2).neighbour.erase(iter->first);
            }
        }
        //
        failure_links.insert(std::pair<unsigned short,unsigned short>(router_id1,router_id2));

		// if new inserted pair is in importantLinks, then flood
		unsigned short router1 = router_id1;
		unsigned short router2 = router_id2;
		typedef multimap<unsigned short, unsigned short>::size_type sz_type1;
		sz_type1 entries1 = importantLinks.count(router1);  // how many entries are there for router1 
		multimap<unsigned short, unsigned short>::iterator iter1 = importantLinks.find(router1);  // get iterator to the first entry for router1
		// loop through the number of entries there are for router1
		for (sz_type1 cnt1 = 0; cnt1 != entries1; ++cnt1, ++iter1) {
			if (iter1->second == router2)
				sendFCPtable();
		}
    }
    
    // Check if any port is failing and add these corresponding router id into the failure_links
    for(vector<rport>::iterator iter = port_table.begin(); iter != port_table.end(); iter++ )
    {
		if (iter->is_alive == false){
			failure_links.insert(std::pair<unsigned short, unsigned short>(router_id, iter->neighbor_id));

			// if new inserted pair is in importantLinks, then flood
			unsigned short router_1 = router_id;
			unsigned short router_2 = iter->neighbor_id;
			typedef multimap<unsigned short, unsigned short>::size_type sz_type2;
			sz_type2 entries2 = importantLinks.count(router_1);  // how many entries are there for router_1 
			multimap<unsigned short, unsigned short>::iterator iter2 = importantLinks.find(router_1);  // get iterator to the first entry for router_1
			// loop through the number of entries there are for router_1
			for (sz_type2 cnt2 = 0; cnt2 != entries2; ++cnt2, ++iter2) {
				if (iter2->second == router_2)
					sendFCPtable();
			}
		}

    }
	 
    // get next hop and forward the packet
    unsigned short next_hop = getNextHop(currentVec, dst);

	if (next_hop != INFINITY_COST)
	{
		vector<unsigned short> svec;
		// add one to the number of data packets sent through current router port
		port_table.at(next_hop).count++;
		cout << "The number of data packets sent through the current port is : " << port_table.at(next_hop).count << endl;
		// add the link to important links if the count exceeds the threshold
		if (port_table.at(next_hop).count == THRESHOLD) {
			typedef multimap<unsigned short, unsigned short>::size_type sz_type;
			sz_type entries = importantLinks.count(this->router_id);  // how many entries are there for this router_id 
			if (entries == 0){
				importantLinks.insert(pair<unsigned short, unsigned short>(this->router_id, get_router(next_hop)));
				importantLinks.insert(pair<unsigned short, unsigned short>(get_router(next_hop), this->router_id));
			}
			else{
				multimap<unsigned short, unsigned short>::iterator iter = importantLinks.find(this->router_id);  // get iterator to the first entry for this router_id
				for (sz_type cnt = 0; cnt != entries; ++cnt, ++iter) {
					svec.push_back(iter->second);
					vector<unsigned short>::const_iterator result = find(svec.begin(), svec.end(), get_router(next_hop));
					if (result == svec.end()){
						importantLinks.insert(pair<unsigned short, unsigned short>(this->router_id, get_router(next_hop)));
						importantLinks.insert(pair<unsigned short, unsigned short>(get_router(next_hop), this->router_id));
					}
				}
			}
		}
	}

    cout << "next_hop is "  << next_hop << endl;
    cout << "The next router is "<< get_router(next_hop) <<endl;
    cout << "The size of link failure "<<failure_links.size() <<endl;
	cout << "Links in importantLinks are :\nRouter1\tRouter2\n";

	// print important links
	for (multimap<unsigned short, unsigned short>::const_iterator iter = importantLinks.begin(); iter != importantLinks.end(); ++iter)
		cout << iter->first << '\t' << iter->second << '\n';
	cout << endl;

    // forward the packet to the next hop
    forwardFCP(src,dst,next_hop,failure_links);
}


// perform the dijkstra algorithm to get the next hop of the shortest path
unsigned short RoutingProtocolImpl::getNextHop(map<unsigned short, FCPtable>currentVec, unsigned short dst){
    int i;        // port_table.at(1).port_number = i
    set<unsigned short> nodeChecked;		    // visited nodes
    set<unsigned short> nodeRemain;				// nodes not visited
    map<unsigned short, unsigned short> nodePair;		// neighbour nodes map: <nodeID, port number>
    map<unsigned short, unsigned short> currentCost;    // the cost of neighbour nodes map: <nodeID, cost>
    set<unsigned short>::iterator setit;			    // set iterator
    map<unsigned short, unsigned short>::iterator mapit;	// map iterator
    
    // initialize node maps
    nodeChecked.insert(router_id);
    for (i = 0; i < num_ports; i++)
        if (port_table.at(i).is_alive) {
            unsigned short nodeID = port_table.at(i).neighbor_id;    // neighbours' IDs
            unsigned short nodeCost = port_table.at(i).cost;    // cost to neighbours
            if (currentVec.find(nodeID) != currentVec.end())
                nodeRemain.insert(nodeID);		// insert neighbours into nodeRemain
            nodePair.insert(pair<unsigned short, unsigned short>(nodeID, i));
            currentCost.insert(pair<unsigned short, unsigned short>(nodeID, nodeCost));
        }
    //
    unsigned short nextHop = INFINITY_COST;
    
    // Dijkstra Algorithm
    while (!nodeRemain.empty()) {
        unsigned short minCost = INFINITY_COST;		// minimal cost to the current checked node
        unsigned short currentCheck = INFINITY_COST;			// current checked node
        
        // get the node with the shortest cost from nodeRemain
        for (setit = nodeRemain.begin(); setit != nodeRemain.end(); setit++)
            if (currentCost[*setit] < minCost) {
                minCost = currentCost[*setit];
                currentCheck = *setit;
            }
        
        // break if found the destination node
        if(currentCheck == dst)
        {
            nextHop = nodePair.at(currentCheck);
            break;
        }
        
        nodeRemain.erase(currentCheck);
        nodeChecked.insert(currentCheck);
        
        // update the remaining nodes
        FCPtable *fcp = &currentVec[currentCheck];
        for (mapit = fcp->neighbour.begin(); mapit != fcp->neighbour.end(); mapit++) {
            if (nodeChecked.find(mapit->first) == nodeChecked.end()) {	// not in checked set
                if (currentCost.find(mapit->first) == currentCost.end()) {
                    currentCost.insert(pair<unsigned short, unsigned short>(mapit->first, minCost + mapit->second));
                    nodePair.insert(pair<unsigned short, unsigned short>(mapit->first, nodePair[currentCheck]));
                    if (currentVec.find(mapit->first) != currentVec.end())
                        nodeRemain.insert(mapit->first);
                }
                else if (currentCost[mapit->first] > minCost + mapit->second) {
                    // update the cost and the next node
                    currentCost[mapit->first] = minCost + mapit->second;
                    nodePair[mapit->first] = nodePair[currentCheck];
                }
            }
        }
    }
    return nextHop;
}

// forward the Failure carrying packet. Failure information is carried using multimap
void RoutingProtocolImpl::forwardFCP(unsigned short src, unsigned short dst, unsigned short port_id,
                                     multimap<unsigned short, unsigned short> fail_links)
{
    char *packet;
    void *v_pointer;
    short map_size = 0;
    short pp_size = 0;

	// multimap<unsigned short, unsigned short> important;
    
    //fail_links.insert(pair<unsigned short, unsigned short>(1,0));
    //map_size = sizeof(multimap<unsigned short, unsigned short>)*fail_links.size();
    map_size = 4*fail_links.size();
    cout<<"Map size is "<<map_size<<endl;
    pp_size = 8+map_size;
    
    packet = (char*)malloc(pp_size);
    
    *packet = DATA;
    *(short *) (packet+2) = (short) htons(pp_size);
    *(short *) (packet+4) = (short) htons(src);
    *(short *) (packet+6) = (short) htons(dst);
    
    int index = 8;
    // multimap store two node ids
    for( multimap<unsigned short, unsigned short>::iterator iter = fail_links.begin(); iter != fail_links.end(); iter++ )
    {
         *(short *)(packet + index) = (unsigned short)htons((unsigned short)iter->first); // This is the router1' id
         *(short *)(packet + index + 2) = (unsigned short)htons((unsigned short)iter->second); // This is the router2' id
         index +=4;
    }
    v_pointer = packet;    

	// if the count number of the packet sent exceeds the THRESHOLD, then the link is inserted into importantLinks
	if (port_id != INFINITY_COST){
		sys->send(port_id, v_pointer, pp_size);

		/* port_table.at(port_id).count++;

		cout << "The number of data packets sent through this port is : " << port_table.at(port_id).count << endl; 

		if (port_table.at(port_id).count == THRESHOLD){
			importantLinks.insert(pair<unsigned short, unsigned short>(src, dst));
			importantLinks.insert(pair<unsigned short, unsigned short>(dst, src));
		}

		cout << "Links in importantLinks are :\nRouter1\tRouter2\n";
		for (multimap<unsigned short, unsigned short>::const_iterator iter = importantLinks.begin(); iter != importantLinks.end(); ++iter)
			cout << iter->first << '\t' << iter->second << '\n';
		cout << endl;*/
	}
    else
        cout <<"No valid path exist"<<endl;
}
