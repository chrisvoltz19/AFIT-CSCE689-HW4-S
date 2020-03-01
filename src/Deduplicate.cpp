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
void Deduplicate::setValues(unsigned int sSID, unsigned int lead)
{
        // set this server's sOffset
      	sOffset s; 
     	s.SID = sSID;
      	s.offset = 0;
     	_diffs.emplace_back(s);
	_mySID = sSID;

        // set the leader SID
	_leaderSID = lead;
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
                        //auto & cmp2 = *j;
                        if((i != j) && checkDup(*i, *j)) // found a duplicate
                        {
                                if(((*i).node_id) == _mySID)
                                {
                                    findTimeSkew((*j), (*i));
                                }
                                else if(((*j).node_id) == _mySID)
                                {
                                    findTimeSkew((*i), (*j));
                                }
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
        s.offset = static_cast<double>(mePlot.timestamp - diffPlot.timestamp);
        _diffs.emplace_back(s);
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
        for(auto i = _plotdb.begin(); i != _plotdb.end(); i++)
        {
                // case that the entry needs to be updated 
                if((*i).node_id == s.SID)
                {
                        // correct a timestamp
                        (*i).timestamp = static_cast<time_t>(static_cast<double>((*i).timestamp) - s.offset);
                }
        }
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
	std::cout << "The SID of this server is:" << _mySID << std::endl;
        std::cout << "The leader for this run is:" << _leaderSID << std::endl;

}





