#include <stdexcept>
#include <strings.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <sstream>
#include "strfuncts.h"
#include "Deduplicate.h"

Deduplicate::Deduplicate(DronePlotDB &plotdb) : _plotdb(plotdb)
{
}

Deduplicate::~Deduplicate()
{
}

/********************************************************************************************
 * setValues - Gets this server's SID and the leader and sets variables 
 *                      
 *             
 *******************************************************************************************/
void Deduplicate::setValues(unsigned int sSID, unsigned int lead, unsigned int numServers)
{
        // set this server's sOffset
      	sOffset s; 
     	s.SID = sSID;
      	s.offset = 0;
     	_diffs.emplace_back(s);
	_mySID = sSID;

        // set the leader SID
	_leaderSID = lead;
        // set the number of servers
        _totalServers = numServers;
}

/********************************************************************************************
 * removeDuplicates - This method loops through everything in the DronePlotdb and removes
 *                   duplicates in the list   
 *                   called in ReplSvr after any replication
 *             
 *******************************************************************************************/
void Deduplicate::removeDuplicates()
{
        // check if we have the time skew 
        // 2-step for loop through all of the list 
        for(auto i = _plotdb.begin(); i != _plotdb.end(); i++)
        {
                for(auto j = i; j != _plotdb.end(); j++)
                {
                        if((i != j) && checkDup(*i, *j)) // found a duplicate
                        {
                                if(((*i).node_id) == _mySID) // i has localSID
                                {
                                    findTimeSkew((*j), (*i));
                                }
                                else if(((*j).node_id) == _mySID) // j has localSID
                                {
                                    findTimeSkew((*i), (*j));
				    // also compare against all others, have to because j gets erased
                                    if(_diffs.size() < _totalServers) // haven't found all entries
			            {
                                    	// compare j against everything else and if a match is found get time skew
                                        for(auto f = j; f!= _plotdb.end(); f++)
					{
						if((f != j) && checkDup(*f, *j))
						{
							findTimeSkew((*f), (*j));
							f = j;	
						}
					}
                                    }
                                }
///*
				else if(_diffs.size() < _totalServers) // j and i have nonlocal SIDs
				{
					for(auto k : _diffs)
       					{
                				if(k.SID == ((*i).node_id)) //i offset was prev found so it is corrected to local
                				{
                        				findHardSkew((*i),(*j));
							break;
                				}
						else if(k.SID == ((*j).node_id)) //j offset was prev found so it is now local time
                				{
                        				findHardSkew((*j),(*i));
							break;
                				}
        				}
				}
//*/
                                // erase the duplciate
                                _plotdb.erase(j);
				// reset j to make sure don't miss any
				j = i;
                        }
                } 
        }
        // sort by timestamp
	_plotdb.sortByTime();
}

/********************************************************************************************
 * checkDup - This method compares 2 plots and returns if it is a duplicate 
 *             
 * Returns: true if it is a duplicate and false if it is not 
 *             
 *******************************************************************************************/
bool Deduplicate::checkDup(DronePlot & plot1, DronePlot & plot2)
{
	// check timestamp (if greater difference than 20 not the same point)
	if(plot1.timestamp > plot2.timestamp + 20 || plot1.timestamp < plot2.timestamp - 20)
	{
		return false;
	}
        if(plot1.drone_id != plot2.drone_id)
        {
                return false;
        }
        if(plot1.latitude != plot2.latitude)
        {
                return false;
        }
        if(plot1.longitude != plot2.longitude)
        {
                return false;
        }
        return true;
}

/********************************************************************************************
 * correctToLeader - corrects to all the time stamps to leader  
 *             
 *  
 *             
 *******************************************************************************************/
void Deduplicate::correctToLeader()
{
        // loop through the list and make sure we have the leader field filled out
        double reversedOffset = 0;
        for(auto i : _diffs)
        {
                if(i.SID == _leaderSID)
                {
                        reversedOffset = -(i.offset);
                }
        }
        //go through the entire list and change each time stamp by the opposite amount that the leader has in difference from the current
        if(_mySID != _leaderSID) //avoid case where this server is the leader. No updates
        {
                for(auto i = _plotdb.begin(); i != _plotdb.end(); i++)
                {
                        (*i).timestamp = static_cast<time_t>(static_cast<double>((*i).timestamp) - reversedOffset);
                }
        }        
}

