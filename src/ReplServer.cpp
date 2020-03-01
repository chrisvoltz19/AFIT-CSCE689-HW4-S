#include <iostream>
#include <exception>
#include "ReplServer.h"

const time_t secs_between_repl = 20;
const unsigned int max_servers = 10;

/*********************************************************************************************
 * ReplServer (constructor) - creates our ReplServer. Initializes:
 *
 *    verbosity - passes this value into QueueMgr and local, plus each connection
 *    _time_mult - how fast to run the simulation - 2.0 = 2x faster
 *    ip_addr - which ip address to bind the server to
 *    port - bind the server here
 *
 *********************************************************************************************/
ReplServer::ReplServer(DronePlotDB &plotdb, float time_mult)
                              :_queue(1),
                               _plotdb(plotdb),
                               _shutdown(false), 
                               _time_mult(time_mult),
                               _verbosity(1),
                               _ip_addr("127.0.0.1"),
                               _port(9999),
                               _dedup(plotdb)
{
}

ReplServer::ReplServer(DronePlotDB &plotdb, const char *ip_addr, unsigned short port, float time_mult,
                                          unsigned int verbosity)
                                 :_queue(verbosity),
                                  _plotdb(plotdb),
                                  _shutdown(false), 
                                  _time_mult(time_mult), 
                                  _verbosity(verbosity),
                                  _ip_addr(ip_addr),
                                  _port(port),
                                  _dedup(plotdb)

{
}

ReplServer::~ReplServer() {

}


/**********************************************************************************************
 * getAdjustedTime - gets the time since the replication server started up in seconds, modified
 *                   by _time_mult to speed up or slow down
 **********************************************************************************************/

double ReplServer::getAdjustedTime() {
   return static_cast<double>(time(nullptr) - _start_time) * _time_mult;
}

/**********************************************************************************************
 * replicate - the main function managing replication activities. Manages the QueueMgr and reads
 *             from the queue, deconflicting entries and populating the DronePlotDB object with
 *             replicated plot points.
 *
 *    Params:  ip_addr - the local IP address to bind the listening socket
 *             port - the port to bind the listening socket
 *             
 *    Throws: socket_error for recoverable errors, runtime_error for unrecoverable types
 **********************************************************************************************/

void ReplServer::replicate(const char *ip_addr, unsigned short port) {
   _ip_addr = ip_addr;
   _port = port;
   replicate();
}

void ReplServer::replicate() {
   // Track when we started the server
   _start_time = time(NULL);
   _last_repl = 0;

   // Set up our queue's listening socket
   _queue.bindSvr(_ip_addr.c_str(), _port);
   _queue.listenSvr();

   if (_verbosity >= 2)
      std::cout << "Server bound to " << _ip_addr << ", port: " << _port << " and listening\n";

  
   // Replicate until we get the shutdown signal
   while (!_shutdown) {

      // Check for new connections, process existing connections, and populate the queue as applicable
      _queue.handleQueue();   

      // send vital information to the Dedup object Voltz
      if(sentInfo == 0)
      {
         // get the SID of the server 
         std::string sidStr = _queue.getServerID();
         //std::cout << sidStr.substr(2) << std::endl;
         mySID = std::stoi(sidStr.substr(2));
         // get the leader 
         election();
         // send the vital information
         _dedup.setValues(mySID, _leader, _numServers);
         // make sure don't do again
         sentInfo = 1;
      }  

      // See if it's time to replicate and, if so, go through the database, identifying new plots
      // that have not been replicated yet and adding them to the queue for replication
      if (getAdjustedTime() - _last_repl > secs_between_repl) {

         queueNewPlots();
         _last_repl = getAdjustedTime();
      }
      
      // Check the queue for updates and pop them until the queue is empty. The pop command only returns
      // incoming replication information--outgoing replication in the queue gets turned into a TCPConn
      // object and automatically removed from the queue by pop
      std::string sid;
      std::vector<uint8_t> data;
      while (_queue.pop(sid, data)) {

         // Incoming replication--add it to this server's local database
         addReplDronePlots(data);         
      }       

      usleep(1000);
   }   
}

/**********************************************************************************************
 * queueNewPlots - looks at the database and grabs the new plots, marshalling them and
 *                 sending them to the queue manager
 *
 *    Returns: number of new plots sent to the QueueMgr
 *
 *    Throws: socket_error for recoverable errors, runtime_error for unrecoverable types
 **********************************************************************************************/

