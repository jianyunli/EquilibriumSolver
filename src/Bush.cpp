/*
    Copyright 2008, 2009 Matthew Steel.

    This file is part of EF.

    EF is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    EF is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with EF.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "Bush.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <cstdlib>

using namespace std;

Bush::Bush(const Origin& o, ABGraph& g, vector<pair<double,unsigned> >& tempStore) :
origin(o), bush(g.numVertices()), sharedNodes(g.nodes()), tempStore(tempStore), graph(g)
{
	//Set up graph data structure:
	topologicalOrdering.reserve(g.numVertices());

	setUpGraph();
	buildTrees();//Sets up predecessors. Unnecessary if we do preds
	//in Dijkstra.
	
	sendInitialFlows();//Sends out initial flow patterns (all-or-nothing)
	
	changes.clear();
}

void Bush::setUpGraph()
{
	vector<unsigned> distanceMap(graph.numVertices(), -1);
		//position of node i in topologicalOrdering
		//Set up in Dijkstra
	
	graph.dijkstra(origin.getOrigin(), distanceMap, topologicalOrdering);
	
	vector<ForwardGraphEdge>::iterator begin = graph.begin(), end=graph.end();
	//Forward edges have all the nice info, unfortunately.
	
	/*
	Our Dijkstra routine gives a proper topological ordering, consistent
	even in the presence of zero-length arcs. I think. Of course, the traffic
	assignment problem with zero-length arcs does not have a unique link-flow
	solution in general, so it isn't really an issue that we aren't as
	careful later on.
	*/
	
	//TEST: destination unreachable from origin.
	for(vector<pair<int, double> >::const_iterator i = origin.dests().begin(); i != origin.dests().end(); ++i) {
		if(distanceMap.at(i->first) == -1)
			std::cout << "Unreachable dest: origin " << origin.getOrigin() << ", dest " << i->first << std::endl;
	}
	
	for(vector<ForwardGraphEdge>::iterator iter = begin; iter != end; ++iter) {
		
		unsigned toId = iter->toNode()-&sharedNodes[0];
		unsigned toPosition = distanceMap.at(toId);
		unsigned fromId = graph.backward(&*iter)->fromNode()-&sharedNodes[0];
		unsigned fromPosition = distanceMap.at(fromId);
		if(toPosition == -1 || fromPosition == -1) continue;
		//Ignore edges to/from unreachable nodes.
		
		if(fromPosition < toPosition) {
			//When we're finally done Dijkstra will just add the predecessors
			//and we'll be away. (Dijkstra will be in this class then, no doubt)
			
			bush.at(toId).push_back((BushEdge(graph.backward(&*iter))));
		}
	}
}

void Bush::sendInitialFlows()
{
	BushNode *root = &sharedNodes.at(origin.getOrigin());
	for(vector<pair<int, double> >::const_iterator i = origin.dests().begin(); i != origin.dests().end(); ++i) {
		//For each origin,
		BushNode* node = &sharedNodes.at(i->first);
		while(node != root) {
			//Flow back to the bush's root
			BushEdge *be = node->getMinPredecessor();
			be->addFlow(i->second);
			ForwardGraphEdge* fge = graph.forward(be->underlyingEdge());
			fge->addFlow(i->second);
			be->underlyingEdge()->setDistance((*fge->costFunction())(fge->getFlow()));
			node = be->fromNode();
		}
	}
}//Performs initial all-or-nothing flows in Bush, adding to both BushEdges and GraphEdges.