/********************************************************************************************
 * findTimeSkew - finds a time skew if it hasn't already been found  
 *             
 *  
 *             
 *******************************************************************************************/
bool Deduplicate::findTimeSkew(DronePlot diffPlot, DronePlot mePlot)
{
        for(auto i : _diffs)
        {
                // the node id is already in _diffs so we don't need to find it
                if(i.SID == diffPlot.node_id)
                {
                        return false;
                }
        }
        // the information for this node is not populated so find it
        sOffset s; 
        s.SID = diffPlot.node_id;
        s.offset = static_cast<double>(diffPlot.timestamp - mePlot.timestamp);
        _diffs.emplace_back(s);
        std::cout << "POPULATED INFORMATION FOR OFFSET FOR:"<< diffPlot.node_id << " WITH OFFSET:" << s.offset << std::endl;
        // fix previous entries with the different node id 
        fixPrevTimeSkew(s); 
        return true;
            
}

/********************************************************************************************
 * findHardSkew - compares a piece of data with a known timestamp against one with an unknown 
 *                to find the skew, used for when 1 server has almost no data to compare
 *                against others
 *             
 *******************************************************************************************/
bool Deduplicate::findHardSkew(DronePlot knownPlot, DronePlot unknownPlot)
{
	for(auto i : _diffs)
        {
                // the node id is already in _diffs so we don't need to find it
                if(i.SID == unknownPlot.node_id)
                {
                        return false;
                }
        }
        // the information for this node is not populated so find it
        sOffset s; 
        s.SID = unknownPlot.node_id;
        s.offset = static_cast<double>(unknownPlot.timestamp - knownPlot.timestamp); //- s.offset)); // don't need to subtract should already be corrected
        _diffs.emplace_back(s);
        std::cout << "POPULATED INFORMATION FOR HARD OFFSET FOR:"<< unknownPlot.node_id << " WITH OFFSET:" << s.offset << std::endl;
        // fix previous entries with the different node id 
        fixPrevTimeSkew(s); 
        return true;
}

/********************************************************************************************
 * fixPrevTimeSkew - fixes previous entries with a SID when a new time offset is found  
 *             
 *             
 *******************************************************************************************/
void Deduplicate::fixPrevTimeSkew(sOffset s)
{
      	std::cout << "*********** CORRECTING PREVIOUS **************** " << std::endl;
        for(auto i = _plotdb.begin(); i != _plotdb.end(); i++)
        {
                // case that the entry needs to be updated 
                if((*i).node_id == s.SID)
                {
                        // correct a timestamp
			std::cout << "PREV TIMESTAMP!!!!!: " << (*i).timestamp << std::endl;
                        (*i).timestamp = static_cast<time_t>(static_cast<double>((*i).timestamp) - s.offset);
			std::cout << "New TIMESTAMP!!!!!: " << (*i).timestamp << std::endl;
                }
        }
        std::cout << "*********** DONE CORRECTING PREVIOUS **************** " << std::endl;
}

/********************************************************************************************
 * fixTimeSkew - fixes entries if their offset has already been found  
 *             
 *             
 *******************************************************************************************/
void Deduplicate::fixTimeSkew(DronePlot & plot)
{
        for(auto i : _diffs)
        {
                // the node id is already in _diffs so we don't need to find it
                if(i.SID == plot.node_id)
                {
                        plot.timestamp = static_cast<time_t>(static_cast<double>(plot.timestamp) - i.offset);
                }
        }
}

void Deduplicate::printValues()
{
        std::cout << "The number of servers in this scenario is:" << _totalServers << std::endl;
	std::cout << "The SID of this server is:" << _mySID << std::endl;
        std::cout << "The leader for this run is:" << _leaderSID << std::endl;

        std::cout << "FINAL SID TO OFFSET RESULTS!!!!!!!!!!!" << std::endl;
        for(auto i : _diffs)
        {
                std::cout << "SID:" << i.SID << " OFFSET:" << i.offset << std::endl;
        }


}