unsigned int ReplServer::queueNewPlots() {
   std::vector<uint8_t> marshall_data;
   unsigned int count = 0;

   if (_verbosity >= 3)
      std::cout << "Replicating plots.\n";

   // Loop through the drone plots, looking for new ones
   std::list<DronePlot>::iterator dpit = _plotdb.begin();
   for ( ; dpit != _plotdb.end(); dpit++) {

      // If this is a new one, marshall it and clear the flag
      if (dpit->isFlagSet(DBFLAG_NEW)) {
         
         dpit->serialize(marshall_data);
         dpit->clrFlags(DBFLAG_NEW);

         count++;
      }
      if (marshall_data.size() % DronePlot::getDataSize() != 0)
         throw std::runtime_error("Issue with marshalling!");

   }
  
   if (count == 0) {
      if (_verbosity >= 3)
         std::cout << "No new plots found to replicate.\n";

      return 0;
   }
 
   // Add the count onto the front
   if (_verbosity >= 3)
      std::cout << "Adding in count: " << count << "\n";

   uint8_t *ctptr_begin = (uint8_t *) &count;
   marshall_data.insert(marshall_data.begin(), ctptr_begin, ctptr_begin+sizeof(unsigned int));

   // Send to the queue manager
   if (marshall_data.size() > 0) {
      _queue.sendToAll(marshall_data);
   }

   if (_verbosity >= 2) 
      std::cout << "Queued up " << count << " plots to be replicated.\n";

   return count;
}

/**********************************************************************************************
 * addReplDronePlots - Adds drone plots to the database from data that was replicated in. 
 *                     Deconflicts issues between plot points.
 * 
 * Params:  data - should start with the number of data points in a 32 bit unsigned integer, 
 *                 then a series of drone plot points
 *
 **********************************************************************************************/

void ReplServer::addReplDronePlots(std::vector<uint8_t> &data) {
   if (data.size() < 4) {
      throw std::runtime_error("Not enough data passed into addReplDronePlots");
   }

   if ((data.size() - 4) % DronePlot::getDataSize() != 0) {
      throw std::runtime_error("Data passed into addReplDronePlots was not the right multiple of DronePlot size");
   }

   // Get the number of plot points
   unsigned int *numptr = (unsigned int *) data.data();
   unsigned int count = *numptr;

   // Store sub-vectors for efficiency
   std::vector<uint8_t> plot;
   auto dptr = data.begin() + sizeof(unsigned int);

   for (unsigned int i=0; i<count; i++) {
      plot.clear();
      plot.assign(dptr, dptr + DronePlot::getDataSize());
      addSingleDronePlot(plot);
      dptr += DronePlot::getDataSize();      
   }
   if (_verbosity >= 2)
      std::cout << "Replicated in " << count << " plots\n";   
}


/**********************************************************************************************
 * addSingleDronePlot - Takes in binary serialized drone data and adds it to the database. 
 *
 **********************************************************************************************/

void ReplServer::addSingleDronePlot(std::vector<uint8_t> &data) {
   DronePlot tmp_plot;

   tmp_plot.deserialize(data);

   // adjest for time skew (if known)
   //std::cout << "PRE TRANSFORM TIMESTAMP: " << 	tmp_plot.timestamp << std::endl;
   _dedup.fixTimeSkew(tmp_plot);
   //std::cout << "!!POST TRANSFORM TIMESTAMP: " << 	tmp_plot.timestamp << std::endl;

   _plotdb.addPlot(tmp_plot.drone_id, tmp_plot.node_id, tmp_plot.timestamp, tmp_plot.latitude,
                                                         tmp_plot.longitude);

   // check the data for duplicates Voltz
   _dedup.removeDuplicates();   
   
}


void ReplServer::shutdown() {
   // do the final check Voltz
   _dedup.removeDuplicates();  
   //_dedup.printValues();
   // redo time stamps according to "leader" time
   //_dedup.correctToLeader();
   
   _shutdown = true;
}

/**********************************************************************************************
 * election - Performs an "election" to determine a leader to sync on
 *            This implementation is to do so with on sending of packets
 *            It simply picks the lowest SID 
 *
 **********************************************************************************************/

void ReplServer::election() {
   std::vector<unsigned int> candidates;
   candidates.emplace_back(mySID);
   _numServers = 1;
   std::vector<std::tuple<std::string, unsigned long, unsigned short>> options = _queue.getServerList();
   for (unsigned int i=0; i<options.size(); i++) {
      std::string potential = std::get<0>(options[i]).c_str();
      unsigned int potentialInt = std::stoi(potential.substr(2));
      candidates.emplace_back(potentialInt);
      _numServers++; 
   }
   _leader = mySID;
   for( auto can : candidates){
      if(can < _leader)
      {
         _leader = can;
      }
   }

    //std::cout << "FINALLY GOT A LEADER!!11 :" << _leader << std::endl;
   
}


