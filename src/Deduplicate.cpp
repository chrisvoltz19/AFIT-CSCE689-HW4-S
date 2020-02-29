#include <stdexcept>
#include <strings.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <sstream>
#include "strfuncts.h"
#include "Deduplicate.h"

Deduplicate::Deduplicate(DronePlot &plotdb, unsigned int SID) : _plotdb(plotdb), _mySID(SID)
{
}

Deduplicate::~Deduplicate()
{
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
                auto & cmp1 = *i; // cmp1 has what is pointed at by i
                for(auto j = i + 1; j != _plotdb.end(); j++)
                {
                        auto & cmp2 = *j;
                        if(checkDup(cmp1, cmp2)) // found a duplicate
                        {
                                // erase the duplciate
                                _plotdb.erase(j);
                                
                        }
                }
                
        }

}

/********************************************************************************************
 * checkDup - This method compares 2 plots and returns if it is a duplicate 
 *             
 * Returns: true if it is a duplicate and false if it is not 
 *             
 *******************************************************************************************/
bool Deduplicate::checkDup(DronePlot & plot1, DronePlot & plot2)
{
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