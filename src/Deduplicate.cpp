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
bool Deduplicate::checkDup(DronePlot & plot1, DronePlot plot2)
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

void Deduplicate::printValues()
{
	std::cout << "The SID of this server is:" << _mySID << std::endl;
        std::cout << "The leader for this run is:" << _leaderSID << std::endl;

}





