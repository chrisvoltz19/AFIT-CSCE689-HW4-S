#ifndef DEDUPLICATE_H
#define DEDUPLICATE_H

#include <map>
#include <memory>
#include "DronePlotDB.h"
#include "QueueMgr.h"

// create a typedef that holds the information of known SIDs and their time offset
typedef struct sOffset
{
        unsigned int SID; //SID of server that found plot
        double offset; // offset of that 
        //int isLeader = 0; // 1 if leader 
        
}sOffset;

/********************************************************************************************
 * Deduplicate - this class handles 4 primary tasks dealing with the duplication
 *             1) find duplicates
 *             2) remove duplicates
 *             3) find time skew
 *             4) fix time skew 
 * 
 *             I discussed a couple of conceptual models about this with Lt Josh Larson and
 *             Lt Albert Taglieri
 *
 *******************************************************************************************/
class Deduplicate
{
public:
        Deduplicate(DronePlotDB &plotdb); 
        ~Deduplicate();
        void removeDuplicates();
	void setValues(unsigned int sSID, unsigned int lead, unsigned int numServers); // set SID values after we get some     
     	void printValues();  
        void correctToLeader(); // this method corrects at the end to make all consistent to leader at the end
        void fixTimeSkew(DronePlot & plot);
        

private:
        bool checkDup(DronePlot & plot1, DronePlot & plot2);
        bool findTimeSkew(DronePlot diffPlot, DronePlot mePlot);
        bool findHardSkew(DronePlot knownPlot, DronePlot unknownPlot);
        void fixPrevTimeSkew(sOffset s); // this method corrects to the replsvr that it is on
        
        
        // Variables
        DronePlotDB &_plotdb; // Holds all the drone plots
        int _totalServers; // how many servers are in scenario to expect data from
        std::vector<sOffset> _diffs; // Holds all the sOffset objects for use
        unsigned int _mySID; // the SID of the server
        unsigned int _leaderSID; // SID of leader
        sOffset _leader; // leader information   

        
};

#endif
