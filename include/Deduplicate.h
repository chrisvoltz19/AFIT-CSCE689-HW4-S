#ifndef DEDUPLICATE_H
#define DEDUPLICATE_H

#include <map>
#include <memory>
#include "DronePlotDB.h"

// create a typedef that holds the information of known SIDs and their time offset
typedef struct sOffset
{
        int SID; //SID of server that found plot
        double offset; // offset of that 
        int updated = 0; // 0 if hasn't been updated, 1 if has 
        int isLeader = 0; // 1 if leader 
        
}sOffset;

/********************************************************************************************
 * Deduplicate - this class handles 4 primary tasks dealing with the duplication
 *             1) find duplicates
 *             2) remove duplicates
 *             3) find time skew
 *             4) fix time skew 
 *
 *******************************************************************************************/
class Deduplicate
{
public:
        Deduplicate(DronePlotDB &plotdb, unsigned int SID); //TODO: is there a better way to get SID and should I find a leader and pass in too?
        ~Deduplicate();
        void removeDuplicates();
        
private:
        bool checkDup(DronePlot & plot1, DronePlot plot2);
        void findTimeSkew() = 0;
        void fixTimeSkew(sOffset s) = 0; // this method corrects to the replsvr that it is on
        void correctToLeader() = 0; // this method corrects at the end to make all consistent to leader at the end
        
        
        // Variables
        DronePlotDB &_plotDB; // Holds all the drone plots
        std::vector<sOffset> _diffs; // Holds all the sOffset objects for use
        int _mySID; // the SID of the server
        sOffset _leader; // leader information      
        
};

#endif