void Bush::printCrap()
{
	//Not really used, exists for debugging purposes if I really break something.
	cout << "Printing  crap:" <<endl;
	cout << "In-arcs:"<<endl;
	
	for(vector<BushNode>::iterator i = sharedNodes.begin(); i != sharedNodes.end(); ++i) {

		cout << i-sharedNodes.begin() << "("<< (*i).minDist() <<","<<(*i).maxDist()<<"):";

		for(BushEdge* j = bush.at(i-sharedNodes.begin()).begin(); j!=bush.at(i-sharedNodes.begin()).end(); ++j) {
			cout << " " << ((j->fromNode()-&sharedNodes[0])) <<
			        "(" << (j->length()) << "," << (j->flow()) << ") ";
		}
		cout << endl;
	}

	cout << "Topological Ordering:" << endl;
	for(vector<unsigned>::const_iterator i = topologicalOrdering.begin(); i != topologicalOrdering.end(); ++i)
		cout << *i << " ";
	cout << endl;
}

bool Bush::fix(double accuracy)
{
	bool localFlowChanged = false;
	do {
		localFlowChanged = equilibriateFlows(accuracy) | localFlowChanged;
	} while (updateEdges());
	return localFlowChanged;
}

bool Bush::equilibriateFlows(double accuracy)
{
	bool flowsChanged = false;//returns whether we've done any updates on our bush flows.
	buildTrees();
	while (true) {
		bool thisTime = false;
		for(vector<pair<int, double> >::const_iterator i = origin.dests().begin(); i != origin.dests().end(); ++i) {
			if (sharedNodes[i->first].getDifference() > accuracy) {
				sharedNodes[i->first].equilibriate(graph);
				//makes it better
				thisTime = true;
			}
		}//Find worst difference.
		flowsChanged = flowsChanged | thisTime;
		if(!thisTime) return flowsChanged;
		buildTrees();
	}
	return flowsChanged;
}

void Bush::buildTrees()
{

	sharedNodes[origin.getOrigin()].setDistance(0.0);
	
	changes.clear();//Just in case, forget any edges need turning around
	unsigned topoIndex = 0;
	for(vector<unsigned>::const_iterator i = topologicalOrdering.begin()+1; i < topologicalOrdering.end(); ++i, ++topoIndex) {
		unsigned id = *i;
		BushNode &v = sharedNodes[id];
		EdgeVector& inEdges = bush[id];
		v.updateInDistances(inEdges);
		
		updateEdges(inEdges, v.maxDist(), id);
	}
}//Resets min, max distances, builds min/max trees.

bool Bush::updateEdges()
{
	if(!changes.empty()) {
		applyBushEdgeChanges();
		topologicalSort();
		changes.clear();
		return true;
	}
	return false;
}//Switches direction of things that need it.

void Bush::applyBushEdgeChanges()
{
	
	typedef vector<pair<unsigned, unsigned> >::iterator it;

	for(it i = changes.begin(); i != changes.end();) {
		unsigned node = i->first;//to-node
		
		it j;
		for(j=i+1; j != changes.end() && j->first == node; ++j);//j points past end of nodes to move
		
		EdgeVector& inEdges = bush[node];
		
		int l = inEdges.length()-(j-i);//new length of inEdges with nodes removed
		it m = j-1;//m is the index of the last change to make?
		
		for(int k = inEdges.length()-1; m>=i; --k, --m) {
			std::swap(inEdges[k], inEdges[m->second]);
		}//Move edges to be inverted to the end.
		//Move the last ones first for a reason - don't screw up order
		
		for(int k = l; k != inEdges.length(); ++k) {
			//Perform the edge direction swaps on the last few entries.
			BushEdge& edge = inEdges[k];
			unsigned fromID = edge.fromNode()-&sharedNodes[0];
			edge.swapDirection(graph);
			bush[fromID].push_back(edge);
		}
		inEdges.resize(j-i);//Delete the last edges from the set
		
		i=j;//Move on to the next lot of changes.
	}
}

void Bush::topologicalSort()
{
	/* Used to be a zero in-degree algorithm, O(V+E). Now just a simple
	 * O(V log V) sort on nodes' max distance. Note that in real world
	 * problems E is O(V), but sorting has a huge constant-factor
	 * advantage.
	 * At the end of the day we don't spend much time in this function
	 * (5% at last count), but it's twice as fast as it was, and
	 * simpler to boot. Win-win.
	 *
	 * I'm considering storing my edge lists sorted instead of how I
	 * do things now, maybe keeping an inverse-topological sort. This
	 * would make buildTrees a somewhat faster, and this function and
	 * updateEdges(2) a little slower. Maybe later, probably worth it
	 * if I can be clever about it.
	 * 
	 * TODO: Only sort between the i and j node if reversed edges.
	 */
	
	/* Preconditions: Edges in outEdges define a strict partial order
	 * on nodes, and an edge e=(v1,v2) exists only if 
	 *   maxDist(v1)< maxDist(v2).
	 * Indexes of all nodes ending up in the order need to begin in it,
	 * too.
	 * NOTE: If we deal with adjacent nodes with potential two-way zero-
	 * length connecting arcs we may need to think harder. Bar-Gera (2002)
	 * says, "strictly positive", though.
	 */
	
	vector<pair<double,unsigned> >::iterator index = tempStore.begin();
	vector<pair<double,unsigned> >::iterator storeEnd = tempStore.begin() + topologicalOrdering.size();
	for(vector<unsigned>::iterator i = topologicalOrdering.begin(); index != storeEnd; ++i, ++index) {
		index->first = sharedNodes[*i].maxDist();
		index->second = *i;
	}
	stable_sort(tempStore.begin(), storeEnd, Bush::pairComparator);
	
	index = tempStore.begin();
	for(vector<unsigned>::iterator i = topologicalOrdering.begin(); index != storeEnd; ++i, ++index) {
		(*i) = index->second;
	}
}

int Bush::giveCount()// const
{
	//NOTE: now returns number of nodes with the same distance.
	buildTrees();
	unsigned count = 0;
	for(vector<unsigned>::iterator i = topologicalOrdering.begin(); i != topologicalOrdering.end();) {
		double dist1 = sharedNodes.at(*i).maxDist();
		vector<unsigned>::iterator j;
		for(j = i+1; j != topologicalOrdering.end() && dist1 == sharedNodes.at(*j).maxDist(); ++j);
		count += (j-i-1);
		i = j;
	}
	return count;
	
	/*
	//Returns the number of arcs with non-zero flow from this bush. Tends to
	//reach about a fifth of all arcs in typical road networks.
	int count = 0;
	for(vector<vector<BushEdge> >::const_iterator i = bush.begin(); i != bush.end(); ++i) {
		for(vector<BushEdge>::const_iterator j = i->begin(); j != i->end(); ++j) {
			if(j->used()) ++count;
		}
	}
	return count;
	*/
}

double Bush::allOrNothingCost()
{
	/*
	NOTE: Probably not too slow (5ms or so on largest test case for all
	bushes once) but if we find a use-case where we call it all the time
	we can make it linear time by iterating in reverse topological order
	and storing flows in linear space.
	TODO: Maybe merge with sendInitialFlows() a bit - looks kinda copy and
	paste at the moment.
	*/

	buildTrees();// NOTE: Breaks constness. Grr. Make sharedNodes mutable?
	
	double cost = 0.0;
	BushNode* root = &sharedNodes.at(origin.getOrigin());
	for(vector<pair<int, double> >::const_iterator i = origin.dests().begin(); i != origin.dests().end(); ++i) {
		//For each origin,
		BushNode* node = &sharedNodes.at(i->first);
		while(node != root) {
			//Flow back to the bush's root
			BushEdge *be = node->getMinPredecessor();
			cost += i->second * be->length();
			
			node = be->fromNode();
		}
	}
	return cost;
}

Bush::~Bush()
{}

double Bush::maxDifference() {
	buildTrees();
	double ret = 0.0;
	for(std::vector<std::pair<int, double> >::const_iterator i = origin.dests().begin(); i != origin.dests().end(); ++i) {
		ret = max(ret, sharedNodes[i->first].getDifference());
	}
	return ret;
}
